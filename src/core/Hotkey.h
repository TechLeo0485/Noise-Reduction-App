#pragma once

#include <string>

namespace vs {

// A system-wide keyboard shortcut as text ("Ctrl+Alt+Shift+M") and as the values RegisterHotKey() takes.
// Qt-free and Windows-header-free so it can be tested on its own.
struct Hotkey {
    // Same bits as MOD_ALT / MOD_CONTROL / MOD_SHIFT / MOD_WIN.
    static constexpr unsigned kAlt = 0x1;
    static constexpr unsigned kCtrl = 0x2;
    static constexpr unsigned kShift = 0x4;
    static constexpr unsigned kWin = 0x8;

    unsigned modifiers = 0;
    unsigned vk = 0; // Windows virtual-key code; 0 = no shortcut

    [[nodiscard]] bool empty() const noexcept { return vk == 0; }
    friend bool operator==(const Hotkey&, const Hotkey&) = default;
};

// Name of a key that may be used in a shortcut (letters, digits, F1-F24, number pad, navigation keys),
// or "" if the key is not supported (modifier keys, punctuation, which differs between keyboard layouts).
[[nodiscard]] std::string hotkeyKeyName(unsigned vk);

// "Ctrl+Alt+Shift+Win+<key>" (modifiers always in that order); "" for an empty hotkey.
[[nodiscard]] std::string formatHotkey(const Hotkey& hotkey);

// Parses formatHotkey()'s text (case-insensitive, any modifier order, spaces ignored). "" parses as an
// empty hotkey. False for anything else.
bool parseHotkey(const std::string& text, Hotkey& out);

// Why a hotkey cannot be used, or "" if it can. A system-wide shortcut takes the keys away from every other
// app, so plain keys and Shift+key (typing) are refused; F13-F24 and Pause may stand alone.
[[nodiscard]] std::string hotkeyProblem(const Hotkey& hotkey);

} // namespace vs
