#include "core/Hotkey.h"

#include <algorithm>
#include <cctype>
#include <iterator>

namespace vs {

namespace {

struct NamedKey {
    unsigned vk;
    const char* name;
};

// Keys besides letters, digits and F-keys (virtual-key codes from WinUser.h).
constexpr NamedKey kNamedKeys[] = {
    {0x13, "Pause"},     {0x20, "Space"},        {0x21, "PageUp"},     {0x22, "PageDown"},  {0x23, "End"},
    {0x24, "Home"},      {0x25, "Left"},         {0x26, "Up"},         {0x27, "Right"},     {0x28, "Down"},
    {0x2D, "Insert"},    {0x2E, "Delete"},       {0x6A, "NumMultiply"}, {0x6B, "NumAdd"},   {0x6D, "NumSubtract"},
    {0x6E, "NumDecimal"}, {0x6F, "NumDivide"},
};

constexpr unsigned kPause = 0x13;
constexpr unsigned kF1 = 0x70;
constexpr unsigned kF13 = 0x7C;
constexpr unsigned kF24 = 0x87;

std::string lower(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return s;
}

} // namespace

std::string hotkeyKeyName(unsigned vk) {
    if ((vk >= 'A' && vk <= 'Z') || (vk >= '0' && vk <= '9')) {
        return std::string(1, static_cast<char>(vk));
    }
    if (vk >= kF1 && vk <= kF24) {
        return "F" + std::to_string(vk - kF1 + 1);
    }
    if (vk >= 0x60 && vk <= 0x69) {
        return "Num" + std::to_string(vk - 0x60);
    }
    for (const NamedKey& k : kNamedKeys) {
        if (k.vk == vk) {
            return k.name;
        }
    }
    return {};
}

std::string formatHotkey(const Hotkey& hotkey) {
    if (hotkey.empty()) {
        return {};
    }
    std::string s;
    if (hotkey.modifiers & Hotkey::kCtrl) s += "Ctrl+";
    if (hotkey.modifiers & Hotkey::kAlt) s += "Alt+";
    if (hotkey.modifiers & Hotkey::kShift) s += "Shift+";
    if (hotkey.modifiers & Hotkey::kWin) s += "Win+";
    return s + hotkeyKeyName(hotkey.vk);
}

bool parseHotkey(const std::string& text, Hotkey& out) {
    std::string t;
    std::copy_if(text.begin(), text.end(), std::back_inserter(t), [](unsigned char c) { return !std::isspace(c); });
    out = {};
    if (t.empty()) {
        return true;
    }
    Hotkey h;
    std::size_t start = 0;
    for (;;) {
        const std::size_t plus = t.find('+', start);
        const std::string part = lower(t.substr(start, plus == std::string::npos ? std::string::npos : plus - start));
        if (plus == std::string::npos) {
            // The key itself: compare with every supported key's name.
            for (unsigned vk = 1; vk < 0x100; ++vk) {
                const std::string name = hotkeyKeyName(vk);
                if (!name.empty() && lower(name) == part) {
                    h.vk = vk;
                    out = h;
                    return true;
                }
            }
            return false;
        }
        unsigned bit = 0;
        if (part == "ctrl" || part == "control") bit = Hotkey::kCtrl;
        else if (part == "alt") bit = Hotkey::kAlt;
        else if (part == "shift") bit = Hotkey::kShift;
        else if (part == "win" || part == "meta") bit = Hotkey::kWin;
        if (bit == 0 || (h.modifiers & bit) != 0) {
            return false;
        }
        h.modifiers |= bit;
        start = plus + 1;
    }
}

std::string hotkeyProblem(const Hotkey& hotkey) {
    if (hotkey.empty()) {
        return {};
    }
    if (hotkeyKeyName(hotkey.vk).empty()) {
        return "Use a letter, a number, an F-key or a navigation key";
    }
    const bool standsAlone = hotkey.vk == kPause || (hotkey.vk >= kF13 && hotkey.vk <= kF24);
    if (!standsAlone && (hotkey.modifiers & (Hotkey::kCtrl | Hotkey::kAlt | Hotkey::kWin)) == 0) {
        return "Hold Ctrl, Alt or Win as well, so the keys still work in other apps";
    }
    return {};
}

} // namespace vs
