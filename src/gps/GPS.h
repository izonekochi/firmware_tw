#pragma once
#include "configuration.h"
#if !MESHTASTIC_EXCLUDE_GPS

#include <memory>

#include "GPSStatus.h"
#include "GpioLogic.h"
#include "Observer.h"
#include "TinyGPS++.h"
#include "concurrency/OSThread.h"
#include "input/RotaryEncoderInterruptImpl1.h"
#include "input/UpDownInterruptImpl1.h"
#include "modules/PositionModule.h"

// Allow defining the polarity of the ENABLE output.  default is active high
#ifndef GPS_EN_ACTIVE
#define GPS_EN_ACTIVE 1
#endif

// Allow defining the polarity of the STANDBY output.  default is LOW for standby
#ifndef GPS_STANDBY_ACTIVE
#define GPS_STANDBY_ACTIVE LOW
#endif

static constexpr uint32_t GPS_UPDATE_ALWAYS_ON_THRESHOLD_MS = 10 * 1000UL;
static constexpr uint32_t GPS_FIX_HOLD_MAX_MS = 20000;
// C/N0 at which a satellite is realistically usable in a position solution (dB-Hz). Below roughly
// this, receivers can list a satellite and still not be able to use it.
static constexpr uint8_t GPS_GSV_TRACKED_SNR_MIN = 25;

typedef enum {
    GNSS_MODEL_ATGM336H,
    GNSS_MODEL_MTK,
    GNSS_MODEL_UBLOX6,
    GNSS_MODEL_UBLOX7,
    GNSS_MODEL_UBLOX8,
    GNSS_MODEL_UBLOX9,
    GNSS_MODEL_UBLOX10,
    GNSS_MODEL_UC6580,
    GNSS_MODEL_UNKNOWN,
    GNSS_MODEL_MTK_L76B,
    GNSS_MODEL_MTK_PA1010D,
    GNSS_MODEL_MTK_PA1616S,
    GNSS_MODEL_AG3335,
    GNSS_MODEL_AG3352,
    GNSS_MODEL_LS20031,
    GNSS_MODEL_CM121,
    GNSS_MODEL_GENERIC_NMEA // generic NMEA source (e.g. gpsd); skips chip-specific probe and init
} GnssModel_t;

typedef enum {
    GNSS_RESPONSE_NONE,
    GNSS_RESPONSE_NAK,
    GNSS_RESPONSE_FRAME_ERRORS,
    GNSS_RESPONSE_OK,
} GPS_RESPONSE;

enum GPSPowerState : uint8_t {
    GPS_ACTIVE,    // Awake and want a position
    GPS_IDLE,      // Awake, but not wanting another position yet
    GPS_SOFTSLEEP, // Physically powered on, but soft-sleeping
    GPS_HARDSLEEP, // Physically powered off, but scheduled to wake
    GPS_OFF        // Powered off indefinitely
};

struct ChipInfo {
    String chipName;        // The name of the chip (for logging)
    String detectionString; // The string to match in the response
    GnssModel_t driver;     // The driver to use
};
/**
 * A gps class that only reads from the GPS periodically and keeps the gps powered down except when reading
 *
 * When new data is available it will notify observers.
 */
class GPS : private concurrency::OSThread
{
  public:
    meshtastic_Position p = meshtastic_Position_init_default;

    /** This is normally bound to config.position.gps_en_gpio but some rare boards (like heltec tracker) need more advanced
     * implementations. Those boards will set this public variable to a custom implementation.
     *
     * Normally set by GPS::createGPS()
     */
    GpioVirtPin *enablePin = NULL;

    // Push coarse aiding into the module: system clock (UBX-MGA-INI-TIME_UTC) and last known
    // position (UBX-MGA-INI-POS_LLH). The integration-manual-prescribed pattern for
    // RTC-crystal-less designs like the T-Deck Max: the module cannot keep time through
    // VCC-off, but BBR ephemeris/almanac + aided time&position = warm starts (visibility and
    // Doppler prediction instead of a blind sky search). Skipped entirely when RTC quality is
    // GPS (the module is the source); time part additionally needs quality >= Device, position
    // part a nonzero localPosition. Fire-and-forget; the module weighs both by their accuracy
    // fields (+/-10s, +/-50km - honest for a portable device that may have moved since).
    void injectAidingToModule(const char *reason);

    // Recover time a RUNNING module still holds (NAV-PVT poll, pre-fix): only pays off when the
    // GPS rail stayed powered across an ESP reset - the RTC-crystal-less module then still
    // counts real time, but its pre-fix NMEA time fields are empty. First ~10s of the first
    // active window per boot.
    void pollModuleHeldTime();

    virtual ~GPS();

    /** We will notify this observable anytime GPS state has changed meaningfully */
    Observable<const meshtastic::GPSStatus *> newStatus;

    /**
     * Returns true if we succeeded
     */
    virtual bool setup();

    // re-enable the thread
    void enable();

    // Disable the thread
    int32_t disable() override;

    // Returns if the thread is enabled
    bool isEnabled();

    // toggle between enabled/disabled
    void toggleGpsMode();

    // Change the power state of the GPS - for power saving / shutdown
    void setPowerState(GPSPowerState newState, uint32_t sleepMs = 0);

    /// Returns true if we have acquired GPS lock.
    virtual bool hasLock();

    /// Return true if we are connected to a GPS
    bool isConnected() const { return hasGPS; }

    bool isPowerSaving() const { return config.position.gps_mode != meshtastic_Config_PositionConfig_GpsMode_ENABLED; }

    // Empty the input buffer as quickly as possible
    void clearBuffer();

    // Creates an instance of the GPS class.
    // Returns the new instance or null if the GPS is not present.
    static std::unique_ptr<GPS> createGps();

    // Wake the GPS hardware - ready for an update
    void up();

    // Let the GPS hardware save power between updates
    void down();

    // Reader/gate state captured when a search window ends. The per-window WARN in down() prints
    // the same fields to serial; a headless node can forward this snapshot over the
    // mesh so a remote diagnostic query can tell apart the module states a fixless window can hide
    // (total silence / quality-0 streaming / fix-with-garbage-date / bogus hdop / RF).
    struct WindowDiag {
        uint32_t windows = 0;   // search windows ended since boot (0 = no window yet, fields invalid)
        uint32_t durationS = 0; // how long the window ran
        uint32_t nmeaOk = 0;    // checksum-valid NMEA sentences during the window
        uint32_t nmeaBad = 0;   // checksum-failed sentences during the window
        uint8_t fixQual = 0;    // GGA fix quality at window end
        uint8_t sats = 0;       // satellites used (GGA) at window end
        uint32_t hdop = 0;      // TinyGPS hdop.value(), hundredths (0 = none/bogus)
        uint32_t locAgeS = 0;   // reader.location.age() at window end (99999 = never valid)
        uint32_t dateAgeS = 0;  // reader.date.age() at window end (99999 = never valid)
        bool accepted = false;  // this window published a fix
        uint8_t fixless = 0;    // consecutive fixless windows, as of this window end
        uint8_t wedgeResets = 0; // silence-watchdog hardware recoveries during the window
        uint8_t gsvInView = 0;  // satellites the module REPORTS SEEING (GSV), 255 = GSV diag not on
        uint8_t gsvMaxSnr = 0;  // strongest C/N0 in that GSV set, dB-Hz (0 = nothing audible)
        uint8_t gsvTracked = 0; // satellites at a USABLE C/N0; a fix needs 4
        // LoRa transmissions during the window. The GPS patch antenna on this board sits centimetres
        // from the LoRa antenna, so TX desense is a live theory - and this FALSIFIES it cheaply: a
        // window with tx 0 that still saw no satellite cannot have been desensed by our own radio.
        uint32_t txDuring = 0;
    };
    const WindowDiag &lastWindowDiag() const { return lastWindowDiag_; }

    // Current reader/gate state for a remote status query. Unlike WindowDiag (frozen
    // at the last window end) this is live, and carries the per-gate rejection counters that name
    // WHICH acceptance gate has been discarding data since boot - the difference between "module
    // silent", "streams but never fixes", "fixes but garbage date" and "fix rejected on quality"
    // is otherwise invisible from the field.
    struct LiveDiag {
        uint8_t powerState;      // GPSPowerState
        bool hasGps;             // probe found a module this boot
        bool gotTime;            // this window produced a plausible GPS date/time
        bool hasValidLoc;        // reader currently holds an acceptable fix
        uint32_t windowElapsedS; // current search window age (0 when not GPS_ACTIVE)
        uint32_t nmeaOk;         // checksum-valid sentences since boot
        uint32_t nmeaBad;        // checksum-failed sentences since boot
        uint8_t fixQual;         // GGA fix quality, last parsed
        uint8_t sats;            // satellites used (GGA), last parsed
        uint32_t hdop;           // TinyGPS hdop.value(), hundredths
        uint32_t locAgeS;        // 99999 = never valid
        uint32_t dateAgeS;       // 99999 = never valid
        uint8_t fixless;         // consecutive fixless windows
        uint32_t windowsTotal;   // search windows ended since boot
        uint32_t windowsAccepted; // ... of which published a fix
        uint32_t hwResets;       // RESET-pin pulses since boot (watchdog + escalation + probe + forced)
        uint32_t rejDate;        // fix-with-implausible-date rejections (lookForTime)
        uint32_t rejStale;       // solution-too-old rejections (lookForLocation)
        uint32_t rejHdop;        // bogus-hdop rejections
        uint32_t rejCoord;       // out-of-range lat/lon rejections
        uint32_t rtcFallbackPub; // fixes published with mesh-RTC time (garbage-date fallback)
        uint8_t gsvInView;       // satellites-in-view per GSV, 255 = GSV diagnostics not enabled
        uint8_t gsvMaxSnr;       // strongest C/N0 seen, dB-Hz
        uint8_t gsvTracked;      // satellites at a USABLE C/N0; a fix needs 4
    };
    LiveDiag liveDiag(); // not const: TinyGPS value() reads clear the field's updated flag

    // Cheap poll for a proactive health alert - liveDiag() is NOT suitable for polling
    // (its TinyGPS reads mutate parser update flags every call).
    uint8_t fixlessWindowCount() const { return fixlessWindows; }

    // Pulse the module RESET pin and re-send the full config at the next hard wake, regardless of the
    // fixlessWindows escalation threshold. Lets a user-forced search always exercise
    // the complete recovery ladder instead of waiting for two scheduled windows to fail first.
    void requestHardRecovery() { hardRecoveryRequested = true; }

  private:
    GPS() : concurrency::OSThread("GPS") {}

    /// Record that we have a GPS
    void setConnected();

    /** Subclasses should look for serial rx characters here and feed it to their GPS parser
     *
     * Return true if we received a valid message from the GPS
     */
    virtual bool whileActive();

    /**
     * Perform any processing that should be done only while the GPS is awake and looking for a fix.
     * Override this method to check for new locations
     *
     * @return true if we've acquired a time
     */
    virtual bool lookForTime();

    /**
     * Perform any processing that should be done only while the GPS is awake and looking for a fix.
     * Override this method to check for new locations
     *
     * @return true if we've acquired a new location
     */
    virtual bool lookForLocation();
    // Load persisted GPS model+baud from /prefs.
    bool loadProbeCache();
    // Clear persisted GPS model+baud cache.
    void clearProbeCache();
    // Persist the currently detected GPS model+baud.
    bool saveProbeCache() const;
    // Verify the cached model+baud still maps to a live GPS device.
    bool verifyCachedProbePresence();
    // Pulse PIN_GPS_RESET (if wired) long enough to hard-reset the GNSS module.
    void hardwareReset();
    // Re-send the model-specific runtime configuration. includeSystemConfig also
    // re-sends commands that reset the receiver (constellation setup).
    void reapplyModuleConfig(bool includeSystemConfig);
    // Scan one completed NMEA line for GSV and record satellites-in-view / peak C/N0.
    // GSV is normally disabled in the module config (bandwidth), so this only ever sees
    // data while the fixless escalation has switched the diagnostic stream on.
    void scanLineForGsv(const char *line, unsigned int len);
    // Detect and recover a module that has gone silent while GPS_ACTIVE.
    void checkWedgeWatchdog();

    GnssModel_t gnssModel = GNSS_MODEL_UNKNOWN;
    int32_t detectedBaud = GPS_BAUDRATE;
    int32_t cachedProbeBaud = 0;
    GnssModel_t cachedProbeModel = GNSS_MODEL_UNKNOWN;

    TinyGPSPlus reader;
    uint8_t fixQual = 0; // fix quality from GPGGA
    uint32_t lastChecksumFailCount = 0;
    uint8_t currentStep = 0;
    int32_t currentDelay = 2000;
    bool gotTime = false;

#ifndef TINYGPS_OPTION_NO_CUSTOM_FIELDS
    // (20210908) TinyGps++ can only read the GPGSA "FIX TYPE" field
    // via optional feature "custom fields", currently disabled (bug #525)
    TinyGPSCustom gsafixtype; // custom extract fix type from GPGSA
    TinyGPSCustom gsapdop;    // custom extract PDOP from GPGSA
    uint8_t fixType = 0;      // fix type from GPGSA
#endif

    uint32_t fixHoldEnds = 0;
    uint32_t rx_gpio = 0;
    uint32_t tx_gpio = 0;

    uint8_t speedSelect = 0;
    uint8_t probeTries = 0;
    // Cache file is successfully loaded.
    bool hasProbeCache = false;
    // Ensures cached probe is attempted once per boot.
    bool triedProbeCache = false;

    // Wedge watchdog: a healthy module streams valid NMEA whenever GPS_ACTIVE.
    // The UC6580 can stop emitting entirely after days of power cycling and only a
    // hardware reset revives it (upstream #5088), so sustained silence is actionable.
    uint32_t silenceStartedMsec = 0;      // start of the current no-valid-NMEA span (0 = re-arm)
    uint32_t sentencesAtSilenceStart = 0; // reader.passedChecksum() when the span began
    uint8_t wedgeRecoveryAttempts = 0;    // hardware recoveries attempted this active window
    uint32_t probeRetryCount = 0;         // failed full probe walks since boot
    // The silence watchdog only catches a module that stops TALKING. A #5088-class UC6580 can
    // instead keep streaming checksum-valid NMEA while never producing an acceptable fix
    // (quality-0 GGA, dropped RMC, hdop 0...), which is invisible to it. Count consecutive
    // search windows that ended without a published fix so setPowerState() can escalate.
    uint8_t fixlessWindows = 0;
    // "This window published an accepted fix" - tracked explicitly because neither latched flag
    // can answer that at down() time: hasValidLocation survives from earlier windows (and on an
    // always-on node the tooLong final tick zeroes it AFTER a whole window of good publishes),
    // and gotTime can flip true from stale reader state before the module says a word.
    bool windowSawAcceptedFix = false;
    // Per-window baselines for the WindowDiag snapshot: the TinyGPS counters are boot totals, and
    // "did the module talk THIS window" needs the delta. Stamped at ACTIVE entry, read at down().
    uint32_t nmeaOkAtWindowStart = 0;
    uint32_t nmeaBadAtWindowStart = 0;
    uint32_t windowStartedMs = 0;
    WindowDiag lastWindowDiag_;
    // One-shot flag from requestHardRecovery(), consumed at the next hard wake.
    bool hardRecoveryRequested = false;
    // Boot-lifetime totals for LiveDiag: per-gate rejection counters and recovery activity.
    uint32_t rejDate = 0;
    uint32_t rejStale = 0;
    uint32_t rejHdop = 0;
    uint32_t rejCoord = 0;
    uint32_t hwResets = 0;
    uint32_t windowsAccepted = 0;
    uint32_t rtcFallbackPub = 0;
    // GSV ("what can the antenna actually hear") diagnostics. Satellites-in-view and C/N0 answer
    // the one question the normal fields cannot: a module reporting sats 0 / hdop 99.99 looks
    // identical whether its antenna is dead or it simply cannot solve a position from signals it
    // does hear. GSV is left OFF in the steady state (it is several extra sentences per second)
    // and switched on only once the node is already failing - so the healthy path is unchanged.
    bool gsvDiagOn = false;    // GSV currently enabled in the module config
    uint8_t gsvInView = 255;   // 255 = no GSV data seen yet; largest per-constellation count
    uint8_t gsvMaxSnr = 0;     // peak C/N0 across the current window, dB-Hz
    uint8_t gsvTracked = 0;    // most satellites at >= GPS_GSV_TRACKED_SNR_MIN in one constellation
    uint8_t gsvGroupTracked = 0; // running tally within the GSV group being parsed
    uint32_t txAtWindowStart = 0; // RadioLibInterface txGood baseline for WindowDiag::txDuring

    /**
     * hasValidLocation - indicates that the position variables contain a complete
     *   GPS location, valid and fresh (< gps_update_interval + position_broadcast_secs)
     */
    bool hasValidLocation = false; // default to false, until we complete our first read

    bool shouldPublish = false; // If we've changed GPS state, this will force a publish the next loop()

    bool hasGPS = false; // Do we have a GPS we are talking to

    bool GPSInitFinished = false; // Init thread finished?
    bool GPSInitStarted = false;  // Init thread finished?

    GPSPowerState powerState = GPS_OFF; // GPS_ACTIVE if we want a location right now

    uint8_t numSatellites = 0;

    CallbackObserver<GPS, void *> notifyDeepSleepObserver = CallbackObserver<GPS, void *>(this, &GPS::prepareDeepSleep);

    /** If !NULL we will use this serial port to construct our GPS */
#if defined(ARCH_RP2040)
    static SerialUART *_serial_gps;
#elif defined(ARCH_NRF52)
    static Uart *_serial_gps;
#else
    static HardwareSerial *_serial_gps;
#endif

    // Create a ublox packet for editing in memory
    uint8_t makeUBXPacket(uint8_t class_id, uint8_t msg_id, uint8_t payload_size, const uint8_t *msg);
    uint8_t makeCASPacket(uint8_t class_id, uint8_t msg_id, uint8_t payload_size, const uint8_t *msg);

    // scratch space for creating ublox packets
    uint8_t UBXscratch[250] = {0};

    int rebootsSeen = 0;

    int getACK(uint8_t *buffer, uint16_t size, uint8_t requestedClass, uint8_t requestedID, uint32_t waitMillis);
    GPS_RESPONSE getACK(uint8_t c, uint8_t i, uint32_t waitMillis);
    GPS_RESPONSE getACK(const char *message, uint32_t waitMillis);

    GPS_RESPONSE getACKCas(uint8_t class_id, uint8_t msg_id, uint32_t waitMillis);

    /// Prepare the GPS for the cpu entering deep sleep, expect to be gone for at least 100s of msecs
    /// always returns 0 to indicate okay to sleep
    int prepareDeepSleep(void *unused);

    /** Set power with EN pin, if relevant
     */
    void writePinEN(bool on);

    /** Set the value of the STANDBY pin, if relevant
     */
    void writePinStandby(bool standby);

    /** Set GPS power with PMU, if relevant
     */
    void setPowerPMU(bool on);

    /** Set UBLOX power, if relevant
     */
    void setPowerUBLOX(bool on, uint32_t sleepMs = 0);

    /**
     * Tell users we have new GPS readings
     */
    void publishUpdate();

    virtual int32_t runOnce() override;

    GnssModel_t getProbeResponse(unsigned long timeout, const std::vector<ChipInfo> &responseMap, int serialSpeed);

    // Get GNSS model
    GnssModel_t probe(int serialSpeed);

    // delay counter to allow more sats before fixed position stops GPS thread
    uint8_t fixeddelayCtr = 0;
};

extern std::unique_ptr<GPS> gps;
#endif // Exclude GPS
