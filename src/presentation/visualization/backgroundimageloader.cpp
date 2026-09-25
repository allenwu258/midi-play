#include "backgroundimageloader.h"

#include <QFile>
#include <QFileInfo>
#include <QImageReader>

#include <webp/decode.h>

namespace midi_play::presentation::visualization {
namespace {

constexpr int maximumDimension = 2048;
constexpr qint64 maximumWebPBytes = 64 * 1024 * 1024;

bool isWebP(const QString& path)
{
    if (QFileInfo(path).suffix().compare(QLatin1String("webp"), Qt::CaseInsensitive) == 0)
        return true;
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) return false;
    const QByteArray header = file.read(12);
    return header.size() == 12 && header.startsWith("RIFF") && header.mid(8, 4) == "WEBP";
}

QByteArray readWebP(const QString& path, QString* error)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        if (error) *error = file.errorString();
        return {};
    }
    if (file.size() > maximumWebPBytes) {
        if (error) *error = QStringLiteral("WebP 文件超过 64 MB 限制");
        return {};
    }
    const QByteArray bytes = file.readAll();
    if (bytes.isEmpty() || bytes.size() != file.size()) {
        if (error) *error = QStringLiteral("无法完整读取 WebP 文件");
        return {};
    }
    return bytes;
}

bool inspectWebP(const QByteArray& bytes, WebPBitstreamFeatures* features, QString* error)
{
    const auto status = WebPGetFeatures(
        reinterpret_cast<const uint8_t*>(bytes.constData()), size_t(bytes.size()), features);
    if (status != VP8_STATUS_OK || features->width <= 0 || features->height <= 0) {
        if (error) *error = QStringLiteral("WebP 图片格式无效或已损坏");
        return false;
    }
    if (features->has_animation) {
        if (error) *error = QStringLiteral("暂不支持动态 WebP 图片");
        return false;
    }
    return true;
}

QSize scaledSize(QSize source)
{
    if (source.width() > maximumDimension || source.height() > maximumDimension)
        source.scale(maximumDimension, maximumDimension, Qt::KeepAspectRatio);
    return source;
}

} // namespace

bool canReadBackgroundImage(const QString& path, QString* error)
{
    if (error) error->clear();
    if (isWebP(path)) {
        const QByteArray bytes = readWebP(path, error);
        WebPBitstreamFeatures features {};
        return !bytes.isEmpty() && inspectWebP(bytes, &features, error);
    }
    QImageReader reader(path);
    const bool readable = reader.canRead();
    if (!readable && error) *error = reader.errorString();
    return readable;
}

QImage loadBackgroundImage(const QString& path)
{
    if (isWebP(path)) {
        const QByteArray bytes = readWebP(path, nullptr);
        WebPDecoderConfig config {};
        if (bytes.isEmpty() || !WebPInitDecoderConfig(&config)
            || !inspectWebP(bytes, &config.input, nullptr)) return {};

        const QSize target = scaledSize({config.input.width, config.input.height});
        QImage image(target, QImage::Format_RGBA8888);
        if (image.isNull()) return {};
        config.options.use_scaling = 1;
        config.options.scaled_width = target.width();
        config.options.scaled_height = target.height();
        config.output.colorspace = MODE_RGBA;
        config.output.is_external_memory = 1;
        config.output.u.RGBA.rgba = image.bits();
        config.output.u.RGBA.stride = image.bytesPerLine();
        config.output.u.RGBA.size = size_t(image.sizeInBytes());
        const auto status = WebPDecode(
            reinterpret_cast<const uint8_t*>(bytes.constData()), size_t(bytes.size()), &config);
        WebPFreeDecBuffer(&config.output);
        return status == VP8_STATUS_OK ? image : QImage();
    }

    QImageReader reader(path);
    reader.setAutoTransform(true);
    const QSize sourceSize = reader.size();
    if (!sourceSize.isValid()) return {};
    reader.setScaledSize(scaledSize(sourceSize));
    const QImage image = reader.read();
    return image.isNull() ? QImage() : image.convertToFormat(QImage::Format_RGBA8888);
}

} // namespace midi_play::presentation::visualization
