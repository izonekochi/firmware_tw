#ifdef MESHTASTIC_INCLUDE_INKHUD

#include "./DMChatApplet.h"

#include "gps/RTC.h"
#include "graphics/niche/InkHUD/SystemApplet.h"
#include "graphics/niche/Utils/FlashData.h"
#include "mesh/NodeDB.h"

using namespace NicheGraphics;

// --- Slot registry + persisted bindings ------------------------------------------------------

NicheGraphics::InkHUD::DMChatApplet *NicheGraphics::InkHUD::DMChatApplet::instances[MAX_SLOTS] = {nullptr};

struct DMChatBindings {
    uint32_t peers[InkHUD::DMChatApplet::MAX_SLOTS] = {0};
};
static DMChatBindings dmChatBindings;
static bool dmChatBindingsLoaded = false;

void InkHUD::DMChatApplet::loadBindings()
{
    if (dmChatBindingsLoaded)
        return;
    dmChatBindingsLoaded = true;
    DMChatBindings loaded;
    if (FlashData<DMChatBindings>::load(&loaded, "dmchats"))
        dmChatBindings = loaded;
}

void InkHUD::DMChatApplet::saveBindings()
{
    for (uint8_t s = 0; s < MAX_SLOTS; s++)
        dmChatBindings.peers[s] = instances[s] ? instances[s]->peer : 0;
    FlashData<DMChatBindings>::save(&dmChatBindings, "dmchats");
}

const char *InkHUD::DMChatApplet::slotBaseName(uint8_t slot)
{
    static const char *names[MAX_SLOTS] = {"DM 1", "DM 2", "DM 3"};
    return names[slot < MAX_SLOTS ? slot : 0];
}

// --- Lifecycle -------------------------------------------------------------------------------

InkHUD::DMChatApplet::DMChatApplet(uint8_t slot) : slot(slot)
{
    if (slot < MAX_SLOTS)
        instances[slot] = this;
    loadBindings();
    if (slot < MAX_SLOTS)
        peer = dmChatBindings.peers[slot]; // rebind a persisted chat
#if defined(MOD_INPUT_MENU)
    Controllable::registerControllable(this, Controllable::Types::DMChat);
#endif // defined(MOD_INPUT_MENU)
}

InkHUD::DMChatApplet::~DMChatApplet()
{
#if defined(MOD_INPUT_MENU)
    Controllable::unregisterControllable(this);
#endif // defined(MOD_INPUT_MENU)
    if (slot < MAX_SLOTS && instances[slot] == this)
        instances[slot] = nullptr;
}

// WindowManager::addApplet overwrites Applet::name AFTER our constructor ran, so a persisted
// binding restores its "DM:<shortname>" title here instead (activation runs later, inside
// InkHUD::begin / updateAppletSelection).
void InkHUD::DMChatApplet::onActivate()
{
    refreshName();
}

void InkHUD::DMChatApplet::refreshName()
{
    if (!isBound()) {
        name = slotBaseName(slot);
        return;
    }
    meshtastic_NodeInfoLite *node = nodeDB->getMeshNode(peer);
    dynName = "DM:";
    if (node)
        dynName += parseShortName(node);
    else
        dynName += hexifyNodeNum(peer);
    name = dynName.c_str();
}

// --- Runtime binding -------------------------------------------------------------------------

// Find the chat bound to `node`, or claim one for it: a free slot first, else evict the
// least-recently-received chat that isn't currently on screen. Returns nullptr when every slot
// is bound AND displayed (the message still lands in the DMs / All Messages applets).
InkHUD::DMChatApplet *InkHUD::DMChatApplet::claimFor(uint32_t node)
{
    if (node == 0 || node == nodeDB->getNodeNum())
        return nullptr;

    for (DMChatApplet *a : instances) {
        if (a && a->peer == node) {
            // Revive a chat the user manually disabled in the Applets menu: a new message from
            // its peer always reactivates the window (bindTo is not re-run for existing chats,
            // so the settings flags are re-asserted here).
            if (!a->isActive()) {
                const uint8_t idx = a->inkhud->getAppletIndex(a);
                if (idx < Persistence::MAX_USERAPPLETS_GLOBAL) {
                    a->settings->userApplets.active[idx] = true;
                    a->settings->userApplets.autoshow[idx] = true;
                }
                a->activate();
            }
            return a;
        }
    }

    DMChatApplet *claim = nullptr;
    for (DMChatApplet *a : instances) {
        if (a && !a->isBound()) {
            claim = a;
            break;
        }
    }
    if (!claim) {
        for (DMChatApplet *a : instances) {
            if (!a || a->isForeground())
                continue;
            if (!claim || (int32_t)(a->lastRxMs - claim->lastRxMs) < 0)
                claim = a;
        }
    }
    if (claim) {
        claim->bindTo(node);
        claim->lastRxMs = millis(); // fresh claim: don't be the immediate next eviction target
    }
    return claim;
}

// Central routing for every incoming DM (called from Events::onReceiveTextMessage, so it runs
// regardless of which applets are active). Find the peer's chat or claim a slot for it, then
// bring it on screen.
void InkHUD::DMChatApplet::onIncomingDM(uint32_t sender)
{
    DMChatApplet *chat = claimFor(sender);
    LOG_INFO("DMChat: incoming DM from 0x%08x -> %s", (unsigned)sender,
             chat ? (chat->isForeground() ? "chat already shown" : "chat queued for autoshow") : "NO SLOT (none registered/free)");
    if (!chat)
        return;
    chat->lastRxMs = millis();
    chat->showForIncoming();
}

// User-initiated chat (e.g. Enter on a node in the Heard list): find/claim and bring on screen.
// Returns the chat, or nullptr if no slot could be claimed.
InkHUD::DMChatApplet *InkHUD::DMChatApplet::openChatWith(uint32_t node)
{
    DMChatApplet *chat = claimFor(node);
    if (chat)
        chat->showForIncoming();
    return chat;
}

void InkHUD::DMChatApplet::bindTo(NodeNum node)
{
    peer = node;
    beginMsgIndex = 0;
    refreshName();
    // Activate the slot directly: the settings arrays are index-aligned with userApplets, and
    // the menu-driven path (updateAppletSelection -> changeActivatedApplets) asserts the menu
    // is open - never true when a packet arrives. Autoshow permission comes along so the chat
    // keeps announcing follow-up messages through the standard mechanism too.
    const uint8_t idx = inkhud->getAppletIndex(this);
    if (idx < Persistence::MAX_USERAPPLETS_GLOBAL) {
        settings->userApplets.active[idx] = true;
        settings->userApplets.autoshow[idx] = true;
    }
    if (!isActive())
        activate();
    saveBindings();
    LOG_INFO("DMChat slot %u bound to 0x%08x", (unsigned)slot, (unsigned)node);
}

// Unbind, returning the slot to the pool. Called from the menu ("Close Chat"), which follows up
// with inkhud->updateAppletSelection() to deactivate us and refill the tile.
void InkHUD::DMChatApplet::closeChat()
{
    if (!isBound())
        return;
    LOG_INFO("DMChat slot %u closed (peer 0x%08x)", (unsigned)slot, (unsigned)peer);
    peer = 0;
    lastRxMs = 0;
    beginMsgIndex = 0;
    refreshName();
    const uint8_t idx = inkhud->getAppletIndex(this);
    if (idx < Persistence::MAX_USERAPPLETS_GLOBAL) {
        settings->userApplets.active[idx] = false;
        settings->userApplets.autoshow[idx] = false;
    }
    saveBindings();
}

// Bring this chat on screen for an incoming message, via the standard autoshow machinery:
// WindowManager::autoshow gives DMChat applets a priority pass, so earlier-registered applets
// can't consume the single autoshow slot first.
// Asleep (T-Deck Max): if this chat has autoshow PERMISSION (Autoshow menu), refresh the
// RETAINED sleep frame right now - the message wake already paid for the CPU, so one
// synchronous FAST refresh surfaces the chat (moon and all) before the imminent re-sleep.
// Without permission the message is only stored (PSRAM message store); the screen is untouched
// and the pending flag simply waits for the next wake render.
void InkHUD::DMChatApplet::showForIncoming()
{
    requestAutoshow();

#if defined(T_DECK_MAX)
    if (!inkhudScreenAwake) {
        const uint8_t idx = inkhud->getAppletIndex(this);
        const bool mayAutoshow = idx < Persistence::MAX_USERAPPLETS_GLOBAL && settings->userApplets.autoshow[idx];
        if (mayAutoshow || isForeground())
            inkhud->forceUpdate(Drivers::EInk::UpdateTypes::FAST, false, false); // render runs autoshow()
        return;
    }
#endif

    requestUpdate(Drivers::EInk::UpdateTypes::FAST);
}

void InkHUD::DMChatApplet::noteSent()
{
    beginMsgIndex = 0; // jump back to the newest message (our own)
    requestUpdate(Drivers::EInk::UpdateTypes::FAST);
}

// Suppress the system DM notification when this chat is on screen showing that very message.
bool InkHUD::DMChatApplet::approveNotification(Notification &n)
{
    if (n.type == Notification::Type::NOTIFICATION_MESSAGE_DIRECT && isBound() && n.getSender() == peer && isForeground())
        return false;
    return true;
}

// --- Rendering -------------------------------------------------------------------------------

// Thread view, structurally mirroring ThreadedMessageApplet::onRender but filtered to the DMs
// exchanged with our peer (incoming: sender == peer; outgoing: we sent TO the peer).
void InkHUD::DMChatApplet::onRender(bool full)
{
    std::string headerText = "DM: ";
    if (isBound()) {
        meshtastic_NodeInfoLite *node = nodeDB->getMeshNode(peer);
        if (node)
            headerText += parseShortName(node);
        else
            headerText += hexifyNodeNum(peer);
    } else {
        headerText += "no chat";
    }
    drawHeader(headerText);

    const int16_t dividerY = Applet::getHeaderHeight() - 1;

    if (!isBound()) {
        printAt(width() / 2, height() / 2, "No open chat", CENTER, MIDDLE);
        return;
    }

    setCrop(0, dividerY + 2, width(), height() - (dividerY + 2));

    constexpr uint16_t padW = 2;
    constexpr int16_t msgL = padW;
    const int16_t msgR = (width() - 1) - padW;
    const uint16_t msgW = (msgR - msgL) + 1;

    int16_t msgB = height() - 1; // messages are bottom-aligned to this cursor

    const uint32_t me = myNodeInfo.my_node_num;
    const auto &allMessages = messageStore.getLiveMessages();
    int msgIdx = (int)allMessages.size() - 1;
    uint8_t skip = beginMsgIndex;

    while (msgB >= (0 - fontSmall.lineHeight()) && msgIdx >= 0) {
        const StoredMessage &m = allMessages.at(msgIdx);

        // Only DMs exchanged with our peer
        const bool incoming = (m.type == MessageType::DM_TO_US) && (m.sender == peer);
        const bool outgoing = (m.type == MessageType::DM_TO_US) && (m.sender == me && m.dest == peer);
        if (!incoming && !outgoing) {
            msgIdx--;
            continue;
        }

        if (skip > 0) { // scroll offset: skip the newest N thread messages
            skip--;
            msgIdx--;
            continue;
        }

        std::string bodyText = parse(std::string(MessageStore::getText(m)));

        const int16_t dotsB = msgB;

        uint16_t bodyH = getWrappedTextHeight(msgL, msgW, bodyText);
        int16_t bodyT = msgB - bodyH;

        if (!outgoing)
            printWrapped(msgL, bodyT, msgW, bodyText);
        else {
            if (getTextWidth(bodyText) < width())
                printAt(msgR, bodyT, bodyText, RIGHT);
            else
                printWrapped(msgL, bodyT, msgW, bodyText);
        }

        msgB -= bodyH;
        msgB -= getFont().lineHeight() * 0.2;

        std::string info;
        if (outgoing)
            info += "Me";
        else {
            meshtastic_NodeInfoLite *sender = nodeDB->getMeshNode(m.sender);
            if (sender)
                info += parseShortName(sender);
            else
                info += hexifyNodeNum(m.sender);
        }
        std::string timeString = getTimeString(m.timestamp);
        if (timeString.length() > 0) {
            info += " - ";
            info += timeString;
        }

        printAt(outgoing ? msgR : msgL, msgB, info, outgoing ? RIGHT : LEFT, BOTTOM);
        printAt(outgoing ? msgR - 1 : msgL + 1, msgB, info, outgoing ? RIGHT : LEFT, BOTTOM);

        const int16_t divY = msgB;
        int16_t divL;
        int16_t divR;
        if (!outgoing) {
            divL = msgL;
            divR = getTextWidth(info) + getFont().lineHeight() / 2;
        } else {
            divR = msgR;
            divL = divR - getTextWidth(info) - getFont().lineHeight() / 2;
        }
        for (int16_t x = divL; x <= divR; x += 2)
            drawPixel(x, divY, BLACK);

        msgB -= fontSmall.lineHeight();

        for (int16_t y = msgB; y < dotsB; y += 1)
            drawPixel(outgoing ? width() - 1 : 0, y, BLACK);

        msgB -= fontSmall.lineHeight() * 0.5;

        msgIdx--;
    }

    // Fade effect below the header divider
    hatchRegion(0, dividerY + 1, width(), fontSmall.lineHeight() / 3, 2, WHITE);
}

// --- Scrolling (Controllable) ----------------------------------------------------------------

#if defined(MOD_INPUT_MENU)
bool InkHUD::DMChatApplet::handleUp()
{
    uint8_t threadCount = 0;
    const uint32_t me = myNodeInfo.my_node_num;
    for (const StoredMessage &m : messageStore.getLiveMessages())
        if (m.type == MessageType::DM_TO_US && (m.sender == peer || (m.sender == me && m.dest == peer)))
            threadCount++;

    if (threadCount > 0 && beginMsgIndex < (uint8_t)(threadCount - 1)) {
        beginMsgIndex++;
        requestUpdate(Drivers::EInk::FAST);
        return true;
    }
    return false;
}

bool InkHUD::DMChatApplet::handleDown()
{
    if (beginMsgIndex > 0) {
        beginMsgIndex--;
        requestUpdate(Drivers::EInk::FAST);
        return true;
    }
    return false;
}

bool InkHUD::DMChatApplet::handleBack()
{
    beginMsgIndex = 0;
    requestUpdate(Drivers::EInk::FAST);
    return true;
}
#endif // defined(MOD_INPUT_MENU)

#endif
