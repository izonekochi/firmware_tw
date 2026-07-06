#ifdef MESHTASTIC_INCLUDE_INKHUD

#include "./ThreadedMessageApplet.h"

#include "RTC.h"
#include "mesh/NodeDB.h"

#if defined(MOD_INKHUD_TUNES)
#include "mesh/generated/meshtastic/storeforward.pb.h"
#endif //defined(MOD_INKHUD_TUNES)

using namespace NicheGraphics;

#if defined(MOD_INKHUD_TUNES)
#if defined(MOD_INPUT_MENU)
InkHUD::ThreadedMessageApplet::ThreadedMessageApplet(uint8_t channelIndex)
    : Controllable(), MeshModule("ThreadedMessageApplet"), channelIndex(channelIndex)
#else //!defined(MOD_INPUT_MENU)
InkHUD::ThreadedMessageApplet::ThreadedMessageApplet(uint8_t channelIndex)
    : MeshModule("ThreadedMessageApplet"), channelIndex(channelIndex)
#endif //defined(MOD_INPUT_MENU)
#else //!defined(MOD_INKHUD_TUNES)
#if defined(MOD_INPUT_MENU)
InkHUD::ThreadedMessageApplet::ThreadedMessageApplet(uint8_t channelIndex)
    : SinglePortModule("ThreadedMessageApplet", meshtastic_PortNum_TEXT_MESSAGE_APP), Controllable(), channelIndex(channelIndex)
#else //!defined(MOD_INPUT_MENU)
InkHUD::ThreadedMessageApplet::ThreadedMessageApplet(uint8_t channelIndex)
    : SinglePortModule("ThreadedMessageApplet", meshtastic_PortNum_TEXT_MESSAGE_APP), channelIndex(channelIndex)
#endif //defined(MOD_INPUT_MENU)
#endif //defined(MOD_INKHUD_TUNES)
{
#if defined(MOD_INPUT_MENU)
    Controllable::registerControllable(this, Controllable::Types::ThreadedMessage);
#endif //defined(MOD_INPUT_MENU)
}

#if defined(MOD_INPUT_MENU)
InkHUD::ThreadedMessageApplet::~ThreadedMessageApplet()
{
    Controllable::unregisterControllable(this);
}
#endif //defined(MOD_INPUT_MENU)

void InkHUD::ThreadedMessageApplet::onRender(bool full)
{
    // =============
    // Draw a header
    // =============

    // Header text
    std::string headerText;
#if defined(MOD_INPUT_MENU)
    headerText += "CH";
#else //!defined(MOD_INPUT_MENU)
    headerText += "Channel ";
#endif //defined(MOD_INPUT_MENU)
    headerText += to_string(channelIndex);
    headerText += ": ";
    if (channels.isDefaultChannel(channelIndex))
        headerText += "Public";
    else
        headerText += channels.getByIndex(channelIndex).settings.name;

    // Draw a "standard" applet header
    drawHeader(headerText);

    // Y position for divider
    const int16_t dividerY = Applet::getHeaderHeight() - 1;

    // ==================
    // Draw each message
    // ==================

    // Restrict drawing area
    // - don't overdraw the header
    // - small gap below divider
    setCrop(0, dividerY + 2, width(), height() - (dividerY + 2));

    // Set padding
    // - separates text from the vertical line which marks its edge
    constexpr uint16_t padW = 2;
    constexpr int16_t msgL = padW;
    const int16_t msgR = (width() - 1) - padW;
    const uint16_t msgW = (msgR - msgL) + 1;

    int16_t msgB = height() - 1; // Vertical cursor for drawing. Messages are bottom-aligned to this value.

    // Iterate the global store newest-first, showing only broadcast messages on our channel
    const auto &allMessages = messageStore.getLiveMessages();
    int msgIdx = (int)allMessages.size() - 1;
#if defined(MOD_INPUT_MENU)
    uint8_t skip = beginMsgIndex; // Scroll offset: skip this many of the newest on-channel messages
#endif //defined(MOD_INPUT_MENU)

    while (msgB >= (0 - fontSmall.lineHeight()) && msgIdx >= 0) {

        const StoredMessage &m = allMessages.at(msgIdx);

        // Skip messages that don't belong to this channel or are DMs
        if (m.type != MessageType::BROADCAST || m.channelIndex != channelIndex) {
            msgIdx--;
            continue;
        }

#if defined(MOD_INPUT_MENU)
        // Apply the scroll offset: skip the newest `beginMsgIndex` on-channel messages
        if (skip > 0) {
            skip--;
            msgIdx--;
            continue;
        }
#endif //defined(MOD_INPUT_MENU)

        // Grab data for message
        bool outgoing = (m.sender == myNodeInfo.my_node_num);
        std::string bodyText = parse(std::string(MessageStore::getText(m))); // Parse any non-ascii chars

        // Cache bottom Y of message text
        // - Used when drawing vertical line alongside
        const int16_t dotsB = msgB;

        // Get dimensions for message text
        uint16_t bodyH = getWrappedTextHeight(msgL, msgW, bodyText);
        int16_t bodyT = msgB - bodyH;

        // Print message
        // - if incoming
        if (!outgoing)
            printWrapped(msgL, bodyT, msgW, bodyText);

        // Print message
        // - if outgoing
        else {
            if (getTextWidth(bodyText) < width())          // If short,
                printAt(msgR, bodyT, bodyText, RIGHT);     // print right align
            else                                           // If long,
                printWrapped(msgL, bodyT, msgW, bodyText); // need printWrapped(), which doesn't support right align
        }

        // Move cursor up
        // - above message text
        msgB -= bodyH;
        msgB -= getFont().lineHeight() * 0.2; // Padding between message and header

        // Compose info string
        // - shortname, if possible, or "me"
        // - time received, if possible
        std::string info;
        if (outgoing)
            info += "Me";
        else {
            // Check if sender is node db
            meshtastic_NodeInfoLite *sender = nodeDB->getMeshNode(m.sender);
            if (sender)
                info += parseShortName(sender); // Handle any unprintable chars in short name
            else
                info += hexifyNodeNum(m.sender); // No node info at all. Print the node num
        }

        std::string timeString = getTimeString(m.timestamp);
        if (timeString.length() > 0) {
            info += " - ";
            info += timeString;
        }

        // Print the info string
        // - Faux bold: printed twice, shifted horizontally by one px
        printAt(outgoing ? msgR : msgL, msgB, info, outgoing ? RIGHT : LEFT, BOTTOM);
        printAt(outgoing ? msgR - 1 : msgL + 1, msgB, info, outgoing ? RIGHT : LEFT, BOTTOM);

        // Underline the info string
        const int16_t divY = msgB;
        int16_t divL;
        int16_t divR;
        if (!outgoing) {
            // Left side - incoming
            divL = msgL;
            divR = getTextWidth(info) + getFont().lineHeight() / 2;
        } else {
            // Right side - outgoing
            divR = msgR;
            divL = divR - getTextWidth(info) - getFont().lineHeight() / 2;
        }
        for (int16_t x = divL; x <= divR; x += 2)
            drawPixel(x, divY, BLACK);

        // Move cursor up: above info string
        msgB -= fontSmall.lineHeight();

        // Vertical line alongside message
        for (int16_t y = msgB; y < dotsB; y += 1)
            drawPixel(outgoing ? width() - 1 : 0, y, BLACK);

        // Move cursor up: padding before next message
        msgB -= fontSmall.lineHeight() * 0.5;

        msgIdx--;
    } // End of loop: drawing each message

    // Fade effect:
    // Area immediately below the divider. Overdraw with sparse white lines.
    // Make text appear to pass behind the header
    hatchRegion(0, dividerY + 1, width(), fontSmall.lineHeight() / 3, 2, WHITE);
}

// Code which runs when the applet begins running
// This might happen at boot, or if user enables the applet at run-time, via the menu
void InkHUD::ThreadedMessageApplet::onActivate()
{
    loadMessagesFromFlash();
    loopbackOk = true; // Allow us to handle messages generated on the node (canned messages)
}

// Code which runs when the applet stop running
// This might be at shutdown, or if the user disables the applet at run-time, via the menu
void InkHUD::ThreadedMessageApplet::onDeactivate()
{
    loopbackOk = false; // Slightly reduce our impact if the applet is disabled
}

// Handle new text messages
// These might be incoming, from the mesh, or outgoing from phone
// Each instance of the ThreadMessageApplet will only listen on one specific channel
ProcessMessage InkHUD::ThreadedMessageApplet::handleReceived(const meshtastic_MeshPacket &mp)
{
    // Abort if applet fully deactivated
    if (!isActive())
        return ProcessMessage::CONTINUE;

    // Abort if wrong channel
    if (mp.channel != this->channelIndex)
        return ProcessMessage::CONTINUE;

#if defined(MOD_INKHUD_TUNES)
    if (mp.decoded.portnum == meshtastic_PortNum_TEXT_MESSAGE_APP) {
        // Abort if message was a DM
        if (mp.to != NODENUM_BROADCAST)
            return ProcessMessage::CONTINUE;

        if (mp.decoded.emoji && mp.decoded.reply_id) {
            // Emoji reaction: append it to the message it replies to (if still in the store)
            std::string reaction =
                std::string("←") + std::string((const char *)mp.decoded.payload.bytes, mp.decoded.payload.size);
            messageStore.appendTextById(mp.decoded.reply_id, reaction);
        } else {
            // Normal text broadcast → store in the global messageStore (captures id = mp.id)
            messageStore.addFromPacket(mp);
        }
    } else if (mp.decoded.portnum == meshtastic_PortNum_STORE_FORWARD_APP) {
        meshtastic_StoreAndForward sf = meshtastic_StoreAndForward_init_zero;
        if (pb_decode_from_bytes(mp.decoded.payload.bytes, mp.decoded.payload.size, &meshtastic_StoreAndForward_msg, &sf)) {
            if (sf.which_variant == meshtastic_StoreAndForward_text_tag &&
                sf.rr == meshtastic_StoreAndForward_RequestResponse_ROUTER_TEXT_BROADCAST) {
                // Mark an existing copy with an envelope, or store the rebroadcast as a new message
                if (!messageStore.appendTextById(mp.id, std::string("✉"))) {
                    std::string text =
                        std::string((const char *)sf.variant.text.bytes, sf.variant.text.size) + std::string("✉");
                    messageStore.addBroadcast(mp.id, mp.from, mp.channel, mp.rx_time, text);
                }
            }
        }
    }
#else //!defined(MOD_INKHUD_TUNES)
    // Abort if message was a DM
    if (mp.to != NODENUM_BROADCAST)
        return ProcessMessage::CONTINUE;

    // Store in the global messageStore - this handles sender, timestamp, channel, text, and ack status
    messageStore.addFromPacket(mp);
#endif //defined(MOD_INKHUD_TUNES)

    // If this was an incoming message, suggest that our applet becomes foreground, if permitted
    if (getFrom(&mp) != nodeDB->getNodeNum())
        requestAutoshow();

    // Redraw the applet, perhaps.
    requestUpdate(); // Want to update display, if applet is foreground

    // Tell Module API to continue informing other firmware components about this message
    // We're not the only component which is interested in new text messages
    return ProcessMessage::CONTINUE;
}

// Don't show notifications for text messages broadcast to our channel, when the applet is displayed
bool InkHUD::ThreadedMessageApplet::approveNotification(Notification &n)
{
    if (n.type == Notification::Type::NOTIFICATION_MESSAGE_BROADCAST && n.getChannel() == channelIndex)
        return false;

    // None of our business. Allow the notification.
    else
        return true;
}

#if defined(MOD_INKHUD_TUNES)
bool InkHUD::ThreadedMessageApplet::wantPacket(const meshtastic_MeshPacket *p)
{
    if (p->decoded.portnum == meshtastic_PortNum_TEXT_MESSAGE_APP)
        return true;
    else if (p->decoded.portnum == meshtastic_PortNum_STORE_FORWARD_APP)
        return true;
    return false;
}
#endif //defined(MOD_INKHUD_TUNES)

#if defined(MOD_INPUT_MENU)
bool InkHUD::ThreadedMessageApplet::handleUp()
{
    // Count broadcast messages currently stored for our channel
    uint8_t channelCount = 0;
    for (const StoredMessage &m : messageStore.getLiveMessages())
        if (m.type == MessageType::BROADCAST && m.channelIndex == channelIndex)
            channelCount++;

    if (channelCount > 0 && beginMsgIndex < (uint8_t)(channelCount - 1)) {
        beginMsgIndex++;
        requestUpdate(Drivers::EInk::FAST);
        return true;
    }
    return false;
}

bool InkHUD::ThreadedMessageApplet::handleDown()
{
    if (beginMsgIndex > 0) {
        beginMsgIndex--;
        requestUpdate(Drivers::EInk::FAST);
        return true;
    }
    return false;
}

bool InkHUD::ThreadedMessageApplet::handleBack()
{
    beginMsgIndex = 0;
    requestUpdate(Drivers::EInk::FAST);
    return true;
}
#endif //defined(MOD_INPUT_MENU)

// Save messages to flash via the global messageStore.
// The global store holds messages for all channels; no per-channel file is needed.
void InkHUD::ThreadedMessageApplet::saveMessagesToFlash()
{
    messageStore.saveToFlash();
}

// Messages are loaded once by InkHUD::begin() before applets start.
// Nothing to do here at per-applet activation time.
void InkHUD::ThreadedMessageApplet::loadMessagesFromFlash()
{
    // No-op: messageStore.loadFromFlash() is called in InkHUD::begin()
}

// Code to run when device is shutting down
// This is in addition to any onDeactivate() code, which will also run
void InkHUD::ThreadedMessageApplet::onShutdown()
{
    // messageStore.saveToFlash() is called centrally by Events::beforeDeepSleep / beforeReboot
}

#endif
