#ifdef MESHTASTIC_INCLUDE_INKHUD

/*

Set of end-point actions for the Menu Applet

Added as menu entries in MenuApplet::showPage
Behaviors assigned in MenuApplet::execute

*/

#pragma once

#include "configuration.h"

namespace NicheGraphics::InkHUD
{

enum MenuAction {
    NO_ACTION,
    SEND_PING,
    FREE_TEXT,
    STORE_CANNEDMESSAGE_SELECTION,
    SEND_CANNEDMESSAGE,
    SHUTDOWN,
    NEXT_TILE,
    TOGGLE_BACKLIGHT,
    TOGGLE_GPS,
    TOGGLE_SMART_POSITION,
    SET_POSITION_BROADCAST_INTERVAL,
    SET_SMART_BROADCAST_INTERVAL,
    SET_SMART_BROADCAST_DISTANCE,
    SET_GPS_UPDATE_INTERVAL,
    TOGGLE_DEVICE_TELEMETRY,       // device_telemetry_enabled: checked live per cycle, no reboot
    TOGGLE_POWER_TELEMETRY,        // power_measurement_enabled: module constructed at boot -> reboots
    SET_DEVICE_TELEMETRY_INTERVAL, // device_update_interval picker (live)
    SET_POWER_TELEMETRY_INTERVAL,  // power_update_interval picker (live)
    SET_NODEINFO_INTERVAL,         // node_info_broadcast_secs picker (live, next module cycle)
    TOGGLE_TDM_DEBUG_HOLD,         // T-Deck Max: stay-awake + fast Info refresh (session-only)
    TOGGLE_TDM_SILENT,             // T-Deck Max: silent mode - no asleep e-ink refresh (persisted)
    DMCHAT_TRACEROUTE,             // DM chat: stage the bound peer for traceroute (channel picker follows)
    HEARD_TRACEROUTE,              // Heard: stage the highlighted node for traceroute (channel picker follows)
    TRACEROUTE_GO,                 // TRACEROUTE_VIA page: fire the trace on the selected channel
    ENABLE_BLUETOOTH,
    TOGGLE_APPLET,
    TOGGLE_AUTOSHOW_APPLET,
    SET_RECENTS,
    ROTATE,
    TOGGLE_JOYSTICK,
    ALIGN_JOYSTICK,
    LAYOUT,
    TOGGLE_BATTERY_ICON,
    TOGGLE_NOTIFICATIONS,
    TOGGLE_NODE_MARKERS,     // NavMap: show/hide node markers
    TOGGLE_AUTO_JUMP_MSG,    // NavMap: recenter on a message's sender
    TOGGLE_MAP_LABELS,       // NavMap: master switch for the label overlay
    TOGGLE_PLACE_LABELS,     // NavMap: place / district name labels
    TOGGLE_HOSPITALS,        // NavMap: hospital / medical icons
    TOGGLE_SHELTERS,         // NavMap: shelter icons
    TOGGLE_EMERGENCY_SVC,    // NavMap: police / fire icons
    TOGGLE_INVERT_COLOR,
    TOGGLE_12H_CLOCK,
    // Regions
    SET_REGION_US,
    SET_REGION_EU_868,
    SET_REGION_EU_433,
    SET_REGION_CN,
    SET_REGION_JP,
    SET_REGION_ANZ,
    SET_REGION_KR,
    SET_REGION_TW,
    SET_REGION_RU,
    SET_REGION_IN,
    SET_REGION_NZ_865,
    SET_REGION_TH,
    SET_REGION_LORA_24,
    SET_REGION_UA_433,
    SET_REGION_MY_433,
    SET_REGION_MY_919,
    SET_REGION_SG_923,
    SET_REGION_PH_433,
    SET_REGION_PH_868,
    SET_REGION_PH_915,
    SET_REGION_ANZ_433,
    SET_REGION_KZ_433,
    SET_REGION_KZ_863,
    SET_REGION_NP_865,
    SET_REGION_BR_902,
    SET_REGION_EU_866,
    SET_REGION_NARROW_868,
    SET_REGION_ITU1_2M,
    SET_REGION_ITU2_2M,
    SET_REGION_ITU3_2M,
    SET_REGION_ITU2_125CM,
    SET_REGION_ITU1_70CM,
    SET_REGION_ITU2_70CM,
    SET_REGION_ITU3_70CM,
    // Device Roles
    SET_ROLE_CLIENT,
    SET_ROLE_CLIENT_MUTE,
    SET_ROLE_ROUTER,
    SET_ROLE_REPEATER,
    // Presets
    SET_PRESET_LONG_SLOW,
    SET_PRESET_LONG_MODERATE,
    SET_PRESET_LONG_FAST,
    SET_PRESET_MEDIUM_SLOW,
    SET_PRESET_MEDIUM_FAST,
    SET_PRESET_SHORT_SLOW,
    SET_PRESET_SHORT_FAST,
    SET_PRESET_SHORT_TURBO,
    SET_PRESET_LITE_SLOW,
    SET_PRESET_LITE_FAST,
    SET_PRESET_NARROW_SLOW,
    SET_PRESET_NARROW_FAST,
    SET_PRESET_TINY_SLOW,
    SET_PRESET_TINY_FAST,
    SET_PRESET_FROM_REGION, // Dynamic: preset chosen from region-available list
    // Timezones
    SET_TZ_US_HAWAII,
    SET_TZ_US_ALASKA,
    SET_TZ_US_PACIFIC,
    SET_TZ_US_ARIZONA,
    SET_TZ_US_MOUNTAIN,
    SET_TZ_US_CENTRAL,
    SET_TZ_US_EASTERN,
    SET_TZ_BR_BRAZILIA,
    SET_TZ_UTC,
    SET_TZ_EU_WESTERN,
    SET_TZ_EU_CENTRAL,
    SET_TZ_EU_EASTERN,
    SET_TZ_ASIA_KOLKATA,
    SET_TZ_ASIA_HONG_KONG,
    SET_TZ_AU_AWST,
    SET_TZ_AU_ACST,
    SET_TZ_AU_AEST,
    SET_TZ_PACIFIC_NZ,
    // Power
    TOGGLE_POWER_SAVE,
    CALIBRATE_ADC,
    // Bluetooth
    TOGGLE_BLUETOOTH,
    TOGGLE_BLUETOOTH_PAIR_MODE,
    // Channel
    TOGGLE_CHANNEL_UPLINK,
    TOGGLE_CHANNEL_DOWNLINK,
    TOGGLE_CHANNEL_POSITION,
    SET_CHANNEL_PRECISION,
    // Display
    SET_DISPLAY_TIMEOUT,
    TOGGLE_DISPLAY_UNITS,
    // Network
    TOGGLE_WIFI,
    // Administration
    RESET_NODEDB_ALL,
    RESET_NODEDB_KEEP_FAVORITES,
    WIPE_MESSAGES_ALL,
    // Map zoom (MapApplet and FavoritesMapApplet)
    MAP_ZOOM_IN,
    MAP_ZOOM_OUT,
    MAP_ZOOM_RESET,
    MAP_GPS_LOCATE,   // NavMap: force a fresh GPS fix and recenter on it - MAP-ONLY, never broadcast
    MAP_GPS_TRACE,    // NavMap: toggle continuous private follow mode (recenter on every fix)
    MAP_SET_POSITION, // NavMap: publish the current map centre as the node's official position
    DMCHAT_CLOSE,   // DMChat: unbind the displayed chat window, returning its slot to the pool
    UNICHAT_PICK,   // Chats: reopen the in-applet channel/DM target list
    UNICHAT_CLEAR,  // Chats: delete every stored message of the open target
    HEARD_TOGGLE_FAVORITE, // Heard: favorite/unfavorite the highlighted node (pins it to the top)
    HEARD_SELECT_MODE,     // Heard: enter node-select mode (highlight on the top visible card)
    MENU_OPEN_INPUT,       // Open the IME driving the applet under the menu (Search Node / Search Place / Reply...)
    CYCLE_VIBRA,           // T-Deck Max: vibration policy All -> DMs only -> Off
    // T-Deck Max hardware toggles (persisted via TDeckMaxPrefs; execute cases gated to T_DECK_MAX)
    TOGGLE_ANTENNA,    // LoRa antenna internal/external (XL9555 P04)
    CYCLE_FRONTLIGHT,  // E-ink frontlight level 0/64/128/255 (GPIO41 PWM)
    TOGGLE_TOUCH,      // Touchscreen coordinate/gesture enable (default OFF)
    TOGGLE_QUICK_SLEEP, // Idle -> immediate light sleep (default ON)
    TOGGLE_CPU_FREQ,    // 80MHz (default) <-> 240MHz race-to-sleep experiment
    TOGGLE_RX_SNIFF,    // SX126x RX duty-cycle sniff depth: 8 symbols (default) <-> 4 (eco)
};

} // namespace NicheGraphics::InkHUD

#endif
