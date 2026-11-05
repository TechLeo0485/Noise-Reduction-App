#include "core/Hotkey.h"

#include <gtest/gtest.h>

using vs::Hotkey;

TEST(Hotkey, FormatsAndParsesBack) {
    const Hotkey mute{Hotkey::kCtrl | Hotkey::kAlt | Hotkey::kShift, 'M'};
    EXPECT_EQ(vs::formatHotkey(mute), "Ctrl+Alt+Shift+M");
    Hotkey parsed;
    ASSERT_TRUE(vs::parseHotkey("Ctrl+Alt+Shift+M", parsed));
    EXPECT_EQ(parsed, mute);
    // Any order, any case, spaces.
    ASSERT_TRUE(vs::parseHotkey(" shift + ALT+ctrl+m ", parsed));
    EXPECT_EQ(parsed, mute);

    for (const char* text : {"Win+F9", "Ctrl+Num7", "Alt+PageDown", "Ctrl+Shift+NumAdd", "F13", "Pause", "Ctrl+0"}) {
        ASSERT_TRUE(vs::parseHotkey(text, parsed)) << text;
        EXPECT_EQ(vs::formatHotkey(parsed), text);
    }
    ASSERT_TRUE(vs::parseHotkey("Win+F9", parsed));
    EXPECT_EQ(parsed.vk, 0x78u);
    EXPECT_EQ(parsed.modifiers, Hotkey::kWin);
}

TEST(Hotkey, EmptyMeansOff) {
    Hotkey parsed{Hotkey::kCtrl, 'A'};
    ASSERT_TRUE(vs::parseHotkey("", parsed));
    EXPECT_TRUE(parsed.empty());
    EXPECT_EQ(vs::formatHotkey(parsed), "");
    EXPECT_EQ(vs::hotkeyProblem(parsed), "");
}

TEST(Hotkey, RejectsWhatItCannotRegister) {
    Hotkey parsed;
    for (const char* text : {"Ctrl+", "Ctrl+Ctrl+M", "Hyper+M", "Ctrl+;", "Ctrl+Alt", "M+Ctrl", "Ctrl+F25"}) {
        EXPECT_FALSE(vs::parseHotkey(text, parsed)) << text;
    }
    // Keys typed in other apps stay theirs.
    EXPECT_NE(vs::hotkeyProblem({0, 'M'}), "");
    EXPECT_NE(vs::hotkeyProblem({Hotkey::kShift, 'M'}), "");
    EXPECT_NE(vs::hotkeyProblem({Hotkey::kCtrl, 0xBA /* ; */}), "");
    EXPECT_EQ(vs::hotkeyProblem({Hotkey::kAlt, 'M'}), "");
    EXPECT_EQ(vs::hotkeyProblem({Hotkey::kShift, 0x7C /* F13 */}), "");
    EXPECT_EQ(vs::hotkeyProblem({0, 0x13 /* Pause */}), "");
}
