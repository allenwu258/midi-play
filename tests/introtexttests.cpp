#include "app/exporttimeline.h"
#include "app/videoexportservice.h"
#include "domain/playback/playbackmodel.h"
#include "domain/visualization/playbackvisualizationprojector.h"
#include "infrastructure/encoding/ffmpegprobe.h"
#include "presentation/exportdialog.h"
#include "presentation/introtextpanel.h"
#include "presentation/introtextpreviewwidget.h"
#include "presentation/videoexportpage.h"
#include "presentation/videoexportpreviewrenderer.h"
#include "presentation/visualization/introtext.h"
#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QFontDatabase>
#include <QFontMetricsF>
#include <QKeyEvent>
#include <QLabel>
#include <QMouseEvent>
#include <QPainter>
#include <QPlainTextEdit>
#include <QProcess>
#include <QPushButton>
#include <QSlider>
#include <QSpinBox>
#include <QStandardPaths>
#include <QTabWidget>
#include <QTemporaryDir>
#include <QTimer>
#include <QDir>
#include <cmath>
#include <cstdio>
#include <cstdlib>

using namespace midi_play;
using presentation::visualization::IntroTextAsset;
namespace {
void require(bool ok, const char* message)
{
    if (!ok) { std::fprintf(stderr, "FAILED: %s\n", message); std::exit(1); }
}
void waitEvents(int milliseconds)
{
    QEventLoop loop;
    QTimer::singleShot(milliseconds, &loop, &QEventLoop::quit);
    loop.exec();
}
app::IntroTextConfig textConfig()
{
    app::IntroTextConfig config;
    config.enabled = true;
    config.text = QStringLiteral("夜曲 Nocturne\n作曲：示例 Composer\n演奏：Piano");
    return config;
}
QImage layer(IntroTextAsset& asset, qint64 timeUs)
{
    QImage image(asset.outputSize(), QImage::Format_RGBA8888);
    image.fill(Qt::transparent);
    require(asset.composite(image, timeUs), "composite succeeds");
    return image;
}
QRect inkBounds(const QImage& image)
{
    QRect bounds;
    for (int y = 0; y < image.height(); ++y)
        for (int x = 0; x < image.width(); ++x)
            if (image.pixelColor(x, y).alpha() > 0) bounds |= QRect(x, y, 1, 1);
    return bounds;
}
void testLayoutAndAnimation()
{
    auto config = textConfig();
    QString error;
    IntroTextAsset asset;
    require(asset.prepare(config, {1280, 720}, &error), qPrintable(error));
    require(asset.logicalBounds().center() == QPointF(640, 360), "block is centered");
    const QImage initial = layer(asset, 0);
    require(!inkBounds(initial).isEmpty(), "Chinese and Latin have visible glyphs");
    require(initial == layer(asset, 5'000'000), "hold boundary remains fully visible");
    require(std::abs(asset.animationState(5'500'000).opacity - .5) < .001, "fade midpoint is half opacity");
    require(asset.animationState(5'999'999).opacity > 0, "visible before end boundary");
    require(inkBounds(layer(asset, 6'000'000)).isEmpty(), "hidden at exact end boundary");
    for (const int fps : {30, 60}) {
        const qint64 hiddenFrame = 6 * fps;
        require(asset.animationState((hiddenFrame - 1) * 1'000'000 / fps).opacity > 0,
                "last animation frame is present at supported FPS");
        require(asset.animationState(hiddenFrame * 1'000'000 / fps).opacity == 0,
                "exact output-frame boundary hides text at supported FPS");
    }
    QImage unchanged(1280, 720, QImage::Format_RGBA8888);
    unchanged.fill(QColor(30, 90, 120));
    const auto key = unchanged.cacheKey();
    require(asset.composite(unchanged, 7'000'000) && unchanged.cacheKey() == key,
            "hidden overlay leaves encoder buffer untouched");
    config.exitEffect = app::IntroTextExitEffect::SlideDown;
    require(asset.prepare(config, {1280, 720}, &error), "slide asset prepares");
    const auto state = asset.animationState(5'500'000);
    require(state.offsetY > 0 && state.opacity == .5, "slide moves and fades");
    require(inkBounds(layer(asset, 5'500'000)).top() > inkBounds(initial).top(), "slide pixels move downward");
    for (const QSize size : {QSize(512, 288), QSize(1280, 720), QSize(1920, 1080), QSize(3840, 2160)}) {
        require(asset.prepare(config, size, &error), "all output sizes prepare");
        require(asset.logicalBounds().center() == QPointF(640, 360), "resolution preserves anchor");
        require(std::abs(asset.outputBounds().width() / size.width() - .8) < .00001,
                "block width scales with resolution");
    }
    config.position = app::IntroTextPosition::LowerLeft;
    require(asset.prepare(config, {1280, 720}, &error), "lower left prepares");
    require(asset.logicalBounds().left() == 64 && asset.logicalBounds().bottom() == 684,
            "lower left uses canvas safety margins");
    config.position = app::IntroTextPosition::Custom;
    config.offsetX = 20; config.offsetY = -40;
    require(asset.prepare(config, {1280, 720}, &error), "custom prepares");
    require(asset.logicalBounds().center() == QPointF(660, 320), "custom center offsets are applied");
    config.position = app::IntroTextPosition::Center;
    config.text = QStringLiteral("ABC");
    int previousLeft = -1;
    for (const auto alignment : {app::IntroTextAlignment::Left, app::IntroTextAlignment::Center, app::IntroTextAlignment::Right}) {
        config.alignment = alignment;
        require(asset.prepare(config, {1280, 720}, &error), "alignment prepares");
        const int left = inkBounds(layer(asset, 0)).left();
        require(left > previousLeft, "internal alignment moves ink left-center-right");
        previousLeft = left;
        require(asset.logicalBounds().center() == QPointF(640, 360), "alignment preserves block anchor");
    }
    config.text = QStringLiteral("中文自动换行 Latin mixed text ").repeated(6);
    config.widthPercent = 30;
    require(asset.prepare(config, {1280, 720}, &error), "wrapped mixed script prepares");
    require(asset.logicalBounds().height() > 100, "long content wraps into multiple lines");
    config.text = QStringLiteral("العربية हिन्दी e\u0301 😀 <b>literal</b>\n制表\t分隔");
    config.widthPercent = 80;
    require(asset.prepare(config, {1280, 720}, &error) && !inkBounds(layer(asset, 0)).isEmpty(),
            "Unicode and literal markup use Qt layout");
    config.displayMilliseconds = 0;
    require(asset.prepare(config, {1280, 720}, &error) && asset.animationState(0).opacity == 1,
            "zero hold starts visible and immediately fades");
}
void testValidation()
{
    auto config = textConfig();
    config.text = QStringLiteral(" \t\n ");
    require(!IntroTextAsset::validationError(config).isEmpty(), "blank enabled text rejected");
    config.enabled = false;
    require(IntroTextAsset::validationError(config).isEmpty(), "disabled text bypasses validation");
    config = textConfig(); config.text = QString(2001, QLatin1Char('a'));
    require(!IntroTextAsset::validationError(config).isEmpty(), "excess text rejected without truncation");
    config = textConfig(); config.text = QStringLiteral("A\n").repeated(50);
    require(!IntroTextAsset::validationError(config).isEmpty(), "excess explicit lines rejected");
    config = textConfig(); config.exitMilliseconds = 0;
    require(!IntroTextAsset::validationError(config).isEmpty(), "zero animation duration rejected");
    config = textConfig(); config.position = app::IntroTextPosition::Custom; config.offsetY = 720;
    require(!IntroTextAsset::validationError(config).isEmpty(), "static off-canvas text rejected");
    config = textConfig(); config.alignment = app::IntroTextAlignment(99);
    require(!IntroTextAsset::validationError(config).isEmpty(), "unknown enum rejected");
    config = textConfig(); config.text = QStringLiteral("Line\n").repeated(40); config.fontPixelSize = 160;
    require(!IntroTextAsset::validationError(config).isEmpty(), "oversized layout rejected before image allocation");
    IntroTextAsset asset;
    QString error;
    config = textConfig();
    require(asset.prepare(config, {1280, 720}, &error), "valid initial asset");
    config.enabled = false;
    require(asset.prepare(config, {}, &error) && !asset.isValid(), "disable clears old image");
}

void testEditorAndDialog(const QString& snapshots)
{
    app::VideoExportOptions options;
    options.outputPath = QStringLiteral("intro-test.mp4");
    options.scene.state.durationUs = 10'000'000;
    options.introText = textConfig();
    options.introText.enabled = false;
    presentation::ExportDialog dialog({}, {}, options, 8'000'000, false, QStringLiteral("测试：无 GPU 预览"));
    auto* tabs = dialog.findChild<QTabWidget*>(QStringLiteral("exportTabs"));
    tabs->setCurrentIndex(1);
    dialog.show();
    waitEvents(30);
    const int compactWidth = dialog.width();
    auto* toggle = dialog.findChild<QCheckBox*>(QStringLiteral("introTextEnabled"));
    auto* panel = dialog.findChild<QWidget*>(QStringLiteral("introTextPanel"));
    require(toggle && panel && panel->isHidden(), "intro editor is hidden by default");
    if (!snapshots.isEmpty()) dialog.grab().save(snapshots + QStringLiteral("/compact.png"));
    toggle->setChecked(true);
    waitEvents(30);
    require(dialog.width() > compactWidth && !panel->isHidden(), "enable expands dialog and right editor");
    auto* preview = dialog.findChild<presentation::IntroTextPreviewWidget*>();
    auto* editor = dialog.findChild<presentation::IntroTextPanel*>();
    QImage background(1024, 576, QImage::Format_RGBA8888);
    background.fill(QColor(25, 33, 41));
    preview->setBaseFrame(background, 0);
    if (!snapshots.isEmpty()) {
        dialog.grab().save(snapshots + QStringLiteral("/expanded-narrow.png"));
        const QSize current = dialog.size();
        dialog.resize(1150, 760);
        waitEvents(20);
        dialog.grab().save(snapshots + QStringLiteral("/expanded.png"));
        dialog.resize(current);
        waitEvents(20);
    }
    auto* position = dialog.findChild<QComboBox*>(QStringLiteral("introTextPosition"));
    position->setCurrentIndex(position->findData(int(app::IntroTextPosition::LowerLeft)));
    const auto lower = editor->config();
    IntroTextAsset asset;
    QString error;
    require(asset.prepare(lower, {1024, 576}, &error), "preset prepares");
    const QPointF priorCenter = asset.logicalBounds().center();
    position->setCurrentIndex(position->findData(int(app::IntroTextPosition::Custom)));
    require(asset.prepare(editor->config(), {1024, 576}, &error), "custom transition prepares");
    require(QLineF(priorCenter, asset.logicalBounds().center()).length() < 1,
            "switch from preset to custom preserves visible position");
    editor->setPositionFromDrag(0, 0);
    preview->setConfig(editor->config());
    // Exercise the actual event path and clamping, rather than only emitting its signal.
    const qreal scale = std::min(preview->width() / 1280.0, preview->height() / 720.0);
    const QPointF center(preview->width() / 2.0, preview->height() / 2.0);
    const QPointF dragDelta(75 * scale, 50 * scale);
    QMouseEvent press(QEvent::MouseButtonPress, center, center, Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
    QApplication::sendEvent(preview, &press);
    QMouseEvent move(QEvent::MouseMove, center + dragDelta, center + dragDelta,
                     Qt::NoButton, Qt::LeftButton, Qt::NoModifier);
    QApplication::sendEvent(preview, &move);
    QMouseEvent release(QEvent::MouseButtonRelease, center + dragDelta, center + dragDelta,
                        Qt::LeftButton, Qt::NoButton, Qt::NoModifier);
    QApplication::sendEvent(preview, &release);
    require(editor->config().offsetX == 75 && editor->config().offsetY == 50,
            "drag converts preview pixels into reference-canvas offsets");
    QKeyEvent key(QEvent::KeyPress, Qt::Key_Right, Qt::ShiftModifier);
    QApplication::sendEvent(preview, &key);
    require(editor->config().offsetX == 85, "shift arrow nudges by ten reference pixels");
    tabs->setCurrentIndex(0);
    waitEvents(20);
    require(dialog.width() == compactWidth, "audio tab restores compact width");
    tabs->setCurrentIndex(1);
    require(dialog.width() > compactWidth, "video tab restores enabled editor");
    toggle->setChecked(false);
    waitEvents(20);
    require(dialog.width() == compactWidth && panel->isHidden(), "disable restores compact size");
    toggle->setChecked(true);
    require(dialog.videoOptions().introText.text == options.introText.text, "toggle preserves text draft");
    auto* hold = dialog.findChild<QDoubleSpinBox*>(QStringLiteral("introTextDisplayTime"));
    hold->setValue(20);
    waitEvents(200);
    require(!dialog.findChild<QPushButton*>(QStringLiteral("introPreviewHidden"))->isEnabled(),
            "short-video shortcut cannot seek beyond last encoded frame");
    require(dialog.findChild<QLabel*>(QStringLiteral("introTextTimeSummary"))->text().contains(QStringLiteral("截断")),
            "short-video truncation is explained");
}

void testInterruptedDrag()
{
    presentation::IntroTextPreviewWidget preview;
    preview.resize(512, 288);
    auto config = textConfig();
    int moves = 0;
    QObject::connect(&preview, &presentation::IntroTextPreviewWidget::positionDragged,
        [&] (int x, int y) {
            ++moves;
            config.position = app::IntroTextPosition::Custom;
            config.offsetX = x;
            config.offsetY = y;
            preview.setConfig(config);
        });
    // Missing release, hide/show, capture loss, deactivation and disabling must all end a drag.
    for (const auto interruption : {QEvent::None, QEvent::Hide, QEvent::UngrabMouse,
                                    QEvent::WindowDeactivate, QEvent::EnabledChange}) {
        config = textConfig();
        preview.setConfig(config);
        preview.setEnabled(true);
        preview.show();
        QApplication::processEvents();
        const QPointF center(preview.width() / 2.0, preview.height() / 2.0);
        QMouseEvent press(QEvent::MouseButtonPress, center, center,
                          Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
        QApplication::sendEvent(&preview, &press);
        QMouseEvent move(QEvent::MouseMove, center + QPointF(10, 5), center + QPointF(10, 5),
                         Qt::NoButton, Qt::LeftButton, Qt::NoModifier);
        const int beforeDrag = moves;
        QApplication::sendEvent(&preview, &move);
        require(moves == beforeDrag + 1, "fresh drag still moves text after previous interruption");
        if (interruption == QEvent::Hide) { preview.hide(); preview.show(); }
        else if (interruption == QEvent::EnabledChange) { preview.setEnabled(false); preview.setEnabled(true); }
        else if (interruption != QEvent::None) {
            QEvent event(interruption);
            QApplication::sendEvent(&preview, &event);
        }
        const int beforeHover = moves;
        QMouseEvent hover(QEvent::MouseMove, center + QPointF(30, 20), center + QPointF(30, 20),
                          Qt::NoButton, Qt::NoButton, Qt::NoModifier);
        QApplication::sendEvent(&preview, &hover);
        require(moves == beforeHover, "hover after drag interruption preserves text coordinates");
        require(QWidget::mouseGrabber() != &preview, "interrupted drag releases mouse capture");
    }
}

std::shared_ptr<music::MusicDocument> document()
{
    auto result = std::make_shared<music::MusicDocument>();
    result->setDuration(1920);
    result->tempos().push_back({0, 120.0, 0});
    music::Track track;
    track.id = QStringLiteral("intro-test");
    music::NoteEvent note;
    note.start = 0; note.duration = 1920; note.pitch = 60; note.velocity = 80; note.noteId = 1;
    track.notes.push_back(note);
    result->tracks().push_back(track);
    return result;
}
QByteArray decode(const QString& ffmpeg, const QString& path)
{
    QProcess process;
    process.setProcessChannelMode(QProcess::SeparateChannels);
    process.start(ffmpeg, {QStringLiteral("-v"), QStringLiteral("error"), QStringLiteral("-i"), path,
        QStringLiteral("-f"), QStringLiteral("rawvideo"), QStringLiteral("-pix_fmt"), QStringLiteral("rgba"), QStringLiteral("pipe:1")});
    require(process.waitForFinished(30000) && process.exitCode() == 0, "encoded intro video decodes");
    return process.readAllStandardOutput();
}
void testIntegration()
{
#if !MIDI_PLAY_HAS_VULKAN
    std::exit(77);
#else
    const QString ffmpeg = QStandardPaths::findExecutable(QStringLiteral("ffmpeg"));
    if (ffmpeg.isEmpty()) std::exit(77);
    QTemporaryDir directory;
    app::VideoExportOptions options;
    options.ffmpegExecutable = ffmpeg;
    options.outputPath = directory.filePath(QStringLiteral("base.mp4"));
    options.scene.outputSize = QSize(640, 360);
    options.includeAudio = false;
    options.ratePercent = 200;
    options.tailMilliseconds = 0;
    options.crf = 17;
    options.fps = 30;
    auto music = document();
    const auto base = app::VideoExportService::exportDocument(music, options);
    require(base.status == app::VideoExportStatus::Success, qPrintable(base.error));
    const QByteArray basePixels = decode(ffmpeg, options.outputPath);
    options.introText = textConfig();
    options.introText.fontPixelSize = 80;
    options.introText.displayMilliseconds = 200;
    options.introText.exitMilliseconds = 400;
    options.introText.exitEffect = app::IntroTextExitEffect::SlideDown;
    options.outputPath = directory.filePath(QStringLiteral("intro.mp4"));
    const auto overlay = app::VideoExportService::exportDocument(music, options);
    require(overlay.status == app::VideoExportStatus::Success, qPrintable(overlay.error));
    require(overlay.frames == base.frames, "intro preserves exact output frame count");
    const QByteArray introPixels = decode(ffmpeg, options.outputPath);
    const qsizetype frameBytes = 640 * 360 * 4;
    require(basePixels.size() == base.frames * frameBytes && introPixels.size() == basePixels.size(),
            "decoded frame counts match service timeline");
    const auto difference = [&](int frame) {
        double sum = 0;
        const auto* a = reinterpret_cast<const unsigned char*>(basePixels.constData() + frame * frameBytes);
        const auto* b = reinterpret_cast<const unsigned char*>(introPixels.constData() + frame * frameBytes);
        for (qsizetype pixel = 0; pixel < frameBytes; pixel += 4)
            for (int channel = 0; channel < 3; ++channel) sum += std::abs(int(a[pixel + channel]) - int(b[pixel + channel]));
        return sum / (640.0 * 360 * 3);
    };
    require(difference(0) > 2.0, "intro appears in first encoded frame");
    require(difference(12) > 0.5, "intro remains during output-time animation at double speed");
    require(difference(24) < 1.5, "intro disappears by configured output time");

    // Persistent preview: rapid seeks coalesce and the final result belongs to the latest request.
    options.scene.state.chart = visualization::PlaybackVisualizationProjector().project(*music, 1, {});
    options.scene.state.durationUs = playback::PlaybackModel(music).durationUs();
    presentation::VideoExportPreviewRenderer preview;
    qint64 received = -1;
    QObject::connect(&preview, &presentation::VideoExportPreviewRenderer::frameReady,
                     [&received](const QImage&, qint64 time) { received = time; });
    preview.setScene(options.scene);
    for (int i = 0; i < 100; ++i) preview.request(i * 10'000, i * 20'000, true);
    QElapsedTimer timer; timer.start();
    while (received != 990'000 && timer.elapsed() < 10000) waitEvents(20);
    require(received == 990'000, "only the final seek is applied");
    {
        presentation::ExportDialog live({}, {}, options, 1'000'000, true, {});
        live.findChild<QTabWidget*>(QStringLiteral("exportTabs"))->setCurrentIndex(1);
        live.show();
        auto* status = live.findChild<QLabel*>(QStringLiteral("videoPreviewStatus"));
        timer.restart();
        while (!status->isHidden() && timer.elapsed() < 10000) waitEvents(20);
        require(status->isHidden(), "live page renders an initial background frame");
        auto* play = live.findChild<QPushButton*>(QStringLiteral("introPreviewPlay"));
        auto* slider = live.findChild<QSlider*>(QStringLiteral("introPreviewTimeline"));
        play->click();
        waitEvents(220);
        require(slider->value() >= 4 && slider->value() <= 10,
                "playback advances on the output clock without cumulative acceleration");
        play->click();
        const int pausedFrame = slider->value();
        waitEvents(150);
        require(slider->value() == pausedFrame, "pause preserves preview time");
        slider->setValue(12);
        require(slider->value() == 12, "timeline seeks to selected output frame");
        live.findChild<QPushButton*>(QStringLiteral("introPreviewLayout"))->click();
        require(slider->value() == 0, "layout mode returns to the first frame");
        const QString snapshots = qEnvironmentVariable("MIDI_PLAY_INTRO_SNAPSHOTS");
        if (!snapshots.isEmpty()) {
            live.resize(1150, 820);
            waitEvents(150);
            live.grab().save(snapshots + QStringLiteral("/live-export-dialog.png"));
        }
        play->click();
        live.findChild<QTabWidget*>(QStringLiteral("exportTabs"))->setCurrentIndex(0);
        const int hiddenFrame = slider->value();
        waitEvents(100);
        require(slider->value() == hiddenFrame, "switching to audio stops preview playback");
    }
    {
        auto* closing = new presentation::VideoExportPreviewRenderer;
        closing->setScene(options.scene);
        closing->request(0, 0, true);
        timer.restart();
        delete closing;
        require(timer.elapsed() < 100, "closing preview does not wait for GPU");
    }
    waitEvents(200);
#endif
}
} // namespace

int main(int argc, char** argv)
{
    QApplication application(argc, argv);
#ifdef Q_OS_WIN
    // The offscreen platform does not enumerate the Windows system font database.
    if (QGuiApplication::platformName() == QStringLiteral("offscreen")) {
        const QString fonts = qEnvironmentVariable("WINDIR") + QStringLiteral("/Fonts/");
        const int id = QFontDatabase::addApplicationFont(fonts + QStringLiteral("msyh.ttc"));
        require(id >= 0, "offscreen tests load a real CJK font");
        application.setFont(QFont(QFontDatabase::applicationFontFamilies(id).first(), 9));
        require(QFontMetricsF(application.font()).inFontUcs4(0x591c), "CJK fixture uses glyphs, not tofu");
    }
#endif
    if (application.arguments().contains(QStringLiteral("--integration"))) {
        testIntegration();
        return 0;
    }
    testLayoutAndAnimation();
    testValidation();
    testInterruptedDrag();
    const QString snapshots = qEnvironmentVariable("MIDI_PLAY_INTRO_SNAPSHOTS");
    if (!snapshots.isEmpty()) QDir().mkpath(snapshots);
    testEditorAndDialog(snapshots);
    std::puts("PASS: intro layout, animation, validation, dragging and dialog lifecycle");
    return 0;
}
