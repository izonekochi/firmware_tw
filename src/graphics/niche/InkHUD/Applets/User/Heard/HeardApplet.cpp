#ifdef MESHTASTIC_INCLUDE_INKHUD

#include "gps/RTC.h"

#include "gps/GeoCoord.h"

#include "./HeardApplet.h"

#if defined(MOD_INPUT_MENU)
#include "graphics/niche/InkHUD/Applets/User/DMChat/DMChatApplet.h" // Enter on a node -> open chat
#endif //defined(MOD_INPUT_MENU)

using namespace NicheGraphics;

void InkHUD::HeardApplet::onActivate()
{
    // When applet begins, pre-fill with stale info from NodeDB
    populateFromNodeDB();
}

void InkHUD::HeardApplet::onDeactivate()
{
    // Avoid an unlikely situation where frequent activation / deactivation populates duplicate info from node DB
    cards.clear();
}

// When base applet hears a new packet, it extracts the info and passes it to us as CardInfo
// We need to store it (at front to sort recent), and request display update if our list has visibly changed as a result
void InkHUD::HeardApplet::handleParsed(CardInfo c)
{
#if defined(MOD_INPUT_MENU)
    // Repopulate instead of push_front: favorites stay pinned to the top (a freshly heard
    // non-favorite must slot in BELOW them), the scroll window (beginCard) is honored, and
    // lastStrength carries the live signal into the rebuilt cards. Re-render only when the
    // visible window actually changed - same wear-avoidance intent as the upstream
    // top-card comparison.
    lastStrength[c.nodeNum] = c.signal;
    LOG_INFO("HeardApplet: heard node !%x, signal=%d", c.nodeNum, static_cast<int>(c.signal));

    const std::deque<CardInfo> before = cards;
    populateFromNodeDB();

    bool changed = (before.size() != cards.size());
    if (!changed) {
        for (size_t i = 0; i < cards.size(); i++) {
            const CardInfo &a = before.at(i);
            const CardInfo &b = cards.at(i);
            if (a.nodeNum != b.nodeNum || a.signal != b.signal || a.distanceMeters != b.distanceMeters ||
                a.hopsAway != b.hopsAway) {
                changed = true;
                break;
            }
        }
    }
    if (changed) {
        requestAutoshow();
        requestUpdate();
    }
#else  //! defined(MOD_INPUT_MENU)
    // Grab the previous entry.
    // To check if the new data is different enough to justify re-render
    // Need to cache now, before we manipulate the deque
    CardInfo previous;
    if (!cards.empty())
        previous = cards.at(0);

    // If we're updating an existing entry, remove the old one. Will reinsert at front
    for (auto it = cards.begin(); it != cards.end(); ++it) {
        if (it->nodeNum == c.nodeNum) {
            cards.erase(it);
            break;
        }
    }

    cards.push_front(c);                                  // Insert into base class' card collection
    cards.resize(min(maxCards(), (uint8_t)cards.size())); // Don't keep more cards than we could *ever* fit on screen
    cards.shrink_to_fit();

    // Our rendered image needs to change if:
    if (previous.nodeNum != c.nodeNum                  // Different node
        || previous.signal != c.signal                 // or different signal strength
        || previous.distanceMeters != c.distanceMeters // or different position
        || previous.hopsAway != c.hopsAway)            // or different hops away
    {
        requestAutoshow();
        requestUpdate();
    }
#endif // defined(MOD_INPUT_MENU)
}

// When applet is activated, pre-fill with stale data from NodeDB
// We're sorting using the last_heard value. Susceptible to weirdness if node's RTC changes.
// No SNR is available in node db, so we can't calculate signal either
// These initial cards from node db will be gradually pushed out by new packets which originate from out base applet instead
#if defined(MOD_INPUT_MENU)
// Full sorted node list (favorites pinned to the top, then by age): shared by the card window
// builder below and the typed node search.
void InkHUD::HeardApplet::buildOrderedNodes(std::vector<meshtastic_NodeInfoLite *> &ordered)
{
    for (auto mn = nodeDB->meshNodes->begin(); mn != nodeDB->meshNodes->end(); ++mn) {
        if (mn->num != 0 && mn->num != nodeDB->getNodeNum())
            ordered.push_back(&*mn);
    }
    std::sort(ordered.begin(), ordered.end(),
              [](const meshtastic_NodeInfoLite *top, const meshtastic_NodeInfoLite *bottom) -> bool {
                  const bool favTop = nodeInfoLiteIsFavorite(top);
                  const bool favBottom = nodeInfoLiteIsFavorite(bottom);
                  if (favTop != favBottom)
                      return favTop;
                  return (top->last_heard > bottom->last_heard);
              });
}
#endif // defined(MOD_INPUT_MENU)

void InkHUD::HeardApplet::populateFromNodeDB()
{
    // Fill a collection with pointers to each node in db
    std::vector<meshtastic_NodeInfoLite *> ordered;
#if defined(MOD_INPUT_MENU)
    buildOrderedNodes(ordered);
#else  //! defined(MOD_INPUT_MENU)
    for (auto mn = nodeDB->meshNodes->begin(); mn != nodeDB->meshNodes->end(); ++mn) {
        // Only copy if valid, and not our own node
        if (mn->num != 0 && mn->num != nodeDB->getNodeNum())
            ordered.push_back(&*mn);
    }

    // Sort the collection by age
    std::sort(ordered.begin(), ordered.end(),
              [](const meshtastic_NodeInfoLite *top, const meshtastic_NodeInfoLite *bottom) -> bool {
                  return (top->last_heard > bottom->last_heard);
              });
#endif // defined(MOD_INPUT_MENU)

    // Keep the most recent entries only
    // Just enough to fill the screen
#if defined(MOD_INPUT_MENU)
    cards.clear();
    if (ordered.size() > maxCards()) {
        for (size_t i = 0; i < maxCards() && i + beginCard < ordered.size(); i++)
            ordered[i] = ordered[i + beginCard];
        ordered.resize(maxCards());
    }
#else //!defined(MOD_INPUT_MENU)
    if (ordered.size() > maxCards())
        ordered.resize(maxCards());
#endif //defined(MOD_INPUT_MENU)

    // Create card info for these (stale) node observations
    const meshtastic_NodeInfoLite *ourNode = nodeDB->getMeshNode(nodeDB->getNodeNum());
    for (const meshtastic_NodeInfoLite *node : ordered) {
        CardInfo c;
        c.nodeNum = node->num;
#if defined(MOD_INPUT_MENU)
        if (lastStrength.find(c.nodeNum) != lastStrength.cend())
            c.signal = lastStrength[c.nodeNum];
        else
            c.signal = getSignalStrength(node->snr, -100.0f);
#endif //defined(MOD_INPUT_MENU)

        if (node->has_hops_away)
            c.hopsAway = node->hops_away;

        if (nodeDB->hasValidPosition(node) && nodeDB->hasValidPosition(ourNode)) {
            meshtastic_PositionLite ourPos;
            meshtastic_PositionLite theirPos;
            if (nodeDB->copyNodePosition(ourNode->num, ourPos) && nodeDB->copyNodePosition(node->num, theirPos)) {
                float ourLat = ourPos.latitude_i * 1e-7;
                float ourLong = ourPos.longitude_i * 1e-7;
                float theirLat = theirPos.latitude_i * 1e-7;
                float theirLong = theirPos.longitude_i * 1e-7;

                c.distanceMeters = (int32_t)GeoCoord::latLongToMeter(theirLat, theirLong, ourLat, ourLong);
            }
        }

        // Insert into the card collection (member of base class)
        cards.push_back(c);
    }
}

// Text drawn in the usual applet header
// Handled by base class: ChronoListApplet
std::string InkHUD::HeardApplet::getHeaderText()
{
    uint16_t nodeCount = nodeDB->getNumMeshNodes() - 1; // Don't count our own node

    std::string text = "Heard: ";

    // Print node count, if nodeDB not yet nearing full
    if (nodeCount < MAX_NUM_NODES) {
        text += to_string(nodeCount); // Max nodes
        text += " ";
        text += (nodeCount == 1) ? "node" : "nodes";
    }

    return text;
}

#if defined(MOD_INPUT_MENU)
bool InkHUD::HeardApplet::handleUp()
{
    // Node-select mode: move the highlight; at the top edge, scroll the window under it
    if (selCard > 0) {
        selCard--;
        requestUpdate(NicheGraphics::Drivers::EInk::FAST);
        return true;
    }
    if (selCard == 0) {
        if (beginCard > 0) {
            beginCard--;
            populateFromNodeDB();
            requestUpdate(NicheGraphics::Drivers::EInk::FAST);
        }
        return true;
    }
    if (beginCard > 0) {
        beginCard--;
        LOG_INFO("HeardApplet: beginCard=%u", beginCard);
        populateFromNodeDB();
        requestUpdate(NicheGraphics::Drivers::EInk::FAST);
        return true;
    }
    return false;
}

bool InkHUD::HeardApplet::handleDown()
{
    // Node-select mode: move the highlight; at the bottom edge, scroll the window under it.
    // Bound by visibleCards(), NOT cards.size(): the deque is sized by maxCards() (largest
    // display dimension), so it holds more entries than the tile can draw - unbounded, the
    // highlight walked off the bottom of the screen.
    if (selCard >= 0) {
        const int16_t visLimit = (int16_t)min((size_t)visibleCards(), cards.size());
        if (selCard + 1 < visLimit) {
            selCard++;
            requestUpdate(NicheGraphics::Drivers::EInk::FAST);
        } else if (beginCard + visLimit < nodeDB->meshNodes->size() - 1) { // -1: self is never listed
            beginCard++;
            populateFromNodeDB();
            if (selCard >= (int16_t)min((size_t)visibleCards(), cards.size()))
                selCard = (int16_t)min((size_t)visibleCards(), cards.size()) - 1; // clamp after refill
            requestUpdate(NicheGraphics::Drivers::EInk::FAST);
        }
        return true;
    }
    if (cards.size() == maxCards() && beginCard + maxCards() < nodeDB->meshNodes->size()) {
        beginCard++;
        LOG_INFO("HeardApplet: beginCard=%u", beginCard);
        populateFromNodeDB();
        requestUpdate(NicheGraphics::Drivers::EInk::FAST);
        return true;
    }
    return false;
}

bool InkHUD::HeardApplet::handleBack()
{
    // Leave node-select mode first; a second Back resets the scroll as before
    if (selCard >= 0) {
        selCard = -1;
        requestUpdate(NicheGraphics::Drivers::EInk::FAST);
        return true;
    }
    if (beginCard != 0) {
        beginCard = 0;
        LOG_INFO("HeardApplet: beginCard=%u", beginCard);
        populateFromNodeDB();
        requestUpdate(NicheGraphics::Drivers::EInk::FAST);
        return true;
    }
    return false;
}

NodeNum InkHUD::HeardApplet::selectedNodeNum()
{
    if (selCard < 0 || selCard >= (int16_t)cards.size())
        return 0;
    return cards.at(selCard).nodeNum;
}

// Menu action on a highlighted node: toggle its favorite flag. NodeDB::set_favorite handles
// the protected-flag bookkeeping, DB re-sort and persistence; here we rebuild the (favorites-
// pinned) list from the top and chase the node to keep the highlight on it.
bool InkHUD::HeardApplet::toggleSelectedFavorite()
{
    const NodeNum n = selectedNodeNum();
    if (!n)
        return false;
    if (!nodeDB->set_favorite(!nodeDB->isFavorite(n), n))
        return false;

    beginCard = 0; // favoriting moves the node toward the top; show it
    populateFromNodeDB();
    selCard = -1;
    const int16_t visLimit = (int16_t)min((size_t)visibleCards(), cards.size());
    for (int16_t i = 0; i < visLimit; i++) {
        if (cards.at(i).nodeNum == n) {
            selCard = i;
            break;
        }
    }
    requestUpdate(NicheGraphics::Drivers::EInk::FAST);
    return true;
}

// Enter only acts on a HIGHLIGHTED node: open (or claim) its DM chat window. With no highlight
// it falls through (returns false) and the background router opens the settings menu - whose
// "Select Node" and "Search Node" items are the entries into select mode (user decision
// 2026-08-07: Enter = menu on list-style applets, IME only on chat-style ones).
bool InkHUD::HeardApplet::handleEnter()
{
    if (selCard < 0 || cards.empty())
        return false;

    if (selCard < (int16_t)cards.size()) {
        const NodeNum n = cards.at(selCard).nodeNum;
        selCard = -1;
        if (DMChatApplet::openChatWith(n))
            return true; // chat window is foreground now
    } else {
        selCard = -1;
    }
    requestUpdate(NicheGraphics::Drivers::EInk::FAST);
    return true;
}

// Menu "Select Node": enter node-select mode with the highlight on the top visible card
void InkHUD::HeardApplet::enterSelectMode()
{
    if (cards.empty())
        populateFromNodeDB();
    selCard = cards.empty() ? -1 : 0;
    requestUpdate(NicheGraphics::Drivers::EInk::FAST);
}

// IME "Search Node": case-insensitive (ASCII) substring match over short + long names in the
// favorites-first ordering; the first match scrolls to the top row and takes the highlight.
// CJK names match by exact byte sequence, so typed CJK works too.
void InkHUD::HeardApplet::searchNode(const char *query)
{
    if (!query || !*query)
        return;

    std::string q = query;
    for (auto &ch : q)
        ch = tolower((uint8_t)ch);
    auto contains = [&q](const char *hay) {
        if (!hay || !*hay)
            return false;
        std::string h = hay;
        for (auto &ch : h)
            ch = tolower((uint8_t)ch);
        return h.find(q) != std::string::npos;
    };

    std::vector<meshtastic_NodeInfoLite *> ordered;
    buildOrderedNodes(ordered);
    for (size_t i = 0; i < ordered.size(); i++) {
        if (contains(ordered[i]->short_name) || contains(ordered[i]->long_name)) {
            beginCard = i; // match lands on the top row
            populateFromNodeDB();
            selCard = cards.empty() ? -1 : 0;
            requestUpdate(NicheGraphics::Drivers::EInk::FAST);
            return;
        }
    }
    // No match: leave the list untouched
}

#endif //defined(MOD_INPUT_MENU)

#endif
