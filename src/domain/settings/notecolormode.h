#pragma once
// 音符配色模式的持久化协议，与时间、音符映射及几何无关。

#include <QMetaType>

namespace midi_play::settings {

// 普通模式保留基础外观，鲜明模式为默认配色。
enum class NoteColorMode : int { Normal = 0, Vivid = 1 };
inline constexpr NoteColorMode kDefaultNoteColorMode = NoteColorMode::Vivid;

// 检查配置整数是否对应已支持的配色。
inline constexpr bool isValidNoteColorMode(int value) noexcept
{
    return value == int(NoteColorMode::Normal) || value == int(NoteColorMode::Vivid);
}

// 非 Normal 输入统一回退默认 Vivid。
inline constexpr NoteColorMode normalizeNoteColorMode(NoteColorMode mode) noexcept
{
    return mode == NoteColorMode::Normal ? mode : kDefaultNoteColorMode;
}

// 写入规范化后的稳定枚举整数。
inline constexpr int noteColorModePersistentValue(NoteColorMode mode) noexcept
{
    return int(normalizeNoteColorMode(mode));
}

// 读取配置并按默认模式处理未知值。
inline constexpr NoteColorMode noteColorModeFromPersistentValue(int value) noexcept
{
    return normalizeNoteColorMode(static_cast<NoteColorMode>(value));
}

} // namespace midi_play::settings

Q_DECLARE_METATYPE(midi_play::settings::NoteColorMode)
