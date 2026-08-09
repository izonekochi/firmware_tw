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
    HeardApplet *asHeardApplet() override { return this; }

#if defined(MOD_INPUT_MENU)
    // Node-select mode accessors for the menu ("Favorite"/"Unfavorite" on the highlighted node)
    NodeNum selectedNodeNum();     // 0 when no node is highlighted
    bool toggleSelectedFavorite(); // flip + persist via NodeDB, re-sort, keep the highlight on the node
    void enterSelectMode();        // menu "Select Node": highlight the top visible card
    void searchNode(const char *query); // IME "Search Node": scroll to + highlight the first name match
#endif //defined(MOD_INPUT_MENU)

  protected:
    void handleParsed(CardInfo c) override; // Store new info, and update display if needed
    std::string getHeaderText() override;   // Set title for this applet

    void populateFromNodeDB(); // Pre-fill the CardInfo collection from NodeDB
#if defined(MOD_INPUT_MENU)
    void buildOrderedNodes(std::vector<meshtastic_NodeInfoLite *> &ordered); // full sorted list (favorites first)
#endif //defined(MOD_INPUT_MENU)
#if defined(MOD_INPUT_MENU)
    bool handleUp() override;
    bool handleDown() override;
    bool handleBack() override;
    bool handleEnter() override; // node-select mode: 1st Enter = show cursor, 2nd = open DM chat
    int16_t highlightedCard() override { return selCard; }
    std::unordered_map<NodeNum, SignalStrength> lastStrength;
    size_t beginCard = 0;
    int16_t selCard = -1; // visible-card selection cursor (-1 = plain scroll mode)
#endif //defined(MOD_INPUT_MENU)
};

} // namespace NicheGraphics::InkHUD

#endif