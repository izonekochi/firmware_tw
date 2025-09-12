#ifdef MESHTASTIC_INCLUDE_INKHUD

#include "configuration.h"

#include "../Menu/MenuApplet.h"
#include "../../User/ThreadedMessage/ThreadedMessageApplet.h"
#include "../../User/Heard/HeardApplet.h"
#include "InputMenuApplet.h"

#include "MeshService.h"
#include "Router.h"
#include "main.h"

#if defined(MOD_CJK_ENABLED)
#include "graphics/niche/Fonts/cubicFont.h"
#endif //defined(MOD_CJK_ENABLED)

using namespace NicheGraphics;

static constexpr uint8_t INPUT_TIMEOUT_SEC = 15; // How many seconds before menu auto-closes
static constexpr uint8_t LOCK_TIMEOUT_SEC = 15;  // How many seconds before menu auto-locks

InkHUD::InputMenuApplet::InputMenuApplet() : concurrency::OSThread("InputMenuApplet")
{
#if defined(MOD_UART_KEYBOARD_12KEY) || defined(MOD_UART_T_KEYBOARD)
    OSThread::setIntervalFromNow(500);
#else //!(defined(MOD_UART_KEYBOARD_12KEY) || defined(MOD_UART_T_KEYBOARD))
    OSThread::disable();
#endif //defined(MOD_UART_KEYBOARD_12KEY) || defined(MOD_UART_T_KEYBOARD)

#if defined(MOD_UART_T_KEYBOARD) // BBQ10-specific keyboard
    if (true) { // function keyboard (0)
        Keyboard kb;
        std::get<0>(kb) = "Fn";
        std::get<1>(kb).emplace_back(std::vector<Key>());
        std::get<1>(kb).back().emplace_back("🔅", 0);
        std::get<1>(kb).back().emplace_back("⏮", 1);
        std::get<1>(kb).back().emplace_back("↑", 2);
        std::get<1>(kb).back().emplace_back("↕", 3);
        std::get<1>(kb).back().emplace_back("off", 4);
        std::get<1>(kb).emplace_back(std::vector<Key>());
        std::get<1>(kb).back().emplace_back("🔆", 5);
        std::get<1>(kb).back().emplace_back("←", 6);
        std::get<1>(kb).back().emplace_back("↓", 7);
        std::get<1>(kb).back().emplace_back("→", 8);
        std::get<1>(kb).back().emplace_back("✖", 9);
        std::get<1>(kb).emplace_back(std::vector<Key>());
        std::get<1>(kb).back().emplace_back("☀", 10);
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
        std::get<1>(kb).back().emplace_back("ˇ", 16);
        std::get<1>(kb).back().emplace_back("ˋ", 17);
        std::get<1>(kb).back().emplace_back("˙", 18);
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
#endif //defined(MOD_CJK_ENABLED)

#else //!defined(MOD_UART_T_KEYBOARD)
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

#endif //defined(MOD_UART_T_KEYBOARD)

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
    Serial2.begin(115200, SERIAL_8N1);
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
#else //!(defined(MOD_UART_KEYBOARD_12KEY) || defined(MOD_UART_T_KEYBOARD))
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
#else //!(defined(MOD_UART_KEYBOARD_12KEY) || defined(MOD_UART_T_KEYBOARD))
    OSThread::disable();
#endif //defined(MOD_UART_KEYBOARD_12KEY) || defined(MOD_UART_T_KEYBOARD)

    SystemApplet::lockRequests = false;
    SystemApplet::handleInput = false;

    if (borrowedTileOwner)
        borrowedTileOwner->bringToForeground();
    Tile *t = getTile();
    t->assignApplet(borrowedTileOwner); // Break our link with the tile, (and relink it with real owner, if it had one)
    borrowedTileOwner = nullptr;

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
        if (millis() > autoHideMillis)
            sendToBackground();
        while (Serial2.available()) {
            autoHideMillis = millis() + INPUT_TIMEOUT_SEC * 1000UL;
            handleMenuVKey(Serial2.read());
        }
    }
    else {
        if (millis() > autoHideMillis)
            touchLocked = true;
        if (comboPressCount > 0) {
            if (millis() - comboStartMillis > comboPressCount * 300) {
                comboPressCount = 0;
                handleBackgroundVKey(comboKeyCode);
            }
        }
        while (Serial2.available()) {
            autoHideMillis = millis() + LOCK_TIMEOUT_SEC * 1000UL;
            const uint8_t code = Serial2.read();
            if (code == 0xE7 || code == 0xE9) {
                const auto now = millis();
                if (comboPressCount == 0) {
                    comboStartMillis = now;
                    comboKeyCode = code;
                    comboPressCount = 1;
                }
                else if (code == comboKeyCode && now - comboStartMillis <= comboPressCount * 300) {
                    comboPressCount++;
                    if (comboPressCount >= 3) {
                        if (comboKeyCode == 0xE7) {
                            touchLocked = !touchLocked;
                        }
                        else if (!touchLocked && comboKeyCode == 0xE9) {
                            inkhud->openMenu();
                        }
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
        if (millis() > autoHideMillis) {
            sendToBackground();
        }
        else {
            while (Serial2.available()) {
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
        if (millis() > autoHideMillis)
            touchLocked = true;
        while (Serial2.available()) {
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
    case 0xE7: // back
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
    case 0xE8: // up
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
                selResult = currentCIMResults.size() - currentCIMResults.size() % 10;
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
    case 0xE9: // enter
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
    case 0xEA: // left
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
    case 0xEB: // down
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
    case 0xEC: // right
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
    case 0xE7: // back
        LOG_INFO("Key press [back]");
        if (!touchLocked) {
            auto app = getActiveControllable();
            if (app)
                app->handleBack();
        }
        break;
    case 0xE8: // up
        LOG_INFO("Key press [up]");
        if (!touchLocked) {
            auto app = getActiveControllable();
            if (app)
                app->handleUp();
        }
        break;
    case 0xE9: // enter
        LOG_INFO("Key press [enter]");
        if (!touchLocked) {
            auto app = getActiveControllable();
            if (app) {
                if (!app->handleEnter())
                    inkhud->nextApplet();
            }
            else {
                inkhud->nextApplet();
            }
        }
        break;
    case 0xEA: // left
        LOG_INFO("Key press [left]");
        if (!touchLocked) {
            if (settings->userTiles.count > 1) {
                if (settings->userTiles.focused > 0)
                    inkhud->nextTile();
            }
            else {
                inkhud->prevApplet();
            }
        }
        break;
    case 0xEB: // down
        LOG_INFO("Key press [down]");
        if (!touchLocked) {
            auto app = getActiveControllable();
            if (app)
                app->handleDown();
        }
        break;
    case 0xEC: // right
        LOG_INFO("Key press [right]");
        if (!touchLocked) {
            if (settings->userTiles.count > 1) {
                if (settings->userTiles.focused == 0)
                    inkhud->nextTile();
            }
            else {
                inkhud->nextApplet();
            }
        }
        break;
    }
    requestUpdate(Drivers::EInk::UpdateTypes::FAST);
}
#elif defined(MOD_UART_T_KEYBOARD)
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
    if ((modCode == 0x84 && keyCode == 0) || keyCode == 0x71) { // switch english and bopomo
        keyboardCIM = !keyboardCIM;
    }
    bool bShift = (modCode & 0x03);
    bool bAlt = (modCode & 0x08);
    bool bSym = (modCode & 0x10);
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
            if (currentCIMKeys.size() == 1) {
                auto idx0 = bopomofoTable[currentCIMKeys[0]];
                if (bopomofoTable[idx0] != -1) {
                    for (int16_t idx1 = bopomofoTable[idx0 + key]; idx1 < bopomofoTable[idx0 + key + 1]; idx1++) {
                        int16_t charIdx = exactIndex[bopomofoTable[idx1]];
                        currentCIMResults.emplace_back(std::string((char*)exactMap + charIdx, (size_t)getUTF8Chars(exactMap + charIdx)));
                    }
                    selMode = 3;
                    selResult = 0;
                }
            }
            else if (currentCIMKeys.size() == 2) {
                auto idx0 = bopomofoTable[currentCIMKeys[0]];
                if (bopomofoTable[idx0] == -1)
                    idx0++;
                else
                    idx0 += 6;
                idx0 = bopomofoTable[idx0 + currentCIMKeys[1]];
                if (bopomofoTable[idx0] != -1) {
                    for (int16_t idx1 = bopomofoTable[idx0 + key]; idx1 < bopomofoTable[idx0 + key + 1]; idx1++) {
                        int16_t charIdx = exactIndex[bopomofoTable[idx1]];
                        currentCIMResults.emplace_back(std::string((char*)exactMap + charIdx, (size_t)getUTF8Chars(exactMap + charIdx)));
                    }
                    selMode = 3;
                    selResult = 0;
                }
            }
            else if (currentCIMKeys.size() == 3) {
                auto idx0 = bopomofoTable[currentCIMKeys[0]];
                if (bopomofoTable[idx0] == -1)
                    idx0++;
                else
                    idx0 += 6;
                idx0 = bopomofoTable[idx0 + currentCIMKeys[1]];
                if (bopomofoTable[idx0] == -1)
                    idx0++;
                else
                    idx0 += 6;
                idx0 = bopomofoTable[idx0 + currentCIMKeys[2]];
                if (bopomofoTable[idx0] != -1) {
                    for (int16_t idx1 = bopomofoTable[idx0 + key]; idx1 < bopomofoTable[idx0 + key + 1]; idx1++) {
                        int16_t charIdx = exactIndex[bopomofoTable[idx1]];
                        currentCIMResults.emplace_back(std::string((char*)exactMap + charIdx, (size_t)getUTF8Chars(exactMap + charIdx)));
                    }
                    selMode = 3;
                    selResult = 0;
                }
            }
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
    if (keyboardCIM && selMode == 3) {
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
                if (selMode == 0x10)
                    sendText(NODENUM_BROADCAST, std::get<1>(sendTargets.at(selTarget)), message);
                else
                    sendText(std::get<1>(sendTargets.at(selTarget)), 0, message);
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
                if (selCol >= 5 && selCol < 10 && selRow >= 0 && selRow < 3) {
                    auto funcCode = std::get<1>(std::get<1>(keyboards[selKB])[selRow][selCol - 5]);
                    LOG_INFO("T-KB: funcCode=%d", (int)funcCode);
                    if (funcCode == 0 || funcCode == 5) {
                        if (funcCode == 0)
                            currentKBBL = (currentKBBL < 15) ? 7 : (currentKBBL - 8);
                        else
                            currentKBBL = (currentKBBL > 246) ? 255 : (currentKBBL + 8);
                        Serial2.write(0x02);
                        Serial2.write(currentKBBL);
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
                        if (settings->userTiles.count > 1) {
                            if (settings->userTiles.focused > 0)
                                inkhud->nextTile();
                        }
                        else {
                            inkhud->prevApplet();
                        }
                    }
                    else if (funcCode == 7) {
                        auto app = getActiveControllable();
                        if (app)
                            app->handleDown();
                    }
                    else if (funcCode == 8) {
                        if (settings->userTiles.count > 1) {
                            if (settings->userTiles.focused == 0)
                                inkhud->nextTile();
                        }
                        else {
                            inkhud->nextApplet();
                        }
                    }
                    else if (funcCode == 9) {
                        sendToBackground();
                    }
                    else if (funcCode == 11) {
                        sendTargets.clear();
                        for (uint8_t i = 0; i < MAX_NUM_CHANNELS; i++) {
                            meshtastic_Channel &channel = channels.getByIndex(i);
                            if (!channel.has_settings || channel.role == meshtastic_Channel_Role_DISABLED)
                                continue;
                            sendTargets.emplace_back(std::string("CH") + std::to_string((int)channel.index) + ":" + channel.settings.name, channel.index);
                        }
                        selMode = 0x10;
                        selTarget = 0;
                    }
                    else if (funcCode == 12) {
                        MenuApplet *menu = (MenuApplet *)inkhud->getSystemApplet("Menu");
                        Tile* t = getTile();
                        sendToBackground();
                        menu->show(t);
                    }
                    else if (funcCode == 13) {
                        uint32_t nodeCount = nodeDB->getNumMeshNodes();
                        sendTargets.clear();
                        for (uint32_t i = 0; i < nodeCount; i++) {
                            meshtastic_NodeInfoLite *node = nodeDB->getMeshNodeByIndex(i);
                            if (!node->is_favorite)
                                continue;
                            if (node->has_user)
                                sendTargets.emplace_back(std::string(node->user.long_name), node->num);
                            else
                                sendTargets.emplace_back(hexifyNodeNum(node->num), node->num);
                        }
                        selMode = 0x11;
                        selTarget = 0;
                    }
                    else if (funcCode == 14) {
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
                    }
                }
            }
            else {
                if (selRow == 1 && selCol == 9) { // backspace
                    if (keyboardCIM && !currentCIMKeys.empty()) {
                        for (size_t pos = 0; pos < currentCIM.length(); ) {
                            size_t numChars = getUTF8Chars((uint8_t*)currentCIM.c_str() + pos);
                            if (numChars < 1)
                                break;
                            if (pos + numChars == currentCIM.length())
                                currentCIM = currentCIM.substr(0, pos);
                            pos += numChars;
                        }
                        currentCIMKeys.pop_back();
                    }
                    else {
                        for (size_t pos = 0; pos < currentInput.length(); ) {
                            size_t numChars = getUTF8Chars((uint8_t*)currentInput.c_str() + pos);
                            if (numChars < 1)
                                break;
                            if (pos + numChars == currentInput.length())
                                currentInput = currentInput.substr(0, pos);
                            pos += numChars;
                        }
                    }
                }
                else if (selRow == 2 && selCol == 9) { // return or send
                    if (keyboardCIM && !currentCIMKeys.empty()) {
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
                        }
                        else {
                            currentInput += '\n';
                        }
                    }
                }
                else if (selRow == 3 && selCol == 2) { // space
                    if (keyboardCIM && !currentCIMKeys.empty()) {
                        // treat as first sound?
                        handleCIMKey(37);
                    }
                    else {
                        currentInput += ' ';
                    }
                }
                else if (selRow < 3) {
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
        if (bAlt && row == 1 && col == 8)
            touchLocked = true;
        else if (row == 0 && col == 0) {
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
            LOG_INFO("Key press [enter]");
            auto app = getActiveControllable();
            if (app) {
                if (!app->handleEnter())
                    inkhud->nextApplet();
            }
            else {
                inkhud->nextApplet();
            }
        }
        else if (row == 1 && col == 0) {
            LOG_INFO("Key press [left]");
            if (settings->userTiles.count > 1) {
                if (settings->userTiles.focused > 0)
                    inkhud->nextTile();
            }
            else {
                inkhud->prevApplet();
            }
        }
        else if (row == 1 && col == 1) {
            LOG_INFO("Key press [down]");
            auto app = getActiveControllable();
            if (app)
                app->handleDown();
        }
        else if (row == 1 && col == 2) {
            if (settings->userTiles.count > 1) {
                if (settings->userTiles.focused == 0)
                    inkhud->nextTile();
            }
            else {
                inkhud->nextApplet();
            }
        }
        else if (row == 2 && col == 7) {
            inkhud->openMenu();
        }
    }
}
#endif //defined(MOD_UART_T_KEYBOARD)

void InkHUD::InputMenuApplet::onRender()
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
        if (selTarget!= -1 && (int16_t)sendTargets.size() > showItems) {
            startIdx = selTarget - showItems + 1;
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
        uint16_t bodyH = getWrappedTextHeight(0, width(), bodyText);
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
#if defined(MOD_UART_T_KEYBOARD)
            else if (keyboardCIM && !currentCIM.empty()) {
#else //!defined(MOD_UART_T_KEYBOARD)
            else if (selKB == 4 && !currentCIM.empty()) {
#endif //defined(MOD_UART_T_KEYBOARD)
                for (int16_t x = 0; x < width(); x += 2)
                    drawPixel(x, kbBarTop, BLACK);
                printAt(0, kbBarTop + 1, currentCIM);
            }
#if defined(MOD_UART_T_KEYBOARD)
            else if (keyboardCIM && selMode == 3) {
#else //!defined(MOD_UART_T_KEYBOARD)
            else if (selKB == 4 && selMode == 3) {
#endif //defined(MOD_UART_T_KEYBOARD)
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
#if defined(MOD_UART_T_KEYBOARD)
            else if (keyboardCIM && selMode == 3 && selResult != -1) {
#else //!defined(MOD_UART_T_KEYBOARD)
            else if (selKB == 4 && selMode == 3 && selResult != -1) {
#endif //defined(MOD_UART_T_KEYBOARD)
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

void InkHUD::InputMenuApplet::onButtonShortPress()
{
#if defined(MOD_UART_KEYBOARD_12KEY) || defined(MOD_UART_T_KEYBOARD)
    autoHideMillis = millis() + INPUT_TIMEOUT_SEC * 1000UL;
#else //!(defined(MOD_UART_KEYBOARD_12KEY) || defined(MOD_UART_T_KEYBOARD))
    OSThread::setIntervalFromNow(INPUT_TIMEOUT_SEC * 1000UL);
#endif //defined(MOD_UART_KEYBOARD_12KEY) || defined(MOD_UART_T_KEYBOARD)

    if (selMode == 0) {
        selKB++;
        if (selKB == (int16_t)keyboards.size())
            selKB = -1;
    }
    else if (selMode == 1) {
        selRow++;
        if (selRow == (int16_t)std::get<1>(keyboards[selKB]).size())
            selRow = -1;
    }
    else if (selMode == 2) {
        selCol++;
        if (selCol == (int16_t)std::get<1>(keyboards[selKB])[selRow].size())
            selCol = -1;
    }
    else if (selMode == 3) {
        selResult++;
        if (selResult == (int16_t)currentCIMResults.size())
            selResult = -1;
    }
    else if (selMode == 0x10 || selMode == 0x11) {
        selTarget++;
        if (selTarget == (int16_t)sendTargets.size())
            selTarget = -1;
    }

    requestUpdate(Drivers::EInk::UpdateTypes::FAST);
}

void InkHUD::InputMenuApplet::onButtonLongPress()
{
#if defined(MOD_UART_KEYBOARD_12KEY) || defined(MOD_UART_T_KEYBOARD)
    autoHideMillis = millis() + INPUT_TIMEOUT_SEC * 1000UL;
#else //!(defined(MOD_UART_KEYBOARD_12KEY) || defined(MOD_UART_T_KEYBOARD))
    OSThread::setIntervalFromNow(INPUT_TIMEOUT_SEC * 1000UL);
#endif //defined(MOD_UART_KEYBOARD_12KEY) || defined(MOD_UART_T_KEYBOARD)

    if (selMode == 0) {
        if (selKB == -1)
            sendToBackground();
        else {
            selMode = 1;
            selRow = 0;
        }
    }
    else if (selMode == 1) {
        if (selRow == -1)
            selMode = 0;
        else {
            if (std::get<1>(keyboards[selKB])[selRow].size() > 1) {
                selMode = 2;
                selCol = 0;
            }
            else {
                selCol = 0;
                handleKeyboardPress();
            }
        }
    }
    else if (selMode == 2) {
        if (selCol == -1)
            selMode = 1;
        else {
            handleKeyboardPress();
        }
    }
    else if (selMode == 3) {
        if (selResult == -1) {
            currentCIM.clear();
            currentCIMKeys.clear();
            currentCIMResults.clear();
            selMode = 1;
        }
        else {
            handleKeyboardPress();
        }
    }
    else if (selMode == 0x10 || selMode == 0x11) {
        if (selTarget == -1) {
            selMode = 1;
        }
        else {
            handleKeyboardPress();
        }
    }

    // If we didn't already request a specialized update, when handling a menu action,
    // then perform the usual fast update.
    // FAST keeps things responsive: important because we're dealing with user input
    if (!wantsToRender())
        requestUpdate(Drivers::EInk::UpdateTypes::FAST);
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
        sendText(std::get<1>(sendTargets.at(selTarget)), 0, message);
        selMode = 1;
#if !defined(MOD_UART_KEYBOARD_12KEY)
        selCol = -1;
        selRow = -1;
#endif //defined(MOD_UART_KEYBOARD_12KEY)
    }
    else {
        if (selKB == 0) {
            if (selRow == 0) {
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
            }
            else if (selRow == 1) {
                if (selCol == 0) {
                    for (size_t pos = 0; pos < currentInput.length(); ) {
                        size_t numChars = getUTF8Chars((uint8_t*)currentInput.c_str() + pos);
                        if (numChars < 1)
                            break;
                        if (pos + numChars == currentInput.length())
                            currentInput = currentInput.substr(0, pos);
                        pos += numChars;
                    }
                    selMode = 1;
#if !defined(MOD_UART_KEYBOARD_12KEY)
                    selCol = -1;
                    selRow = -1;
#endif //defined(MOD_UART_KEYBOARD_12KEY)
                }
                else if (selCol == 1) {
                    currentInput.clear();
                    selMode = 1;
#if !defined(MOD_UART_KEYBOARD_12KEY)
                    selCol = -1;
                    selRow = -1;
#endif //defined(MOD_UART_KEYBOARD_12KEY)
                }
                else {
                    Controllable* ctrlPtr = nullptr;
                    if (ctrlPtr0) {
                        if (ctrlType == Controllable::Types::ThreadedMessage) {
                            auto ctrlPtr1 = (ThreadedMessageApplet*)ctrlPtr0;
                            ctrlPtr = (Controllable*)ctrlPtr1;
                        }
                        else if (ctrlType == Controllable::Types::Heard) {
                            auto ctrlPtr1 = (HeardApplet*)ctrlPtr0;
                            ctrlPtr = (Controllable*)ctrlPtr1;
                        }
                        if (ctrlPtr) {
                            if (selCol == 2) {
                                ctrlPtr->handleUp();
                            }
                            else if (selCol == 3) {
                                ctrlPtr->handleDown();
                            }
                            if (bBorrowed)
                                sendToBackground();
                        }
                    }
                }
            }
            else if (selRow == 2) {
                sendTargets.clear();
                if (selCol == 0) {
                    for (uint8_t i = 0; i < MAX_NUM_CHANNELS; i++) {
                        meshtastic_Channel &channel = channels.getByIndex(i);
                        if (!channel.has_settings || channel.role == meshtastic_Channel_Role_DISABLED)
                            continue;
                        sendTargets.emplace_back(std::string("CH") + std::to_string((int)channel.index) + ":" + channel.settings.name, channel.index);
                    }
                    selMode = 0x10;
#if defined(MOD_UART_KEYBOARD_12KEY)
                    selTarget = 0;
#else //!defined(MOD_UART_KEYBOARD_12KEY)
                    selTarget = -1;
#endif //defined(MOD_UART_KEYBOARD_12KEY)
                }
                else if (selCol == 1) {
                    uint32_t nodeCount = nodeDB->getNumMeshNodes();
                    for (uint32_t i = 0; i < nodeCount; i++) {
                        meshtastic_NodeInfoLite *node = nodeDB->getMeshNodeByIndex(i);
                        if (!node->is_favorite)
                            continue;
                        if (node->has_user)
                            sendTargets.emplace_back(std::string(node->user.long_name), node->num);
                        else
                            sendTargets.emplace_back(hexifyNodeNum(node->num), node->num);
                    }
                    selMode = 0x11;
#if defined(MOD_UART_KEYBOARD_12KEY)
                    selTarget = 0;
#else //!defined(MOD_UART_KEYBOARD_12KEY)
                    selTarget = -1;
#endif //defined(MOD_UART_KEYBOARD_12KEY)
                }
            }
            else if (selRow == 3) {
                if (selCol == 0) {
                    settings->userTiles.count++;
                    if (settings->userTiles.count == 3)
                        settings->userTiles.count++;
                    if (settings->userTiles.count > settings->userTiles.maxCount)
                        settings->userTiles.count = 1;
                    inkhud->updateLayout();
                }
                else if (selCol == 1) {
                    inkhud->nextTile();
                }
                else if (selCol == 2) {
                    LOG_INFO("Shutting down from input menu");
                    shutdownAtMsec = millis();
                }
                else if (selCol == 3) {
                    MenuApplet *menu = (MenuApplet *)inkhud->getSystemApplet("Menu");
                    Tile* t = getTile();
                    sendToBackground();
                    menu->show(t);
                }
            }
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
    #ifdef MOD_CJK_ENABLED
        else if (selKB == 4) {
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
                    if (currentCIMKeys.size() == 1) {
                        auto idx0 = bopomofoTable[currentCIMKeys[0]];
                        if (bopomofoTable[idx0] != -1) {
                            for (int16_t idx1 = bopomofoTable[idx0 + key]; idx1 < bopomofoTable[idx0 + key + 1]; idx1++) {
                                int16_t charIdx = exactIndex[bopomofoTable[idx1]];
                                currentCIMResults.emplace_back(std::string((char*)exactMap + charIdx, (size_t)getUTF8Chars(exactMap + charIdx)));
                            }
                            selMode = 3;
#if defined(MOD_UART_KEYBOARD_12KEY)
                            selResult = 0;
#else //!defined(MOD_UART_KEYBOARD_12KEY)
                            selResult = -1;
#endif //defined(MOD_UART_KEYBOARD_12KEY)
                        }
                    }
                    else if (currentCIMKeys.size() == 2) {
                        auto idx0 = bopomofoTable[currentCIMKeys[0]];
                        if (bopomofoTable[idx0] == -1)
                            idx0++;
                        else
                            idx0 += 6;
                        idx0 = bopomofoTable[idx0 + currentCIMKeys[1]];
                        if (bopomofoTable[idx0] != -1) {
                            for (int16_t idx1 = bopomofoTable[idx0 + key]; idx1 < bopomofoTable[idx0 + key + 1]; idx1++) {
                                int16_t charIdx = exactIndex[bopomofoTable[idx1]];
                                currentCIMResults.emplace_back(std::string((char*)exactMap + charIdx, (size_t)getUTF8Chars(exactMap + charIdx)));
                            }
                            selMode = 3;
#if defined(MOD_UART_KEYBOARD_12KEY)
                            selResult = 0;
#else //!defined(MOD_UART_KEYBOARD_12KEY)
                            selResult = -1;
#endif //defined(MOD_UART_KEYBOARD_12KEY)
                        }
                    }
                    else if (currentCIMKeys.size() == 3) {
                        auto idx0 = bopomofoTable[currentCIMKeys[0]];
                        if (bopomofoTable[idx0] == -1)
                            idx0++;
                        else
                            idx0 += 6;
                        idx0 = bopomofoTable[idx0 + currentCIMKeys[1]];
                        if (bopomofoTable[idx0] == -1)
                            idx0++;
                        else
                            idx0 += 6;
                        idx0 = bopomofoTable[idx0 + currentCIMKeys[2]];
                        if (bopomofoTable[idx0] != -1) {
                            for (int16_t idx1 = bopomofoTable[idx0 + key]; idx1 < bopomofoTable[idx0 + key + 1]; idx1++) {
                                int16_t charIdx = exactIndex[bopomofoTable[idx1]];
                                currentCIMResults.emplace_back(std::string((char*)exactMap + charIdx, (size_t)getUTF8Chars(exactMap + charIdx)));
                            }
                            selMode = 3;
#if defined(MOD_UART_KEYBOARD_12KEY)
                            selResult = 0;
#else //!defined(MOD_UART_KEYBOARD_12KEY)
                            selResult = -1;
#endif //defined(MOD_UART_KEYBOARD_12KEY)
                        }
                    }
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
                    if (key == 42)
                        currentInput += "，";
                    else if (key == 43)
                        currentInput += "。";
                    else if (key == 44)
                        currentInput += "　";
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
    }
#endif //MOD_CJK_ENABLED
}

void InkHUD::InputMenuApplet::sendText(NodeNum dest, ChannelIndex channel, const std::string& message)
{
    meshtastic_MeshPacket *p = router->allocForSending();
    p->decoded.portnum = meshtastic_PortNum_TEXT_MESSAGE_APP;
    p->to = dest;
    p->channel = channel;
    p->want_ack = true;
    p->decoded.payload.size = message.length();
    memcpy(p->decoded.payload.bytes, message.c_str(), p->decoded.payload.size);

    LOG_INFO("Send message id=%d, dest=%x, msg=%.*s", p->id, p->to, p->decoded.payload.size, p->decoded.payload.bytes);

    service->sendToMesh(p, RX_SRC_LOCAL, true); // Send to mesh, cc to phone
}


#endif