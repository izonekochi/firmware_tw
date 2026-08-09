#ifdef MESHTASTIC_INCLUDE_INKHUD

#include "./UniChatApplet.h"

#include "Channels.h"
#include "gps/RTC.h"
#include "graphics/niche/InkHUD/Applets/User/ThreadedMessage/ThreadedMessageApplet.h"
#include "graphics/niche/Utils/FlashData.h"
#include "mesh/NodeDB.h"

using namespace NicheGraphics;

// --- Slot registry + persisted per-slot targets ----------------------------------------------

NicheGraphics::InkHUD::UniChatApplet *NicheGraphics::InkHUD::UniChatApplet::instances[MAX_CHAT_SLOTS] = {nullptr};

struct UniChatTargets {
    uint8_t kind[InkHUD::UniChatApplet::MAX_CHAT_SLOTS] = {0};    // 0 = none, 1 = channel, 2 = DM
    uint8_t channel[InkHUD::UniChatApplet::MAX_CHAT_SLOTS] = {0}; // channel index (kind 1)
    uint32_t peer[InkHUD::UniChatApplet::MAX_CHAT_SLOTS] = {0};   // peer nodenum (kind 2)
};
static UniChatTargets uniChatTargets;
static bool uniChatTargetsLoaded = false;

void InkHUD::UniChatApplet::loadTargets()
{
    if (uniChatTargetsLoaded)
        return;
    uniChatTargetsLoaded = true;
    UniChatTargets loaded;
    if (FlashData<UniChatTargets>::load(&loaded, "unichats"))
        uniChatTargets = loaded;
}

void InkHUD::UniChatApplet::saveTargets()
{
    for (uint8_t s = 0; s < MAX_CHAT_SLOTS; s++) {
        UniChatApplet *a = instances[s];
        if (!a || !a->targetValid) {
            uniChatTargets.kind[s] = 0;
            continue;
        }
        uniChatTargets.kind[s] = a->curTarget.isDM ? 2 : 1;
        uniChatTargets.channel[s] = a->curTarget.channelIndex;
        uniChatTargets.peer[s] = a->curTarget.peer;
    }
    FlashData<UniChatTargets>::save(&uniChatTargets, "unichats");
}

const char *InkHUD::UniChatApplet::slotBaseName(uint8_t slot)
{
    static const char *names[MAX_CHAT_SLOTS] = {"Chat 1", "Chat 2", "Chat 3", "Chat 4", "Chat 5"};
    return names[slot < MAX_CHAT_SLOTS ? slot : 0];
}

#if defined(MOD_INPUT_MENU)
InkHUD::UniChatApplet::UniChatApplet(uint8_t slot)
    : SinglePortModule("UniChatApplet", meshtastic_PortNum_TEXT_MESSAGE_APP), Controllable(), slot(slot)
{
    Controllable::registerControllable(this, Controllable::Types::UniChat);
#else  //! defined(MOD_INPUT_MENU)
InkHUD::UniChatApplet::UniChatApplet(uint8_t slot)
    : SinglePortModule("UniChatApplet", meshtastic_PortNum_TEXT_MESSAGE_APP), slot(slot)
{
#endif // defined(MOD_INPUT_MENU)
    if (slot < MAX_CHAT_SLOTS)
        instances[slot] = this;
    // Restore this slot's persisted target: the chat reopens on its thread across reboots
    loadTargets();
    if (slot < MAX_CHAT_SLOTS && uniChatTargets.kind[slot] != 0) {
        curTarget.isDM = (uniChatTargets.kind[slot] == 2);
        curTarget.channelIndex = uniChatTargets.channel[slot];
        curTarget.peer = uniChatTargets.peer[slot];
        targetValid = true;
        view = View::Thread;
    }
}

InkHUD::UniChatApplet::~UniChatApplet()
{
#if defined(MOD_INPUT_MENU)
    Controllable::unregisterControllable(this);
#endif // defined(MOD_INPUT_MENU)
    if (slot < MAX_CHAT_SLOTS && instances[slot] == this)
        instances[slot] = nullptr;
}

// WindowManager::addApplet overwrites Applet::name AFTER the constructor, so a restored target
// re-applies its label here (activation runs later, inside InkHUD::begin / updateAppletSelection).
void InkHUD::UniChatApplet::onActivate()
{
    loopbackOk = true; // see our own locally-generated messages (canned/IME sends)
    refreshName();
}

void InkHUD::UniChatApplet::onDeactivate()
{
    loopbackOk = false;
}

void InkHUD::UniChatApplet::refreshName()
{
    // Applet:: qualified: MeshModule (via SinglePortModule) has its own `name` member
    if (!targetValid) {
        Applet::name = slotBaseName(slot);
        return;
    }
    dynName = targetLabel(curTarget);
    Applet::name = dynName.c_str();
}

// Storage-primary election: the lowest registered slot stores; every other instance only
// displays. Registered (not active) so message capture continues even with all chat applets
// disabled in the menu.
bool InkHUD::UniChatApplet::isStoragePrimary()
{
    for (uint8_t s = 0; s < MAX_CHAT_SLOTS; s++) {
        if (instances[s])
            return instances[s] == this;
    }
    return false;
}

// --- Targets ---------------------------------------------------------------------------------

// Channels first (every usable one, so an empty channel can still be opened to send), then DM
// peers discovered in the shared store, newest first.
void InkHUD::UniChatApplet::rebuildTargets()
{
    targets.clear();

    for (uint8_t ch = 0; ch < channels.getNumChannels(); ch++) {
        if (!channels.getByIndex(ch).has_settings || channels.getByIndex(ch).role == meshtastic_Channel_Role_DISABLED)
            continue;
        Target t;
        t.isDM = false;
        t.channelIndex = ch;
        targets.push_back(t);
    }

    const uint32_t me = myNodeInfo.my_node_num;
    const auto &all = messageStore.getLiveMessages();
    for (auto it = all.rbegin(); it != all.rend(); ++it) { // newest first
        if (it->type != MessageType::DM_TO_US)
            continue;
        const uint32_t peer = (it->sender == me) ? it->dest : it->sender;
        if (peer == 0 || peer == NODENUM_BROADCAST)
            continue;
        bool known = false;
        for (const Target &t : targets)
            if (t.isDM && t.peer == peer) {
                known = true;
                break;
            }
        if (!known) {
            Target t;
            t.isDM = true;
            t.peer = peer;
            targets.push_back(t);
        }
    }

    if (cursor >= (int16_t)targets.size())
        cursor = targets.empty() ? 0 : (int16_t)targets.size() - 1;
}

std::string InkHUD::UniChatApplet::targetLabel(const Target &t)
{
    std::string label;
    if (!t.isDM) {
        label = "CH" + to_string(t.channelIndex) + " ";
        if (channels.isDefaultChannel(t.channelIndex))
            label += "Public";
        else
            label += channels.getByIndex(t.channelIndex).settings.name;
    } else {
        label = "DM ";
        meshtastic_NodeInfoLite *node = nodeDB->getMeshNode(t.peer);
        if (node)
            label += parseShortName(node);
        else
            label += hexifyNodeNum(t.peer);
    }
    return label;
}

bool InkHUD::UniChatApplet::targetMatches(const StoredMessage &m)
{
    if (!targetValid)
        return false;
    if (!curTarget.isDM)
        return m.type == MessageType::BROADCAST && m.channelIndex == curTarget.channelIndex;
    const uint32_t me = myNodeInfo.my_node_num;
    return m.type == MessageType::DM_TO_US && (m.sender == curTarget.peer || (m.sender == me && m.dest == curTarget.peer));
}

// --- Storage ---------------------------------------------------------------------------------

// Does an ACTIVE dedicated per-channel applet already store this channel? (Its handleReceived
// keeps doing so; we must not double-store. Disable it in the menu and this applet takes over.)
bool InkHUD::UniChatApplet::channelHasDedicatedApplet(uint8_t ch)
{
    for (Applet *a : inkhud->userApplets) {
        ThreadedMessageApplet *t = a->asThreadedMessageApplet();
        if (t && t->isActive() && t->getChannelIndex() == ch)
            return true;
    }
    return false;
}

bool InkHUD::UniChatApplet::storedIdExists(uint32_t id)
{
    for (const StoredMessage &m : messageStore.getLiveMessages())
        if (m.id == id)
            return true;
    return false;
}

ProcessMessage InkHUD::UniChatApplet::handleReceived(const meshtastic_MeshPacket &mp)
{
    if (mp.decoded.portnum != meshtastic_PortNum_TEXT_MESSAGE_APP)
        return ProcessMessage::CONTINUE;

    const bool isBroadcastMsg = (mp.to == NODENUM_BROADCAST);

    // Storage duty - ONE instance only (lowest slot), and it runs even while the applet is
    // inactive so message capture never depends on menu state: broadcasts on channels with no
    // active dedicated applet (Events stores all DMs centrally; dedicated
    // ThreadedMessageApplets store their own channels - including the emoji-reaction handling -
    // and storedIdExists guards against any ordering overlap).
    if (isBroadcastMsg && isStoragePrimary() && !channelHasDedicatedApplet(mp.channel)) {
        if (mp.decoded.emoji && mp.decoded.reply_id) {
            std::string reaction = std::string("←") + std::string((const char *)mp.decoded.payload.bytes, mp.decoded.payload.size);
            messageStore.appendTextById(mp.decoded.reply_id, reaction);
        } else if (!storedIdExists(mp.id)) {
            messageStore.tryAddFromPacket(mp);
        }
    }

    if (!isActive())
        return ProcessMessage::CONTINUE; // display reactions below only for active applets

    // Refresh/show only when the message belongs to the OPEN thread (list view refreshes on
    // its own next visit; unrelated targets never yank the display).
    StoredMessage probe; // minimal fields for targetMatches
    probe.type = isBroadcastMsg ? MessageType::BROADCAST : MessageType::DM_TO_US;
    probe.channelIndex = mp.channel;
    probe.sender = (mp.from == 0) ? nodeDB->getNodeNum() : mp.from;
    probe.dest = mp.to;
    if (view == View::Thread && targetMatches(probe)) {
#if defined(T_DECK_MAX)
        // Sleep-UX: while awake, autoshow + FAST like ThreadedMessage. While asleep: with
        // autoshow PERMISSION, spend one synchronous FAST refresh on the retained sleep frame
        // (the message wake already paid for the CPU; render runs autoshow(), surfacing this
        // thread). Without permission the message is only stored - screen untouched.
        if (inkhudScreenAwake) {
            if (getFrom(&mp) != nodeDB->getNodeNum())
                requestAutoshow();
            requestUpdate(Drivers::EInk::UpdateTypes::FAST);
        } else {
            if (getFrom(&mp) != nodeDB->getNodeNum())
                requestAutoshow();
            const uint8_t idx = inkhud->getAppletIndex(this);
            const bool mayAutoshow = idx < Persistence::MAX_USERAPPLETS_GLOBAL && Applet::settings->userApplets.autoshow[idx];
            if (mayAutoshow || isForeground())
                inkhud->forceUpdate(Drivers::EInk::UpdateTypes::FAST, false, false);
        }
#else
        if (getFrom(&mp) != nodeDB->getNodeNum())
            requestAutoshow();
        requestUpdate(Drivers::EInk::UpdateTypes::FAST);
#endif
    } else if (isForeground() && view == View::List
#if defined(T_DECK_MAX)
               && inkhudScreenAwake // asleep list view: store only - the wake render repaints anyway
#endif
    ) {
        requestUpdate(Drivers::EInk::UpdateTypes::FAST); // a new DM peer may have appeared in the list
    }

    return ProcessMessage::CONTINUE;
}

// Suppress the system notification only when the open thread already shows that very message.
bool InkHUD::UniChatApplet::approveNotification(Notification &n)
{
    if (!isForeground() || view != View::Thread || !targetValid)
        return true;
    if (!curTarget.isDM && n.type == Notification::Type::NOTIFICATION_MESSAGE_BROADCAST && n.getChannel() == curTarget.channelIndex)
        return false;
    if (curTarget.isDM && n.type == Notification::Type::NOTIFICATION_MESSAGE_DIRECT && n.getSender() == curTarget.peer)
        return false;
    return true;
}

void InkHUD::UniChatApplet::noteSent()
{
    beginMsgIndex = 0;
    requestUpdate(Drivers::EInk::UpdateTypes::FAST);
}

// Menu "Pick Chat": jump back to the target list (cursor on the currently open target)
void InkHUD::UniChatApplet::openTargetList()
{
    rebuildTargets();
    if (targetValid) {
        for (uint16_t i = 0; i < targets.size(); i++) {
            const Target &t = targets.at(i);
            if (t.isDM == curTarget.isDM && (t.isDM ? t.peer == curTarget.peer : t.channelIndex == curTarget.channelIndex)) {
                cursor = (int16_t)i;
                break;
            }
        }
    }
    view = View::List;
    requestUpdate(Drivers::EInk::UpdateTypes::FAST);
}

// Menu "Clear Thread": wipe the open target's messages from the shared store (RAM now; the
// smaller flash image follows at the next autosave/shutdown).
void InkHUD::UniChatApplet::clearThread()
{
    if (!hasThreadTarget())
        return;
    if (curTarget.isDM)
        messageStore.deleteAllMessagesWithPeer(curTarget.peer);
    else
        messageStore.deleteAllMessagesInChannel(curTarget.channelIndex);
    beginMsgIndex = 0;
    requestUpdate(Drivers::EInk::UpdateTypes::FAST);
}

// --- Rendering -------------------------------------------------------------------------------

void InkHUD::UniChatApplet::onRender(bool full)
{
    if (view == View::List)
        drawTargetList();
    else
        drawThread();
}

uint8_t InkHUD::UniChatApplet::listVisibleRows()
{
    const int16_t startY = Applet::getHeaderHeight() + 1;
    const uint16_t rowH = fontSmall.lineHeight() + 4;
    if (height() <= startY + rowH)
        return 1;
    return (uint8_t)((height() - startY) / rowH);
}

void InkHUD::UniChatApplet::drawTargetList()
{
    rebuildTargets(); // cheap; keeps DM peers current every repaint

    drawHeader("Chats: pick target");

    const int16_t startY = Applet::getHeaderHeight() + 1;
    const uint16_t rowH = fontSmall.lineHeight() + 4;
    const uint8_t visible = listVisibleRows();

    // Keep the cursor inside the scroll window
    if (cursor < (int16_t)listScroll)
        listScroll = cursor;
    else if (cursor >= (int16_t)(listScroll + visible))
        listScroll = cursor - visible + 1;

    if (targets.empty()) {
        printAt(width() / 2, height() / 2, "No channels / DMs", CENTER, MIDDLE);
        return;
    }

    setFont(fontSmall);
    int16_t y = startY;
    for (uint16_t i = listScroll; i < targets.size() && y + (int16_t)rowH <= height(); i++) {
        printAt(4, y + rowH / 2, targetLabel(targets.at(i)), LEFT, MIDDLE);
        if ((int16_t)i == cursor) { // selection border, doubled for weight on e-ink
            drawRect(0, y, width(), rowH, BLACK);
            drawRect(1, y + 1, width() - 2, rowH - 2, BLACK);
        }
        y += rowH;
    }

    // More entries below? subtle hint
    if (listScroll + listVisibleRows() < targets.size())
        printAt(width() - 2, height() - 1, "+", RIGHT, BOTTOM);
}

// Thread view: same layout as ThreadedMessage/DMChat, filtered by targetMatches
void InkHUD::UniChatApplet::drawThread()
{
    drawHeader(targetLabel(curTarget));

    const int16_t dividerY = Applet::getHeaderHeight() - 1;

    setCrop(0, dividerY + 2, width(), height() - (dividerY + 2));

    constexpr uint16_t padW = 2;
    constexpr int16_t msgL = padW;
    const int16_t msgR = (width() - 1) - padW;
    const uint16_t msgW = (msgR - msgL) + 1;

    int16_t msgB = height() - 1;

    const uint32_t me = myNodeInfo.my_node_num;
    const auto &allMessages = messageStore.getLiveMessages();
    int msgIdx = (int)allMessages.size() - 1;
    uint8_t skip = beginMsgIndex;

    while (msgB >= (0 - fontSmall.lineHeight()) && msgIdx >= 0) {
        const StoredMessage &m = allMessages.at(msgIdx);

        if (!targetMatches(m)) {
            msgIdx--;
            continue;
        }

        if (skip > 0) {
            skip--;
            msgIdx--;
            continue;
        }

        const bool outgoing = (m.sender == me);
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

    hatchRegion(0, dividerY + 1, width(), fontSmall.lineHeight() / 3, 2, WHITE);
}

// --- Navigation (Controllable) ---------------------------------------------------------------

#if defined(MOD_INPUT_MENU)
bool InkHUD::UniChatApplet::handleUp()
{
    if (view == View::List) {
        if (cursor > 0) {
            cursor--;
            requestUpdate(Drivers::EInk::FAST);
        }
        return true;
    }
    // Thread: scroll back through history
    uint8_t threadCount = 0;
    for (const StoredMessage &m : messageStore.getLiveMessages())
        if (targetMatches(m))
            threadCount++;
    if (threadCount > 0 && beginMsgIndex < (uint8_t)(threadCount - 1)) {
        beginMsgIndex++;
        requestUpdate(Drivers::EInk::FAST);
        return true;
    }
    return false;
}

bool InkHUD::UniChatApplet::handleDown()
{
    if (view == View::List) {
        if (cursor + 1 < (int16_t)targets.size()) {
            cursor++;
            requestUpdate(Drivers::EInk::FAST);
        }
        return true;
    }
    if (beginMsgIndex > 0) {
        beginMsgIndex--;
        requestUpdate(Drivers::EInk::FAST);
        return true;
    }
    return false;
}

bool InkHUD::UniChatApplet::handleEnter()
{
    if (view != View::List)
        return false; // thread view: let Enter fall through (applet cycling)
    if (targets.empty())
        return false;
    if (cursor >= 0 && cursor < (int16_t)targets.size()) {
        curTarget = targets.at(cursor);
        targetValid = true;
        beginMsgIndex = 0;
        view = View::Thread;
        refreshName();
        saveTargets(); // selection persists across reboots (rare write)
        requestUpdate(Drivers::EInk::FAST);
        return true;
    }
    return false;
}

bool InkHUD::UniChatApplet::handleBack()
{
    if (view == View::Thread) {
        view = View::List;
        beginMsgIndex = 0;
        requestUpdate(Drivers::EInk::FAST);
        return true;
    }
    if (cursor != 0 || listScroll != 0) {
        cursor = 0;
        listScroll = 0;
        requestUpdate(Drivers::EInk::FAST);
        return true;
    }
    return false;
}
#endif // defined(MOD_INPUT_MENU)

#endif
