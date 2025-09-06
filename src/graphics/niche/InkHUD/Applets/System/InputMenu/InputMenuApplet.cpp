#ifdef MESHTASTIC_INCLUDE_INKHUD

#include "configuration.h"

#include "../Menu/MenuApplet.h"
#include "../../User/ThreadedMessage/ThreadedMessageApplet.h"
#include "../../User/Heard/HeardApplet.h"
#include "InputMenuApplet.h"

#include "MeshService.h"
#include "Router.h"
#include "main.h"

#ifdef MOD_CJK_ENABLED
#include "graphics/niche/Fonts/cubicFont.h"
#endif //MOD_CJK_ENABLED

using namespace NicheGraphics;

static constexpr uint8_t INPUT_TIMEOUT_SEC = 15; // How many seconds before menu auto-closes

InkHUD::InputMenuApplet::InputMenuApplet() : concurrency::OSThread("InputMenuApplet")
{
#if defined(MOD_UART_KEYBOARD_12KEY)
    OSThread::setIntervalFromNow(500);
#else //!defined(MOD_UART_KEYBOARD_12KEY)
    OSThread::disable();
#endif //defined(MOD_UART_KEYBOARD_12KEY)

    if (true) { // control keyboard
        Keyboard kb;
        std::get<0>(kb) = "Fn";
        std::get<1>(kb).emplace_back(std::vector<Key>());
        std::get<1>(kb).back().emplace_back("Send", 0);
        std::get<1>(kb).emplace_back(std::vector<Key>());
        std::get<1>(kb).back().emplace_back("@cc", 0);
        std::get<1>(kb).back().emplace_back("stats", 0);
        std::get<1>(kb).back().emplace_back("@ab", 0);
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

#ifdef MOD_CJK_ENABLED
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
#endif //MOD_CJK_ENABLED

    if (settings->optionalMenuItems.backlight)
        backlight = Drivers::LatchingBacklight::getInstance();

#if defined(MOD_UART_KEYBOARD_12KEY)
    LOG_INFO("Init serial peripheral interface");
    Serial2.setPins(PIN_SERIAL2_RX, PIN_SERIAL2_TX);
    Serial2.begin(9600, SERIAL_8N1);
    Serial2.setTimeout(250);
#endif //defined(MOD_UART_KEYBOARD_12KEY)
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
#if defined(MOD_UART_KEYBOARD_12KEY)
    autoHideMillis = millis() + INPUT_TIMEOUT_SEC * 1000UL;
    OSThread::setIntervalFromNow(50);
    while (Serial2.available()) // empty the buffer first after being foreground
        Serial2.read();
#else //!defined(MOD_UART_KEYBOARD_12KEY)
    OSThread::setIntervalFromNow(INPUT_TIMEOUT_SEC * 1000UL);
#endif //defined(MOD_UART_KEYBOARD_12KEY)
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

#if defined(MOD_UART_KEYBOARD_12KEY)
    OSThread::setIntervalFromNow(50);
#else //!defined(MOD_UART_KEYBOARD_12KEY)
    OSThread::disable();
#endif //defined(MOD_UART_KEYBOARD_12KEY)

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
        if (millis() > autoHideMillis) {
            sendToBackground();
        }
        while (Serial2.available()) {
            autoHideMillis = millis() + INPUT_TIMEOUT_SEC * 1000UL;
            uint8_t data = Serial2.read();
            switch (data) {
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
    }
    else {
        auto getActiveControllable = [this]() {
            for (auto app : inkhud->userApplets) {
                if (app->isForeground() && app->getTile() == inkhud->getFocusedTile()) {
                    const auto type = Controllable::checkControllable(app);
                    if (type == Controllable::Types::ThreadedMessage) {
                        auto app1 = (ThreadedMessageApplet*)app;
                        return (Controllable*)app1;
                    }
                    else if (type == Controllable::Types::Heard) {
                        auto app1 = (HeardApplet*)app;
                        return (Controllable*)app1;
                    }
                }
            }
            return (Controllable*)nullptr;
        };
        while (Serial2.available()) {
            uint8_t data = Serial2.read();
            switch (data) {
            case 0xE7: // back
                LOG_INFO("Key press [back]");
                {
                    auto app = getActiveControllable();
                    if (app)
                        app->handleBack();
                }
                break;
            case 0xE8: // up
                LOG_INFO("Key press [up]");
                {
                    auto app = getActiveControllable();
                    if (app)
                        app->handleUp();
                }
                break;
            case 0xE9: // enter
                LOG_INFO("Key press [enter]");
                {
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
                if (settings->userTiles.count > 1) {
                    if (settings->userTiles.focused > 0)
                        inkhud->nextTile();
                }
                else {
                    inkhud->prevApplet();
                }
                break;
            case 0xEB: // down
                LOG_INFO("Key press [down]");
                {
                    auto app = getActiveControllable();
                    if (app)
                        app->handleDown();
                }
                break;
            case 0xEC: // right
                LOG_INFO("Key press [right]");
                if (settings->userTiles.count > 1) {
                    if (settings->userTiles.focused == 0)
                        inkhud->nextTile();
                }
                else {
                    inkhud->nextApplet();
                }
                break;
            }
            requestUpdate(Drivers::EInk::UpdateTypes::FAST);
        }
    }
    return 100;
#else
    sendToBackground();
    return OSThread::disable();
#endif
}

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
            else if (selKB == 4 && !currentCIM.empty()) {
                for (int16_t x = 0; x < width(); x += 2)
                    drawPixel(x, kbBarTop, BLACK);
                printAt(0, kbBarTop + 1, currentCIM);
            }
            else if (selKB == 4 && selMode == 3) {
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
            else if (selKB == 4 && selMode == 3 && selResult != -1) {
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
#if defined(MOD_UART_KEYBOARD_12KEY)
    autoHideMillis = millis() + INPUT_TIMEOUT_SEC * 1000UL;
#else //!defined(MOD_UART_KEYBOARD_12KEY)
    OSThread::setIntervalFromNow(INPUT_TIMEOUT_SEC * 1000UL);
#endif //defined(MOD_UART_KEYBOARD_12KEY)

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
#if defined(MOD_UART_KEYBOARD_12KEY)
    autoHideMillis = millis() + INPUT_TIMEOUT_SEC * 1000UL;
#else //!defined(MOD_UART_KEYBOARD_12KEY)
    OSThread::setIntervalFromNow(INPUT_TIMEOUT_SEC * 1000UL);
#endif //defined(MOD_UART_KEYBOARD_12KEY)

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
        sendText(NODENUM_BROADCAST, std::get<1>(sendTargets.at(selResult)), message);
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
        sendText(std::get<1>(sendTargets.at(selResult)), 0, message);
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
                    sendText(0xed8abca5, 0, "@cc");
                    sendToBackground();
                }
                else if (selCol == 1) {
                    sendText(0xed8abca5, 0, "@cc stats");
                    sendToBackground();
                }
                else if (selCol == 2) {
                    if (ctrlPtr0 && ctrlType == Controllable::Types::ThreadedMessage) {
                        auto ctrlPtr = (ThreadedMessageApplet*)ctrlPtr0;
                        sendText(NODENUM_BROADCAST, ctrlPtr->getChannelIndex(), "@ab");
                        if (bBorrowed)
                            sendToBackground();
                    }
                }
                selMode = 1;
#if !defined(MOD_UART_KEYBOARD_12KEY)
                selCol = -1;
                selRow = -1;
#endif //defined(MOD_UART_KEYBOARD_12KEY)
            }
            else if (selRow == 2) {
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
            else if (selRow == 3) {
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
                    selResult = 0;
#else //!defined(MOD_UART_KEYBOARD_12KEY)
                    selResult = -1;
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
                    selResult = 0;
#else //!defined(MOD_UART_KEYBOARD_12KEY)
                    selResult = -1;
#endif //defined(MOD_UART_KEYBOARD_12KEY)
                }
            }
            else if (selRow == 4) {
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