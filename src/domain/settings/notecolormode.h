#pragma once

#include <QMetaType>

namespace midi_play::settings {

enum class NoteColorMode : int { Normal = 0, Vivid = 1 };
inline constexpr NoteColorMode kDefaultNoteColorMode = NoteColorMode::Vivid;

inline constexpr bool isValidNoteColorMode(int value) noexcept
{
    return value == int(NoteColorMode::Normal) || value == int(NoteColorMode::Vivid);
}

inline constexpr NoteColorMode normalizeNoteColorMode(NoteColorMode mode) noexcept
{
    return mode == NoteColorMode::Normal ? mode : kDefaultNoteColorMode;
}

inline constexpr int noteColorModePersistentValue(NoteColorMode mode) noexcept
{
    return int(normalizeNoteColorMode(mode));
}

inline constexpr NoteColorMode noteColorModeFromPersistentValue(int value) noexcept
{
    return normalizeNoteColorMode(static_cast<NoteColorMode>(value));
}

} // namespace midi_play::settings

Q_DECLARE_METATYPE(midi_play::settings::NoteColorMode)
