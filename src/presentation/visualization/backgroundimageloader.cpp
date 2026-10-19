#include "backgroundimageloader.h"

#include "domain/settings/playersettings.h"

// 实现通用 Qt 图片读取与有界 WebP 静态解码。
#include <QFile>
#include <QFileInfo>
#include <QImageReader>

#include <webp/decode.h>

namespace midi_play::presentation::visualization {
namespace {

constexpr qint64 maximumWebPBytes = 64 * 1024 * 1024;

// 优先识别扩展名，再通过 RIFF/WEBP 文件头识别无标准后缀输入。
bool isWebP(const QString& path)
{
    if (QFileInfo(path).suffix().compare(QLatin1String("webp"), Qt::CaseInsensitive) == 0)
        return true;
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) return false;
    const QByteArray header = file.read(12);
    return header.size() == 12 && header.startsWith("RIFF") && header.mid(8, 4) == "WEBP";
}

// WebP 一次读入，限制为 64 MiB 并检查完整读取。
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

// 检查位流尺寸和动画标记，背景只支持静态图。
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

// 保持宽高比，仅在任一边超过上限时缩小。
QSize scaledSize(QSize source, int maximumDimension)
{
    if (source.width() > maximumDimension || source.height() > maximumDimension)
        source.scale(maximumDimension, maximumDimension, Qt::KeepAspectRatio);
    return source;
}

} // namespace

// 预检不生成完整像素数据，实际解码错误仍由加载路径处理。
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

// WebP 在解码阶段缩放，其他格式交由 QImageReader 处理方向和缩放。
QImage loadBackgroundImage(const QString& path, int maximumDimension)
{
    maximumDimension = midi_play::settings::normalizeBackgroundImageMaximumDimension(maximumDimension);
    if (isWebP(path)) {
        const QByteArray bytes = readWebP(path, nullptr);
        WebPDecoderConfig config {};
        if (bytes.isEmpty() || !WebPInitDecoderConfig(&config)
            || !inspectWebP(bytes, &config.input, nullptr)) return {};

        const QSize target = scaledSize({config.input.width, config.input.height}, maximumDimension);
        QImage image(target, QImage::Format_RGBA8888);
        if (image.isNull()) return {};
        config.options.use_scaling = 1;
        config.options.scaled_width = target.width();
        config.options.scaled_height = target.height();
        config.output.colorspace = MODE_RGBA;
        // 解码器直接写入 QImage 的外部缓冲，避免再分配一份完整像素数组。
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
    reader.setScaledSize(scaledSize(sourceSize, maximumDimension));
    const QImage image = reader.read();
    return image.isNull() ? QImage() : image.convertToFormat(QImage::Format_RGBA8888);
}

} // namespace midi_play::presentation::visualization
