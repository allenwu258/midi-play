#include "introtextpanel.h"
#include "visualization/introtext.h"
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QFontComboBox>
#include <QFormLayout>
#include <QLabel>
#include <QPlainTextEdit>
#include <QSpinBox>
#include <QVBoxLayout>

namespace midi_play::presentation {
IntroTextPanel::IntroTextPanel(const app::IntroTextConfig& defaults, QWidget* parent)
    : QWidget(parent)
{
    setObjectName(QStringLiteral("introTextSettings"));
    auto* panelLayout = new QVBoxLayout(this);
    panelLayout->setContentsMargins(0, 0, 0, 0);
    panelLayout->setSpacing(8);
    auto* title = new QLabel(QStringLiteral("入场文字"), this);
    QFont titleFont = title->font();
    titleFont.setBold(true);
    title->setFont(titleFont);
    panelLayout->addWidget(title);
    m_introText = new QPlainTextEdit(this);
    m_introText->setObjectName(QStringLiteral("introTextInput"));
    m_introText->setPlaceholderText(QStringLiteral("例如：\n作品名称\n作曲：\n演奏："));
    m_introText->setPlainText(defaults.text);
    m_introText->setMinimumHeight(104);
    m_introText->setTabChangesFocus(true);
    m_introText->setMaximumHeight(132);
    panelLayout->addWidget(m_introText);

    auto* typeForm = new QFormLayout;
    typeForm->setContentsMargins(0, 0, 0, 0);
    typeForm->setHorizontalSpacing(10);
    typeForm->setVerticalSpacing(7);
    m_introFont = new QFontComboBox(this);
    m_introFont->setObjectName(QStringLiteral("introTextFont"));
    m_introFont->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
    m_introFont->setMinimumContentsLength(12);
    const QFont initialFont(defaults.fontFamily.isEmpty()
                                ? QStringLiteral("Microsoft YaHei") : defaults.fontFamily);
    m_introFont->setCurrentFont(initialFont);
    typeForm->addRow(QStringLiteral("字体"), m_introFont);

    m_introSize = new QSpinBox(this);
    m_introSize->setObjectName(QStringLiteral("introTextFontSize"));
    m_introSize->setRange(12, 160);
    m_introSize->setSuffix(QStringLiteral(" 基准 px"));
    m_introSize->setValue(defaults.fontPixelSize);
    typeForm->addRow(QStringLiteral("字号"), m_introSize);
    m_introWidth = new QSpinBox(this);
    m_introWidth->setObjectName(QStringLiteral("introTextWidth"));
    m_introWidth->setRange(20, 90);
    m_introWidth->setSuffix(QStringLiteral("%"));
    m_introWidth->setValue(defaults.widthPercent);
    typeForm->addRow(QStringLiteral("文字块宽度"), m_introWidth);

    m_introAlignment = new QComboBox(this);
    m_introAlignment->setObjectName(QStringLiteral("introTextAlignment"));
    m_introAlignment->addItem(QStringLiteral("左对齐"), int(app::IntroTextAlignment::Left));
    m_introAlignment->addItem(QStringLiteral("居中"), int(app::IntroTextAlignment::Center));
    m_introAlignment->addItem(QStringLiteral("右对齐"), int(app::IntroTextAlignment::Right));
    m_introAlignment->setCurrentIndex(m_introAlignment->findData(int(defaults.alignment)));
    typeForm->addRow(QStringLiteral("文字内部对齐"), m_introAlignment);

    m_introPosition = new QComboBox(this);
    m_introPosition->setObjectName(QStringLiteral("introTextPosition"));
    m_introPosition->addItem(QStringLiteral("居中"), int(app::IntroTextPosition::Center));
    m_introPosition->addItem(QStringLiteral("左下"), int(app::IntroTextPosition::LowerLeft));
    m_introPosition->addItem(QStringLiteral("自定义 X/Y"), int(app::IntroTextPosition::Custom));
    m_introPosition->setCurrentIndex(m_introPosition->findData(int(defaults.position)));
    typeForm->addRow(QStringLiteral("文字块位置"), m_introPosition);

    auto* offsets = new QWidget(this);
    auto* offsetLayout = new QHBoxLayout(offsets);
    offsetLayout->setContentsMargins(0, 0, 0, 0);
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
    typeForm->addRow(QStringLiteral("偏移 X / Y"), offsets);
    panelLayout->addLayout(typeForm);

    auto* timingForm = new QFormLayout;
    timingForm->setContentsMargins(0, 2, 0, 0);
    timingForm->setHorizontalSpacing(10);
    timingForm->setVerticalSpacing(7);
    m_introDisplay = new QDoubleSpinBox(this);
    m_introDisplay->setObjectName(QStringLiteral("introTextDisplayTime"));
    m_introDisplay->setRange(0.0, 600.0);
    m_introDisplay->setSingleStep(0.1);
    m_introDisplay->setDecimals(2);
    m_introDisplay->setSuffix(QStringLiteral(" 秒"));
    m_introDisplay->setValue(defaults.displayMilliseconds / 1000.0);
    timingForm->addRow(QStringLiteral("完整显示"), m_introDisplay);

    m_introExit = new QDoubleSpinBox(this);
    m_introExit->setObjectName(QStringLiteral("introTextExitDuration"));
    m_introExit->setRange(0.1, 5.0);
    m_introExit->setSingleStep(0.1);
    m_introExit->setDecimals(2);
    m_introExit->setSuffix(QStringLiteral(" 秒"));
    m_introExit->setValue(defaults.exitMilliseconds / 1000.0);
    timingForm->addRow(QStringLiteral("退场用时"), m_introExit);

    m_introEffect = new QComboBox(this);
    m_introEffect->setObjectName(QStringLiteral("introTextExitEffect"));
    m_introEffect->addItem(QStringLiteral("淡出"), int(app::IntroTextExitEffect::Fade));
    m_introEffect->addItem(QStringLiteral("向下滑出"), int(app::IntroTextExitEffect::SlideDown));
    m_introEffect->setCurrentIndex(m_introEffect->findData(int(defaults.exitEffect)));
    m_introEffect->setToolTip(QStringLiteral("向下滑出：文字块向下移出画布，并同时逐渐淡出。"));
    timingForm->addRow(QStringLiteral("退场效果"), m_introEffect);
    panelLayout->addLayout(timingForm);


    auto* hint = new QLabel(QStringLiteral("白字黑描边；字号和偏移按 720p 画布等比缩放。\nX 向右、Y 向下；在左侧布局预览中可拖动文字块。"), this);
    hint->setWordWrap(true);
    panelLayout->addWidget(hint);
    m_lastConfig = config();
    m_introX->setEnabled(m_lastConfig.position == app::IntroTextPosition::Custom);
    m_introY->setEnabled(m_introX->isEnabled());
    connect(m_introText, &QPlainTextEdit::textChanged, this, [this] { changed(true); });
    connect(m_introFont, &QFontComboBox::currentFontChanged, this, [this] { changed(true); });
    for (auto* spin : {m_introSize, m_introWidth, m_introX, m_introY})
        connect(spin, &QSpinBox::valueChanged, this, [this] { changed(true); });
    connect(m_introAlignment, &QComboBox::currentIndexChanged, this, [this] { changed(true); });
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
    config.widthPercent = m_introWidth->value();
    config.text = m_introText ? m_introText->toPlainText() : QString();
    config.fontFamily = m_introFont ? m_introFont->currentFont().family() : QStringLiteral("Microsoft YaHei");
    config.fontPixelSize = m_introSize ? m_introSize->value() : 32;
    config.alignment = m_introAlignment
        ? app::IntroTextAlignment(m_introAlignment->currentData().toInt()) : app::IntroTextAlignment::Center;
    config.position = m_introPosition
        ? app::IntroTextPosition(m_introPosition->currentData().toInt()) : app::IntroTextPosition::Center;
    config.offsetX = m_introX ? m_introX->value() : 0;
    config.offsetY = m_introY ? m_introY->value() : 0;
    config.displayMilliseconds = m_introDisplay ? qRound(m_introDisplay->value() * 1000.0) : 5000;
    config.exitMilliseconds = m_introExit ? qRound(m_introExit->value() * 1000.0) : 1000;
    config.exitEffect = m_introEffect
        ? app::IntroTextExitEffect(m_introEffect->currentData().toInt()) : app::IntroTextExitEffect::Fade;
    return config;
}

} // namespace midi_play::presentation
