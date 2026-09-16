#include "widgetstyles.h"

namespace midi_play::presentation::theme {
namespace {

// Named placeholders keep shared layout rules independent from the palette.
QString colors(QString style, const WidgetColors& w)
{
    style.replace(QLatin1StringView("@window@"), w.window.name());
    style.replace(QLatin1StringView("@panel@"), w.panel.name());
    style.replace(QLatin1StringView("@text@"), w.text.name());
    style.replace(QLatin1StringView("@fileText@"), w.fileText.name());
    style.replace(QLatin1StringView("@mutedText@"), w.mutedText.name());
    style.replace(QLatin1StringView("@secondaryText@"), w.secondaryText.name());
    style.replace(QLatin1StringView("@timeText@"), w.timeText.name());
    style.replace(QLatin1StringView("@separator@"), w.separator.name());
    style.replace(QLatin1StringView("@buttonText@"), w.buttonText.name());
    style.replace(QLatin1StringView("@hover@"), w.hover.name());
    style.replace(QLatin1StringView("@border@"), w.border.name());
    style.replace(QLatin1StringView("@pressed@"), w.pressed.name());
    style.replace(QLatin1StringView("@disabledText@"), w.disabledText.name());
    style.replace(QLatin1StringView("@actionBorder@"), w.actionBorder.name());
    style.replace(QLatin1StringView("@play@"), w.play.name());
    style.replace(QLatin1StringView("@playHover@"), w.playHover.name());
    style.replace(QLatin1StringView("@playEdge@"), w.playEdge.name());
    style.replace(QLatin1StringView("@playPressed@"), w.playPressed.name());
    style.replace(QLatin1StringView("@pause@"), w.pause.name());
    style.replace(QLatin1StringView("@pauseHover@"), w.pauseHover.name());
    style.replace(QLatin1StringView("@pauseEdge@"), w.pauseEdge.name());
    style.replace(QLatin1StringView("@pausePressed@"), w.pausePressed.name());
    style.replace(QLatin1StringView("@stop@"), w.stop.name());
    style.replace(QLatin1StringView("@stopHover@"), w.stopHover.name());
    style.replace(QLatin1StringView("@stopEdge@"), w.stopEdge.name());
    style.replace(QLatin1StringView("@stopPressed@"), w.stopPressed.name());
    style.replace(QLatin1StringView("@disabled@"), w.disabled.name());
    style.replace(QLatin1StringView("@disabledBorder@"), w.disabledBorder.name());
    style.replace(QLatin1StringView("@divider@"), w.divider.name());
    style.replace(QLatin1StringView("@metronomeText@"), w.metronomeText.name());
    style.replace(QLatin1StringView("@metronome@"), w.metronome.name());
    style.replace(QLatin1StringView("@metronomeBorder@"), w.metronomeBorder.name());
    style.replace(QLatin1StringView("@metronomeHover@"), w.metronomeHover.name());
    style.replace(QLatin1StringView("@metronomeEdge@"), w.metronomeEdge.name());
    style.replace(QLatin1StringView("@checkedText@"), w.checkedText.name());
    style.replace(QLatin1StringView("@checked@"), w.checked.name());
    style.replace(QLatin1StringView("@checkedBorder@"), w.checkedBorder.name());
    style.replace(QLatin1StringView("@checkedHover@"), w.checkedHover.name());
    style.replace(QLatin1StringView("@closeHover@"), w.closeHover.name());
    style.replace(QLatin1StringView("@closePressed@"), w.closePressed.name());
    style.replace(QLatin1StringView("@sliderTrack@"), w.sliderTrack.name());
    style.replace(QLatin1StringView("@accent@"), w.accent.name());
    style.replace(QLatin1StringView("@input@"), w.input.name());
    style.replace(QLatin1StringView("@inputHoverBorder@"), w.inputHoverBorder.name());
    style.replace(QLatin1StringView("@readOnly@"), w.readOnly.name());
    style.replace(QLatin1StringView("@inputText@"), w.inputText.name());
    style.replace(QLatin1StringView("@inputHover@"), w.inputHover.name());
    style.replace(QLatin1StringView("@hintText@"), w.hintText.name());
    style.replace(QLatin1StringView("@errorText@"), w.errorText.name());
    style.replace(QLatin1StringView("@rateText@"), w.rateText.name());
    style.replace(QLatin1StringView("@rate@"), w.rate.name());
    style.replace(QLatin1StringView("@rateBorder@"), w.rateBorder.name());
    style.replace(QLatin1StringView("@rateHover@"), w.rateHover.name());
    style.replace(QLatin1StringView("@focus@"), w.focus.name());
    style.replace(QLatin1StringView("@ratePressed@"), w.ratePressed.name());
    style.replace(QLatin1StringView("@popup@"), w.popup.name());
    style.replace(QLatin1StringView("@popupBorder@"), w.popupBorder.name());
    style.replace(QLatin1StringView("@popupText@"), w.popupText.name());
    style.replace(QLatin1StringView("@popupInput@"), w.popupInput.name());
    style.replace(QLatin1StringView("@popupInputBorder@"), w.popupInputBorder.name());
    style.replace(QLatin1StringView("@rateTrack@"), w.rateTrack.name());
    style.replace(QLatin1StringView("@rateAccent@"), w.rateAccent.name());
    style.replace(QLatin1StringView("@handle@"), w.handle.name());
    style.replace(QLatin1StringView("@handleHover@"), w.handleHover.name());
    style.replace(QLatin1StringView("@onAccent@"), w.onAccent.name());
    return style;
}

} // namespace

QString mainWindowStyle(const AppTheme& theme)
{
    return colors(QStringLiteral(R"(
        QWidget#applicationRoot { background: @window@; color: @text@; }
        QWidget#topBar, QWidget#transportBar { background: @panel@; }
        QWidget#topBar { border-bottom: 1px solid @divider@; }
        QWidget#transportBar { border-top: 1px solid @divider@; }
        QLabel#brandLabel { color: @text@; font-size: 17px; font-weight: 600; }
        QLabel#fileLabel { color: @fileText@; font-size: 12px; }
        QLabel#statusLabel { color: @mutedText@; font-size: 11px; }
        QLabel#metricLabel { color: @secondaryText@; font-size: 11px; }
        QLabel#timeLabel { color: @timeText@; font-family: Consolas, monospace; font-size: 11px; }
        QFrame#toolbarSeparator { color: @separator@; max-height: 26px; }
        QToolButton { color: @buttonText@; border: 1px solid transparent; padding: 6px 8px; }
        QToolButton:hover { background: @hover@; border-color: @border@; }
        QToolButton:pressed { background: @pressed@; }
        QToolButton:disabled { color: @disabledText@; }
        QToolButton#playButton, QToolButton#pauseButton, QToolButton#stopButton {
            border: 1px solid @actionBorder@; border-radius: 0px; padding: 5px;
        }
        QToolButton#playButton { background: @play@; }
        QToolButton#playButton:hover { background: @playHover@; border-color: @playEdge@; }
        QToolButton#playButton:pressed { background: @playPressed@; }
        QToolButton#pauseButton { background: @pause@; }
        QToolButton#pauseButton:hover { background: @pauseHover@; border-color: @pauseEdge@; }
        QToolButton#pauseButton:pressed { background: @pausePressed@; }
        QToolButton#stopButton { background: @stop@; }
        QToolButton#stopButton:hover { background: @stopHover@; border-color: @stopEdge@; }
        QToolButton#stopButton:pressed { background: @stopPressed@; }
        QToolButton#playButton:disabled, QToolButton#pauseButton:disabled,
        QToolButton#stopButton:disabled { background: @disabled@; border-color: @disabledBorder@; }
        QToolButton#metronomeButton { color: @metronomeText@; background: @metronome@; border: 1px solid @metronomeBorder@; border-radius: 0px; font-weight: 600; }
        QToolButton#metronomeButton:hover { background: @metronomeHover@; border-color: @metronomeEdge@; }
        QToolButton#metronomeButton:checked { color: @checkedText@; background: @checked@; border-color: @checkedBorder@; }
        QToolButton#metronomeButton:checked:hover { background: @checkedHover@; }
        QToolButton#metronomeButton:disabled { color: @disabledText@; background: @disabled@; border-color: @disabledBorder@; }
        QToolButton#windowCloseButton:hover { background: @closeHover@; border-color: @closeHover@; }
        QToolButton#windowCloseButton:pressed { background: @closePressed@; border-color: @closePressed@; }
        QSlider::groove:horizontal { height: 4px; background: @sliderTrack@; }
        QSlider::sub-page:horizontal { background: @accent@; }
        QSlider::handle:horizontal { width: 14px; margin: -5px 0; border-radius: 7px; background: @handle@; }
        QSlider::handle:horizontal:hover { background: @accent@; }

        QToolTip { background: @popup@; color: @text@; border: 1px solid @border@; padding: 4px; }
    )"), theme.widgets);
}

QString settingsDialogStyle(const AppTheme& theme)
{
    return colors(QStringLiteral(R"(
        QDialog { background: @panel@; color: @text@; }
        QLabel { color: @text@; }
        QCheckBox { color: @text@; spacing: 8px; min-height: 28px; }
        QLabel#settingsTitle { color: @text@; font-size: 16px; font-weight: 600; }
        QLabel#settingsHint { color: @hintText@; font-size: 12px; }
        QLabel#settingsError { color: @errorText@; font-size: 12px; }
        QComboBox { min-height: 28px; padding: 2px 8px; background: @input@; color: @text@; border: 1px solid @border@; }
        QComboBox:hover { border-color: @inputHoverBorder@; }
        QSpinBox { min-height: 28px; padding: 2px 8px; background: @input@; color: @text@; border: 1px solid @border@; }
        QSpinBox:hover { border-color: @inputHoverBorder@; }
        QLineEdit { min-height: 28px; padding: 2px 8px; background: @readOnly@; color: @inputText@; border: 1px solid @border@; }
        QLineEdit:read-only { color: @secondaryText@; }
        QPushButton { min-width: 72px; min-height: 28px; color: @buttonText@; background: @input@; border: 1px solid @border@; }
        QPushButton:hover { background: @inputHover@; }
        QPushButton:disabled { color: @disabledText@; background: @readOnly@; border-color: @divider@; }

        QToolTip { background: @popup@; color: @text@; border: 1px solid @border@; padding: 4px; }

        QComboBox QAbstractItemView { background: @input@; color: @text@; selection-background-color: @play@; selection-color: @onAccent@; }
        QComboBox:disabled, QSpinBox:disabled { color: @disabledText@; background: @disabled@; }
        QComboBox:focus, QSpinBox:focus, QLineEdit:focus, QPushButton:focus { border-color: @focus@; }
        QCheckBox:disabled { color: @disabledText@; }
        QPushButton:pressed { background: @pressed@; }
    )"), theme.widgets);
}

QString playbackRateStyle(const AppTheme& theme)
{
    return colors(QStringLiteral(R"(
        QToolButton#playbackRateButton { color: @rateText@; background: @rate@; border: 1px solid @rateBorder@; border-radius: 0px; font-weight: 600; }
        QToolButton#playbackRateButton:hover, QToolButton#playbackRateButton:focus { background: @rateHover@; border-color: @focus@; }
        QToolButton#playbackRateButton:pressed { background: @ratePressed@; }
        QFrame#playbackRatePopup { background: @popup@; border: 1px solid @popupBorder@; border-radius: 0px; }
        QLabel#playbackRateLabel { color: @popupText@; font-size: 12px; }
        QSpinBox#playbackRateSpinBox { min-height: 28px; padding: 2px 6px; background: @popupInput@; color: @text@; border: 1px solid @popupInputBorder@; border-radius: 0px; }
        QSpinBox#playbackRateSpinBox:focus { border-color: @focus@; }
        QSlider#playbackRateSlider::groove:horizontal { height: 5px; background: @rateTrack@; border-radius: 0px; }
        QSlider#playbackRateSlider::sub-page:horizontal { background: @rateAccent@; border-radius: 0px; }
        QSlider#playbackRateSlider::add-page:horizontal { background: @rateTrack@; border-radius: 0px; }
        QSlider#playbackRateSlider::handle:horizontal { width: 16px; height: 16px; margin: -6px 0; border-radius: 0px; background: @handle@; }
        QSlider#playbackRateSlider::handle:horizontal:hover { background: @handleHover@; }

        QToolTip { background: @popup@; color: @text@; border: 1px solid @border@; padding: 4px; }
    )"), theme.widgets);
}

} // namespace midi_play::presentation::theme
