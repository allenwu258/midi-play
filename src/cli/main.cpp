// 命令行入口：提供读入检查、音源试听、音频导出和传统 Qt 单帧渲染检查。
// 返回码约定：0 为成功，1 为资源/处理失败，2 为命令或参数错误。
#include "app/audioexportservice.h"
#include "domain/visualization/playbackscenestate.h"
#include "domain/visualization/playbackvisualizationprojector.h"
#include "domain/visualization/visiblenoteindex.h"
#include "infrastructure/audio/fluidsynthengine.h"
#include "infrastructure/midi/midireader.h"
#include "infrastructure/readers/midireaderadapter.h"
#include "infrastructure/readers/musicreaderregistry.h"
#include "infrastructure/readers/musicxmlreaderadapter.h"
#include "presentation/visualization/fallingnotesrenderer.h"
#include "presentation/visualization/scenelayoutengine.h"

#include <QCoreApplication>
#include <QDebug>
#include <QDir>
#include <QFileInfo>
#include <QGuiApplication>
#include <QImage>
#include <QPainter>
#include <QTimer>

#include <algorithm>
#include <cstdio>
#include <memory>

namespace {

// 向指定标准流输出支持的命令及位置参数约定。
void printUsage(FILE* stream)
{
    std::fputs(
        "Usage: midi_play_cli <musicxml|mxl|mid|midi|kar file>\n"
        "       midi_play_cli --audio-test <sf2|sf3 file> [replacement SoundFont]\n"
        "       midi_play_cli --midi-test <MIDI file>\n"
        "       midi_play_cli --export-mp3|--export-wav <music file> <output file> --soundfont <sf2|sf3> [--bitrate 128|160|192|256|320] [--sample-rate 44100|48000] [--tail-ms 0..5000] [--metronome]\n"
        "       midi_play_cli --render-test <music file> <output.png> [position_us [width [height]]] [--theme dark|light] [--note-colors normal|vivid]\n"
        "       midi_play_cli --help\n",
        stream);
}

// 命令共用的读入器装配，按扩展名选择 MusicXML 或 MIDI 适配器。
void registerReaders(midi_play::readers::MusicReaderRegistry& registry)
{
    registry.registerReader(std::make_unique<midi_play::readers::MusicXmlReaderAdapter>());
    registry.registerReader(std::make_unique<midi_play::readers::MidiReaderAdapter>());
}

// 加载音源并试听中央 C；可选替换音源用于检查重复加载，事件循环在 1.5 秒后结束。
int runAudioTest(const QStringList& arguments, QCoreApplication& application)
{
    midi_play::audio::FluidSynthEngine engine;
    QString error;
    if (!engine.load(arguments.at(2), &error) || !engine.configureTrack(0, 0, &error)) {
        qCritical().noquote() << error;
        return 1;
    }
    if (arguments.size() == 4
        && (!engine.load(arguments.at(3), &error) || !engine.configureTrack(0, 0, &error))) {
        qCritical().noquote() << error;
        return 1;
    }

    // 发出按下事件，900 ms 后释放；引擎生存期覆盖整个定时器事件循环。
    midi_play::playback::PlaybackEvent noteOn;
    noteOn.channel = 0;
    noteOn.pitch = 60;
    noteOn.velocity = 100;
    noteOn.kind = midi_play::playback::PlaybackEventKind::NoteOn;
    midi_play::playback::PlaybackEvent noteOff = noteOn;
    noteOff.kind = midi_play::playback::PlaybackEventKind::NoteOff;
    engine.start();
    engine.setTransportPosition(0);
    engine.submit(noteOn);
    QTimer::singleShot(900, &application, [&engine, noteOff] { engine.submit(noteOff); });
    QTimer::singleShot(1500, &application, &QCoreApplication::quit);
    return application.exec();
}

// 直接调用 MIDI 读入器，输出轨道数和按文档速度图换算的微秒时长。
int runMidiTest(const QString& inputPath)
{
    const auto result = midi_play::midi::MidiReader().read(inputPath);
    if (!result.ok()) {
        qCritical().noquote() << result.error;
        return 1;
    }
    qInfo().noquote() << "MIDI loaded tracks=" << result.document->tracks().size()
                      << "duration_us="
                      << result.document->tickToMicroseconds(result.document->duration());
    return 0;
}

// 解析音频导出参数、读入统一文档，再同步调用与 GUI 共用的导出应用服务。
int runAudioExport(const QStringList& arguments)
{
    if (arguments.size() < 6) {
        printUsage(stderr);
        return 2;
    }
    midi_play::app::AudioExportOptions options;
    options.format = arguments.at(1) == QStringLiteral("--export-wav")
        ? midi_play::encoding::AudioFileFormat::Wav : midi_play::encoding::AudioFileFormat::Mp3;
    const QString inputPath = arguments.at(2);
    options.outputPath = arguments.at(3);
    // 节拍器开关不消耗值，其余选项必须成对出现；数值范围由导出服务统一验证。
    for (int index = 4; index < arguments.size();) {
        const QString flag = arguments.at(index++);
        if (flag == QStringLiteral("--metronome")) {
            options.includeMetronome = true;
            continue;
        }
        if (index >= arguments.size()) {
            qCritical().noquote() << "Missing value for" << flag;
            return 2;
        }
        const QString value = arguments.at(index++);
        bool validNumber = false;
        if (flag == QStringLiteral("--soundfont")) options.soundFontPath = value;
        else if (flag == QStringLiteral("--bitrate")) {
            options.bitrateKbps = value.toInt(&validNumber);
            if (!validNumber) return 2;
        } else if (flag == QStringLiteral("--sample-rate")) {
            options.sampleRate = value.toInt(&validNumber);
            if (!validNumber) return 2;
        } else if (flag == QStringLiteral("--tail-ms")) {
            options.tailMilliseconds = value.toInt(&validNumber);
            if (!validNumber) return 2;
        } else {
            qCritical().noquote() << "Unknown export option:" << flag;
            return 2;
        }
    }
    if (options.soundFontPath.isEmpty()) {
        qCritical() << "--soundfont is required";
        return 2;
    }
    midi_play::readers::MusicReaderRegistry registry;
    registerReaders(registry);
    const auto* reader = registry.find(QFileInfo(inputPath).suffix());
    if (!reader) {
        qCritical().noquote() << "Unsupported music file extension:" << QFileInfo(inputPath).suffix();
        return 1;
    }
    const auto read = reader->read(inputPath);
    if (!read.ok()) {
        qCritical().noquote() << read.error;
        return 1;
    }
    // CLI 不传取消标志，以 stderr 报告进度；成功输出样本帧数、峰值与削波统计。
    const auto result = midi_play::app::AudioExportService::exportDocument(
        read.document, options, nullptr, [](int percent) {
            if (percent % 10 == 0) std::fprintf(stderr, "\rExporting: %d%%", percent);
        });
    std::fputc('\n', stderr);
    if (result.status == midi_play::app::AudioExportStatus::Success) {
        qInfo().noquote() << "Exported:" << QFileInfo(options.outputPath).absoluteFilePath()
                          << "frames=" << result.frames
                          << "peaks=" << result.peakLeft << result.peakRight
                          << "clipped_samples=" << result.clippedSamples;
        return 0;
    }
    qCritical().noquote() << result.error;
    return 1;
}

// 生成指定音乐位置和尺寸的 PNG；主题/音符色彩选项先提取，再解析剩余位置参数。
int runRenderTest(QStringList arguments)
{
    auto mode = midi_play::settings::kDefaultThemeMode;
    auto colorMode = midi_play::settings::kDefaultNoteColorMode;
    // 选项只能出现一次且位于输入/输出文件之后，移除后保持位置参数下标稳定。
    const int themeArgument = arguments.indexOf(QStringLiteral("--theme"));
    if (themeArgument >= 0) {
        const QString value = arguments.value(themeArgument + 1);
        if (themeArgument < 4 || arguments.count(QStringLiteral("--theme")) != 1
            || (value != QStringLiteral("dark") && value != QStringLiteral("light"))) {
            qCritical() << "--theme requires exactly one value: dark or light";
            return 2;
        }
        mode = value == QStringLiteral("light")
            ? midi_play::settings::ThemeMode::Light : midi_play::settings::ThemeMode::Dark;
        arguments.removeAt(themeArgument + 1);
        arguments.removeAt(themeArgument);
    }
    const int colorArgument = arguments.indexOf(QStringLiteral("--note-colors"));
    if (colorArgument >= 0) {
        const QString value = arguments.value(colorArgument + 1);
        if (colorArgument < 4 || arguments.count(QStringLiteral("--note-colors")) != 1
            || (value != QStringLiteral("normal") && value != QStringLiteral("vivid"))) {
            qCritical() << "--note-colors requires exactly one value: normal or vivid";
            return 2;
        }
        colorMode = value == QStringLiteral("normal")
            ? midi_play::settings::NoteColorMode::Normal : midi_play::settings::NoteColorMode::Vivid;
        arguments.removeAt(colorArgument + 1);
        arguments.removeAt(colorArgument);
    }
    if (arguments.size() < 4 || arguments.size() > 7) {
        printUsage(stderr);
        return 2;
    }
    midi_play::readers::MusicReaderRegistry registry;
    registerReaders(registry);
    const QString inputPath = arguments.at(2);
    const QString outputPath = arguments.at(3);
    const auto* reader = registry.find(QFileInfo(inputPath).suffix());
    if (!reader) {
        qCritical().noquote() << "Unsupported music file extension:" << QFileInfo(inputPath).suffix();
        return 1;
    }
    const auto result = reader->read(inputPath);
    if (!result.ok()) {
        qCritical().noquote() << result.error;
        return 1;
    }
    // 从读入文档构建独立图表，无需创建实时播放器或音频设备。
    midi_play::visualization::VisualizationProjectionOptions options;
    options.fallbackTitle = QFileInfo(inputPath).completeBaseName();
    const auto chart = midi_play::visualization::PlaybackVisualizationProjector().project(
        *result.document, 1, options);
    if (!chart) {
        qCritical() << "Unable to create playback visualization";
        return 1;
    }
    const qint64 requestedPosition = arguments.size() > 4
        ? arguments.at(4).toLongLong() : chart->durationUs() / 10;
    midi_play::visualization::PlaybackSceneState state;
    state.themeMode = mode;
    state.noteColorMode = colorMode;
    state.chart = chart;
    state.durationUs = chart->durationUs();
    state.transportPositionUs = std::clamp<qint64>(requestedPosition, 0, chart->durationUs());
    state.transportState = midi_play::playback::State::Playing;
    state.updateVisibleWindow();
    // 查询可见候选并借用局部向量；向量在同步渲染结束前始终有效。
    midi_play::visualization::VisibleNoteIndex index(chart->notes());
    QVector<int> candidates;
    index.query(state.visibleWindowStartUs, state.visibleWindowEndUs, candidates);
    state.candidateNoteIndices = std::span<const int>(candidates.constData(), candidates.size());
    // 输出尺寸夹取到支持范围，使用传统 QPainter 路径验证布局和绘制结果。
    const int width = arguments.size() > 5 ? std::clamp(arguments.at(5).toInt(), 320, 7680) : 1280;
    const int height = arguments.size() > 6 ? std::clamp(arguments.at(6).toInt(), 240, 4320) : 720;
    QImage image(width, height, QImage::Format_ARGB32_Premultiplied);
    image.fill(Qt::transparent);
    QPainter painter(&image);
    const auto geometry = midi_play::presentation::visualization::SceneLayoutEngine().layout(
        image.size(), chart.get(), state.lookAheadUs);
    midi_play::presentation::visualization::FallingNotesRenderer renderer;
    renderer.render(painter, geometry, state);
    painter.end();
    std::fprintf(stdout,
                 "render chart_tracks=%lld notes=%lld visible=%lld active=%lld position_us=%lld duration_us=%lld\n",
                 static_cast<long long>(chart->tracks().size()),
                 static_cast<long long>(chart->notes().size()),
                 static_cast<long long>(renderer.visibleNoteCount()),
                 static_cast<long long>(renderer.activeNoteCount()),
                 static_cast<long long>(state.transportPositionUs),
                 static_cast<long long>(state.durationUs));
    return image.save(outputPath) ? 0 : 1;
}

// 按扩展名读入并报告文档摘要，用于检查统一读入接口的基本可用性。
int runReaderSmokeTest(const QString& inputPath)
{
    midi_play::readers::MusicReaderRegistry registry;
    registerReaders(registry);
    const auto* reader = registry.find(QFileInfo(inputPath).suffix());
    if (!reader) {
        qCritical().noquote() << "Unsupported music file extension:" << QFileInfo(inputPath).suffix();
        return 1;
    }
    const auto result = reader->read(inputPath);
    if (!result.ok()) {
        qCritical().noquote() << result.error;
        return 1;
    }
    qInfo().noquote() << "Loaded:" << result.document->title()
                      << "tracks=" << result.document->tracks().size()
                      << "duration_us="
                      << result.document->tickToMicroseconds(result.document->duration());
    return 0;
}

} // namespace

// 按命令分派检查或导出操作；只有渲染命令创建具备字体/绘图支持的 GUI 应用对象。
int main(int argc, char* argv[])
{
    const bool renderMode = argc > 1 && QByteArray(argv[1]) == QByteArrayLiteral("--render-test");
    std::unique_ptr<QCoreApplication> application;
    // 图像渲染需要 QGuiApplication 和平台插件，其他命令使用 QCoreApplication。
    if (renderMode) {
        const QString executableDirectory = QFileInfo(QString::fromLocal8Bit(argv[0])).absolutePath();
        const QString bundledPlatforms = QDir(executableDirectory).filePath(QStringLiteral("platforms"));
        if (QDir(bundledPlatforms).exists()) {
            QCoreApplication::addLibraryPath(bundledPlatforms);
        }
        application = std::make_unique<QGuiApplication>(argc, argv);
    } else {
        application = std::make_unique<QCoreApplication>(argc, argv);
    }
    application->setApplicationName(QStringLiteral("MidiPlayCli"));
    application->setOrganizationName(QStringLiteral("MidiPlay"));
    const QStringList arguments = application->arguments();
    const QString command = arguments.value(1);
    // 无参数等同帮助；未识别的命令或位置参数数量错误按使用错误返回 2。
    if (arguments.size() == 1 || command == QStringLiteral("--help")
        || command == QStringLiteral("-h")) {
        printUsage(stdout);
        return 0;
    }
    if (command == QStringLiteral("--audio-test") && arguments.size() >= 3 && arguments.size() <= 4) {
        return runAudioTest(arguments, *application);
    }
    if (command == QStringLiteral("--midi-test") && arguments.size() == 3) {
        return runMidiTest(arguments.at(2));
    }
    if (command == QStringLiteral("--export-mp3") || command == QStringLiteral("--export-wav")) {
        return runAudioExport(arguments);
    }
    if (command == QStringLiteral("--render-test")) {
        return runRenderTest(arguments);
    }
    if (!command.startsWith(QLatin1Char('-')) && arguments.size() == 2) {
        return runReaderSmokeTest(command);
    }
    printUsage(stderr);
    return 2;
}
