#ifdef MESHTASTIC_INCLUDE_INKHUD

#include "configuration.h"

#include "../Menu/MenuApplet.h"
#include "../../User/ThreadedMessage/ThreadedMessageApplet.h"
#include "../../User/Heard/HeardApplet.h"
#include "../../User/DMChat/DMChatApplet.h"
#include "../../User/UniChat/UniChatApplet.h"
#include "../../User/NavMap/NavMapApplet.h"
#include "InputMenuApplet.h"

#include <cstdlib> // strtof (coordinate parse)

#include "MeshService.h"
#include "MessageStore.h" // sendText: record outgoing DMs (no local loopback exists for them)
#include "PowerFSM.h" // T-Deck Max sleep-UX: EVENT_INPUT to re-stamp the ON timer on keyboard activity
#include "sleep.h"    // T-Deck Max interactive-nap: tdeckMaxKbWakeUntilMs keystroke extension
#include "Router.h"
#include "main.h"

#if defined(MOD_CJK_ENABLED)
#include "graphics/niche/Fonts/cubicFont.h"
#endif //defined(MOD_CJK_ENABLED)

#if defined(MOD_I2C_TCA8418_KEYBOARD)
// Concrete driver header (it includes TCA8418KeyboardBase.h itself, which has no include guard,
// so we must not include the base directly as well): needed for clearModifiers() casts below.
#include "input/TDeckMaxTKeyboard.h"
// Published by the board's lateInitVariant() (extra_variants/t_deck_max/variant.cpp).
// inkhudI2CKeyboard is the standalone driver (InputBroker is excluded in InkHUD builds).
// Input is POLL-ONLY: KB_IRQ_PIN must never get an attachInterrupt handler (the light-sleep
// wake arming rewrites the pin to level-low and the held-low INT line then storms the ISR -
// see the warning in the variant file).
TCA8418KeyboardBase *inkhudI2CKeyboard = nullptr;
#endif //defined(MOD_I2C_TCA8418_KEYBOARD)

#if defined(MOD_UART_KEYBOARD_12KEY)
#if defined(MOD_UART_KEYBOARD_12KEY_UPSIDEDOWN)
#define KEY_TOUCH_BACK  0xEC
#define KEY_TOUCH_UP    0xEB
#define KEY_TOUCH_ENTER 0xEA
#define KEY_TOUCH_LEFT  0xE9
#define KEY_TOUCH_DOWN  0xE8
#define KEY_TOUCH_RIGHT 0xE7
#else //!defined(MOD_UART_KEYBOARD_12KEY_UPSIDEDOWN)
#define KEY_TOUCH_BACK  0xE7
#define KEY_TOUCH_UP    0xE8
#define KEY_TOUCH_ENTER 0xE9
#define KEY_TOUCH_LEFT  0xEA
#define KEY_TOUCH_DOWN  0xEB
#define KEY_TOUCH_RIGHT 0xEC
#endif //defined(MOD_UART_KEYBOARD_12KEY_UPSIDEDOWN)
#endif //defined(MOD_UART_KEYBOARD_12KEY)

// The T-keyboard CIM path selects the bopomofo boards keyboards[5]/[6] (selKB = bShift ? 6 : 5) on the
// '0'/CIM toggle, but those boards are only constructed under MOD_CJK_ENABLED. Enforce the coupling so a
// future TKey-model-without-CJK build fails to compile instead of indexing keyboards out of bounds at runtime.
#if defined(MOD_TKEY_MODEL) && !defined(MOD_CJK_ENABLED)
#error "The T-keyboard input model requires MOD_CJK_ENABLED (the CIM toggle selects the CJK-only keyboards[5]/[6])."
#endif

using namespace NicheGraphics;

static constexpr uint8_t INPUT_TIMEOUT_SEC = 15; // How many seconds before menu auto-closes
static constexpr uint8_t LOCK_TIMEOUT_SEC = 15;  // How many seconds before menu auto-locks
static constexpr uint16_t COMBO_TAP_WINDOW_MS = 350; // Grove 12-key: per-tap window for the single/double/triple combo decoder

InkHUD::InputMenuApplet::InputMenuApplet() : concurrency::OSThread("InputMenuApplet")
{
#if defined(MOD_UART_KEYBOARD_12KEY) || defined(MOD_UART_T_KEYBOARD) || defined(MOD_I2C_TCA8418_KEYBOARD)
    OSThread::setIntervalFromNow(500);
#else //!(defined(MOD_UART_KEYBOARD_12KEY) || defined(MOD_UART_T_KEYBOARD) || defined(MOD_I2C_TCA8418_KEYBOARD))
    OSThread::disable();
#endif //defined(MOD_UART_KEYBOARD_12KEY) || defined(MOD_UART_T_KEYBOARD) || defined(MOD_I2C_TCA8418_KEYBOARD)

#if defined(MOD_TKEY_MODEL) // BBQ10-layout keyboards (UART T-keyboard / T-Deck Max TCA8418)
    if (true) { // function keyboard (0): full 10x3, mirroring the physical alt layer 1:1
        // The right half (cols 5-9) keeps the legacy 5x3 assignments at their original physical
        // positions. The left half adds a second arrow cluster at the T-Deck Max printed alt
        // keycap positions (↑ on E, ← on S, → on F, ↓ on X) plus Esc-to-close on Q; unlabeled
        // cells are funcCode -1 no-ops, free for future mappings.
        Keyboard kb;
        std::get<0>(kb) = "Fn";
        std::get<1>(kb).emplace_back(std::vector<Key>());
        std::get<1>(kb).back().emplace_back("Esc", 9);
        std::get<1>(kb).back().emplace_back("", -1);
        std::get<1>(kb).back().emplace_back("↑", 2);
        std::get<1>(kb).back().emplace_back("", -1);
        std::get<1>(kb).back().emplace_back("", -1);
        std::get<1>(kb).back().emplace_back("🔅", 0);
        std::get<1>(kb).back().emplace_back("⏮", 1);
        std::get<1>(kb).back().emplace_back("↑", 2);
        std::get<1>(kb).back().emplace_back("↕", 3);
        std::get<1>(kb).back().emplace_back("off", 4);
        std::get<1>(kb).emplace_back(std::vector<Key>());
        std::get<1>(kb).back().emplace_back("", -1);
        std::get<1>(kb).back().emplace_back("←", 6);
        std::get<1>(kb).back().emplace_back("<->", 15); // toggle focused tile (wraps; for the 2-tile split)
        std::get<1>(kb).back().emplace_back("→", 8);
        std::get<1>(kb).back().emplace_back("", -1);
        std::get<1>(kb).back().emplace_back("🔆", 5);
        std::get<1>(kb).back().emplace_back("←", 6);
        std::get<1>(kb).back().emplace_back("↓", 7);
        std::get<1>(kb).back().emplace_back("→", 8);
        std::get<1>(kb).back().emplace_back("✖", 9);
        std::get<1>(kb).emplace_back(std::vector<Key>());
        std::get<1>(kb).back().emplace_back("", -1);
        std::get<1>(kb).back().emplace_back("", -1);
        std::get<1>(kb).back().emplace_back("↓", 7);
        std::get<1>(kb).back().emplace_back("", -1);
        std::get<1>(kb).back().emplace_back("Vib", 16); // alt+V: vibration policy All -> DMs -> Off
        std::get<1>(kb).back().emplace_back("☀", 10);   // alt+B: e-ink frontlight cycle (now wired)
        std::get<1>(kb).back().emplace_back("Ch", 11);
        std::get<1>(kb).back().emplace_back("⚙", 12);
        std::get<1>(kb).back().emplace_back("@", 13);
        std::get<1>(kb).back().emplace_back("✔", 14);
        keyboards.emplace_back(kb);
    }
    if (true) { // base keyboard (1)
        Keyboard kb;
        std::get<0>(kb) = "ab";
        std::get<1>(kb).emplace_back(std::vector<Key>());
        std::get<1>(kb).back().emplace_back("q", 0);
        std::get<1>(kb).back().emplace_back("w", 1);
        std::get<1>(kb).back().emplace_back("e", 2);
        std::get<1>(kb).back().emplace_back("r", 3);
        std::get<1>(kb).back().emplace_back("t", 4);
        std::get<1>(kb).back().emplace_back("y", 5);
        std::get<1>(kb).back().emplace_back("u", 6);
        std::get<1>(kb).back().emplace_back("i", 7);
        std::get<1>(kb).back().emplace_back("o", 8);
        std::get<1>(kb).back().emplace_back("p", 9);
        std::get<1>(kb).emplace_back(std::vector<Key>());
        std::get<1>(kb).back().emplace_back("a", 10);
        std::get<1>(kb).back().emplace_back("s", 11);
        std::get<1>(kb).back().emplace_back("d", 12);
        std::get<1>(kb).back().emplace_back("f", 13);
        std::get<1>(kb).back().emplace_back("g", 14);
        std::get<1>(kb).back().emplace_back("h", 15);
        std::get<1>(kb).back().emplace_back("j", 16);
        std::get<1>(kb).back().emplace_back("k", 17);
        std::get<1>(kb).back().emplace_back("l", 18);
        std::get<1>(kb).back().emplace_back("⬅", 19);
        std::get<1>(kb).emplace_back(std::vector<Key>());
        std::get<1>(kb).back().emplace_back("⇒", 20);
        std::get<1>(kb).back().emplace_back("z", 21);
        std::get<1>(kb).back().emplace_back("x", 22);
        std::get<1>(kb).back().emplace_back("c", 23);
        std::get<1>(kb).back().emplace_back("v", 24);
        std::get<1>(kb).back().emplace_back("b", 25);
        std::get<1>(kb).back().emplace_back("n", 26);
        std::get<1>(kb).back().emplace_back("m", 27);
        std::get<1>(kb).back().emplace_back("@", 28);
        std::get<1>(kb).back().emplace_back("↙", 29);
        keyboards.emplace_back(kb);
    }
    if (true) { // shift keyboard (2)
        Keyboard kb;
        std::get<0>(kb) = "AB";
        std::get<1>(kb).emplace_back(std::vector<Key>());
        std::get<1>(kb).back().emplace_back("Q", 0);
        std::get<1>(kb).back().emplace_back("W", 1);
        std::get<1>(kb).back().emplace_back("E", 2);
        std::get<1>(kb).back().emplace_back("R", 3);
        std::get<1>(kb).back().emplace_back("T", 4);
        std::get<1>(kb).back().emplace_back("Y", 5);
        std::get<1>(kb).back().emplace_back("U", 6);
        std::get<1>(kb).back().emplace_back("I", 7);
        std::get<1>(kb).back().emplace_back("O", 8);
        std::get<1>(kb).back().emplace_back("P", 9);
        std::get<1>(kb).emplace_back(std::vector<Key>());
        std::get<1>(kb).back().emplace_back("A", 10);
        std::get<1>(kb).back().emplace_back("S", 11);
        std::get<1>(kb).back().emplace_back("D", 12);
        std::get<1>(kb).back().emplace_back("F", 13);
        std::get<1>(kb).back().emplace_back("G", 14);
        std::get<1>(kb).back().emplace_back("H", 15);
        std::get<1>(kb).back().emplace_back("J", 16);
        std::get<1>(kb).back().emplace_back("K", 17);
        std::get<1>(kb).back().emplace_back("L", 18);
        std::get<1>(kb).back().emplace_back("⬅", 19);
        std::get<1>(kb).emplace_back(std::vector<Key>());
        std::get<1>(kb).back().emplace_back("⇒", 20);
        std::get<1>(kb).back().emplace_back("Z", 21);
        std::get<1>(kb).back().emplace_back("X", 22);
        std::get<1>(kb).back().emplace_back("C", 23);
        std::get<1>(kb).back().emplace_back("V", 24);
        std::get<1>(kb).back().emplace_back("B", 25);
        std::get<1>(kb).back().emplace_back("N", 26);
        std::get<1>(kb).back().emplace_back("M", 27);
        std::get<1>(kb).back().emplace_back("$", 28);
        std::get<1>(kb).back().emplace_back("✔", 29);
        keyboards.emplace_back(kb);
    }
    if (true) { // symbol keyboard (3)
        Keyboard kb;
        std::get<0>(kb) = "#";
        std::get<1>(kb).emplace_back(std::vector<Key>());
        std::get<1>(kb).back().emplace_back("#", 0);
        std::get<1>(kb).back().emplace_back("1", 1);
        std::get<1>(kb).back().emplace_back("2", 2);
        std::get<1>(kb).back().emplace_back("3", 3);
        std::get<1>(kb).back().emplace_back("(", 4);
        std::get<1>(kb).back().emplace_back(")", 5);
        std::get<1>(kb).back().emplace_back("_", 6);
        std::get<1>(kb).back().emplace_back("-", 7);
        std::get<1>(kb).back().emplace_back("+", 8);
        std::get<1>(kb).back().emplace_back("@", 9);
        std::get<1>(kb).emplace_back(std::vector<Key>());
        std::get<1>(kb).back().emplace_back("*", 10);
        std::get<1>(kb).back().emplace_back("4", 11);
        std::get<1>(kb).back().emplace_back("5", 12);
        std::get<1>(kb).back().emplace_back("6", 13);
        std::get<1>(kb).back().emplace_back("/", 14);
        std::get<1>(kb).back().emplace_back(":", 15);
        std::get<1>(kb).back().emplace_back(";", 16);
        std::get<1>(kb).back().emplace_back("\'", 17);
        std::get<1>(kb).back().emplace_back("\"", 18);
        std::get<1>(kb).back().emplace_back("⬅", 19);
        std::get<1>(kb).emplace_back(std::vector<Key>());
        std::get<1>(kb).back().emplace_back("⇒", 20);
        std::get<1>(kb).back().emplace_back("7", 21);
        std::get<1>(kb).back().emplace_back("8", 22);
        std::get<1>(kb).back().emplace_back("9", 23);
        std::get<1>(kb).back().emplace_back("?", 24);
        std::get<1>(kb).back().emplace_back("!", 25);
        std::get<1>(kb).back().emplace_back(",", 26);
        std::get<1>(kb).back().emplace_back(".", 27);
        std::get<1>(kb).back().emplace_back("~", 28);
        std::get<1>(kb).back().emplace_back("↙", 29);
        keyboards.emplace_back(kb);
    }
    if (true) { // emoji keyboard (4)
        Keyboard kb;
        std::get<0>(kb) = "😊";
        std::get<1>(kb).emplace_back(std::vector<Key>());
        std::get<1>(kb).back().emplace_back("👍", 0);
        std::get<1>(kb).back().emplace_back("👎", 1);
        std::get<1>(kb).back().emplace_back("😊", 2);
        std::get<1>(kb).back().emplace_back("😂", 3);
        std::get<1>(kb).back().emplace_back("👋", 4);
        std::get<1>(kb).back().emplace_back("☀", 5);
        std::get<1>(kb).back().emplace_back("🔔", 6);
        std::get<1>(kb).back().emplace_back("🌧", 7);
        std::get<1>(kb).back().emplace_back("☁", 8);
        std::get<1>(kb).back().emplace_back("🧡", 9);
        std::get<1>(kb).emplace_back(std::vector<Key>());
        std::get<1>(kb).back().emplace_back("💩", 10);
        std::get<1>(kb).back().emplace_back("😭", 11);
        std::get<1>(kb).back().emplace_back("🙏", 12);
        std::get<1>(kb).back().emplace_back("😘", 13);
        std::get<1>(kb).back().emplace_back("🎉", 14);
        std::get<1>(kb).back().emplace_back("😄", 15);
        std::get<1>(kb).back().emplace_back("🥺", 16);
        std::get<1>(kb).back().emplace_back("😅", 17);
        std::get<1>(kb).back().emplace_back("🔥", 18);
        std::get<1>(kb).back().emplace_back("⬅", 19);
        std::get<1>(kb).emplace_back(std::vector<Key>());
        std::get<1>(kb).back().emplace_back("⇒", 20);
        std::get<1>(kb).back().emplace_back("🤦", 21);
        std::get<1>(kb).back().emplace_back("🤷", 22);
        std::get<1>(kb).back().emplace_back("🙄", 23);
        std::get<1>(kb).back().emplace_back("🤗", 24);
        std::get<1>(kb).back().emplace_back("😉", 25);
        std::get<1>(kb).back().emplace_back("🤔", 26);
        std::get<1>(kb).back().emplace_back("🫡", 27);
        std::get<1>(kb).back().emplace_back("👌", 28);
        std::get<1>(kb).back().emplace_back("✔", 29);
        keyboards.emplace_back(kb);
    }
#if defined(MOD_CJK_ENABLED)
    if (true) { // bopomo keyboard1 (5)
        Keyboard kb;
        std::get<0>(kb) = "ㄅ";
        std::get<1>(kb).emplace_back(std::vector<Key>());
        std::get<1>(kb).back().emplace_back("ㄅ", 0);
        std::get<1>(kb).back().emplace_back("ㄆ", 1);
        std::get<1>(kb).back().emplace_back("ㄇ", 2);
        std::get<1>(kb).back().emplace_back("ㄈ", 3);
        std::get<1>(kb).back().emplace_back("ㄉ", 4);
        std::get<1>(kb).back().emplace_back("ㄊ", 5);
        std::get<1>(kb).back().emplace_back("ㄋ", 6);
        std::get<1>(kb).back().emplace_back("ㄌ", 7);
        std::get<1>(kb).back().emplace_back(" ", 37);
        std::get<1>(kb).back().emplace_back("ˊ", 38);
        std::get<1>(kb).emplace_back(std::vector<Key>());
        std::get<1>(kb).back().emplace_back("ㄍ", 8);
        std::get<1>(kb).back().emplace_back("ㄎ", 9);
        std::get<1>(kb).back().emplace_back("ㄏ", 10);
        std::get<1>(kb).back().emplace_back("ㄐ", 11);
        std::get<1>(kb).back().emplace_back("ㄑ", 12);
        std::get<1>(kb).back().emplace_back("ㄒ", 13);
        std::get<1>(kb).back().emplace_back("ˇ", 39);
        std::get<1>(kb).back().emplace_back("ˋ", 40);
        std::get<1>(kb).back().emplace_back("˙", 41);
        std::get<1>(kb).back().emplace_back("⬅", 42);
        std::get<1>(kb).emplace_back(std::vector<Key>());
        std::get<1>(kb).back().emplace_back("⇒", 43);
        std::get<1>(kb).back().emplace_back("ㄓ", 14);
        std::get<1>(kb).back().emplace_back("ㄔ", 15);
        std::get<1>(kb).back().emplace_back("ㄕ", 16);
        std::get<1>(kb).back().emplace_back("ㄖ", 17);
        std::get<1>(kb).back().emplace_back("ㄗ", 18);
        std::get<1>(kb).back().emplace_back("ㄘ", 19);
        std::get<1>(kb).back().emplace_back("ㄙ", 20);
        std::get<1>(kb).back().emplace_back("", 44);
        std::get<1>(kb).back().emplace_back("↙", 45);
        keyboards.emplace_back(kb);
    }
    if (true) { // bopomo keyboard2 (6)
        Keyboard kb;
        std::get<0>(kb) = "ㄚ";
        std::get<1>(kb).emplace_back(std::vector<Key>());
        std::get<1>(kb).back().emplace_back("ㄚ", 21);
        std::get<1>(kb).back().emplace_back("ㄛ", 22);
        std::get<1>(kb).back().emplace_back("ㄜ", 23);
        std::get<1>(kb).back().emplace_back("ㄝ", 24);
        std::get<1>(kb).back().emplace_back("ㄞ", 25);
        std::get<1>(kb).back().emplace_back("ㄟ", 26);
        std::get<1>(kb).back().emplace_back("ㄠ", 27);
        std::get<1>(kb).back().emplace_back("ㄡ", 28);
        std::get<1>(kb).back().emplace_back(" ", 37);
        std::get<1>(kb).back().emplace_back("ˊ", 38);
        std::get<1>(kb).emplace_back(std::vector<Key>());
        std::get<1>(kb).back().emplace_back("ㄢ", 29);
        std::get<1>(kb).back().emplace_back("ㄣ", 30);
        std::get<1>(kb).back().emplace_back("ㄤ", 31);
        std::get<1>(kb).back().emplace_back("ㄥ", 32);
        std::get<1>(kb).back().emplace_back("ㄦ", 33);
        std::get<1>(kb).back().emplace_back("", 46);
        std::get<1>(kb).back().emplace_back("ˇ", 39);
        std::get<1>(kb).back().emplace_back("ˋ", 40);
        std::get<1>(kb).back().emplace_back("˙", 41);
        std::get<1>(kb).back().emplace_back("⬅", 42);
        std::get<1>(kb).emplace_back(std::vector<Key>());
        std::get<1>(kb).back().emplace_back("⇒", 43);
        std::get<1>(kb).back().emplace_back("ㄧ", 34);
        std::get<1>(kb).back().emplace_back("ㄨ", 35);
        std::get<1>(kb).back().emplace_back("ㄩ", 36);
        std::get<1>(kb).back().emplace_back("，", 47);
        std::get<1>(kb).back().emplace_back("。", 48);
        std::get<1>(kb).back().emplace_back("：", 49);
        std::get<1>(kb).back().emplace_back("；", 50);
        std::get<1>(kb).back().emplace_back("　", 51);
        std::get<1>(kb).back().emplace_back("✔", 52);
        keyboards.emplace_back(kb);
    }
    if (true) { // full bopomofo keyboard (7): every Zhuyin symbol + tones on ONE board, for
                // on-screen (touch / cursor) composing. The physical CIM path keeps using the
                // half-mapped boards 5/6 (selKB is re-derived from the modifiers on every key),
                // but both feed the same compose state (currentCIM/currentCIMKeys/candidates),
                // so hardware keys and on-screen picks can be mixed mid-syllable. Layout mirrors
                // the generic (non-TKey) bopomofo board; punctuation uses fresh codes 53-55 to
                // stay clear of the TKey control codes (42 ⬅ / 43 ⇒ / 44-46 fillers).
        std::vector<std::string> strBoPoMo = {"ㄅ", "ㄆ", "ㄇ", "ㄈ", "ㄉ", "ㄊ", "ㄋ", "ㄌ", "ㄍ", "ㄎ", "ㄏ", "ㄐ", "ㄑ", "ㄒ", "ㄓ", "ㄔ", "ㄕ", "ㄖ", "ㄗ", "ㄘ", "ㄙ", "ㄚ", "ㄛ", "ㄜ", "ㄝ", "ㄞ", "ㄟ", "ㄠ", "ㄡ", "ㄢ", "ㄣ", "ㄤ", "ㄥ", "ㄦ", "ㄧ", "ㄨ", "ㄩ"};
        Keyboard kb;
        std::get<0>(kb) = "注";
        std::get<1>(kb).emplace_back(std::vector<Key>());
        std::get<1>(kb).back().emplace_back(" ", 37);
        for (int i = 0; i < 8; i++)
            std::get<1>(kb).back().emplace_back(strBoPoMo[i], i);
        std::get<1>(kb).emplace_back(std::vector<Key>());
        std::get<1>(kb).back().emplace_back("ˊ", 38);
        for (int i = 8; i < 14; i++)
            std::get<1>(kb).back().emplace_back(strBoPoMo[i], i);
        std::get<1>(kb).back().emplace_back("，", 53);
        std::get<1>(kb).back().emplace_back("。", 54);
        std::get<1>(kb).emplace_back(std::vector<Key>());
        std::get<1>(kb).back().emplace_back("ˇ", 39);
        for (int i = 14; i < 21; i++)
            std::get<1>(kb).back().emplace_back(strBoPoMo[i], i);
        std::get<1>(kb).back().emplace_back("　", 55);
        std::get<1>(kb).emplace_back(std::vector<Key>());
        std::get<1>(kb).back().emplace_back("ˋ", 40);
        for (int i = 21; i < 29; i++)
            std::get<1>(kb).back().emplace_back(strBoPoMo[i], i);
        std::get<1>(kb).emplace_back(std::vector<Key>());
        std::get<1>(kb).back().emplace_back("˙", 41);
        for (int i = 29; i < 37; i++)
            std::get<1>(kb).back().emplace_back(strBoPoMo[i], i);
        keyboards.emplace_back(kb);
    }
#endif //defined(MOD_CJK_ENABLED)

#else //!defined(MOD_TKEY_MODEL)
    if (true) { // control keyboard
        Keyboard kb;
        std::get<0>(kb) = "Fn";
        std::get<1>(kb).emplace_back(std::vector<Key>());
        std::get<1>(kb).back().emplace_back("Send", 0);
        std::get<1>(kb).emplace_back(std::vector<Key>());
        std::get<1>(kb).back().emplace_back("BS", 1);
        std::get<1>(kb).back().emplace_back("Clr", 2);
        std::get<1>(kb).back().emplace_back("Up", 3);
        std::get<1>(kb).back().emplace_back("Down", 4);
        std::get<1>(kb).emplace_back(std::vector<Key>());
        std::get<1>(kb).back().emplace_back("Send to", 5);
        std::get<1>(kb).back().emplace_back("Send DM", 6);
        std::get<1>(kb).emplace_back(std::vector<Key>());
        std::get<1>(kb).back().emplace_back("1-2", 7);
        std::get<1>(kb).back().emplace_back("<->", 8);
        std::get<1>(kb).back().emplace_back("Off", 9);
        std::get<1>(kb).back().emplace_back("Menu", 10);
        std::get<1>(kb).emplace_back(std::vector<Key>());
        std::get<1>(kb).back().emplace_back("", -1);
        std::get<1>(kb).back().emplace_back("1", 11);
        std::get<1>(kb).back().emplace_back("9", 12);
        std::get<1>(kb).back().emplace_back("17", 13);
        std::get<1>(kb).back().emplace_back("27", 14);
        keyboards.emplace_back(kb);
    }

    if (true) { // abc keyboard
        Keyboard kb;
        std::get<0>(kb) = "ab";
        std::get<1>(kb).emplace_back(std::vector<Key>());
        for (int i = 0; i < 7; i++)
            std::get<1>(kb).back().emplace_back(std::string(1, (char)('a' + (char)i)), i);
        std::get<1>(kb).emplace_back(std::vector<Key>());
        for (int i = 7; i < 14; i++)
            std::get<1>(kb).back().emplace_back(std::string(1, (char)('a' + (char)i)), i);
        std::get<1>(kb).emplace_back(std::vector<Key>());
        for (int i = 14; i < 21; i++)
            std::get<1>(kb).back().emplace_back(std::string(1, (char)('a' + (char)i)), i);
        std::get<1>(kb).emplace_back(std::vector<Key>());
        std::get<1>(kb).back().emplace_back(" ", 26);
        for (int i = 21; i < 26; i++)
            std::get<1>(kb).back().emplace_back(std::string(1, (char)('a' + (char)i)), i);
        std::get<1>(kb).back().emplace_back("@", 27);
        keyboards.emplace_back(kb);
    }

    if (true) { // ABC keyboard
        Keyboard kb;
        std::get<0>(kb) = "AB";
        std::get<1>(kb).emplace_back(std::vector<Key>());
        for (int i = 0; i < 7; i++)
            std::get<1>(kb).back().emplace_back(std::string(1, (char)('A' + (char)i)), i);
        std::get<1>(kb).emplace_back(std::vector<Key>());
        for (int i = 7; i < 14; i++)
            std::get<1>(kb).back().emplace_back(std::string(1, (char)('A' + (char)i)), i);
        std::get<1>(kb).emplace_back(std::vector<Key>());
        for (int i = 14; i < 21; i++)
            std::get<1>(kb).back().emplace_back(std::string(1, (char)('A' + (char)i)), i);
        std::get<1>(kb).emplace_back(std::vector<Key>());
        std::get<1>(kb).back().emplace_back(" ", 26);
        for (int i = 21; i < 26; i++)
            std::get<1>(kb).back().emplace_back(std::string(1, (char)('A' + (char)i)), i);
        std::get<1>(kb).back().emplace_back("@", 27);
        keyboards.emplace_back(kb);
    }

    if (true) { // 123 keyboard
        Keyboard kb;
        std::get<0>(kb) = "12";
        std::get<1>(kb).emplace_back(std::vector<Key>());
        for (int i = 0; i < 5; i++)
            std::get<1>(kb).back().emplace_back(std::string(1, (char)('0' + (char)i)), i);
        std::get<1>(kb).emplace_back(std::vector<Key>());
        for (int i = 5; i < 10; i++)
            std::get<1>(kb).back().emplace_back(std::string(1, (char)('0' + (char)i)), i);
        std::get<1>(kb).emplace_back(std::vector<Key>());
        std::get<1>(kb).back().emplace_back(" ", 10);
        std::get<1>(kb).back().emplace_back(",", 11);
        std::get<1>(kb).back().emplace_back(".", 12);
        std::get<1>(kb).back().emplace_back("?", 13);
        std::get<1>(kb).back().emplace_back("!", 14);
        std::get<1>(kb).back().emplace_back(":", 15);
        std::get<1>(kb).back().emplace_back(";", 16);
        std::get<1>(kb).emplace_back(std::vector<Key>());
        std::get<1>(kb).back().emplace_back("@", 17);
        std::get<1>(kb).back().emplace_back("\'", 18);
        std::get<1>(kb).back().emplace_back("\"", 19);
        std::get<1>(kb).back().emplace_back("+", 20);
        std::get<1>(kb).back().emplace_back("-", 21);
        std::get<1>(kb).back().emplace_back("=", 22);
        std::get<1>(kb).back().emplace_back("(", 23);
        std::get<1>(kb).back().emplace_back(")", 24);
        std::get<1>(kb).emplace_back(std::vector<Key>());
        std::get<1>(kb).back().emplace_back("👍", 25);
        std::get<1>(kb).back().emplace_back("👎", 26);
        std::get<1>(kb).back().emplace_back("😊", 27);
        std::get<1>(kb).back().emplace_back("😂", 28);
        std::get<1>(kb).back().emplace_back("👋", 29);
        std::get<1>(kb).back().emplace_back("🔔", 30);
        std::get<1>(kb).back().emplace_back("👌", 32);
        keyboards.emplace_back(kb);
    }

#if defined(MOD_CJK_ENABLED)
    if (true) { // bopomo keyboard
        std::vector<std::string> strBoPoMo = {"ㄅ", "ㄆ", "ㄇ", "ㄈ", "ㄉ", "ㄊ", "ㄋ", "ㄌ", "ㄍ", "ㄎ", "ㄏ", "ㄐ", "ㄑ", "ㄒ", "ㄓ", "ㄔ", "ㄕ", "ㄖ", "ㄗ", "ㄘ", "ㄙ", "ㄚ", "ㄛ", "ㄜ", "ㄝ", "ㄞ", "ㄟ", "ㄠ", "ㄡ", "ㄢ", "ㄣ", "ㄤ", "ㄥ", "ㄦ", "ㄧ", "ㄨ", "ㄩ"};
        Keyboard kb;
        std::get<0>(kb) = "ㄅ";
        std::get<1>(kb).emplace_back(std::vector<Key>());
        std::get<1>(kb).back().emplace_back(" ", 37);
        for (int i = 0; i < 8; i++)
            std::get<1>(kb).back().emplace_back(strBoPoMo[i], i);
        std::get<1>(kb).emplace_back(std::vector<Key>());
        std::get<1>(kb).back().emplace_back("ˊ", 38);
        for (int i = 8; i < 14; i++)
            std::get<1>(kb).back().emplace_back(strBoPoMo[i], i);
        std::get<1>(kb).back().emplace_back("，", 42);
        std::get<1>(kb).back().emplace_back("。", 43);
        std::get<1>(kb).emplace_back(std::vector<Key>());
        std::get<1>(kb).back().emplace_back("ˇ", 39);
        for (int i = 14; i < 21; i++)
            std::get<1>(kb).back().emplace_back(strBoPoMo[i], i);
        std::get<1>(kb).back().emplace_back("　", 44);
        std::get<1>(kb).emplace_back(std::vector<Key>());
        std::get<1>(kb).back().emplace_back("ˋ", 40);
        for (int i = 21; i < 29; i++)
            std::get<1>(kb).back().emplace_back(strBoPoMo[i], i);
        std::get<1>(kb).emplace_back(std::vector<Key>());
        std::get<1>(kb).back().emplace_back("˙", 41);
        for (int i = 29; i < 37; i++)
            std::get<1>(kb).back().emplace_back(strBoPoMo[i], i);
        keyboards.emplace_back(kb);
    }
#endif //defined(MOD_CJK_ENABLED)

#endif //defined(MOD_TKEY_MODEL)

    if (settings->optionalMenuItems.backlight)
        backlight = Drivers::LatchingBacklight::getInstance();

#if defined(MOD_UART_KEYBOARD_12KEY)
    LOG_INFO("Init serial peripheral interface");
    Serial2.setPins(PIN_SERIAL2_RX, PIN_SERIAL2_TX);
    Serial2.begin(9600, SERIAL_8N1);
    Serial2.setTimeout(250);
#elif defined(MOD_UART_T_KEYBOARD)
    LOG_INFO("Init serial peripheral interface");
    Serial2.setPins(PIN_SERIAL2_RX, PIN_SERIAL2_TX);
    // 921600 MUST match the keyboard firmware's KB_UART_BAUD. 8x the old 115200 -> ~60ms tile fetches and a
    // ~2min full map upload. nRF52 UARTE1 maps this to its Baud921600 preset (core Uart.cpp baud ladder).
    Serial2.begin(921600, SERIAL_8N1);
    Serial2.setTimeout(250);
#endif //defined(MOD_UART_T_KEYBOARD)
}

void InkHUD::InputMenuApplet::onForeground()
{
    if (settings->optionalMenuItems.backlight) {
        assert(backlight);
        if (!backlight->isOn())
            backlight->peek();
    }

    SystemApplet::lockRequests = true;
    SystemApplet::handleInput = true;

    // Begin the auto-close timeout
#if defined(MOD_UART_KEYBOARD_12KEY) || defined(MOD_UART_T_KEYBOARD)
    autoHideMillis = millis() + INPUT_TIMEOUT_SEC * 1000UL;
    OSThread::setIntervalFromNow(100);
    while (Serial2.available()) // empty the buffer first after being foreground
        Serial2.read();
#elif defined(MOD_I2C_TCA8418_KEYBOARD)
    autoHideMillis = millis() + INPUT_TIMEOUT_SEC * 1000UL;
    OSThread::setIntervalFromNow(25); // responsive typing cadence while the IME is open
    // Discard any key events queued while we were backgrounded so the IME opens clean
    if (inkhudI2CKeyboard) {
        inkhudI2CKeyboard->trigger();
        while (inkhudI2CKeyboard->hasEvent())
            inkhudI2CKeyboard->dequeueEvent();
        // ...and any stale held-alt/shift state from the background session (sticky sym mode
        // deliberately survives, so the IME reopens on the board the user left it in)
        static_cast<TDeckMaxTKeyboard *>(inkhudI2CKeyboard)->clearModifiers();
    }
#else //!(defined(MOD_UART_KEYBOARD_12KEY) || defined(MOD_UART_T_KEYBOARD) || defined(MOD_I2C_TCA8418_KEYBOARD))
    OSThread::setIntervalFromNow(INPUT_TIMEOUT_SEC * 1000UL);
#endif //defined(MOD_UART_KEYBOARD_12KEY) || defined(MOD_UART_T_KEYBOARD)
    OSThread::enabled = true;

    // Upgrade the refresh to FAST, for guaranteed responsiveness
    inkhud->forceUpdate(EInk::UpdateTypes::FAST);
}

void InkHUD::InputMenuApplet::onBackground()
{
    if (settings->optionalMenuItems.backlight) {
        assert(backlight);
        if (!backlight->isLatched())
            backlight->off();
    }

#if defined(MOD_UART_KEYBOARD_12KEY) || defined(MOD_UART_T_KEYBOARD)
    // Begin the auto-lock timeout
    autoHideMillis = millis() + LOCK_TIMEOUT_SEC * 1000UL;
    OSThread::setIntervalFromNow(100);
#elif defined(MOD_I2C_TCA8418_KEYBOARD)
    // Keep polling the keyboard while backgrounded: it drives applet navigation (arrows /
    // enter / esc / F1) even when the IME is closed. Slower cadence to save I2C traffic.
    OSThread::setIntervalFromNow(100);
    // Drop any held-alt/shift state carried out of the IME session: it must not re-map the
    // first background keypress (e.g. stale alt turning 'D' into the alt+D tile toggle).
    // Idempotent vs a physically-held key: its release event simply finds the bit already clear.
    if (inkhudI2CKeyboard)
        static_cast<TDeckMaxTKeyboard *>(inkhudI2CKeyboard)->clearModifiers();
#else //!(defined(MOD_UART_KEYBOARD_12KEY) || defined(MOD_UART_T_KEYBOARD) || defined(MOD_I2C_TCA8418_KEYBOARD))
    OSThread::disable();
#endif //defined(MOD_UART_KEYBOARD_12KEY) || defined(MOD_UART_T_KEYBOARD)

    SystemApplet::lockRequests = false;
    SystemApplet::handleInput = false;

    if (borrowedTileOwner)
        borrowedTileOwner->bringToForeground();
    Tile *t = getTile();
    t->assignApplet(borrowedTileOwner); // Break our link with the tile, (and relink it with real owner, if it had one)
    borrowedTileOwner = nullptr;
    neighborTileOwner = nullptr; // clear the neighbour reference too (symmetric with borrowedTileOwner)

    // If we were opened as a transient 1->2 split (over a controllable applet in a 1-tile layout), merge the
    // layout back now that we're closing. restoreFromMenuSplit rebuilds the tiles and renders, so return early to
    // skip the local refresh below (avoids a double update). No re-entry: our foreground flag is already false
    // here, so the changeLayout() it runs won't try to background us again.
    if (inkhud->isMenuSplitActive()) {
        inkhud->restoreFromMenuSplit();
        return;
    }

    inkhud->forceUpdate(EInk::UpdateTypes::FAST);
}

// Open the simple input
// Parameter specifies which user-tile the menu will use
// The user applet originally on this tile will be restored when the menu closes
void InkHUD::InputMenuApplet::show(Tile *t, Tile *neighborTile)
{

    // Remember who *really* owns this tile
    borrowedTileOwner = t->getAssignedApplet();

    if (neighborTile && Controllable::checkControllable(neighborTile->getAssignedApplet()) != Controllable::Types::Uncontrollable)
        neighborTileOwner = neighborTile->getAssignedApplet();
    else
        neighborTileOwner = nullptr;

    // Hide the owner, if it is a valid applet
    if (borrowedTileOwner)
        borrowedTileOwner->sendToBackground();

    // Break the owner's link with tile
    // Relink it to menu applet
    t->assignApplet(this);

    // Show menu
    bringToForeground();
}

// Auto-exit the menu applet after a period of inactivity
// The values shown on the root menu are only a snapshot: they are not re-rendered while the menu remains open.
// By exiting the menu, we prevent users mistakenly believing that the data will update.
int32_t InkHUD::InputMenuApplet::runOnce()
{
#if defined(MOD_UART_KEYBOARD_12KEY)
    if (isForeground()) {
        if ((int32_t)(millis() - autoHideMillis) > 0)
            sendToBackground();
        while (Serial2.available()) {
            autoHideMillis = millis() + INPUT_TIMEOUT_SEC * 1000UL;
            handleMenuVKey(Serial2.read());
        }
    }
    else {
#if defined(MOD_UART_KEYBOARD_12KEY_AUTOLOCK)
        if ((int32_t)(millis() - autoHideMillis) > 0)
            touchLocked = true;
#endif //defined(MOD_UART_KEYBOARD_12KEY_AUTOLOCK)
        // Combo-tap gesture decoder for the Grove 12-key capacitive pad: its BACK/ENTER keys
        // send no long-press signal, so single/double/triple taps within a ~350ms-per-press
        // window are synthesized here into longpress / touch-lock actions. This is a hardware
        // workaround for that specific touchpad, not a general navigation aid.
        if (comboPressCount > 0) {
            if (millis() - comboStartMillis > comboPressCount * COMBO_TAP_WINDOW_MS) {
                comboPressCount = 0;
                handleBackgroundVKey(comboKeyCode);
            }
        }
        while (Serial2.available()) {
            autoHideMillis = millis() + LOCK_TIMEOUT_SEC * 1000UL;
            const uint8_t code = Serial2.read();
            if (code == KEY_TOUCH_BACK || code == KEY_TOUCH_ENTER) {
                const auto now = millis();
                if (comboPressCount == 0) {
                    comboStartMillis = now;
                    comboKeyCode = code;
                    comboPressCount = 1;
                }
                else if (code == comboKeyCode && now - comboStartMillis <= comboPressCount * COMBO_TAP_WINDOW_MS) {
                    comboPressCount++;
                    // ENTER=double-tap->longpress, BACK=triple-tap->toggle touch-lock. Keep the two
                    // mutually exclusive so ENTER's reset at count 2 can't zero the counter before a
                    // 3rd BACK tap accumulates (that ordering left the unlock toggle unreachable).
                    if (comboKeyCode == KEY_TOUCH_ENTER && comboPressCount >= 2) {
                        if (!touchLocked)
                            inkhud->longpress();
                        comboPressCount = 0;
                    }
                    else if (comboKeyCode == KEY_TOUCH_BACK && comboPressCount >= 3) {
                        touchLocked = !touchLocked;
                        comboPressCount = 0;
                    }
                }
                else {
                    for (int i = 0; i < comboPressCount; i++)
                        handleBackgroundVKey(comboKeyCode);
                    handleBackgroundVKey(code);
                    comboPressCount = 0;
                }
            }
            else {
                handleBackgroundVKey(code);
            }
        }
    }
    return 100;
#elif defined(MOD_UART_T_KEYBOARD)
    if (isForeground()) {
        if ((int32_t)(millis() - autoHideMillis) > 0) {
            sendToBackground();
        }
        else {
            // T-KB frames each event as modCode+keyCode; require both bytes so a poll landing
            // between them can't read keyCode as -1 (0xFF) and desync. A lone stray byte is
            // dropped by the 0x80 lead-bit check once the next full packet arrives.
            while (Serial2.available() >= 2) {
                autoHideMillis = millis() + INPUT_TIMEOUT_SEC * 1000UL;
                uint8_t modCode = Serial2.read();
                if (!(modCode & 0x80))
                    continue;
                uint8_t keyCode = Serial2.read();
                LOG_INFO("T-KB: 0x%x 0x%x", modCode, keyCode);
                LOG_INFO("T-KB: menu key...");
                handleMenuTKey(modCode, keyCode);
            }
        }
    }
    else {
        if ((int32_t)(millis() - autoHideMillis) > 0)
            touchLocked = true;
        while (Serial2.available() >= 2) { // modCode+keyCode pair; wait for both (see foreground note)
            autoHideMillis = millis() + LOCK_TIMEOUT_SEC * 1000UL;
            uint8_t modCode = Serial2.read();
            if (!(modCode & 0x80))
                continue;
            uint8_t keyCode = Serial2.read();
            LOG_INFO("T-KB: 0x%x 0x%x", modCode, keyCode);
            if (keyCode & 0x40) {
                if (touchLocked) {
                    const bool bAlt = (modCode & 0x08);
                    const uint8_t row = (keyCode >> 4) & 0x03;
                    const uint8_t col = keyCode & 0xF;
                    if (bAlt && row == 1 && col == 8) {
                        LOG_INFO("T-KB: unlocked");
                        touchLocked = false;
                    }
                }
                else {
                    LOG_INFO("T-KB: background key...");
                    handleBackgroundTKey(modCode, keyCode);
                }
            }
        }
    }
    return 100;
#elif defined(MOD_I2C_TCA8418_KEYBOARD)
    // I2C BBQ10-style keyboard (TCA8418). The TDeckMaxTKeyboard driver queues raw positional
    // 2-byte (modCode, keyCode) events -- the same wire format as the UART T-keyboard -- which
    // feed the shared handleMenuTKey/handleBackgroundTKey handlers. We poll unconditionally on
    // the OSThread cadence (25ms foreground / 100ms background); the INT line is never wired
    // to an ISR (see InputMenuApplet.cpp top / the variant file for the storm warning).
#if defined(T_DECK_MAX)
    // Screen just woke (asleep->awake edge = a BOOT press): flush anything the TCA8418 buffered
    // while we were asleep -- including presses during light sleep, when this poll wasn't running
    // -- so stale keystrokes don't fire the instant the screen returns. (While awake-idle in DARK
    // the loops below already drain+discard; this catches the true-LS gap where we don't poll.)
    static bool i2cKbWasAwake = true;
    if (inkhudScreenAwake && !i2cKbWasAwake && inkhudI2CKeyboard) {
        inkhudI2CKeyboard->trigger();
        while (inkhudI2CKeyboard->hasEvent())
            inkhudI2CKeyboard->dequeueEvent();
        // Also drop any held-alt/shift state from keys mashed during sleep (sym mode persists)
        static_cast<TDeckMaxTKeyboard *>(inkhudI2CKeyboard)->clearModifiers();
    }
    i2cKbWasAwake = inkhudScreenAwake;
#endif
    if (inkhudI2CKeyboard) {
        inkhudI2CKeyboard->trigger();
        while (inkhudI2CKeyboard->hasEvent()) {
            // Events are queued as modCode+keyCode pairs; the 0x80 lead bit rejects a desynced
            // stray byte (same framing rule as the UART T-keyboard path).
            uint8_t modCode = (uint8_t)inkhudI2CKeyboard->dequeueEvent();
            if (!(modCode & 0x80))
                continue;
            if (!inkhudI2CKeyboard->hasEvent())
                break; // pairs are queued atomically, so this shouldn't happen; drop the fragment
            uint8_t keyCode = (uint8_t)inkhudI2CKeyboard->dequeueEvent();
#if defined(T_DECK_MAX)
            // Sleep-UX: only act on the keyboard while the screen is logically ON (a deliberate
            // BOOT wake, or an interactive-nap window). While the "asleep" glyph is showing --
            // e.g. the CPU is only briefly up to service a LoRa packet-wake nap -- drain and
            // DISCARD keys so they can't type or navigate behind a frozen e-ink frame. With the
            // moon shown the keyboard is never a wake source either (the interactive-nap window
            // is zeroed when the indicator is stamped), so only a BOOT press restores input.
            if (!inkhudScreenAwake)
                continue;
            powerFSM.trigger(EVENT_INPUT); // awake (else we continue'd): re-stamp ON, never a wake
            {
                // Interactive-nap: each processed keystroke RECOUNTS the keyboard-wake window,
                // so typing keeps the nap-between-keystrokes mode alive (variant.h / sleep.h)
                const uint32_t until = millis() + TDECKMAX_INTERACTIVE_WINDOW_MS;
                if ((int32_t)(until - tdeckMaxKbWakeUntilMs) > 0)
                    tdeckMaxKbWakeUntilMs = until;
            }
#endif
            if (isForeground()) {
                autoHideMillis = millis() + INPUT_TIMEOUT_SEC * 1000UL;
                handleMenuTKey(modCode, keyCode);
            }
            else if (keyCode & 0x40) {
                if (touchLocked) {
                    // Keyboard lock (engaged by alt+L in handleBackgroundTKey); alt+L again unlocks
                    const bool bAlt = (modCode & 0x08);
                    const uint8_t row = (keyCode >> 4) & 0x03;
                    const uint8_t col = keyCode & 0xF;
                    if (bAlt && row == 1 && col == 8) {
                        touchLocked = false;
                        inkhud->forceUpdate(Drivers::EInk::UpdateTypes::FAST, true); // clear the corner lock glyph
                    }
                }
                else {
                    handleBackgroundTKey(modCode, keyCode);
                }
            }
        }
    }
    if (isForeground()) {
        if ((int32_t)(millis() - autoHideMillis) > 0)
            sendToBackground();
        return 25;
    }
    return 100;
#else
    sendToBackground();
    return OSThread::disable();
#endif
}

InkHUD::Controllable* InkHUD::InputMenuApplet::getActiveControllable() {
    for (auto app : inkhud->userApplets) {
        if (app->isForeground() && app->getTile() == inkhud->getFocusedTile()) {
            const auto type = Controllable::checkControllable(app);
            if (type == Controllable::Types::ThreadedMessage) {
                auto app1 = (ThreadedMessageApplet*)app;
                return app1;
            }
            if (type == Controllable::Types::NavMap) {
                auto app1 = (NavMapApplet*)app;
                return app1;
            }
            if (type == Controllable::Types::DMChat) {
                auto app1 = (DMChatApplet*)app;
                return app1;
            }
            if (type == Controllable::Types::UniChat) {
                auto app1 = (UniChatApplet*)app;
                return app1;
            }
            else if (type == Controllable::Types::Heard) {
                auto app1 = (HeardApplet*)app;
                return app1;
            }
        }
    }
    return nullptr;
}

#if defined(MOD_UART_KEYBOARD_12KEY)
void InkHUD::InputMenuApplet::handleMenuVKey(const uint8_t code) {
    switch (code) {
    case KEY_TOUCH_BACK: // back
        LOG_INFO("Key press [back]");
        if (selMode == 0) { // select KB
            sendToBackground();
        }
        else if (selMode == 1 || selMode == 2) { // select row / col
            selMode = 0;
        }
        else if (selMode == 3) { // select result
            currentCIM.clear();
            currentCIMKeys.clear();
            currentCIMResults.clear();
            selMode = 2;
        }
        else { // select send target
            selMode = 2;
        }
        break;
    case KEY_TOUCH_UP: // up
        LOG_INFO("Key press [up]");
        if (selMode == 0) { // select KB
            sendToBackground();
        }
        else if (selMode == 1 || selMode == 2) { // select row / col
            if (selMode == 1) { // auto upgrade to mode 2
                selMode = 2;
                selCol = 0;
            }
            if (selRow <= 0)
                selRow = std::get<1>(keyboards[selKB]).size() - 1;
            else
                selRow--;
        }
        else if (selMode == 3) { // select result
            if (selResult / 10 <= 0) {
                // Wrap to the first index of the last page. ((size-1)/10)*10 matches the
                // T-Keyboard path (line ~1038); the old `size - size%10` over-indexed by a
                // whole page when the candidate count was an exact multiple of 10.
                selResult = (currentCIMResults.size() - 1) / 10 * 10;
            }
            else {
                selResult -= 10;
            }
        }
        else { // select send target
            if (selTarget <= 0)
                selTarget = sendTargets.size() - 1;
            else
                selTarget--;
        }
        break;
    case KEY_TOUCH_ENTER: // enter
        LOG_INFO("Key press [enter]");
        if (selMode == 0) { // select KB
            if (selKB >= 0 && selKB < (int16_t)keyboards.size()) {
                selMode = 2; // go into detailed mode
                selRow = 0;
                selCol = 0;
            }
        }
        else if (selMode == 1) { // select row
            if (selRow >= 0 && selCol >= 0 && selRow < (int16_t)std::get<1>(keyboards[selKB]).size() && selCol < (int16_t)std::get<1>(keyboards[selKB])[selRow].size()) {
                selMode = 2;
            }
        }
        else if (selMode == 2) { // select row/col
            if (selRow >= 0 && selCol >= 0 && selRow < (int16_t)std::get<1>(keyboards[selKB]).size() && selCol < (int16_t)std::get<1>(keyboards[selKB])[selRow].size()) {
                handleKeyboardPress();
            }
        }
        else if (selMode == 3) { // select result
            if (selResult >= 0 && selResult < (int16_t)currentCIMResults.size())
                handleKeyboardPress();
        }
        else { // select send target
            if (selTarget >= 0 && selTarget < (int16_t)sendTargets.size())
                handleKeyboardPress();
        }
        break;
    case KEY_TOUCH_LEFT: // left
        LOG_INFO("Key press [left]");
        if (selMode == 0) { // select KB
            if (selKB <= 0)
                selKB = keyboards.size() - 1;
            else
                selKB--;
        }
        else if (selMode == 1 || selMode == 2) { // select row / col
            if (selMode == 1) {
                selMode = 2;
                selCol = 0;
            }
            if (selRow >= 0 && selRow < (int16_t)std::get<1>(keyboards[selKB]).size()) {
                if (selCol <= 0)
                    selCol = std::get<1>(keyboards[selKB])[selRow].size() - 1;
                else
                    selCol--;
            }
        }
        else if (selMode == 3) { // select result
            if (selResult <= 0)
                selResult = currentCIMResults.size() - 1;
            else
                selResult--;
        }
        else { // select send target
            if (selTarget <= 0)
                selTarget = sendTargets.size() - 1;
            else
                selTarget--;
        }
        break;
    case KEY_TOUCH_DOWN: // down
        LOG_INFO("Key press [down]");
        if (selMode == 0) { // select KB
            if (selKB >= 0 && selKB < (int16_t)keyboards.size()) {
                selMode = 2; // go into detailed mode
                selRow = 0;
                selCol = 0;
            }
        }
        else if (selMode == 1 || selMode == 2) { // select row / col
            if (selMode == 1) { // auto upgrade to mode 2
                selMode = 2;
                selCol = 0;
            }
            if (selRow == (int16_t)std::get<1>(keyboards[selKB]).size() - 1)
                selRow = 0;
            else
                selRow++;
        }
        else if (selMode == 3) { // select result
            if (selResult == (int16_t)currentCIMResults.size() - 1) {
                selResult = 0;
            }
            else if (selResult + 10 >= (int16_t)currentCIMResults.size()) {
                selResult = currentCIMResults.size() - 1;
            }
            else {
                selResult += 10;
            }
        }
        else { // select send target
            if (selTarget == (int16_t)sendTargets.size() - 1)
                selTarget = 0;
            else
                selTarget++;
        }
        break;
    case KEY_TOUCH_RIGHT: // right
        LOG_INFO("Key press [right]");
        if (selMode == 0) { // select KB
            if (selKB == (int16_t)keyboards.size() - 1)
                selKB = 0;
            else
                selKB++;
        }
        else if (selMode == 1 || selMode == 2) { // select row / col
            if (selMode == 1) {
                selMode = 2;
                selCol = 0;
            }
            if (selRow >= 0 && selRow < (int16_t)std::get<1>(keyboards[selKB]).size()) {
                if (selCol == (int16_t)std::get<1>(keyboards[selKB])[selRow].size() - 1)
                    selCol = 0;
                else
                    selCol++;
            }
        }
        else if (selMode == 3) { // select result
            if (selResult == (int16_t)currentCIMResults.size() - 1)
                selResult = 0;
            else
                selResult++;
        }
        else { // select send target
            if (selTarget == (int16_t)sendTargets.size() - 1)
                selTarget = 0;
            else
                selTarget++;
        }
        break;
    }
    requestUpdate(Drivers::EInk::UpdateTypes::FAST);
}

void InkHUD::InputMenuApplet::handleBackgroundVKey(const uint8_t code) {
    switch (code) {
    case KEY_TOUCH_BACK: // back
        LOG_INFO("Key press [back]");
        if (!touchLocked) {
            auto app = getActiveControllable();
            if (app)
                app->handleBack();
        }
        break;
    case KEY_TOUCH_UP: // up
        LOG_INFO("Key press [up]");
        if (!touchLocked) {
            auto app = getActiveControllable();
            if (app)
                app->handleUp();
        }
        break;
    case KEY_TOUCH_ENTER: // enter
        LOG_INFO("Key press [enter]");
        if (!touchLocked) {
            inkhud->shortpress();
        }
        break;
    case KEY_TOUCH_LEFT: // left
        LOG_INFO("Key press [left]");
        if (!touchLocked) {
            tilePrevOrApplet();
        }
        break;
    case KEY_TOUCH_DOWN: // down
        LOG_INFO("Key press [down]");
        if (!touchLocked) {
            auto app = getActiveControllable();
            if (app)
                app->handleDown();
        }
        break;
    case KEY_TOUCH_RIGHT: // right
        LOG_INFO("Key press [right]");
        if (!touchLocked) {
            tileNextOrApplet();
        }
        break;
    }
    requestUpdate(Drivers::EInk::UpdateTypes::FAST);
}
#elif defined(MOD_TKEY_MODEL)
void InkHUD::InputMenuApplet::handleMenuTKey(const uint8_t modCode, const uint8_t keyCode)
{
    Applet* ctrlPtr0 = nullptr;
    Controllable::Types ctrlType = Controllable::Types::Uncontrollable;
    bool bBorrowed = false;
    if (neighborTileOwner && Controllable::checkControllable(neighborTileOwner) != Controllable::Types::Uncontrollable) {
        ctrlPtr0 = neighborTileOwner;
        ctrlType = Controllable::checkControllable(neighborTileOwner);
    }
    else if (borrowedTileOwner && Controllable::checkControllable(borrowedTileOwner) != Controllable::Types::Uncontrollable) {
        ctrlPtr0 = borrowedTileOwner;
        ctrlType = Controllable::checkControllable(borrowedTileOwner);
        bBorrowed = true;
    }
    // The physical "0" key (keyCode 0x71) doubles as the English/bopomofo (CIM) switch, so the Fn symbol layer
    // otherwise has no way to type a digit zero. Make bSym + "0" insert a literal "0" instead (plain "0" still
    // switches CIM). Codes confirmed on hardware: "0" alone = (0x84,0x71), bSym+"0" = (0x94,0x71) -- bSym = 0x10.
    if (keyCode == 0x71 && (modCode & 0x10)) {
        currentInput += '0';
        requestUpdate(Drivers::EInk::UpdateTypes::FAST);
        return;
    }
    if ((modCode == 0x84 && keyCode == 0) || keyCode == 0x71) { // switch english and bopomo
        keyboardCIM = !keyboardCIM;
    }
    bool bShift = (modCode & 0x03);
    bool bAlt = (modCode & 0x08);
    bool bSym = (modCode & 0x10);
#if defined(MOD_I2C_TCA8418_KEYBOARD)
    // Sticky sym mode (TDeckMaxTKeyboard): the 0x10 bit rides on every event while symbol mode is
    // latched. CIM and symbol mode are mutually exclusive - a sym-flagged event while in CIM drops
    // straight to the symbol board (mic re-enters CIM). Safe from the mic key itself: sym+mic
    // (literal '0') already returned above, so this can't fire while typing zeros.
    if (bSym && keyboardCIM)
        keyboardCIM = false;
#endif
    if (keyboardCIM) {
        selKB = bShift ? 6 : 5;
    }
    else {
        selKB = bAlt ? 0 : (bSym ? (bShift ? 4 : 3) : (bShift ? 2 : 1));
    }
    auto handleCIMKey = [&](int16_t key) {
        if (key < 37) {
            currentCIM += std::get<0>(std::get<1>(keyboards[selKB])[selRow][selCol]);
            currentCIMKeys.emplace_back(key);
            selMode = 2;
            selCol = -1;
            selRow = -1;
        }
        else if (key < 42) {
            key -= 37;
            lookupBopomofoCandidates(key, 0);
            currentCIM.clear();
            currentCIMKeys.clear();
            if (selMode != 3) {
                currentCIMResults.clear();
                selMode = 2;
                selCol = -1;
                selRow = -1;
            }
        }
        else {
            if (key == 47)
                currentInput += "，";
            else if (key == 48)
                currentInput += "。";
            else if (key == 49)
                currentInput += "：";
            else if (key == 50)
                currentInput += "；";
            else if (key == 51)
                currentInput += "　";
            selMode = 2;
            selCol = -1;
            selRow = -1;
        }
    };
    // Candidate list up: physical top row picks, regardless of the CIM toggle -- the list may
    // have been raised by composing on the full on-screen bopomofo board (7) via touch/cursor.
    if (selMode == 3) {
        if (keyCode & 0x40) {
            selRow = (keyCode >> 4) & 0x03;
            selCol = keyCode & 0xF;
            if (selRow == 0) {
                if (selResult + selCol < (int16_t)currentCIMResults.size()) {
                    currentInput += currentCIMResults[selResult + selCol];
                    currentCIM.clear();
                    currentCIMKeys.clear();
                    currentCIMResults.clear();
                    selMode = 2;
                    selCol = -1;
                    selRow = -1;
                }
            }
            else if (selRow == 1) {
                if (selCol == 0) {
                    if (selResult < 10)
                        selResult = (currentCIMResults.size() - 1) / 10 * 10;
                    else
                        selResult = (selResult / 10 - 1) * 10;
                }
                else if (selCol == 1) {
                    selResult = (selResult / 10 + 1) * 10;
                    if (selResult >= (int16_t)currentCIMResults.size())
                        selResult = 0;
                }
                else if (selCol == 9) {
                    currentCIM.clear();
                    currentCIMKeys.clear();
                    currentCIMResults.clear();
                    selMode = 2;
                    selCol = -1;
                    selRow = -1;
                }
            }
        }
    }
    else if (selMode == 0x10 || selMode == 0x11) {
        if (keyCode & 0x40) {
            selRow = (keyCode >> 4) & 0x03;
            selCol = keyCode & 0xF;
            if ((selRow == 0 && selCol == 1) || (selRow == 1 && selCol == 0)) {
                if (selTarget == 0)
                    selTarget = (int16_t)sendTargets.size() - 1;
                else
                    selTarget--;
            }
            else if (selRow == 1 && (selCol == 1 || selCol == 2)) {
                if (selTarget == (int16_t)sendTargets.size() - 1)
                    selTarget = 0;
                else
                    selTarget++;
            }
            else if (selRow == 1 && selCol == 9) {
                selMode = 2;
            }
            else if (selRow == 2 && selCol == 9) {
                std::string message = currentInput;
                currentInput.clear();
                currentCIM.clear();
                currentCIMKeys.clear();
                currentCIMResults.clear();
                if (selTarget >= 0 && selTarget < (int16_t)sendTargets.size()) { // never .at() an empty list
                    if (selMode == 0x10)
                        sendText(NODENUM_BROADCAST, std::get<1>(sendTargets.at(selTarget)), message);
                    else
                        sendText(std::get<1>(sendTargets.at(selTarget)), 0, message);
                }
                selMode = 2;
                selCol = -1;
                selRow = -1;
            }
        }
    }
    else {
        selMode = 2;
        selRow = -1;
        selCol = -1;
        if (keyCode & 0x40) {
            selRow = (keyCode >> 4) & 0x03;
            selCol = keyCode & 0xF;
            if (bAlt) {
                // Fn board is a full 10x3 mirror of the typing matrix; unassigned cells carry
                // funcCode -1 and are ignored. (Was cols 5-9 only, board index selCol-5, before
                // the board was extended to 10 columns.)
                if (selRow >= 0 && selRow < 3 && selCol >= 0 &&
                    selCol < (int16_t)std::get<1>(keyboards[selKB])[selRow].size()) {
                    auto funcCode = std::get<1>(std::get<1>(keyboards[selKB])[selRow][selCol]);
                    LOG_INFO("T-KB: funcCode=%d", (int)funcCode);
                    if (funcCode >= 0)
                        dispatchTKeyFunc(funcCode, ctrlPtr0, ctrlType, bBorrowed);
                }
            }
            else {
                if (selRow == 1 && selCol == 9) { // backspace
                    // Pending syllable (from either the physical CIM layer or the on-screen full
                    // bopomofo board): delete the last symbol, not the last committed character
                    if (!currentCIMKeys.empty()) {
                        utf8PopLast(currentCIM);
                        currentCIMKeys.pop_back();
                    }
                    else {
                        utf8PopLast(currentInput);
                    }
                }
                else if (selRow == 2 && selCol == 9) { // return or send
                    if (!currentCIMKeys.empty()) {
                        // input incomplete, do nothing?
                    }
                    else {
                        if (bShift) {
                            if (ctrlPtr0 && ctrlType == Controllable::Types::ThreadedMessage) {
                                auto ctrlPtr = (ThreadedMessageApplet*)ctrlPtr0;
                                std::string message = currentInput;
                                currentInput.clear();
                                currentCIM.clear();
                                currentCIMKeys.clear();
                                currentCIMResults.clear();
                                selMode = 2;
                                selCol = -1;
                                selRow = -1;
                                sendText(NODENUM_BROADCAST, ctrlPtr->getChannelIndex(), message);
                                if (bBorrowed)
                                    sendToBackground();
                            }
                            else if (ctrlPtr0 && ctrlType == Controllable::Types::DMChat) {
                                // Chat window: composed text goes as a DM to the bound peer
                                auto ctrlPtr = (DMChatApplet*)ctrlPtr0;
                                if (ctrlPtr->isBound()) {
                                    std::string message = currentInput;
                                    currentInput.clear();
                                    currentCIM.clear();
                                    currentCIMKeys.clear();
                                    currentCIMResults.clear();
                                    selMode = 2;
                                    selCol = -1;
                                    selRow = -1;
                                    sendText(ctrlPtr->getPeer(), 0, message);
                                    ctrlPtr->noteSent();
                                    if (bBorrowed)
                                        sendToBackground();
                                }
                            }
                            else if (ctrlPtr0 && ctrlType == Controllable::Types::UniChat) {
                                // Unified chats: send to whichever target thread is open
                                auto ctrlPtr = (UniChatApplet*)ctrlPtr0;
                                if (ctrlPtr->hasThreadTarget()) {
                                    std::string message = currentInput;
                                    currentInput.clear();
                                    currentCIM.clear();
                                    currentCIMKeys.clear();
                                    currentCIMResults.clear();
                                    selMode = 2;
                                    selCol = -1;
                                    selRow = -1;
                                    if (ctrlPtr->targetIsDM())
                                        sendText(ctrlPtr->getPeer(), 0, message);
                                    else
                                        sendText(NODENUM_BROADCAST, ctrlPtr->getChannelIndex(), message);
                                    ctrlPtr->noteSent();
                                    if (bBorrowed)
                                        sendToBackground();
                                }
                            }
                        }
                        else {
                            currentInput += '\n';
                        }
                    }
                }
                else if (selRow == 3 && selCol == 2) { // space
                    if (!currentCIMKeys.empty()) { // pending syllable: 1st-tone lookup (either input source)
                        // treat as first sound?
                        handleCIMKey(37);
                    }
                    else {
                        currentInput += ' ';
                    }
                }
                else if (selRow < 3 && selCol < (int16_t)std::get<1>(keyboards[selKB])[selRow].size()) {
                    // selCol comes straight off the wire (keyCode & 0xF, 0..15); the typing rows hold
                    // <=10 keys, so bound it before indexing the row (a stray col>=size would read past end).
                    if (keyboardCIM) {
                        auto key = std::get<1>(std::get<1>(keyboards[selKB])[selRow][selCol]);
                        handleCIMKey(key);
                    }
                    else {
                        auto keyValue = std::get<0>(std::get<1>(keyboards[selKB])[selRow][selCol]);
                        currentInput = currentInput + keyValue;
                    }
                }
            }
        }
    }
    requestUpdate(NicheGraphics::Drivers::EInk::FAST);
}

void InkHUD::InputMenuApplet::handleBackgroundTKey(const uint8_t modCode, const uint8_t keyCode)
{
    if (keyCode & 0x40) {
        const bool bAlt = (modCode & 0x08);
        const uint8_t row = (keyCode >> 4) & 0x03;
        const uint8_t col = keyCode & 0xF;
        if (bAlt && row == 1 && col == 8) {
            touchLocked = true;
            inkhud->forceUpdate(Drivers::EInk::UpdateTypes::FAST, true); // show the corner lock glyph
            return;
        }
        // alt+D "<->": toggle the focused tile (wraps), same funcCode-15 shortcut as the Fn board.
        // Checked before the menu-first block so the alt chord isn't swallowed as a plain D=select.
        if (bAlt && row == 1 && col == 2) {
            if (settings->userTiles.count > 1)
                inkhud->nextTile();
            return;
        }
        // alt+A: fast internal/external LoRa antenna toggle (menu Hardware -> Ext Antenna, incl.
        // persistence). Feedback is the corner antenna-mast glyph (shown while external).
        if (bAlt && row == 1 && col == 0) {
#if defined(T_DECK_MAX)
            ((MenuApplet *)inkhud->getSystemApplet("Menu"))->quickToggleAntenna();
            // all=true: the cluster may have SHRUNK - the applet beneath must repaint the freed span
            inkhud->forceUpdate(Drivers::EInk::UpdateTypes::FAST, true);
#endif
            return;
        }
        // alt+H: Debug Hold fast toggle (menu Hardware -> Debug Hold): session-only stay-awake
        // hold + fast Info-applet refresh. Feedback is the corner eye glyph (BatteryIconApplet);
        // the Hardware-page checkbox tracks the global directly.
        if (bAlt && row == 1 && col == 5) {
#if defined(T_DECK_MAX)
            tdeckmaxDebugHold = !tdeckmaxDebugHold;
            inkhud->forceUpdate(Drivers::EInk::UpdateTypes::FAST, true); // all: see alt+A note
#endif
            return;
        }
        // alt+B: e-ink frontlight cycle (the Fn board's ☀ position); persists via TDeckMaxPrefs.
        if (bAlt && row == 2 && col == 5) {
#if defined(T_DECK_MAX)
            ((MenuApplet *)inkhud->getSystemApplet("Menu"))->quickCycleFrontlight();
#endif
            return;
        }
        // alt+N: keyboard backlight toggle (beside alt+B = frontlight). Session-only - the
        // same KB_BL pin the Fn board's 🔅/🔆 keys drive, now reachable without the IME open.
        if (bAlt && row == 2 && col == 6) {
#if defined(MOD_I2C_TCA8418_KEYBOARD)
            if (inkhudI2CKeyboard) {
                auto *kb = (TDeckMaxTKeyboard *)inkhudI2CKeyboard;
                kb->setBacklight(!kb->getBacklight());
            }
#endif
            return;
        }
        // alt+S: silent mode toggle (menu Hardware -> Silent Mode): super power saving - while
        // asleep the e-ink never refreshes, messages are only recorded. Feedback is the corner
        // moon glyph. (Plain S = down-nav, untouched; in the IME alt+S stays the Fn <- arrow.)
        if (bAlt && row == 1 && col == 1) {
#if defined(T_DECK_MAX)
            ((MenuApplet *)inkhud->getSystemApplet("Menu"))->quickToggleSilent();
            inkhud->forceUpdate(Drivers::EInk::UpdateTypes::FAST, true); // all: see alt+A note
#endif
            return;
        }
        // alt+V: vibration policy cycle All -> DMs only -> Off (menu Hardware -> Vibration).
        // Feedback is the corner bell glyph (shown when not-All).
        if (bAlt && row == 2 && col == 4) {
#if defined(T_DECK_MAX)
            ((MenuApplet *)inkhud->getSystemApplet("Menu"))->quickCycleVibra();
            inkhud->forceUpdate(Drivers::EInk::UpdateTypes::FAST, true);
#endif
            return;
        }
        // alt+F: force a FULL e-ink refresh of the whole screen - instant deghost for the
        // gradual fade partial refreshes leave behind, and it pays back FULL-refresh debt.
        // (With the IME open, alt+F stays the Fn -> arrow.)
        if (bAlt && row == 1 && col == 3) {
            inkhud->forceUpdate(Drivers::EInk::UpdateTypes::FULL, true);
            return;
        }
        // alt+G: fast GPS toggle - identical effect to menu Node Config -> Position -> GPS,
        // including driving the XL9555 GPS rail immediately. Feedback is the corner GPS reticle
        // (BatteryIconApplet), so force a repaint of the current frame.
        if (bAlt && row == 1 && col == 4) {
#if !MESHTASTIC_EXCLUDE_GPS && HAS_GPS
            if (config.position.gps_mode == meshtastic_Config_PositionConfig_GpsMode_DISABLED)
                config.position.gps_mode = meshtastic_Config_PositionConfig_GpsMode_ENABLED;
            else if (config.position.gps_mode == meshtastic_Config_PositionConfig_GpsMode_ENABLED)
                config.position.gps_mode = meshtastic_Config_PositionConfig_GpsMode_DISABLED;
            else
                return; // NOT_PRESENT: nothing to toggle
            nodeDB->saveToDisk(SEGMENT_CONFIG);
            service->reloadConfig(SEGMENT_CONFIG);
#if defined(T_DECK_MAX)
            tdeckmaxApplyGpsRail(); // power/cut the rail now, not at next reboot
#endif
            inkhud->forceUpdate(Drivers::EInk::UpdateTypes::FAST, true); // all: see alt+A note
#endif
            return;
        }
        // (The old R/T/Y bezel-replacement mappings are gone - user decision 2026-08-07: W/S
        // scroll, A/D switch tile/applet, and R is now the "other menu" key, see below.)
        // Menu-first: while the menu is open, QWEASD navigate it instead of the Controllable applet.
        // Direct handler calls (not inkhud->navUp etc.) skip the joystick rotation remap, which would
        // misdirect keys with fixed labels on a rotated display.
        MenuApplet *menu = (MenuApplet *)inkhud->getSystemApplet("Menu");
        if (menu->isForeground() && ((row <= 1 && col <= 2) || (row == 2 && col == 9))) {
            if (row == 2 && col == 9) // physical Enter: select (same as E/D)
                menu->onNavRight();
            else if (row == 0 && col == 0) // Q: exit menu
                menu->onExitShort();
            else if (row == 0 && col == 1) { // W: previous item
                menu->onNavUp();
                menu->showCursorHighlight(); // key nav: cursor must be VISIBLE (onNavUp hides it
                                             // on this board's touch-first layout - swipe UX)
            }
            else if (row == 0 && col == 2) // E: select
                menu->onNavRight();
            else if (row == 1 && col == 0) // A: previous menu page
                menu->onNavLeft();
            else if (row == 1 && col == 1) { // S: next item
                menu->onNavDown();
                menu->showCursorHighlight(); // see W above
            }
            else // D: select
                menu->onNavRight();
            return;
        }
        // Shared by E and the physical Enter key: the applet's own action first (e.g. Heard
        // select-mode opens a DM chat, UniChat list picks a thread); the fallback is
        // context-aware (user decision 2026-08-07): chat-style applets (Chats/DM/BBS) open the
        // IME to compose straight away, everything else (NavMap, Heard, Info...) opens the
        // settings menu. R is the mirror image, see below.
        // Type-check via the tile's Applet pointer (the registry key); a Controllable* can't be
        // downcast through the virtual base.
        auto focusedWantsIme = [&]() -> bool {
            Applet *focused = inkhud->getFocusedTile() ? inkhud->getFocusedTile()->getAssignedApplet() : nullptr;
            const auto ctype = Controllable::checkControllable(focused);
            if (ctype == Controllable::Types::UniChat)
                return ((UniChatApplet *)focused)->hasThreadTarget();
            if (ctype == Controllable::Types::DMChat)
                return ((DMChatApplet *)focused)->isBound();
            if (ctype == Controllable::Types::ThreadedMessage)
                return true;
            return false;
        };
        auto enterAction = [&]() {
            LOG_INFO("Key press [enter]");
            auto app = getActiveControllable();
            if (!(app && app->handleEnter())) {
                if (focusedWantsIme())
                    inkhud->openInputMenu();
                else
                    inkhud->openMenu();
            }
        };
        if (row == 0 && col == 0) {
            LOG_INFO("Key press [back]");
            auto app = getActiveControllable();
            if (app)
                app->handleBack();
        }
        else if (row == 0 && col == 1) {
            LOG_INFO("Key press [up]");
            auto app = getActiveControllable();
            if (app)
                app->handleUp();
        }
        else if (row == 0 && col == 2) {
            enterAction(); // E: applet action, else IME (chats/BBS) / menu (everything else)
        }
        else if (row == 0 && col == 3) {
            // R: the "other menu" - the mirror image of Enter's context default. On chat-style
            // pages (where Enter = IME) R opens the settings menu; everywhere else (where Enter
            // = menu) R opens the IME directly - e.g. instant typed search on NavMap / Heard.
            // Inert while a system applet (menu etc.) is consuming input: stacking the IME over
            // an open menu would fight the MENU_OPEN_INPUT deferred-handoff path.
            bool sysInput = false;
            for (SystemApplet *sa : inkhud->systemApplets) {
                if (sa->handleInput) {
                    sysInput = true;
                    break;
                }
            }
            if (!sysInput) {
                LOG_INFO("Key press [other-menu]");
                if (focusedWantsIme())
                    inkhud->openMenu();
                else
                    inkhud->openInputMenu();
            }
        }
        else if (row == 1 && col == 0) {
            LOG_INFO("Key press [left]");
            auto app = getActiveControllable();
            if (!(app && app->handleLeft())) { // let a foreground applet (e.g. NavMap) consume left; else switch
                tilePrevOrApplet();
            }
        }
        else if (row == 1 && col == 1) {
            LOG_INFO("Key press [down]");
            auto app = getActiveControllable();
            if (app)
                app->handleDown();
        }
        else if (row == 1 && col == 2) {
            auto app = getActiveControllable();
            if (!(app && app->handleRight())) { // let a foreground applet consume right; else switch
                tileNextOrApplet();
            }
        }
        else if (row == 2 && col == 7) {
            inkhud->openMenu();
        }
        else if (row == 1 && col == 9) {
            inkhud->shortpress();
        }
        else if (row == 2 && col == 9) {
            enterAction(); // physical Enter: same as E (was inkhud->longpress())
        }
    }
}
#endif //defined(MOD_TKEY_MODEL)

// Direct touch on the IME. Regions are computed with the same math as onRender:
//   [board cells]     tap -> selRow/selCol -> handleKeyboardPress (types / composes / Fn dispatch)
//   [bar above board] selMode 3 -> tap picks a candidate; composing -> ignored; selMode 0 -> tap
//                     switches boards; otherwise a tap restores the board tab bar
//   [target list]     tap selects an entry (commit stays on ✔/Enter -- no accidental sends)
//   [anywhere else]   consumed with no action, so a stray tap can't fall through to the
//                     tap-to-focus / short-press fallbacks while composing
// Every path shares state with the physical-key handlers, so hardware and touch input can be
// freely mixed -- e.g. compose Zhuyin by tapping the full bopomofo board (7), then pick the
// candidate with the physical top row, or vice versa.
bool InkHUD::InputMenuApplet::onTouchPoint(uint16_t x, uint16_t y, bool longPress)
{
    if (!getTile())
        return false;
    const uint16_t tileL = getTile()->getLeft();
    const uint16_t tileT = getTile()->getTop();
    if (x < tileL || x >= tileL + getTile()->getWidth() || y < tileT || y >= tileT + getTile()->getHeight())
        return false; // outside our tile: leave it to other handlers (e.g. tap-to-focus)

    const int16_t lx = (int16_t)(x - tileL);
    const int16_t ly = (int16_t)(y - tileT);
    const int16_t kbKeyHeight = fontSmall.lineHeight() + 1;

    noteUserActivity();

    // Send-target picker: tap highlights an entry; committing stays on ✔/Enter
    if (selMode == 0x10 || selMode == 0x11) {
        constexpr int16_t padDivH = 2;
        const int16_t headerDivY = padDivH + fontSmall.lineHeight() + padDivH - 1;
        const int16_t itemHeight = fontSmall.lineHeight() + 1;
        if (ly > headerDivY) {
            const int16_t showItems = (height() - headerDivY - 1) / itemHeight;
            int16_t startIdx = 0;
            if (selTarget != -1 && (int16_t)sendTargets.size() > showItems) {
                startIdx = selTarget - showItems + 1; // same scroll window as onRender
                if (startIdx < 0)
                    startIdx = 0;
            }
            const int16_t idx = startIdx + (ly - headerDivY - 1) / itemHeight;
            if (idx >= 0 && idx < (int16_t)sendTargets.size()) {
                selTarget = idx;
                requestUpdate(Drivers::EInk::UpdateTypes::FAST);
            }
        }
        return true;
    }

    if (selKB >= 0 && selKB < (int16_t)keyboards.size()) {
        const auto &rows = std::get<1>(keyboards[selKB]);
        const int16_t kbRows = (int16_t)rows.size();
        const int16_t kbBoardHeight = kbKeyHeight * kbRows + 1;
        const int16_t kbTop = height() - kbBoardHeight;
        const int16_t kbBarTop = kbTop - kbKeyHeight;

        if (ly >= kbTop) { // board cell -> press it
            int16_t row = (ly - kbTop) / kbKeyHeight;
            if (row >= kbRows)
                row = kbRows - 1;
            const int16_t cols = (int16_t)rows[row].size();
            if (cols == 0)
                return true;
            int16_t col = (int16_t)((int32_t)lx * cols / width());
            if (col >= cols)
                col = cols - 1;
            selRow = row;
            selCol = col;
            handleKeyboardPress();
            requestUpdate(Drivers::EInk::UpdateTypes::FAST);
            return true;
        }

        if (ly >= kbBarTop) { // the bar strip above the board
            if (selMode == 3) { // candidate bar: tap picks (same paging as the physical top row)
                const int16_t count = (int16_t)currentCIMResults.size();
                int16_t base = (selResult != -1 && count > 10) ? (selResult - selResult % 10) : 0;
                int16_t idx = (int16_t)((int32_t)lx * 10 / width());
                if (idx > 9)
                    idx = 9;
                if (base + idx < count) {
                    currentInput += currentCIMResults[base + idx];
                    currentCIM.clear();
                    currentCIMKeys.clear();
                    currentCIMResults.clear();
                    selMode = 1;
                    selRow = -1;
                    selCol = -1;
                    requestUpdate(Drivers::EInk::UpdateTypes::FAST);
                }
                return true;
            }
            if (!currentCIM.empty())
                return true; // compose text showing: nothing to tap
            if (selMode == 0) { // board tab bar visible: tap switches boards
                const int16_t kbCount = (int16_t)keyboards.size();
                int16_t idx = (int16_t)((int32_t)lx * kbCount / width());
                if (idx >= kbCount)
                    idx = kbCount - 1;
                selKB = idx;
                selRow = -1;
                selCol = -1;
            } else {
                selMode = 0; // bar currently hidden: restore it so the next tap can switch boards
            }
            requestUpdate(Drivers::EInk::UpdateTypes::FAST);
            return true;
        }

        return true; // composed-text area: consume, no action
    }

    // No board open: just the tab bar along the bottom edge
    if (selMode == 0 && ly >= height() - kbKeyHeight - 1) {
        const int16_t kbCount = (int16_t)keyboards.size();
        int16_t idx = (int16_t)((int32_t)lx * kbCount / width());
        if (idx >= kbCount)
            idx = kbCount - 1;
        selKB = idx;
        selRow = -1;
        selCol = -1;
        requestUpdate(Drivers::EInk::UpdateTypes::FAST);
    }
    return true;
}

void InkHUD::InputMenuApplet::onRender(bool full)
{
    constexpr int16_t padDivH = 2;
    const int16_t headerDivY = padDivH + fontSmall.lineHeight() + padDivH - 1;

    for (int16_t x = 0; x < width(); x += 2) {
        drawPixel(x, 0, BLACK);
        drawPixel(x, headerDivY, BLACK); // Dotted 50%
    }
    printAt(0, padDivH, "Input Menu");

    if (selMode == 0x10 || selMode == 0x11) {
        int16_t itemHeight = fontSmall.lineHeight() + 1;
        int16_t showItems = (height() - headerDivY - 1) / itemHeight;
        int16_t startIdx = 0;
        if (selTarget != -1 && (int16_t)sendTargets.size() > showItems) {
            // Scroll so the selected target stays visible; clamp to 0 when it is within the first
            // window (selTarget < showItems would otherwise make startIdx negative -> OOB read).
            startIdx = selTarget - showItems + 1;
            if (startIdx < 0)
                startIdx = 0;
        }
        for (int i = 0; i < showItems && startIdx + i < (int)sendTargets.size(); i++) {
            printAt(0, headerDivY + 1 + itemHeight * i, std::get<0>(sendTargets[startIdx + i]));
            for (int16_t x = 0; x < width(); x += 2)
                drawPixel(x, headerDivY + 1 + itemHeight * (i + 1), BLACK);
            if (startIdx + i == selTarget) {
                drawRect(0, headerDivY + 1 + itemHeight * i, width(), itemHeight, BLACK);
            }
        }
    }
    else {
        std::string bodyText = parse(currentInput);
        printWrapped(0, headerDivY, width(), bodyText);
        if (selMode == 0 && selKB == -1) {
            int16_t kbKeyHeight = fontSmall.lineHeight() + 1;
            int16_t kbBarTop = height() - kbKeyHeight - 1;
            for (int16_t x = 0; x < width(); x += 2)
                drawPixel(x, kbBarTop, BLACK);
            int16_t kbKeyboards = keyboards.size();
            for (int16_t y = kbBarTop + 2; y < kbBarTop + kbKeyHeight - 1; y += 2)
                drawPixel(0, y, BLACK);
            for (int16_t kbIdx = 0; kbIdx < kbKeyboards; kbIdx++) {
                int16_t kbKeyLeft = (int)(width() - 1) * kbIdx / kbKeyboards;
                int16_t kbKeyRight = (int)(width() - 1) * (kbIdx + 1) / kbKeyboards;
                printAt((kbKeyLeft + kbKeyRight + 1) / 2, kbBarTop + 1, std::get<0>(keyboards[kbIdx]), CENTER, TOP);
                for (int16_t y = kbBarTop; y < kbBarTop + kbKeyHeight - 1; y += 2)
                    drawPixel(kbKeyRight, y, BLACK);
            }
            for (int16_t x = 0; x < width(); x += 2)
                drawPixel(x, height() - 1, BLACK);
        }
        if (selKB >= 0 && selKB < (int16_t)keyboards.size()) {
            auto& kb = keyboards[selKB];
            int16_t kbKeyHeight = fontSmall.lineHeight() + 1;
            int16_t kbRows = std::get<1>(kb).size();
            int16_t kbBoardHeight = kbKeyHeight * kbRows + 1;
            int16_t kbTop = height() - kbBoardHeight;
            int16_t kbBarTop = height() - kbBoardHeight - kbKeyHeight;
            if (selMode == 0) {
                for (int16_t x = 0; x < width(); x += 2)
                    drawPixel(x, kbBarTop, BLACK);
                int16_t kbKeyboards = keyboards.size();
                for (int16_t y = kbBarTop + 2; y < kbBarTop + kbKeyHeight - 1; y += 2)
                    drawPixel(0, y, BLACK);
                for (int16_t kbIdx = 0; kbIdx < kbKeyboards; kbIdx++) {
                    int16_t kbKeyLeft = (int)(width() - 1) * kbIdx / kbKeyboards;
                    int16_t kbKeyRight = (int)(width() - 1) * (kbIdx + 1) / kbKeyboards;
                    printAt((kbKeyLeft + kbKeyRight + 1) / 2, kbBarTop + 1, std::get<0>(keyboards[kbIdx]), CENTER, TOP);
                    for (int16_t y = kbBarTop; y < kbBarTop + kbKeyHeight - 1; y += 2)
                        drawPixel(kbKeyRight, y, BLACK);
                }
            }
#if defined(MOD_TKEY_MODEL)
            else if ((keyboardCIM || selKB == 5 || selKB == 6 || selKB == 7) && !currentCIM.empty()) {
#else //!defined(MOD_TKEY_MODEL)
            else if (selKB == 4 && !currentCIM.empty()) {
#endif //defined(MOD_TKEY_MODEL)
                for (int16_t x = 0; x < width(); x += 2)
                    drawPixel(x, kbBarTop, BLACK);
                printAt(0, kbBarTop + 1, currentCIM);
            }
#if defined(MOD_TKEY_MODEL)
            else if ((keyboardCIM || selKB == 5 || selKB == 6 || selKB == 7) && selMode == 3) {
#else //!defined(MOD_TKEY_MODEL)
            else if (selKB == 4 && selMode == 3) {
#endif //defined(MOD_TKEY_MODEL)
                for (int16_t x = 0; x < width(); x += 2)
                    drawPixel(x, kbBarTop, BLACK);
                for (int16_t y = kbBarTop + 2; y < kbBarTop + kbKeyHeight - 1; y += 2)
                    drawPixel(0, y, BLACK);
                int16_t resultCount = currentCIMResults.size();
                int16_t resultBase = 0;
                if (selResult!= -1 && resultCount > 10) {
                    resultBase = selResult - selResult % 10;
                }
                for (int16_t resultIdx = 0; resultIdx < 10; resultIdx++) {
                    int16_t resultKeyLeft = (int)(width() - 1) * resultIdx / 10;
                    int16_t resultKeyRight = (int)(width() - 1) * (resultIdx + 1) / 10;
                    if (resultBase + resultIdx < (int16_t)currentCIMResults.size())
                        printAt((resultKeyLeft + resultKeyRight + 1) / 2, kbBarTop + 1, currentCIMResults[resultBase + resultIdx], CENTER, TOP);
                    for (int16_t y = kbBarTop; y < kbBarTop + kbKeyHeight - 1; y += 2)
                        drawPixel(resultKeyRight, y, BLACK);
                }
            }
            for (int16_t x = 0; x < width(); x += 2)
                drawPixel(x, kbTop, BLACK);
            if (selMode == 0 && selKB != -1) {
                int16_t kbKeyboards = keyboards.size();
                int16_t kbKeyLeft = (int)(width() - 1) * selKB / kbKeyboards;
                int16_t kbKeyRight = (int)(width() - 1) * (selKB + 1) / kbKeyboards;
                drawRect(kbKeyLeft, kbBarTop, kbKeyRight - kbKeyLeft + 1, kbKeyHeight + 1, BLACK);
            }
#if defined(MOD_TKEY_MODEL)
            else if ((keyboardCIM || selKB == 5 || selKB == 6 || selKB == 7) && selMode == 3 && selResult != -1) {
#else //!defined(MOD_TKEY_MODEL)
            else if (selKB == 4 && selMode == 3 && selResult != -1) {
#endif //defined(MOD_TKEY_MODEL)
                int16_t resultKeyLeft = (int)(width() - 1) * (selResult % 10) / 10;
                int16_t resultKeyRight = (int)(width() - 1) * (selResult % 10 + 1) / 10;
                drawRect(resultKeyLeft, kbBarTop, resultKeyRight - resultKeyLeft + 1, kbKeyHeight + 1, BLACK);
            }
            for (int16_t row = 0; row < kbRows; row++) {
                int16_t kbCols = std::get<1>(kb)[row].size();
                for (int16_t y = kbTop + row * kbKeyHeight + 2; y < kbTop + (row + 1) * kbKeyHeight - 1; y += 2)
                    drawPixel(0, y, BLACK);
                for (int16_t col = 0; col < kbCols; col++) {
                    int16_t kbKeyCenter = (int)(width() - 1) * (col * 2 + 1) / kbCols / 2;
                    int16_t kbKeyRight = (int)(width() - 1) * (col + 1) / kbCols;
                    printAt(kbKeyCenter, kbTop + row * kbKeyHeight + 1, std::get<0>(std::get<1>(kb)[row][col]), CENTER, TOP);
                    for (int16_t y = kbTop + row * kbKeyHeight + 2; y < kbTop + (row + 1) * kbKeyHeight - 1; y += 2)
                        drawPixel(kbKeyRight, y, BLACK);
                }
                for (int16_t x = 0; x < width(); x += 2)
                    drawPixel(x, kbTop + (row + 1) * kbKeyHeight, BLACK);
                if (selMode == 1 && selRow == row) {
                    drawRect(0, kbTop + row * kbKeyHeight, width(), kbKeyHeight + 1, BLACK);
                }
                else if (selMode == 2 && selRow == row && selCol != -1) {
                    int16_t kbKeyLeft = (int)(width() - 1) * selCol / kbCols;
                    int16_t kbKeyRight = (int)(width() - 1) * (selCol + 1) / kbCols;
                    drawRect(kbKeyLeft, kbTop + row * kbKeyHeight, kbKeyRight - kbKeyLeft + 1, kbKeyHeight + 1, BLACK);
                }
            }
        }
    }
}

void InkHUD::InputMenuApplet::noteUserActivity()
{
#if defined(MOD_UART_KEYBOARD_12KEY) || defined(MOD_UART_T_KEYBOARD) || defined(MOD_I2C_TCA8418_KEYBOARD)
    autoHideMillis = millis() + INPUT_TIMEOUT_SEC * 1000UL;
#else //!(defined(MOD_UART_KEYBOARD_12KEY) || defined(MOD_UART_T_KEYBOARD) || defined(MOD_I2C_TCA8418_KEYBOARD))
    OSThread::setIntervalFromNow(INPUT_TIMEOUT_SEC * 1000UL);
#endif //defined(MOD_UART_KEYBOARD_12KEY) || defined(MOD_UART_T_KEYBOARD) || defined(MOD_I2C_TCA8418_KEYBOARD)
}

int16_t &InkHUD::InputMenuApplet::currentCursor()
{
    switch (selMode) {
    case 0:
        return selKB;
    case 1:
        return selRow;
    case 2:
        return selCol;
    case 3:
        return selResult;
    default: // 0x10 / 0x11
        return selTarget;
    }
}

int16_t InkHUD::InputMenuApplet::currentLevelCount()
{
    switch (selMode) {
    case 0:
        return (int16_t)keyboards.size();
    case 1:
        return (int16_t)std::get<1>(keyboards[selKB]).size();
    case 2:
        return (int16_t)std::get<1>(keyboards[selKB])[selRow].size();
    case 3:
        return (int16_t)currentCIMResults.size();
    default: // 0x10 / 0x11
        return (int16_t)sendTargets.size();
    }
}

// Move the active level's cursor by delta (+1/-1), cycling through the same
// [-1 .. count-1] range the single-button short-press uses (-1 = "nothing selected").
void InkHUD::InputMenuApplet::cursorStep(int delta)
{
    int16_t &cur = currentCursor();
    const int16_t count = currentLevelCount();
    cur = (int16_t)(cur + delta);
    if (cur >= count)
        cur = -1;
    else if (cur < -1)
        cur = (int16_t)(count - 1);
    requestUpdate(Drivers::EInk::UpdateTypes::FAST);
}

// Descend one level, or commit at the current cursor.
// Mirrors the "cursor != -1" branch of the original onButtonLongPress.
void InkHUD::InputMenuApplet::levelActivate()
{
    // -1 sentinel = "nothing selected": activating steps back instead, like onButtonLongPress.
    // Also prevents indexing keyboards/candidates/targets with -1 (reachable via onNavRight).
    if (currentCursor() == -1) {
        levelBack();
        return;
    }

    if (selMode == 0) {
        selMode = 1;
        selRow = 0;
    }
    else if (selMode == 1) {
        if (std::get<1>(keyboards[selKB])[selRow].size() > 1) {
            selMode = 2;
            selCol = 0;
        }
        else {
            selCol = 0;
            handleKeyboardPress();
        }
    }
    else { // selMode 2, 3, 0x10, 0x11: commit at the cursor
        handleKeyboardPress();
    }

    // FAST update unless the action already requested a specialized one
    if (!wantsToRender())
        requestUpdate(Drivers::EInk::UpdateTypes::FAST);
}

// Step up one level, exit at the top, or clear the CIM.
// Mirrors the "cursor == -1" branch of the original onButtonLongPress.
void InkHUD::InputMenuApplet::levelBack()
{
    if (selMode == 0) {
        sendToBackground();
    }
    else if (selMode == 1) {
        selMode = 0;
    }
    else if (selMode == 2) {
        selMode = 1;
    }
    else if (selMode == 3) {
        currentCIM.clear();
        currentCIMKeys.clear();
        currentCIMResults.clear();
        selMode = 1;
    }
    else { // 0x10 / 0x11
        selMode = 1;
    }

    if (!wantsToRender())
        requestUpdate(Drivers::EInk::UpdateTypes::FAST);
}

void InkHUD::InputMenuApplet::onButtonShortPress()
{
    noteUserActivity();
    cursorStep(+1); // advance to next item in the current level (single-button cycle)
}

void InkHUD::InputMenuApplet::onButtonLongPress()
{
    noteUserActivity();
    // -1 sentinel means "nothing selected" -> step back a level; otherwise descend/commit
    if (currentCursor() == -1)
        levelBack();
    else
        levelActivate();
}

// --- Directional navigation (joystick / rocker / keyboard arrows) ---
// Same cursor model as the single button, just with explicit up/down and back/activate.

void InkHUD::InputMenuApplet::onNavUp()
{
    noteUserActivity();
    cursorStep(-1);
}

void InkHUD::InputMenuApplet::onNavDown()
{
    noteUserActivity();
    cursorStep(+1);
}

void InkHUD::InputMenuApplet::onNavLeft()
{
    noteUserActivity();
    levelBack();
}

void InkHUD::InputMenuApplet::onNavRight()
{
    noteUserActivity();
    levelActivate();
}

void InkHUD::InputMenuApplet::onExitShort()
{
    noteUserActivity();
    levelBack();
}

void InkHUD::InputMenuApplet::onExitLong()
{
    noteUserActivity();
    sendToBackground();
}

void InkHUD::InputMenuApplet::handleKeyboardPress()
{
    Applet* ctrlPtr0 = nullptr;
    Controllable::Types ctrlType = Controllable::Types::Uncontrollable;
    bool bBorrowed = false;
    if (neighborTileOwner && Controllable::checkControllable(neighborTileOwner) != Controllable::Types::Uncontrollable) {
        ctrlPtr0 = neighborTileOwner;
        ctrlType = Controllable::checkControllable(neighborTileOwner);
    }
    else if (borrowedTileOwner && Controllable::checkControllable(borrowedTileOwner) != Controllable::Types::Uncontrollable) {
        ctrlPtr0 = borrowedTileOwner;
        ctrlType = Controllable::checkControllable(borrowedTileOwner);
        bBorrowed = true;
    }
    if (selMode == 0x10) { // send to channel
        std::string message = currentInput;
        currentInput.clear();
        currentCIM.clear();
        currentCIMKeys.clear();
        currentCIMResults.clear();
        if (selTarget >= 0 && selTarget < (int16_t)sendTargets.size()) // never .at() an empty list
            sendText(NODENUM_BROADCAST, std::get<1>(sendTargets.at(selTarget)), message);
        selMode = 1;
#if !defined(MOD_UART_KEYBOARD_12KEY)
        selCol = -1;
        selRow = -1;
#endif //defined(MOD_UART_KEYBOARD_12KEY)
    }
    else if (selMode == 0x11) { // send to user
        std::string message = currentInput;
        currentInput.clear();
        currentCIM.clear();
        currentCIMKeys.clear();
        currentCIMResults.clear();
        if (selTarget >= 0 && selTarget < (int16_t)sendTargets.size()) // never .at() an empty list
            sendText(std::get<1>(sendTargets.at(selTarget)), 0, message);
        selMode = 1;
#if !defined(MOD_UART_KEYBOARD_12KEY)
        selCol = -1;
        selRow = -1;
#endif //defined(MOD_UART_KEYBOARD_12KEY)
    }
    else {
        if (selKB == 0) {
#if defined(MOD_TKEY_MODEL)
            const auto funcCode = std::get<1>(std::get<1>(keyboards[selKB])[selRow][selCol]);
            LOG_INFO("T-KB: funcCode=%d", (int)funcCode);
            dispatchTKeyFunc(funcCode, ctrlPtr0, ctrlType, bBorrowed);
#else //!defined(MOD_TKEY_MODEL)
            const auto cmdCode = std::get<1>(std::get<1>(keyboards[selKB])[selRow][selCol]);
            if (cmdCode == 0) {
                if (ctrlPtr0 && ctrlType == Controllable::Types::ThreadedMessage) {
                    auto ctrlPtr = (ThreadedMessageApplet*)ctrlPtr0;
                    std::string message = currentInput;
                    currentInput.clear();
                    currentCIM.clear();
                    currentCIMKeys.clear();
                    currentCIMResults.clear();
                    selMode = 1;
#if !defined(MOD_UART_KEYBOARD_12KEY)
                    selCol = -1;
                    selRow = -1;
#endif //defined(MOD_UART_KEYBOARD_12KEY)
                    sendText(NODENUM_BROADCAST, ctrlPtr->getChannelIndex(), message);
                    if (bBorrowed)
                        sendToBackground();
                }
                else if (ctrlPtr0 && ctrlType == Controllable::Types::DMChat) {
                    // Chat window: composed text goes as a DM to the bound peer
                    auto ctrlPtr = (DMChatApplet*)ctrlPtr0;
                    if (ctrlPtr->isBound()) {
                        std::string message = currentInput;
                        currentInput.clear();
                        currentCIM.clear();
                        currentCIMKeys.clear();
                        currentCIMResults.clear();
                        selMode = 1;
#if !defined(MOD_UART_KEYBOARD_12KEY)
                        selCol = -1;
                        selRow = -1;
#endif //defined(MOD_UART_KEYBOARD_12KEY)
                        sendText(ctrlPtr->getPeer(), 0, message);
                        ctrlPtr->noteSent();
                        if (bBorrowed)
                            sendToBackground();
                    }
                }
                else if (ctrlPtr0 && ctrlType == Controllable::Types::UniChat) {
                    // Unified chats: send to whichever target thread is open
                    auto ctrlPtr = (UniChatApplet*)ctrlPtr0;
                    if (ctrlPtr->hasThreadTarget()) {
                        std::string message = currentInput;
                        currentInput.clear();
                        currentCIM.clear();
                        currentCIMKeys.clear();
                        currentCIMResults.clear();
                        selMode = 1;
#if !defined(MOD_UART_KEYBOARD_12KEY)
                        selCol = -1;
                        selRow = -1;
#endif //defined(MOD_UART_KEYBOARD_12KEY)
                        if (ctrlPtr->targetIsDM())
                            sendText(ctrlPtr->getPeer(), 0, message);
                        else
                            sendText(NODENUM_BROADCAST, ctrlPtr->getChannelIndex(), message);
                        ctrlPtr->noteSent();
                        if (bBorrowed)
                            sendToBackground();
                    }
                }
            }
            else if (cmdCode == 1) {
                utf8PopLast(currentInput);
                selMode = 1;
#if !defined(MOD_UART_KEYBOARD_12KEY)
                selCol = -1;
                selRow = -1;
#endif //defined(MOD_UART_KEYBOARD_12KEY)
            }
            else if (cmdCode == 2) {
                currentInput.clear();
                selMode = 1;
#if !defined(MOD_UART_KEYBOARD_12KEY)
                selCol = -1;
                selRow = -1;
#endif //defined(MOD_UART_KEYBOARD_12KEY)
            }
            else if (cmdCode == 3 || cmdCode == 4) {
                Controllable* ctrlPtr = nullptr;
                if (ctrlPtr0) {
                    if (ctrlType == Controllable::Types::ThreadedMessage) {
                        auto ctrlPtr1 = (ThreadedMessageApplet*)ctrlPtr0;
                        ctrlPtr = static_cast<Controllable*>(ctrlPtr1);
                    }
                    else if (ctrlType == Controllable::Types::Heard) {
                        auto ctrlPtr1 = (HeardApplet*)ctrlPtr0;
                        ctrlPtr = static_cast<Controllable*>(ctrlPtr1);
                    }
                    else if (ctrlType == Controllable::Types::DMChat) {
                        auto ctrlPtr1 = (DMChatApplet*)ctrlPtr0;
                        ctrlPtr = static_cast<Controllable*>(ctrlPtr1);
                    }
                    else if (ctrlType == Controllable::Types::UniChat) {
                        auto ctrlPtr1 = (UniChatApplet*)ctrlPtr0;
                        ctrlPtr = static_cast<Controllable*>(ctrlPtr1);
                    }
                    if (ctrlPtr) {
                        if (cmdCode == 3) {
                            ctrlPtr->handleUp();
                        }
                        else if (cmdCode == 4) {
                            ctrlPtr->handleDown();
                        }
                        if (bBorrowed)
                            sendToBackground();
                    }
                }
            }
            else if (cmdCode == 5) {
                populateChannelTargets();
                if (!sendTargets.empty()) { // don't enter the target picker with nothing to send to
                    selMode = 0x10;
                    selTarget = 0; // pre-select the first target (consistent with structural descents)
                }
            }
            else if (cmdCode == 6) {
                populateFavoriteTargets();
                if (!sendTargets.empty()) { // e.g. zero favorited nodes -> stay put, don't crash on .at()
                    selMode = 0x11;
                    selTarget = 0; // pre-select the first target (consistent with structural descents)
                }
            }
            else if (cmdCode == 7) {
                settings->userTiles.count++;
                if (settings->userTiles.count == 3)
                    settings->userTiles.count++;
                if (settings->userTiles.count > settings->userTiles.maxCount)
                    settings->userTiles.count = 1;
                inkhud->updateLayout();
            }
            else if (cmdCode == 8) {
                inkhud->nextTile();
            }
            else if (cmdCode == 9) {
                LOG_INFO("Shutting down from input menu");
                shutdownAtMsec = millis();
            }
            else if (cmdCode == 10) {
                MenuApplet *menu = (MenuApplet *)inkhud->getSystemApplet("Menu");
                // sendToBackground() may merge a transient split, freeing every tile -> getTile() would dangle.
                // Capture it only when NOT splitting; else show on the freshly-rebuilt focused tile.
                Tile *t = inkhud->isMenuSplitActive() ? nullptr : getTile();
                sendToBackground();
                menu->show(t ? t : inkhud->getFocusedTile());
            }
            else if (cmdCode == 11) {
                config.lora.tx_power = 1;
                service->reloadConfig(SEGMENT_CONFIG);
                rebootAtMsec = millis() + 500;
            }
            else if (cmdCode == 12) {
                config.lora.tx_power = 9;
                service->reloadConfig(SEGMENT_CONFIG);
                rebootAtMsec = millis() + 500;
            }
            else if (cmdCode == 13) {
                config.lora.tx_power = 17;
                service->reloadConfig(SEGMENT_CONFIG);
                rebootAtMsec = millis() + 500;
            }
            else if (cmdCode == 14) {
                config.lora.tx_power = 27;
                service->reloadConfig(SEGMENT_CONFIG);
                rebootAtMsec = millis() + 500;
            }
#endif //defined(MOD_TKEY_MODEL)
        }
        else if (selKB == 1) {
            currentInput += std::get<0>(std::get<1>(keyboards[selKB])[selRow][selCol]);
#if defined(MOD_UART_KEYBOARD_12KEY)
            selMode = 2;
#else //!defined(MOD_UART_KEYBOARD_12KEY)
            selMode = 1;
            selCol = -1;
#endif //defined(MOD_UART_KEYBOARD_12KEY)
        }
        else if (selKB == 2) {
            currentInput += std::get<0>(std::get<1>(keyboards[selKB])[selRow][selCol]);
#if defined(MOD_UART_KEYBOARD_12KEY)
            selMode = 2;
#else //!defined(MOD_UART_KEYBOARD_12KEY)
            selMode = 1;
            selCol = -1;
#endif //defined(MOD_UART_KEYBOARD_12KEY)
        }
        else if (selKB == 3) {
            currentInput += std::get<0>(std::get<1>(keyboards[selKB])[selRow][selCol]);
#if defined(MOD_UART_KEYBOARD_12KEY)
            selMode = 2;
#else //!defined(MOD_UART_KEYBOARD_12KEY)
            selMode = 1;
            selCol = -1;
#endif //defined(MOD_UART_KEYBOARD_12KEY)
        }
#if defined(MOD_TKEY_MODEL)
        else if (selKB == 4) { // emoji page: plain append, like the letter boards (bopomofo pages are 5/6 on this build)
            currentInput += std::get<0>(std::get<1>(keyboards[selKB])[selRow][selCol]);
            selMode = 1;
            selCol = -1;
        }
#endif //defined(MOD_TKEY_MODEL)
#if defined(MOD_CJK_ENABLED)
#if defined(MOD_TKEY_MODEL)
        else if (selKB == 5 || selKB == 6 || selKB == 7) { // half-mapped pages + the full on-screen board
#else //!defined(MOD_TKEY_MODEL)
        else if (selKB == 4) {
#endif //defined(MOD_TKEY_MODEL)
            if (selMode == 3) {
                currentInput += currentCIMResults[selResult];
                currentCIM.clear();
                currentCIMKeys.clear();
                currentCIMResults.clear();
#if defined(MOD_UART_KEYBOARD_12KEY)
                selMode = 2;
#else //!defined(MOD_UART_KEYBOARD_12KEY)
                selMode = 1;
                selCol = -1;
                selRow = -1;
#endif //defined(MOD_UART_KEYBOARD_12KEY)
            }
            else {
                int16_t key = std::get<1>(std::get<1>(keyboards[selKB])[selRow][selCol]);
                if (key < 37) {
                    currentCIM += std::get<0>(std::get<1>(keyboards[selKB])[selRow][selCol]);
                    currentCIMKeys.emplace_back(key);
#if defined(MOD_UART_KEYBOARD_12KEY)
                    selMode = 2;
#else //!defined(MOD_UART_KEYBOARD_12KEY)
                    selMode = 1;
                    selCol = -1;
                    selRow = -1;
#endif //defined(MOD_UART_KEYBOARD_12KEY)
                }
                else if (key < 42) {
                    key -= 37;
                    lookupBopomofoCandidates(key, 0); // pre-highlight candidate 0 on every build
                    currentCIM.clear();
                    currentCIMKeys.clear();
                    if (selMode != 3) {
                        currentCIMResults.clear();
#if defined(MOD_UART_KEYBOARD_12KEY)
                        selMode = 2;
#else //!defined(MOD_UART_KEYBOARD_12KEY)
                        selMode = 1;
                        selCol = -1;
                        selRow = -1;
#endif //defined(MOD_UART_KEYBOARD_12KEY)
                    }
                }
                else {
#if defined(MOD_TKEY_MODEL)
                    // Key codes of the T-KB bopomofo pages 5/6 (same map as handleCIMKey); other codes
                    // (⇒ 43, fillers, ✔ 52) are placeholders for physical keys and only reset the selection
                    if (key == 42) { // ⬅ : drop the pending symbol first, else the last committed character
                        if (!currentCIMKeys.empty()) {
                            utf8PopLast(currentCIM);
                            currentCIMKeys.pop_back();
                        }
                        else {
                            utf8PopLast(currentInput);
                        }
                    }
                    else if (key == 47)
                        currentInput += "，";
                    else if (key == 48)
                        currentInput += "。";
                    else if (key == 49)
                        currentInput += "：";
                    else if (key == 50)
                        currentInput += "；";
                    else if (key == 51)
                        currentInput += "　";
                    // 53-55: punctuation on the full on-screen bopomofo board (7)
                    else if (key == 53)
                        currentInput += "，";
                    else if (key == 54)
                        currentInput += "。";
                    else if (key == 55)
                        currentInput += "　";
#else //!defined(MOD_TKEY_MODEL)
                    if (key == 42)
                        currentInput += "，";
                    else if (key == 43)
                        currentInput += "。";
                    else if (key == 44)
                        currentInput += "　";
#endif //defined(MOD_TKEY_MODEL)
#if defined(MOD_UART_KEYBOARD_12KEY)
                    selMode = 2;
#else //!defined(MOD_UART_KEYBOARD_12KEY)
                    selMode = 1;
                    selCol = -1;
                    selRow = -1;
#endif //defined(MOD_UART_KEYBOARD_12KEY)
                }
            }
        }
#endif //defined(MOD_CJK_ENABLED)
    }
}

// Remove the final UTF-8 codepoint from s (used by every CIM-aware backspace path). Uses the signed
// getUTF8Chars result so a malformed lead byte (-1) stops the walk instead of wrapping.
void InkHUD::InputMenuApplet::utf8PopLast(std::string &s)
{
    for (size_t pos = 0; pos < s.length();) {
        int numChars = getUTF8Chars((const uint8_t *)s.c_str() + pos);
        if (numChars < 1)
            break;
        if (pos + (size_t)numChars == s.length())
            s = s.substr(0, pos);
        pos += (size_t)numChars;
    }
}

// (Re)fill sendTargets with the enabled channels ("CHn:name"). Callers set selMode/selTarget.
void InkHUD::InputMenuApplet::populateChannelTargets()
{
    sendTargets.clear();
    for (uint8_t i = 0; i < MAX_NUM_CHANNELS; i++) {
        meshtastic_Channel &channel = channels.getByIndex(i);
        if (!channel.has_settings || channel.role == meshtastic_Channel_Role_DISABLED)
            continue;
        sendTargets.emplace_back(std::string("CH") + std::to_string((int)channel.index) + ":" + channel.settings.name, channel.index);
    }
}

// (Re)fill sendTargets with the favorite nodes (long name, else hex num). Callers set selMode/selTarget.
void InkHUD::InputMenuApplet::populateFavoriteTargets()
{
    sendTargets.clear();
    uint32_t nodeCount = nodeDB->getNumMeshNodes();
    for (uint32_t i = 0; i < nodeCount; i++) {
        meshtastic_NodeInfoLite *node = nodeDB->getMeshNodeByIndex(i);
        if (!nodeInfoLiteIsFavorite(node))
            continue;
        if (nodeInfoLiteHasUser(node))
            sendTargets.emplace_back(std::string(node->long_name), node->num);
        else
            sendTargets.emplace_back(hexifyNodeNum(node->num), node->num);
    }
}

// "Left" navigation: focus the previous user tile in a multi-tile layout (bounded — no-op at the leftmost
// tile), or step to the previous applet when there is only one tile. Bounded prevTile()/nextTile() keep the
// 1- and 2-tile behaviour identical to the old focused==0/focused>0 toggle while making 3/4-tile layouts
// navigable (the old code called nextTile() for "left", which moved the wrong way past two tiles).
void InkHUD::InputMenuApplet::tilePrevOrApplet()
{
    if (settings->userTiles.count > 1) {
        if (settings->userTiles.focused > 0)
            inkhud->prevTile();
    }
    else {
        inkhud->prevApplet();
    }
}

// "Right" navigation: focus the next user tile in a multi-tile layout (bounded — no-op at the rightmost
// tile), or step to the next applet when there is only one tile.
void InkHUD::InputMenuApplet::tileNextOrApplet()
{
    if (settings->userTiles.count > 1) {
        if (settings->userTiles.focused < settings->userTiles.count - 1)
            inkhud->nextTile();
    }
    else {
        inkhud->nextApplet();
    }
}

#if defined(MOD_CJK_ENABLED)
// Walk the bopomofoTable trie for the accumulated currentCIMKeys plus the given tone (0..4), appending
// each matching character to currentCIMResults. On a non-empty result, enter candidate mode (selMode=3,
// selResult=selResultOnFound). Guards each descent against a -1 child pointer (invalid symbol sequence)
// and only commits to candidate mode when characters were actually produced.
void InkHUD::InputMenuApplet::lookupBopomofoCandidates(int16_t toneKey, int16_t selResultOnFound)
{
    if (currentCIMKeys.size() == 1) {
        // Terminal node: slots idx0..idx0+5 are this syllable's tone-range boundaries, so no child
        // descent and none of the +=6/++ skips the multi-symbol cases below use.
        auto idx0 = bopomofoTable[currentCIMKeys[0]];
        if (bopomofoTable[idx0] != -1) {
            for (int16_t idx1 = bopomofoTable[idx0 + toneKey]; idx1 < bopomofoTable[idx0 + toneKey + 1]; idx1++) {
                int16_t charIdx = exactIndex[bopomofoTable[idx1]];
                currentCIMResults.emplace_back(std::string((char*)exactMap + charIdx, (size_t)getUTF8Chars(exactMap + charIdx)));
            }
        }
    }
    else if (currentCIMKeys.size() == 2) {
        auto idx0 = bopomofoTable[currentCIMKeys[0]];
        if (bopomofoTable[idx0] == -1)
            idx0++;
        else
            idx0 += 6;
        idx0 = bopomofoTable[idx0 + currentCIMKeys[1]]; // may be -1 for an invalid symbol prefix
        if (idx0 >= 0 && bopomofoTable[idx0] != -1) {
            for (int16_t idx1 = bopomofoTable[idx0 + toneKey]; idx1 < bopomofoTable[idx0 + toneKey + 1]; idx1++) {
                int16_t charIdx = exactIndex[bopomofoTable[idx1]];
                currentCIMResults.emplace_back(std::string((char*)exactMap + charIdx, (size_t)getUTF8Chars(exactMap + charIdx)));
            }
        }
    }
    else if (currentCIMKeys.size() == 3) {
        auto idx0 = bopomofoTable[currentCIMKeys[0]];
        if (bopomofoTable[idx0] == -1)
            idx0++;
        else
            idx0 += 6;
        idx0 = bopomofoTable[idx0 + currentCIMKeys[1]]; // may be -1 for an invalid symbol prefix
        if (idx0 >= 0) {
            if (bopomofoTable[idx0] == -1)
                idx0++;
            else
                idx0 += 6;
            idx0 = bopomofoTable[idx0 + currentCIMKeys[2]]; // may be -1 too
        }
        if (idx0 >= 0 && bopomofoTable[idx0] != -1) {
            for (int16_t idx1 = bopomofoTable[idx0 + toneKey]; idx1 < bopomofoTable[idx0 + toneKey + 1]; idx1++) {
                int16_t charIdx = exactIndex[bopomofoTable[idx1]];
                currentCIMResults.emplace_back(std::string((char*)exactMap + charIdx, (size_t)getUTF8Chars(exactMap + charIdx)));
            }
        }
    }
    // Only enter candidate mode when the walk actually produced characters (a valid syllable may lack a
    // given tone); this also keeps the (size()-1) paging arithmetic away from an empty vector.
    if (!currentCIMResults.empty()) {
        selMode = 3;
        selResult = selResultOnFound;
    }
}
#endif //defined(MOD_CJK_ENABLED)

#if defined(MOD_TKEY_MODEL)
// Shared funcCode dispatch for the T-Keyboard Fn layer, reached two ways: the physical bAlt+key combo
// (handleMenuTKey) and Enter on the on-screen Fn board (handleKeyboardPress). Both resolve funcCode from
// the keyboard map, then call this. funcCode==14 commits the composed text to the neighbour applet.
void InkHUD::InputMenuApplet::dispatchTKeyFunc(int16_t funcCode, Applet *ctrlPtr0, Controllable::Types ctrlType, bool bBorrowed)
{
    if (funcCode == 0 || funcCode == 5) {
#if defined(MOD_UART_T_KEYBOARD)
        // UART T-keyboard: dim/brighten its PWM backlight over the wire
        if (funcCode == 0)
            currentKBBL = (currentKBBL < 15) ? 7 : (currentKBBL - 8);
        else
            currentKBBL = (currentKBBL > 246) ? 255 : (currentKBBL + 8);
        Serial2.write(0x02);
        Serial2.write(currentKBBL);
#elif defined(MOD_I2C_TCA8418_KEYBOARD)
        // TCA8418 keyboard backlight is a simple GPIO: 🔅 off / 🔆 on
        if (inkhudI2CKeyboard)
            inkhudI2CKeyboard->setBacklight(funcCode == 5);
#endif
    }
    else if (funcCode == 1) {
        auto app = getActiveControllable();
        if (app)
            app->handleBack();
    }
    else if (funcCode == 2) {
        auto app = getActiveControllable();
        if (app)
            app->handleUp();
    }
    else if (funcCode == 3) {
        auto app = getActiveControllable();
        if (app)
            app->handleEnter();
    }
    else if (funcCode == 4) {
        LOG_INFO("Shutting down from input menu");
        shutdownAtMsec = millis();
    }
    else if (funcCode == 6) {
        tilePrevOrApplet();
    }
    else if (funcCode == 7) {
        auto app = getActiveControllable();
        if (app)
            app->handleDown();
    }
    else if (funcCode == 8) {
        tileNextOrApplet();
    }
    else if (funcCode == 9) {
        sendToBackground();
    }
    else if (funcCode == 10) {
#if defined(T_DECK_MAX)
        // ☀ (alt+B): e-ink frontlight cycle Off -> Low -> Med -> High. The legend existed on the
        // Fn board since the port but was never wired to a dispatch case.
        ((MenuApplet *)inkhud->getSystemApplet("Menu"))->quickCycleFrontlight();
#endif
    }
    else if (funcCode == 16) {
#if defined(T_DECK_MAX)
        // "Vib" (alt+V): vibration policy All -> DMs only -> Off; the corner bell glyph reflects it
        ((MenuApplet *)inkhud->getSystemApplet("Menu"))->quickCycleVibra();
        inkhud->forceUpdate(Drivers::EInk::UpdateTypes::FAST, true);
#endif
    }
    else if (funcCode == 11) {
        populateChannelTargets();
        if (!sendTargets.empty()) { // don't enter the target picker with nothing to send to
            selMode = 0x10;
            selTarget = 0;
        }
    }
    else if (funcCode == 12) {
        MenuApplet *menu = (MenuApplet *)inkhud->getSystemApplet("Menu");
        // sendToBackground() may merge a transient split, which frees every tile -> getTile() would
        // dangle. Capture it only when NOT splitting; else show on the freshly-rebuilt focused tile.
        Tile *t = inkhud->isMenuSplitActive() ? nullptr : getTile();
        sendToBackground();
        menu->show(t ? t : inkhud->getFocusedTile());
    }
    else if (funcCode == 13) {
        populateFavoriteTargets();
        if (!sendTargets.empty()) { // e.g. zero favorited nodes -> stay put, don't crash on .at()
            selMode = 0x11;
            selTarget = 0;
        }
    }
    else if (funcCode == 14) {
        commitInputToNeighbor(ctrlPtr0, ctrlType, bBorrowed);
    }
    else if (funcCode == 15) {
        // Toggle the focused tile ("<->", alt+D). Unlike ←/→ (funcCodes 6/8, bounded prev/next),
        // nextTile() wraps, so in the 2-tile split it flips upper<->lower with a single chord.
        // With the IME open on a 2-tile layout, nextTile() re-shows it driving the other half.
        if (settings->userTiles.count > 1)
            inkhud->nextTile();
    }
}

// funcCode 14: commit currentInput to the neighbour/borrowed controllable. Broadcast for ThreadedMessage,
// "lat,lng"/place-name jump for NavMap, node search for Heard. Clears the compose buffers and
// closes the menu when we borrowed the tile (or a transient split is active, for NavMap).
void InkHUD::InputMenuApplet::commitInputToNeighbor(Applet *ctrlPtr0, Controllable::Types ctrlType, bool bBorrowed)
{
    if (ctrlPtr0 && ctrlType == Controllable::Types::ThreadedMessage) {
        auto ctrlPtr = (ThreadedMessageApplet*)ctrlPtr0;
        std::string message = currentInput;
        currentInput.clear();
        currentCIM.clear();
        currentCIMKeys.clear();
        currentCIMResults.clear();
        selMode = 2;
        selCol = -1;
        selRow = -1;
        sendText(NODENUM_BROADCAST, ctrlPtr->getChannelIndex(), message);
        if (bBorrowed)
            sendToBackground();
    }
    else if (ctrlPtr0 && ctrlType == Controllable::Types::DMChat) {
        // Chat window: composed text goes as a DM to the bound peer
        auto ctrlPtr = (DMChatApplet*)ctrlPtr0;
        if (ctrlPtr->isBound()) {
            std::string message = currentInput;
            currentInput.clear();
            currentCIM.clear();
            currentCIMKeys.clear();
            currentCIMResults.clear();
            selMode = 2;
            selCol = -1;
            selRow = -1;
            sendText(ctrlPtr->getPeer(), 0, message);
            ctrlPtr->noteSent();
            if (bBorrowed)
                sendToBackground();
        }
    }
    else if (ctrlPtr0 && ctrlType == Controllable::Types::UniChat) {
        // Unified chats: send to whichever target thread is open
        auto ctrlPtr = (UniChatApplet*)ctrlPtr0;
        if (ctrlPtr->hasThreadTarget()) {
            std::string message = currentInput;
            currentInput.clear();
            currentCIM.clear();
            currentCIMKeys.clear();
            currentCIMResults.clear();
            selMode = 2;
            selCol = -1;
            selRow = -1;
            if (ctrlPtr->targetIsDM())
                sendText(ctrlPtr->getPeer(), 0, message);
            else
                sendText(NODENUM_BROADCAST, ctrlPtr->getChannelIndex(), message);
            ctrlPtr->noteSent();
            if (bBorrowed)
                sendToBackground();
        }
    }
    else if (ctrlPtr0 && ctrlType == Controllable::Types::NavMap) {
        // Parse the edited "lat,lng" text and jump the map there (gotoCenter range-checks).
        char *end1 = nullptr;
        float lat = strtof(currentInput.c_str(), &end1);
        const char *s2 = end1;
        while (*s2 == ',' || *s2 == ' ' || *s2 == '\t' || *s2 == '\n' || *s2 == '\r')
            s2++;
        char *end2 = nullptr;
        float lng = strtof(s2, &end2);
        if (end1 != currentInput.c_str() && end2 != s2)
            ((NavMapApplet *)ctrlPtr0)->gotoCenter(lat, lng);
        else
            ((NavMapApplet *)ctrlPtr0)->gotoPlace(currentInput.c_str()); // not lat,lng -> place name
        currentInput.clear();
        currentCIM.clear();
        currentCIMKeys.clear();
        currentCIMResults.clear();
        selMode = 2;
        selCol = -1;
        selRow = -1;
        // Close on commit when driving via a transient split too (there bBorrowed is false, as the
        // NavMap is the neighbour tile); onBackground then merges the split back.
        if (bBorrowed || inkhud->isMenuSplitActive())
            sendToBackground();
    }
    else if (ctrlPtr0 && ctrlType == Controllable::Types::Heard) {
        // Node search (menu "Search Node"): scroll the Heard list to the first short/long-name
        // match and highlight it - Enter on the highlight then opens the DM chat as usual.
        ((HeardApplet *)ctrlPtr0)->searchNode(currentInput.c_str());
        currentInput.clear();
        currentCIM.clear();
        currentCIMKeys.clear();
        currentCIMResults.clear();
        selMode = 2;
        selCol = -1;
        selRow = -1;
        if (bBorrowed || inkhud->isMenuSplitActive())
            sendToBackground();
    }
}
#endif //defined(MOD_TKEY_MODEL)

void InkHUD::InputMenuApplet::sendText(NodeNum dest, ChannelIndex channel, const std::string& message)
{
    meshtastic_MeshPacket *p = router->allocForSending();
    p->decoded.portnum = meshtastic_PortNum_TEXT_MESSAGE_APP;
    p->to = dest;
    p->channel = channel;
    p->want_ack = true;
    // Clamp to the payload capacity, cutting only on a UTF-8 character boundary
    size_t sendBytes = message.length();
    if (sendBytes > sizeof(p->decoded.payload.bytes)) {
#if defined(MOD_CJK_ENABLED)
        sendBytes = 0;
        for (size_t pos = 0; pos < message.length();) {
            int numChars = getUTF8Chars((uint8_t *)message.c_str() + pos);
            if (numChars < 1 || pos + numChars > sizeof(p->decoded.payload.bytes))
                break;
            pos += numChars;
            sendBytes = pos;
        }
#else //!defined(MOD_CJK_ENABLED)
        sendBytes = sizeof(p->decoded.payload.bytes);
#endif //defined(MOD_CJK_ENABLED)
    }
    p->decoded.payload.size = sendBytes;
    memcpy(p->decoded.payload.bytes, message.c_str(), p->decoded.payload.size);

    LOG_INFO("Send message id=%u, dest=%x, msg=%.*s", p->id, p->to, (int)p->decoded.payload.size, (const char *)p->decoded.payload.bytes);

    // Record an outgoing DM in the message store BEFORE handing the packet off (sendToMesh may
    // release it). Broadcasts are deliberately excluded: Router::sendLocal loops those back
    // through handleReceived, where the chat applets already store them (and
    // ThreadedMessageApplet has no id-dedupe, so an unconditional append would double-store).
    // A DM to a remote peer never loops back - without this append the sent message existed
    // NOWHERE, and the Chats/DM thread showed only the peer's side of the conversation.
    if (!isBroadcast(p->to))
        messageStore.tryAddFromPacket(*p);

    service->sendToMesh(p, RX_SRC_LOCAL, true); // Send to mesh, cc to phone
}


#endif