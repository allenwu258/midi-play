#include "videoframereader.h"
#include "domain/settings/playersettings.h"
#include <QFileInfo>
#include <QTransform>
#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>
extern "C" {
#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
#include <libavutil/display.h>
#include <libswscale/swscale.h>
}

namespace midi_play::media {
namespace {
constexpr AVRational microseconds {1, 1'000'000};
void check(int result, const char* operation)
{
    if (result >= 0) return;
    char detail[AV_ERROR_MAX_STRING_SIZE] {};
    av_strerror(result, detail, sizeof(detail));
    throw std::runtime_error(QStringLiteral("%1: %2").arg(QString::fromLatin1(operation),
        QString::fromUtf8(detail)).toStdString());
}
}

class VideoFrameReader::Impl {
public:
    ~Impl()
    {
        sws_freeContext(scale);
        av_frame_free(&current); av_frame_free(&next);
        av_packet_free(&packet); avcodec_free_context(&codec);
        avformat_close_input(&format);
    }
    void open(const QString& path, int dimension, const std::atomic_bool* cancel)
    {
        canceled = cancel;
        if (canceled && canceled->load()) throw std::runtime_error("Video decoding canceled");
        const QFileInfo file(path);
        if (!file.isFile() || !file.isReadable()) throw std::runtime_error("Video file is not readable");
        maximumDimension = settings::normalizeBackgroundImageMaximumDimension(dimension);
        format = avformat_alloc_context();
        if (!format) throw std::bad_alloc();
        format->interrupt_callback = {[](void* opaque) {
            const auto* self = static_cast<Impl*>(opaque);
            return self->canceled && self->canceled->load() ? 1 : 0;
        }, this};
        AVDictionary* options = nullptr;
        av_dict_set(&options, "protocol_whitelist", "file", 0);
        const auto utf8 = file.absoluteFilePath().toUtf8();
        const int opened = avformat_open_input(&format, utf8.constData(), nullptr, &options);
        av_dict_free(&options);
        check(opened, "Open background video");
        check(avformat_find_stream_info(format, nullptr), "Inspect background video");
        const AVCodec* decoder = nullptr;
        streamIndex = av_find_best_stream(format, AVMEDIA_TYPE_VIDEO, -1, -1, &decoder, 0);
        check(streamIndex, "Find video track");
        stream = format->streams[streamIndex];
        start = stream->start_time == AV_NOPTS_VALUE ? 0 : stream->start_time;
        info.durationUs = stream->duration > 0
            ? av_rescale_q(stream->duration, stream->time_base, microseconds) : 0;
        info.sourceSize = {stream->codecpar->width, stream->codecpar->height};
        if (info.sourceSize.isEmpty() || info.sourceSize.width() > 8192 || info.sourceSize.height() > 8192
            || qint64(info.sourceSize.width()) * info.sourceSize.height() > 40'000'000)
            throw std::runtime_error("Background video dimensions exceed the 8K decode limit");
        info.hasAudio = av_find_best_stream(format, AVMEDIA_TYPE_AUDIO, -1, -1, nullptr, 0) >= 0;
        if (stream->codecpar->color_trc == AVCOL_TRC_SMPTE2084 || stream->codecpar->color_trc == AVCOL_TRC_ARIB_STD_B67)
            throw std::runtime_error("HDR video requires conversion to SDR before use as a background");
        codec = avcodec_alloc_context3(decoder);
        if (!codec) throw std::bad_alloc();
        check(avcodec_parameters_to_context(codec, stream->codecpar), "Configure video decoder");
        codec->thread_count = 2;
        codec->max_pixels = 40'000'000; // Also bound streams that change resolution after their headers.
        check(avcodec_open2(codec, decoder, nullptr), "Open video decoder");
        packet = av_packet_alloc(); current = av_frame_alloc(); next = av_frame_alloc();
        if (!packet || !current || !next) throw std::bad_alloc();
        if (info.durationUs <= 0) {
            // Matroska commonly omits stream duration. Container duration may
            // instead describe a longer audio track, so inspect video packets.
            // This is a cancellable, bounded-memory demux pass, not full decode.
            info.durationUs = inspectVideoDuration();
            seek(0);
        }
        if (info.durationUs <= 0)
            throw std::runtime_error("Background video requires a finite duration");
        const auto* matrix = av_packet_side_data_get(stream->codecpar->coded_side_data,
            stream->codecpar->nb_coded_side_data, AV_PKT_DATA_DISPLAYMATRIX);
        if (matrix && matrix->size >= 9 * sizeof(int32_t)) {
            const double angle = -av_display_rotation_get(reinterpret_cast<const int32_t*>(matrix->data));
            if (std::isfinite(angle)) rotation = angle;
        }
    }
    qint64 inspectVideoDuration()
    {
        check(av_seek_frame(format, streamIndex, start, AVSEEK_FLAG_BACKWARD), "Inspect video timeline");
        qint64 lastPts = AV_NOPTS_VALUE, previousPts = AV_NOPTS_VALUE, lastDuration = 0;
        while (true) {
            if (canceled && canceled->load()) throw std::runtime_error("Video decoding canceled");
            av_packet_unref(packet);
            const int read = av_read_frame(format, packet);
            if (read == AVERROR_EOF) break;
            check(read, "Inspect video duration");
            if (packet->stream_index != streamIndex || packet->pts == AV_NOPTS_VALUE) continue;
            if (lastPts == AV_NOPTS_VALUE || packet->pts > lastPts) {
                previousPts = lastPts;
                lastPts = packet->pts;
                lastDuration = packet->duration;
            } else if (packet->pts < lastPts && (previousPts == AV_NOPTS_VALUE || packet->pts > previousPts)) {
                previousPts = packet->pts;
            }
        }
        av_packet_unref(packet);
        if (lastPts == AV_NOPTS_VALUE) throw std::runtime_error("Video has no presentation timestamps");
        if (lastDuration <= 0 && previousPts != AV_NOPTS_VALUE) lastDuration = lastPts - previousPts;
        if (lastDuration <= 0) {
            const auto rate = av_guess_frame_rate(format, stream, nullptr);
            if (rate.num > 0 && rate.den > 0)
                lastDuration = av_rescale_q(1, av_inv_q(rate), stream->time_base);
        }
        if (lastDuration <= 0) throw std::runtime_error("Cannot determine the final video frame duration");
        return av_rescale_q(lastPts - start + lastDuration, stream->time_base, microseconds);
    }
    void seek(qint64 target)
    {
        check(avformat_seek_file(format, streamIndex, std::numeric_limits<int64_t>::min(),
            start + av_rescale_q(target, microseconds, stream->time_base),
            start + av_rescale_q(target, microseconds, stream->time_base), AVSEEK_FLAG_BACKWARD), "Seek background video");
        avcodec_flush_buffers(codec);
        av_packet_unref(packet); av_frame_unref(current); av_frame_unref(next);
        packetPending = false; draining = false; eof = false;
        currentUs = nextUs = -1; cached = {};
    }
    bool decode(AVFrame* frame, qint64& timestamp)
    {
        av_frame_unref(frame);
        while (!eof) {
            if (canceled && canceled->load()) throw std::runtime_error("Video decoding canceled");
            const int received = avcodec_receive_frame(codec, frame);
            if (received == 0) {
                if (frame->width <= 0 || frame->height <= 0 || frame->width > 8192 || frame->height > 8192
                    || qint64(frame->width) * frame->height > 40'000'000)
                    throw std::runtime_error("Decoded video frame exceeds the 8K decode limit");
                if (frame->color_trc == AVCOL_TRC_SMPTE2084 || frame->color_trc == AVCOL_TRC_ARIB_STD_B67)
                    throw std::runtime_error("HDR video requires conversion to SDR before use as a background");
                if (frame->best_effort_timestamp == AV_NOPTS_VALUE)
                    throw std::runtime_error("Video frame has no presentation timestamp");
                timestamp = std::max<qint64>(0,
                    av_rescale_q(frame->best_effort_timestamp - start, stream->time_base, microseconds));
                return true;
            }
            if (received == AVERROR_EOF) { eof = true; return false; }
            if (received != AVERROR(EAGAIN)) check(received, "Decode video frame");
            if (draining) { eof = true; return false; }
            if (!packetPending) {
                int read;
                do {
                    if (canceled && canceled->load()) throw std::runtime_error("Video decoding canceled");
                    av_packet_unref(packet);
                    read = av_read_frame(format, packet);
                } while (read >= 0 && packet->stream_index != streamIndex);
                if (read == AVERROR_EOF) {
                    check(avcodec_send_packet(codec, nullptr), "Drain video decoder");
                    draining = true;
                    continue;
                }
                check(read, "Read video packet");
                packetPending = true;
            }
            const int sent = avcodec_send_packet(codec, packet);
            if (sent == AVERROR(EAGAIN)) continue;
            check(sent, "Submit video packet");
            av_packet_unref(packet); packetPending = false;
        }
        return false;
    }
    VideoFrame frameAt(qint64 target, const std::atomic_bool* requestCanceled)
    {
        struct CancellationScope {
            const std::atomic_bool*& pointer;
            const std::atomic_bool* previous;
            ~CancellationScope() { pointer = previous; }
        } scope {canceled, canceled};
        if (requestCanceled) canceled = requestCanceled;
        if (canceled && canceled->load()) throw std::runtime_error("Video decoding canceled");
        target = std::clamp(target, qint64(0), info.durationUs - 1);
        if ((currentUs < 0 && target > 1'000'000)
            || (currentUs >= 0 && (target < currentUs || target - currentUs > 1'000'000))) seek(target);
        if (currentUs < 0 && !decode(current, currentUs)) throw std::runtime_error("Video contains no decoded frames");
        while (true) {
            if (nextUs < 0 && !decode(next, nextUs)) break;
            if (nextUs > target) break;
            av_frame_unref(current);
            av_frame_move_ref(current, next);
            currentUs = nextUs; nextUs = -1; cached = {};
        }
        if (cached.isNull()) {
            QSize size(current->width, current->height);
            const AVRational sar = av_guess_sample_aspect_ratio(format, stream, current);
            if (sar.num > 0 && sar.den > 0)
                size.setWidth(std::max(1, int(std::lround(size.width() * av_q2d(sar)))));
            if (size.width() > maximumDimension || size.height() > maximumDimension)
                size.scale(maximumDimension, maximumDimension, Qt::KeepAspectRatio);
            size = size.expandedTo({1, 1});
            cached = QImage(size, QImage::Format_RGBA8888);
            if (cached.isNull()) throw std::bad_alloc();
            scale = sws_getCachedContext(scale, current->width, current->height,
                static_cast<AVPixelFormat>(current->format), size.width(), size.height(), AV_PIX_FMT_RGBA,
                SWS_BILINEAR, nullptr, nullptr, nullptr);
            if (!scale) throw std::runtime_error("Cannot convert video pixels");
            const int space = current->colorspace == AVCOL_SPC_BT709 ? SWS_CS_ITU709
                : current->colorspace == AVCOL_SPC_BT2020_NCL || current->colorspace == AVCOL_SPC_BT2020_CL
                ? SWS_CS_BT2020 : SWS_CS_DEFAULT;
            const int* coefficients = sws_getCoefficients(space);
            sws_setColorspaceDetails(scale, coefficients, current->color_range == AVCOL_RANGE_JPEG,
                coefficients, 1, 0, 1 << 16, 1 << 16);
            uint8_t* output[] {cached.bits(), nullptr, nullptr, nullptr};
            int strides[] {int(cached.bytesPerLine()), 0, 0, 0};
            check(sws_scale(scale, current->data, current->linesize, 0, current->height, output, strides), "Convert video frame");
            if (rotation != 0) cached = cached.transformed(QTransform().rotate(rotation));
        }
        return {cached, currentUs};
    }
    VideoMetadata info;
private:
    AVFormatContext* format = nullptr;
    AVCodecContext* codec = nullptr;
    AVStream* stream = nullptr;
    AVPacket* packet = nullptr;
    AVFrame* current = nullptr;
    AVFrame* next = nullptr;
    SwsContext* scale = nullptr;
    const std::atomic_bool* canceled = nullptr;
    int streamIndex = -1, maximumDimension = 2048;
    qint64 start = 0, currentUs = -1, nextUs = -1;
    double rotation = 0;
    bool packetPending = false, draining = false, eof = false;
    QImage cached;
};

VideoFrameReader::VideoFrameReader() = default;
VideoFrameReader::~VideoFrameReader() = default;
bool VideoFrameReader::open(const QString& path, int dimension, QString* error, const std::atomic_bool* canceled)
{
    if (error) error->clear();
    m_impl.reset();
    try {
        auto impl = std::make_unique<Impl>();
        impl->open(path, dimension, canceled);
        m_impl = std::move(impl);
        return true;
    } catch (const std::exception& e) { if (error) *error = QString::fromUtf8(e.what()); return false; }
}
VideoMetadata VideoFrameReader::metadata() const { return m_impl ? m_impl->info : VideoMetadata {}; }
VideoFrame VideoFrameReader::frameAt(qint64 position, QString* error, const std::atomic_bool* canceled)
{
    if (error) error->clear();
    if (!m_impl) { if (error) *error = QStringLiteral("Video decoder is not open"); return {}; }
    try { return m_impl->frameAt(position, canceled); }
    catch (const std::exception& e) { if (error) *error = QString::fromUtf8(e.what()); return {}; }
}
}
