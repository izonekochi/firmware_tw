#ifdef MESHTASTIC_INCLUDE_INKHUD

/*

Dynamic per-peer direct-message chat window.

A fixed pool of slots is registered at boot (nicheGraphics.h) and BOUND to nodes at runtime:
when a DM arrives from a node with no open chat, a free slot claims it (evicting the least
recently active background chat when the pool is full), activates itself, takes the applet name
"DM:<shortname>", and comes to the foreground so the user can read and reply immediately.
Routing happens centrally in Events::onReceiveTextMessage -> onIncomingDM(), so a NEW peer can
claim a slot even while that slot's applet is inactive.

Messages come straight from the shared MessageStore filtered by peer (incoming DMs are stored
by Events, outgoing ones by MeshService's local-message hook), so a chat survives reboots as
long as the store does. Slot->peer bindings persist in their own FlashData file ("dmchats").

Replying: the applet is a Controllable (Types::DMChat); the IME's commit paths send composed
text to the bound peer as a DM. A bound chat is closed ("Close Chat" in the menu) by unbinding:
the slot deactivates and returns to the pool.

*/

#pragma once

#include "configuration.h"

#include "MessageStore.h"
#include "graphics/niche/InkHUD/Applet.h"

#if defined(MOD_INPUT_MENU)
#include "graphics/niche/InkHUD/Applets/Bases/Controllable/Controllable.h"
#endif // defined(MOD_INPUT_MENU)

namespace NicheGraphics::InkHUD
{

class Applet;

#if defined(MOD_INPUT_MENU)
class DMChatApplet : public Applet, virtual public Controllable
#else  //! defined(MOD_INPUT_MENU)
class DMChatApplet : public Applet
#endif // defined(MOD_INPUT_MENU)
{
  public:
    static constexpr uint8_t MAX_SLOTS = 3;

    explicit DMChatApplet(uint8_t slot);
    DMChatApplet() = delete;
    ~DMChatApplet() override;

    void onRender(bool full) override;
    void onActivate() override;
    bool approveNotification(Notification &n) override;
    DMChatApplet *asDMChatApplet() override { return this; }

    NodeNum getPeer() const { return peer; }
    bool isBound() const { return peer != 0; }
    void closeChat(); // unbind + persist + clear our settings flags; caller runs updateAppletSelection()
    void noteSent();  // IME just sent a DM to our peer: repaint the thread

    static const char *slotBaseName(uint8_t slot);      // "DM 1".. — registration/unbound applet name
    static void onIncomingDM(uint32_t sender);          // central routing entry (Events.cpp)
    static DMChatApplet *openChatWith(uint32_t node);   // user-initiated (Heard list): find/claim + show

#if defined(MOD_INPUT_MENU)
    bool handleUp() override;
    bool handleDown() override;
    bool handleBack() override;
#endif // defined(MOD_INPUT_MENU)

  private:
    static DMChatApplet *claimFor(uint32_t node); // find bound chat or claim/evict a slot (binds it)
    void bindTo(NodeNum node); // claim: set peer, name, settings flags, activate, persist
    void showForIncoming();    // bring this chat on screen (sleep-aware), refresh
    void refreshName();        // applet name: base name, or "DM:<shortname>" when bound

    static DMChatApplet *instances[MAX_SLOTS]; // registered by constructor, slot-indexed
    static void loadBindings();                // FlashData "dmchats" (once)
    static void saveBindings();

    uint8_t slot;
    NodeNum peer = 0;
    std::string dynName;       // backing storage for Applet::name when bound
    uint32_t lastRxMs = 0;     // eviction: least recently received loses its slot
    uint8_t beginMsgIndex = 0; // scroll offset, mirrors ThreadedMessageApplet
};

} // namespace NicheGraphics::InkHUD

#endif
