#ifdef MESHTASTIC_INCLUDE_INKHUD

/*

Displays a thread-view of incoming and outgoing message for a specific channel

The channel for this applet is set in the constructor,
when the applet is added to WindowManager in the setupNicheGraphics method.

Messages are stored in the shared global messageStore (see src/MessageStore.h),
which persists a few recent messages to flash and is loaded once by InkHUD::begin().

Multiple instances of this channel may be used. This must be done at buildtime.
Suggest a max of two channel, to minimize fs usage?

*/

#pragma once

#include "configuration.h"

#include "MessageStore.h"
#include "graphics/niche/InkHUD/Applet.h"

#include "modules/TextMessageModule.h"

#if defined(MOD_INPUT_MENU)
#include "graphics/niche/InkHUD/Applets/Bases/Controllable/Controllable.h"
#endif //defined(MOD_INPUT_MENU)

namespace NicheGraphics::InkHUD
{

class Applet;

#if defined(MOD_INKHUD_TUNES)
#if defined(MOD_INPUT_MENU)
class ThreadedMessageApplet : public Applet, virtual public Controllable, public MeshModule
#else //!defined(MOD_INPUT_MENU)
class ThreadedMessageApplet : public Applet, public MeshModule
#endif //defined(MOD_INPUT_MENU)
#else //!defined(MOD_INKHUD_TUNES)
#if defined(MOD_INPUT_MENU)
class ThreadedMessageApplet : public Applet, virtual public Controllable, public SinglePortModule
#else //!defined(MOD_INPUT_MENU)
class ThreadedMessageApplet : public Applet, public SinglePortModule
#endif //defined(MOD_INPUT_MENU)
#endif //defined(MOD_INKHUD_TUNES)
{
  public:
    explicit ThreadedMessageApplet(uint8_t channelIndex);
    ThreadedMessageApplet() = delete;
#if defined(MOD_INPUT_MENU)
    ~ThreadedMessageApplet() override;
#endif //defined(MOD_INPUT_MENU)

    void onRender(bool full) override;

    void onActivate() override;
    void onDeactivate() override;
    void onShutdown() override;
    ProcessMessage handleReceived(const meshtastic_MeshPacket &mp) override;

    bool approveNotification(Notification &n) override; // Which notifications to suppress

#if defined(MOD_INKHUD_TUNES)
    bool wantPacket(const meshtastic_MeshPacket *p) override;
#endif //defined(MOD_INKHUD_TUNES)

#if defined(MOD_INPUT_MENU)
    uint8_t getChannelIndex() const { return channelIndex; }
    bool handleUp() override;
    bool handleDown() override;
    bool handleBack() override;
#endif //defined(MOD_INPUT_MENU)

  protected:
    void loadMessagesFromFlash();

    uint8_t channelIndex = 0;

#if defined(MOD_INPUT_MENU)
    uint8_t beginMsgIndex = 0; // Scroll offset: number of newest on-channel messages to skip when rendering
#endif //defined(MOD_INPUT_MENU)
};

} // namespace NicheGraphics::InkHUD

#endif
