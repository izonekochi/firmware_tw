#ifdef MESHTASTIC_INCLUDE_INKHUD

/*

Shows a list of all nodes (recently heard or not), sorted by time last heard.
Most of the work is done by the InkHUD::NodeListApplet base class

*/

#pragma once

#include "configuration.h"

#include "graphics/niche/InkHUD/Applets/Bases/NodeList/NodeListApplet.h"

#if defined(MOD_INPUT_MENU)
#include "graphics/niche/InkHUD/Applets/Bases/Controllable/Controllable.h"
#endif //defined(MOD_INPUT_MENU)

namespace NicheGraphics::InkHUD
{

#if defined(MOD_INPUT_MENU)
class HeardApplet : public NodeListApplet, virtual public Controllable
#else //!defined(MOD_INPUT_MENU)
class HeardApplet : public NodeListApplet
#endif //defined(MOD_INPUT_MENU)
{
  public:
#if defined(MOD_INPUT_MENU)
    HeardApplet() : Controllable(), NodeListApplet("HeardApplet") {
      registerControllable(this, Controllable::Types::Heard);
    }
    ~HeardApplet() {
      unregisterControllable(this);
    }
#else //!defined(MOD_INPUT_MENU)
    HeardApplet() : NodeListApplet("HeardApplet") {}
#endif //defined(MOD_INPUT_MENU)
    void onActivate() override;
    void onDeactivate() override;

  protected:
    void handleParsed(CardInfo c) override; // Store new info, and update display if needed
    std::string getHeaderText() override;   // Set title for this applet

    void populateFromNodeDB(); // Pre-fill the CardInfo collection from NodeDB
#if defined(MOD_INPUT_MENU)
    bool handleUp() override;
    bool handleDown() override;
    bool handleBack() override;
    std::unordered_map<NodeNum, SignalStrength> lastStrength;
    size_t beginCard = 0;
#endif //defined(MOD_INPUT_MENU)
};

} // namespace NicheGraphics::InkHUD

#endif