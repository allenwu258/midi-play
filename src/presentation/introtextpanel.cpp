#include "introtextpanel.h"

#include "visualization/introtext.h"

#include <QComboBox>
#include <QDoubleSpinBox>
#include <QFontComboBox>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QPlainTextEdit>
#include <QSlider>
#include <QSignalBlocker>
#include <QSpinBox>
#include <QVBoxLayout>
#include <algorithm>
#include <cmath>

namespace midi_play::presentation {
namespace {
constexpr int minimumFontSize = 12;
constexpr int maximumFontSize = 160;
constexpr int maximumOutlineWidthTenths = 40;

QWidget* section(const QString& title, QWidget* parent)
{
    auto* group = new QGroupBox(title, parent);
    group->setFlat(true);
    return group;
}

QSpinBox* createFontSize(int value, const QString& objectName, QWidget* parent)
{
    auto* size = new QSpinBox(parent);
    size->setObjectName(objectName);
    size->setRange(minimumFontSize, maximumFontSize);
    size->setSuffix(QStringLiteral(" px"));
    size->setValue(value);
    size->setToolTip(QStringLiteral("按 1280×720 参考画布像素设置，随输出分辨率等比缩放。"));
    return size;
}

QDoubleSpinBox* createSeconds(double minimum, double maximum, double step,
                              int milliseconds, const QString& objectName, QWidget* parent)
{
    auto* control = new QDoubleSpinBox(parent);
    control->setObjectName(objectName);
    control->setRange(minimum, maximum);
    control->setSingleStep(step);
    control->setDecimals(2);
    control->setSuffix(QStringLiteral(" 秒"));
    control->setValue(milliseconds / 1000.0);
    return control;
}
} // namespace

IntroTextPanel::IntroTextPanel(const app::IntroTextConfig& defaults, QWidget* parent)
    : QWidget(parent)
{
    setObjectName(QStringLiteral("introTextSettings"));
    auto* panelLayout = new QVBoxLayout(this);
    panelLayout->setContentsMargins(0, 0, 0, 0);
    panelLayout->setSpacing(8);

    auto* heading = new QLabel(QStringLiteral("入场文字"), this);
    QFont headingFont = heading->font();
    headingFont.setBold(true);
    heading->setFont(headingFont);
    panelLayout->addWidget(heading);

    auto* contentGroup = section(QStringLiteral("内容"), this);
    auto* contentLayout = new QVBoxLayout(contentGroup);
    contentLayout->setContentsMargins(8, 8, 8, 8);
    contentLayout->setSpacing(5);

    const auto addTextEditor = [this, contentLayout](const QString& label, const QString& objectName,
                                                    const QString& placeholder, const QString& value,
                                                    int minimumHeight, QSpinBox*& sizeControl,
                                                    const QString& sizeObjectName, int fontSize) {
        auto* header = new QWidget(this);
        auto* headerLayout = new QHBoxLayout(header);
        headerLayout->setContentsMargins(0, 0, 0, 0);
        auto* description = new QLabel(label, header);
        headerLayout->addWidget(description);
        headerLayout->addStretch(1);
        auto* sizeLabel = new QLabel(QStringLiteral("字号"), header);
        headerLayout->addWidget(sizeLabel);
        sizeControl = createFontSize(fontSize, sizeObjectName, header);
        sizeControl->setFixedWidth(104);
        headerLayout->addWidget(sizeControl);
        contentLayout->addWidget(header);

        auto* editor = new QPlainTextEdit(this);
        editor->setObjectName(objectName);
        editor->setAccessibleName(label);
        editor->setPlaceholderText(placeholder);
        editor->setPlainText(value);
        editor->setMinimumHeight(minimumHeight);
        editor->setMaximumHeight(minimumHeight + 24);
        editor->setTabChangesFocus(true);
        editor->setLineWrapMode(QPlainTextEdit::WidgetWidth);
        contentLayout->addWidget(editor);
        return editor;
    };

    m_titleText = addTextEditor(QStringLiteral("标题（可选）"), QStringLiteral("introTextTitleInput"),
        QStringLiteral("作品名称"), defaults.titleText, 54, m_titleSize,
        QStringLiteral("introTextTitleFontSize"), defaults.titleFontPixelSize);
    m_bodyText = addTextEditor(QStringLiteral("正文（可选）"), QStringLiteral("introTextBodyInput"),
        QStringLiteral("作词：…\n作曲：…\n演奏：…"), defaults.bodyText, 86, m_bodySize,
        QStringLiteral("introTextBodyFontSize"), defaults.bodyFontPixelSize);
    panelLayout->addWidget(contentGroup);

    auto* styleGroup = section(QStringLiteral("文字样式"), this);
    auto* styleForm = new QFormLayout(styleGroup);
    styleForm->setContentsMargins(8, 8, 8, 8);
    styleForm->setHorizontalSpacing(10);
    styleForm->setVerticalSpacing(5);

    m_introFont = new QFontComboBox(styleGroup);
    m_introFont->setObjectName(QStringLiteral("introTextFont"));
    m_introFont->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
    m_introFont->setMinimumContentsLength(12);
    m_introFont->setCurrentFont(QFont(defaults.fontFamily.isEmpty()
        ? QStringLiteral("Microsoft YaHei") : defaults.fontFamily));
    styleForm->addRow(QStringLiteral("字体"), m_introFont);

    auto* outlineRow = new QWidget(styleGroup);
    auto* outlineLayout = new QHBoxLayout(outlineRow);
    outlineLayout->setContentsMargins(0, 0, 0, 0);
    outlineLayout->setSpacing(8);
    m_outlineSlider = new QSlider(Qt::Horizontal, outlineRow);
    m_outlineSlider->setObjectName(QStringLiteral("introTextOutlineSlider"));
    m_outlineSlider->setRange(0, maximumOutlineWidthTenths);
    m_outlineSlider->setSingleStep(1);
    m_outlineSlider->setPageStep(2);
    m_outlineSlider->setAccessibleName(QStringLiteral("描边粗细"));
    m_outlineSlider->setToolTip(QStringLiteral("0 表示无描边；最大 4.0 基准 px。"));
    m_outlineSlider->setValue(std::clamp(defaults.outlineWidthTenths, 0, maximumOutlineWidthTenths));
    outlineLayout->addWidget(m_outlineSlider, 1);
    m_outlineSize = new QDoubleSpinBox(outlineRow);
    m_outlineSize->setObjectName(QStringLiteral("introTextOutlineWidth"));
    m_outlineSize->setRange(0.0, maximumOutlineWidthTenths / 10.0);
    m_outlineSize->setSingleStep(0.1);
    m_outlineSize->setDecimals(1);
    m_outlineSize->setSuffix(QStringLiteral(" px"));
    m_outlineSize->setValue(m_outlineSlider->value() / 10.0);
    m_outlineSize->setFixedWidth(92);
    outlineLayout->addWidget(m_outlineSize);
    styleForm->addRow(QStringLiteral("描边粗细"), outlineRow);

    auto* outlineHint = new QLabel(QStringLiteral("0 为无描边；字号和描边均按 720p 基准等比缩放。"), styleGroup);
    outlineHint->setObjectName(QStringLiteral("introTextOutlineHint"));
    outlineHint->setWordWrap(true);
    styleForm->addRow(QString(), outlineHint);
    panelLayout->addWidget(styleGroup);

    auto* layoutGroup = section(QStringLiteral("整体布局"), this);
    auto* layoutForm = new QFormLayout(layoutGroup);
    layoutForm->setContentsMargins(8, 8, 8, 8);
    layoutForm->setHorizontalSpacing(10);
    layoutForm->setVerticalSpacing(5);

    m_introWidth = new QSpinBox(layoutGroup);
    m_introWidth->setObjectName(QStringLiteral("introTextWidth"));
    m_introWidth->setRange(20, 90);
    m_introWidth->setSuffix(QStringLiteral("%"));
    m_introWidth->setValue(defaults.widthPercent);
    layoutForm->addRow(QStringLiteral("文字块宽度"), m_introWidth);

    m_introAlignment = new QComboBox(layoutGroup);
    m_introAlignment->setObjectName(QStringLiteral("introTextAlignment"));
    m_introAlignment->addItem(QStringLiteral("左对齐"), int(app::IntroTextAlignment::Left));
    m_introAlignment->addItem(QStringLiteral("居中"), int(app::IntroTextAlignment::Center));
    m_introAlignment->addItem(QStringLiteral("右对齐"), int(app::IntroTextAlignment::Right));
    m_introAlignment->setCurrentIndex(m_introAlignment->findData(int(defaults.alignment)));
    layoutForm->addRow(QStringLiteral("文字内部对齐"), m_introAlignment);

    m_introPosition = new QComboBox(layoutGroup);
    m_introPosition->setObjectName(QStringLiteral("introTextPosition"));
    m_introPosition->addItem(QStringLiteral("居中"), int(app::IntroTextPosition::Center));
    m_introPosition->addItem(QStringLiteral("左下"), int(app::IntroTextPosition::LowerLeft));
    m_introPosition->addItem(QStringLiteral("自定义 X/Y"), int(app::IntroTextPosition::Custom));
    m_introPosition->setCurrentIndex(m_introPosition->findData(int(defaults.position)));
    layoutForm->addRow(QStringLiteral("画布位置"), m_introPosition);

    auto* offsets = new QWidget(layoutGroup);
    auto* offsetLayout = new QHBoxLayout(offsets);
    offsetLayout->setContentsMargins(0, 0, 0, 0);
    offsetLayout->setSpacing(6);
    offsetLayout->addWidget(new QLabel(QStringLiteral("X"), offsets));
    m_introX = new QSpinBox(offsets);
    m_introX->setObjectName(QStringLiteral("introTextOffsetX"));
    m_introX->setRange(-1280, 1280);
    m_introX->setValue(defaults.offsetX);
    offsetLayout->addWidget(m_introX, 1);
    offsetLayout->addWidget(new QLabel(QStringLiteral("Y"), offsets));
    m_introY = new QSpinBox(offsets);
    m_introY->setObjectName(QStringLiteral("introTextOffsetY"));
    m_introY->setRange(-720, 720);
    m_introY->setValue(defaults.offsetY);
    offsetLayout->addWidget(m_introY, 1);
    layoutForm->addRow(QStringLiteral("偏移 X / Y"), offsets);
    panelLayout->addWidget(layoutGroup);

    auto* timingGroup = section(QStringLiteral("显示与退场"), this);
    auto* timingForm = new QFormLayout(timingGroup);
    timingForm->setContentsMargins(8, 8, 8, 8);
    timingForm->setHorizontalSpacing(10);
    timingForm->setVerticalSpacing(5);
    m_introDisplay = createSeconds(0.0, 600.0, 0.1, defaults.displayMilliseconds,
        QStringLiteral("introTextDisplayTime"), timingGroup);
    timingForm->addRow(QStringLiteral("完整显示"), m_introDisplay);
    m_introExit = createSeconds(0.1, 5.0, 0.1, defaults.exitMilliseconds,
        QStringLiteral("introTextExitDuration"), timingGroup);
    timingForm->addRow(QStringLiteral("退场用时"), m_introExit);
    m_introEffect = new QComboBox(timingGroup);
    m_introEffect->setObjectName(QStringLiteral("introTextExitEffect"));
    m_introEffect->addItem(QStringLiteral("淡出"), int(app::IntroTextExitEffect::Fade));
    m_introEffect->addItem(QStringLiteral("向下滑出"), int(app::IntroTextExitEffect::SlideDown));
    m_introEffect->setCurrentIndex(m_introEffect->findData(int(defaults.exitEffect)));
    m_introEffect->setToolTip(QStringLiteral("向下滑出：文字块向下移出画布，并同时逐渐淡出。"));
    timingForm->addRow(QStringLiteral("退场效果"), m_introEffect);
    panelLayout->addWidget(timingGroup);

    auto* hint = new QLabel(QStringLiteral("标题与正文作为一个文字块预览和拖动。X 向右、Y 向下。"), this);
    hint->setWordWrap(true);
    panelLayout->addWidget(hint);

    m_lastConfig = config();
    const bool custom = m_lastConfig.position == app::IntroTextPosition::Custom;
    m_introX->setEnabled(custom);
    m_introY->setEnabled(custom);

    connect(m_titleText, &QPlainTextEdit::textChanged, this, [this] { changed(true); });
    connect(m_bodyText, &QPlainTextEdit::textChanged, this, [this] { changed(true); });
    connect(m_introFont, &QFontComboBox::currentFontChanged, this, [this] { changed(true); });
    connect(m_titleSize, &QSpinBox::valueChanged, this, [this] { changed(true); });
    connect(m_bodySize, &QSpinBox::valueChanged, this, [this] { changed(true); });
    connect(m_introWidth, &QSpinBox::valueChanged, this, [this] { changed(true); });
    connect(m_introX, &QSpinBox::valueChanged, this, [this] { changed(true); });
    connect(m_introY, &QSpinBox::valueChanged, this, [this] { changed(true); });
    connect(m_introAlignment, &QComboBox::currentIndexChanged, this, [this] { changed(true); });
    connect(m_outlineSlider, &QSlider::valueChanged, this, [this](int tenths) {
        const QSignalBlocker blocker(m_outlineSize);
        m_outlineSize->setValue(tenths / 10.0);
        changed(true);
    });
    connect(m_outlineSize, &QDoubleSpinBox::valueChanged, this, [this](double width) {
        const int tenths = qRound(width * 10.0);
        const QSignalBlocker blocker(m_outlineSlider);
        m_outlineSlider->setValue(tenths);
        changed(true);
    });
    connect(m_introPosition, &QComboBox::currentIndexChanged, this, [this] {
        if (m_updating) return;
        if (config().position == app::IntroTextPosition::Custom) {
            visualization::IntroTextAsset asset;
            QString error;
            if (asset.prepare(m_lastConfig, QSize(512, 288), &error)) {
                const QPointF offset = asset.logicalBounds().center() - QPointF(640, 360);
                m_updating = true;
                m_introX->setValue(qRound(offset.x()));
                m_introY->setValue(qRound(offset.y()));
                m_updating = false;
            }
        }
        changed(true);
    });
    connect(m_introDisplay, &QDoubleSpinBox::valueChanged, this, [this] { changed(false); });
    connect(m_introExit, &QDoubleSpinBox::valueChanged, this, [this] { changed(false); });
    connect(m_introEffect, &QComboBox::currentIndexChanged, this, [this] { changed(false); });
}

void IntroTextPanel::changed(bool layoutChanged)
{
    if (m_updating) return;
    m_lastConfig = config();
    const bool custom = m_lastConfig.position == app::IntroTextPosition::Custom;
    m_introX->setEnabled(custom);
    m_introY->setEnabled(custom);
    emit configurationChanged(layoutChanged);
}

void IntroTextPanel::setPositionFromDrag(int x, int y)
{
    m_updating = true;
    m_introPosition->setCurrentIndex(m_introPosition->findData(int(app::IntroTextPosition::Custom)));
    m_introX->setValue(x);
    m_introY->setValue(y);
    m_updating = false;
    changed(true);
}

app::IntroTextConfig IntroTextPanel::config() const
{
    app::IntroTextConfig config;
    config.enabled = true;
    config.titleText = m_titleText ? m_titleText->toPlainText() : QString();
    config.bodyText = m_bodyText ? m_bodyText->toPlainText() : QString();
    config.fontFamily = m_introFont ? m_introFont->currentFont().family() : QStringLiteral("Microsoft YaHei");
    config.titleFontPixelSize = m_titleSize ? m_titleSize->value() : 48;
    config.bodyFontPixelSize = m_bodySize ? m_bodySize->value() : 32;
    config.outlineWidthTenths = m_outlineSlider ? m_outlineSlider->value() : 8;
    config.widthPercent = m_introWidth ? m_introWidth->value() : 80;
    config.alignment = m_introAlignment
        ? app::IntroTextAlignment(m_introAlignment->currentData().toInt()) : app::IntroTextAlignment::Center;
    config.position = m_introPosition
        ? app::IntroTextPosition(m_introPosition->currentData().toInt()) : app::IntroTextPosition::Center;
    config.offsetX = m_introX ? m_introX->value() : 0;
    config.offsetY = m_introY ? m_introY->value() : 0;
    config.displayMilliseconds = m_introDisplay ? qRound(m_introDisplay->value() * 1000.0) : 5'000;
    config.exitMilliseconds = m_introExit ? qRound(m_introExit->value() * 1000.0) : 1'000;
    config.exitEffect = m_introEffect
        ? app::IntroTextExitEffect(m_introEffect->currentData().toInt()) : app::IntroTextExitEffect::Fade;
    return config;
}

} // namespace midi_play::presentation
