#ifdef MESHTASTIC_INCLUDE_INKHUD

/*

One reusable chat applet ("Chats"): pick the conversation INSIDE the applet instead of
registering one applet per channel / per DM peer (the 16-slot settings cap made that model
untenable).

Two views:
 - Target list: every usable channel (role != DISABLED) followed by every DM peer present in
   the shared MessageStore, newest first. W/S move the cursor, Enter opens the thread.
 - Thread: the familiar threaded view (same layout as ThreadedMessage/DMChat), filtered to the
   selected target and read straight from the shared MessageStore, so history survives reboots
   with the store. W/S scroll, Q returns to the list. IME replies go to the open target
   (channel broadcast, or DM to the peer).

Storage: this applet is a text-message module and stores broadcasts for channels WITHOUT an
active dedicated ThreadedMessageApplet (dedicated applets keep storing their own channels;
asThreadedMessageApplet() is the discovery hook, so disabling a channel applet hands its
storage duty to this one automatically). DMs are stored centrally by Events, never here.

*/

#pragma once

#include "configuration.h"

#include "MessageStore.h"
#include "graphics/niche/InkHUD/Applet.h"

#include "modules/TextMessageModule.h"

#if defined(MOD_INPUT_MENU)
#include "graphics/niche/InkHUD/Applets/Bases/Controllable/Controllable.h"
#endif // defined(MOD_INPUT_MENU)

namespace NicheGraphics::InkHUD
{

class Applet;

#if defined(MOD_INPUT_MENU)
class UniChatApplet : public Applet, virtual public Controllable, public SinglePortModule
#else  //! defined(MOD_INPUT_MENU)
class UniChatApplet : public Applet, public SinglePortModule
#endif // defined(MOD_INPUT_MENU)
{
  public:
    static constexpr uint8_t MAX_CHAT_SLOTS = 5;

    explicit UniChatApplet(uint8_t slot);
    UniChatApplet() = delete;
    ~UniChatApplet() override;

    void onRender(bool full) override;
    void onActivate() override;
    void onDeactivate() override;
    ProcessMessage handleReceived(const meshtastic_MeshPacket &mp) override;
    bool approveNotification(Notification &n) override;
    UniChatApplet *asUniChatApplet() override { return this; }

    static const char *slotBaseName(uint8_t slot); // "Chat 1".. — registration/unbound applet name
    void openTargetList();                         // menu "Pick Chat": jump back to the target list
    void clearThread();                            // menu "Clear Thread": wipe the open target's messages

    // IME reply plumbing: valid only while a thread is open
    bool hasThreadTarget() const { return view == View::Thread && targetValid; }
    bool targetIsDM() const { return curTarget.isDM; }
    uint8_t getChannelIndex() const { return curTarget.channelIndex; }
    NodeNum getPeer() const { return curTarget.peer; }
    void noteSent(); // IME sent to the open target: jump to newest + repaint

#if defined(MOD_INPUT_MENU)
    bool handleUp() override;
    bool handleDown() override;
    bool handleEnter() override; // list: open the highlighted target
    bool handleBack() override;  // thread: back to list; list: reset cursor
#endif // defined(MOD_INPUT_MENU)

  private:
    struct Target {
        bool isDM = false;
        uint8_t channelIndex = 0;
        NodeNum peer = 0;
    };

    void rebuildTargets();                       // channels (role != DISABLED) + DM peers from the store
    bool targetMatches(const StoredMessage &m);  // does a stored message belong to curTarget?
    bool channelHasDedicatedApplet(uint8_t ch);  // is an active ThreadedMessageApplet storing this channel?
    bool isStoragePrimary();                     // lowest-slot instance stores; the rest only display
    bool storedIdExists(uint32_t id);            // dedupe vs. other storing modules
    void refreshName();                          // applet name: "Chat n", or the open target's label
    void drawTargetList();
    void drawThread();
    std::string targetLabel(const Target &t);    // "CH0 Public" / "DM ABCD"
    uint8_t listVisibleRows();

    static UniChatApplet *instances[MAX_CHAT_SLOTS]; // registered by constructor, slot-indexed
    static void loadTargets();                       // FlashData "unichats": per-slot selected target
    static void saveTargets();                       // written only when a selection changes (rare)

    enum class View : uint8_t { List, Thread };
    View view = View::List;

    uint8_t slot;
    std::string dynName; // backing storage for Applet::name while a target is open

    std::vector<Target> targets;
    int16_t cursor = 0;      // index into targets
    uint16_t listScroll = 0; // first visible row

    Target curTarget{};
    bool targetValid = false;
    uint8_t beginMsgIndex = 0; // thread scroll offset, mirrors ThreadedMessageApplet
};

} // namespace NicheGraphics::InkHUD

#endif
