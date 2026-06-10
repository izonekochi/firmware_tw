#ifdef MESHTASTIC_INCLUDE_INKHUD

/*

Displays a thread-view of incoming and outgoing message for a specific channel

The channel for this applet is set in the constructor,
when the applet is added to WindowManager in the setupNicheGraphics method.

Several messages are saved to flash at shutdown, to preserve applet between reboots.
This class has its own internal method for saving and loading to fs, which interacts directly with the FSCommon layer.
If the amount of flash usage is unacceptable, we could keep these in RAM only.

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

#if defined(MOD_INPUT_MENU)
class ThreadedMessageApplet : public Applet, virtual public Controllable, public SinglePortModule
#else //!defined(MOD_INPUT_MENU)
class ThreadedMessageApplet : public Applet, public SinglePortModule
#endif //defined(MOD_INPUT_MENU)
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

#if defined(MOD_INPUT_MENU)
    uint8_t getChannelIndex() const { return channelIndex; }
#endif //defined(MOD_INPUT_MENU)

  protected:
    void saveMessagesToFlash();
    void loadMessagesFromFlash();

    uint8_t channelIndex = 0;
};

} // namespace NicheGraphics::InkHUD

#endif
