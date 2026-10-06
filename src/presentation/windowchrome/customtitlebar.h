#pragma once

#include <QWidget>

namespace midi_play::presentation::windowchrome {

class CustomTitleBar final : public QWidget {
public:
    explicit CustomTitleBar(QWidget* parent = nullptr);
    void setWindowActive(bool active);
};

} // namespace midi_play::presentation::windowchrome
