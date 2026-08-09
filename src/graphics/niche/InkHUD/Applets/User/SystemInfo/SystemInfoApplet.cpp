#ifdef MESHTASTIC_INCLUDE_INKHUD

#include "./SystemInfoApplet.h"

#include "DisplayFormatters.h"
#include "GPSStatus.h"
#include "NodeDB.h"
#include "NodeStatus.h"
#include "PowerStatus.h"
#include "airtime.h"
#include "memGet.h"
#include "mesh/RadioInterface.h" // myRegion (region name)
#ifdef ARCH_ESP32
#include "sleep.h" // lightSleepMsTotal / lightSleepWakes (sleep-duty diagnostics)
#endif
#if defined(HAS_BQ27220) || defined(XPOWERS_CHIP_SY6970)
#include "Power.h" // bq27220 gauge accessors (capital P: case-sensitive FS)
#endif

using namespace NicheGraphics;

InkHUD::SystemInfoApplet::SystemInfoApplet() : concurrency::OSThread("SysInfo")
{
    OSThread::setIntervalFromNow(60 * 1000UL);
}

int32_t InkHUD::SystemInfoApplet::runOnce()
{
    // Battery sampling runs regardless of visibility so the discharge window is already
    // populated when the user opens the applet
    sampleBattery();

    // Keep the displayed battery / utilization / uptime fresh, but only spend e-ink
    // refreshes while we're actually the visible applet
    if (isForeground())
        requestUpdate();
#if defined(T_DECK_MAX)
    // Debug Hold (Hardware menu): near-live cadence while watching the charger/gauge rows -
    // they read the chips at render time, so every refresh is a fresh measurement. Costs
    // FAST-refresh wear; acceptable for a deliberate, session-only debug mode.
    if (tdeckmaxDebugHold && isForeground())
        return 5 * 1000UL;
#endif
    return 60 * 1000UL;
}

void InkHUD::SystemInfoApplet::sampleBattery()
{
    if (!powerStatus || !powerStatus->getHasBattery() || powerStatus->getHasUSB()) {
        battCount = 0; // no battery, or charging: any accumulated discharge slope is void
#if defined(HAS_BQ27220)
        rcBaselineMs = 0; // charging also voids the coulomb-counter drain window (see onRender)
#endif
        return;
    }

    const uint32_t now = millis();
    int soc = powerStatus->getBatteryChargePercent();
    if (soc < 0)
        soc = 0;
    if (soc > 100)
        soc = 100;

    if (battCount > 0) {
        const BattSample &newest = battSamples[(uint8_t)((battHead + battCount - 1) % BATT_SAMPLES)];
        if (now < newest.ms || (uint8_t)soc > newest.soc + 1) {
            battCount = 0; // millis wrapped (~49d), or SoC jumped up (charged while we napped)
        } else if ((uint32_t)(now - newest.ms) < BATT_SAMPLE_INTERVAL_MS) {
            return; // not due yet
        }
    }

    if (battCount == BATT_SAMPLES) { // full: drop the oldest
        battHead = (uint8_t)((battHead + 1) % BATT_SAMPLES);
        battCount--;
    }
    battSamples[(uint8_t)((battHead + battCount) % BATT_SAMPLES)] = {now, (uint8_t)soc};
    battCount++;
    lastBattSampleMs = now;
}

bool InkHUD::SystemInfoApplet::battEstimate(uint16_t &tenthsPerHour, uint32_t &minutesLeft)
{
    if (battCount < 2)
        return false;

    const BattSample &oldest = battSamples[battHead];
    const BattSample &newest = battSamples[(uint8_t)((battHead + battCount - 1) % BATT_SAMPLES)];
    if ((uint32_t)(newest.ms - oldest.ms) < BATT_MIN_SPAN_MS)
        return false; // too little history for a stable fit

    // Least-squares slope of soc (%) over time (minutes), across the whole window
    float sumX = 0, sumY = 0, sumXX = 0, sumXY = 0;
    for (uint8_t i = 0; i < battCount; i++) {
        const BattSample &s = battSamples[(uint8_t)((battHead + i) % BATT_SAMPLES)];
        const float x = (float)(uint32_t)(s.ms - oldest.ms) / 60000.0f; // minutes since window start
        const float y = s.soc;
        sumX += x;
        sumY += y;
        sumXX += x * x;
        sumXY += x * y;
    }
    const float n = battCount;
    const float denom = n * sumXX - sumX * sumX;
    if (denom <= 0)
        return false;
    const float slopePerMin = (n * sumXY - sumX * sumY) / denom; // %/minute, negative when draining

    const float ratePerHour = -slopePerMin * 60.0f;
    if (ratePerHour < 0.05f)
        return false; // flat (or rising): no meaningful drain to project

    tenthsPerHour = (uint16_t)(ratePerHour * 10.0f + 0.5f);
    minutesLeft = (uint32_t)((float)newest.soc / -slopePerMin);
    return true;
}

void InkHUD::SystemInfoApplet::onRender(bool full)
{
    setFont(fontSmall);
    drawHeader(std::string("Info - ") + owner.short_name);

    const int16_t rowH = fontSmall.lineHeight() + 2;
    int16_t y = Applet::getHeaderHeight() + 2;
    char buf[64];

    // Print one row, clipping cleanly on a half-height tile
    auto row = [&](const char *text) {
        if (y + rowH > height())
            return;
        printAt(0, y, text);
        y += rowH;
    };

    // Battery: percent, voltage, charge source
    if (powerStatus && powerStatus->getHasBattery()) {
        const uint32_t mv = powerStatus->getBatteryVoltageMv();
        snprintf(buf, sizeof(buf), "Batt %d%% %lu.%02luV%s", powerStatus->getBatteryChargePercent(), (unsigned long)(mv / 1000),
                 (unsigned long)((mv % 1000) / 10), powerStatus->getHasUSB() ? " USB" : "");
    } else {
        snprintf(buf, sizeof(buf), "Batt: none (USB power)");
    }
    row(buf);

    // Battery life estimate. Two sources, complementary:
    //  - BQ27220 fuel gauge: hardware coulomb counter, keeps integrating through light sleep, so
    //    its TimeToFull/TimeToEmpty are valid seconds after boot. TimeToEmpty rides the gauge's
    //    short averaging window (and an unlearned full-charge capacity), so it wanders more.
    //  - Discharge slope (sampleBattery/battEstimate): needs a long warm-up but averages the true
    //    duty cycle over the whole window - the steadier long-horizon number, kept as primary.
    // Charging shows the gauge's time-to-full; on battery the gauge fills the slope's warm-up gap.
    if (powerStatus && powerStatus->getHasBattery()) {
        uint16_t tenthsPerHour;
        uint32_t minutesLeft;
#if defined(HAS_BQ27220)
        uint16_t ttfMin = 0xFFFF, tteMin = 0xFFFF;
        const bool haveGauge = bq27220GetGaugeTimes(ttfMin, tteMin);
#endif
        if (powerStatus->getHasUSB()) {
#if defined(HAS_BQ27220)
            int16_t chgMa = 0;
            uint16_t rcNowMah = 0;
            const bool haveLive = bq27220GetGaugeLive(chgMa, rcNowMah);
            if (haveGauge && ttfMin > 0 && ttfMin != 0xFFFF) {
                if (ttfMin >= 60)
                    snprintf(buf, sizeof(buf), "Chg: full in %uh %02um", (unsigned)(ttfMin / 60), (unsigned)(ttfMin % 60));
                else
                    snprintf(buf, sizeof(buf), "Chg: full in %um", (unsigned)ttfMin);
            } else if (haveGauge && (!powerStatus->getIsCharging() || (haveLive && chgMa <= 25))) {
                // TTF == 65535 means the gauge sees no meaningful charge current (TI semantics:
                // TimeToFull invalidates once AverageCurrent <= 0). The SY6970 status bit keeps
                // reporting "charging" through the CV taper / float (seen on hardware: TTF=65535
                // with isCharging=1 at 97%), which left this row stuck on "Est: charging" at
                // full. Trust the coulomb counter instead: near-zero battery current on USB
                // power = effectively full.
                snprintf(buf, sizeof(buf), "Chg: complete");
            } else
#endif
                snprintf(buf, sizeof(buf), "Est: charging");
        } else if (battEstimate(tenthsPerHour, minutesLeft)) {
            if (minutesLeft >= 14 * 24 * 60)
                snprintf(buf, sizeof(buf), "Est >14d  -%u.%u%%/h", tenthsPerHour / 10, tenthsPerHour % 10);
            else if (minutesLeft >= 24 * 60)
                snprintf(buf, sizeof(buf), "Est %lud %luh  -%u.%u%%/h", (unsigned long)(minutesLeft / (24 * 60)),
                         (unsigned long)((minutesLeft % (24 * 60)) / 60), tenthsPerHour / 10, tenthsPerHour % 10);
            else
                snprintf(buf, sizeof(buf), "Est %luh %lum  -%u.%u%%/h", (unsigned long)(minutesLeft / 60),
                         (unsigned long)(minutesLeft % 60), tenthsPerHour / 10, tenthsPerHour % 10);
        }
#if defined(HAS_BQ27220)
        else if (haveGauge && tteMin > 0 && tteMin != 0xFFFF) {
            // Slope still warming up: show the gauge's own prediction meanwhile
            if (tteMin >= 14 * 24 * 60)
                snprintf(buf, sizeof(buf), "Est >14d (gauge)");
            else if (tteMin >= 24 * 60)
                snprintf(buf, sizeof(buf), "Est %ud %uh (gauge)", (unsigned)(tteMin / (24 * 60)),
                         (unsigned)((tteMin % (24 * 60)) / 60));
            else
                snprintf(buf, sizeof(buf), "Est %uh %um (gauge)", (unsigned)(tteMin / 60), (unsigned)(tteMin % 60));
        }
#endif
        else {
            snprintf(buf, sizeof(buf), "Est: measuring...");
        }
        row(buf);
    }

#if defined(HAS_BQ27220)
    // Measured battery current from the coulomb counter (10mOhm sense, R85). "Avg" is the
    // delta-mAh drained since the last unplug/charge event divided by elapsed time - the gauge
    // integrates through light sleep on battery power, so this is the true average consumption
    // including naps: the "what does it really draw unplugged" number a USB meter can't see.
    // "Cur" is the instantaneous (~1s) reading; while USB is present it shows the CHARGE current
    // instead (system runs from VBUS there, so no drain window is kept).
    if (powerStatus && powerStatus->getHasBattery()) {
        int16_t nowMa;
        uint16_t rcMah;
        if (bq27220GetGaugeLive(nowMa, rcMah)) {
            const bool onUsb = powerStatus->getHasUSB();
            if (onUsb || (rcBaselineMs != 0 && rcMah > rcBaselineMah))
                rcBaselineMs = 0; // USB present, or capacity rose (charged unseen): window void
            if (!onUsb && rcBaselineMs == 0) {
                rcBaselineMah = rcMah;
                rcBaselineMs = millis() | 1; // |1 keeps 0 free as the "no window" sentinel
            }
            const uint32_t elapsedMs = rcBaselineMs ? (uint32_t)(millis() - rcBaselineMs) : 0;
            if (onUsb) {
                snprintf(buf, sizeof(buf), "Cur %+dmA (USB)", (int)nowMa);
            } else if (elapsedMs >= 10 * 60 * 1000UL && rcMah <= rcBaselineMah) {
                // tenths of mA = drained mAh * 3.6e6 ms/h * 10 / elapsed ms (64-bit: <=1500*3.6e7)
                const uint32_t tenths = (uint32_t)(((uint64_t)(rcBaselineMah - rcMah) * 36000000ULL) / elapsedMs);
                snprintf(buf, sizeof(buf), "Cur %dmA  Avg %lu.%lumA", (int)nowMa, (unsigned long)(tenths / 10),
                         (unsigned long)(tenths % 10));
            } else {
                snprintf(buf, sizeof(buf), "Cur %dmA  Avg <10min", (int)nowMa);
            }
            row(buf);
        }
    }
#endif

    // Mesh: node counts + channel utilization (RX) and our TX airtime duty
    {
        const int chTenths = airTime ? (int)(airTime->channelUtilizationPercent() * 10 + 0.5f) : 0;
        const int txTenths = airTime ? (int)(airTime->utilizationTXPercent() * 10 + 0.5f) : 0;
        snprintf(buf, sizeof(buf), "Nodes %u/%u  Util %d.%d%%", nodeStatus ? nodeStatus->getNumOnline() : 0,
                 nodeStatus ? nodeStatus->getNumTotal() : 0, chTenths / 10, chTenths % 10);
        row(buf);
        snprintf(buf, sizeof(buf), "AirTX %d.%d%%  Pwr %ddBm", txTenths / 10, txTenths % 10, (int)config.lora.tx_power);
        row(buf);
    }

    // Radio config: region, modem preset, frequency slot
    snprintf(buf, sizeof(buf), "%s %s slot %u", myRegion ? myRegion->name : "??",
             DisplayFormatters::getModemPresetDisplayName(config.lora.modem_preset, true, config.lora.use_preset),
             (unsigned)config.lora.channel_num);
    row(buf);

    // GPS: off / searching / fix with coordinates (1e-7 fixed point -> 4 decimals)
    if (config.position.gps_mode != meshtastic_Config_PositionConfig_GpsMode_ENABLED) {
        snprintf(buf, sizeof(buf), "GPS off");
    } else if (gpsStatus && gpsStatus->getHasLock()) {
        const int32_t lat = gpsStatus->getLatitude();
        const int32_t lng = gpsStatus->getLongitude();
        snprintf(buf, sizeof(buf), "GPS %usat %ld.%04ld %ld.%04ld", gpsStatus->getNumSatellites(), (long)(lat / 10000000),
                 (long)((labs(lat) % 10000000) / 1000), (long)(lng / 10000000), (long)((labs(lng) % 10000000) / 1000));
    } else {
        snprintf(buf, sizeof(buf), "GPS searching (%usat)", gpsStatus ? gpsStatus->getNumSatellites() : 0);
    }
    row(buf);

    // Wall clock, only when the RTC actually knows the time (GPS / mesh / phone sync).
    // getTimeString() applies the timezone and the 12/24h preference, and returns ""
    // while the RTC is still cold - no row at all in that case. Freshness rides the
    // 60s runOnce cadence (and the sleep render gating), same as the menu's clock.
    {
        std::string clock = getTimeString();
        if (!clock.empty()) {
            snprintf(buf, sizeof(buf), "Time %s", clock.c_str());
            row(buf);
        }
    }

    // Uptime (millis-based: wraps after ~49 days) + firmware version
    {
        const uint32_t up = millis() / 1000;
        snprintf(buf, sizeof(buf), "Up %lud %02lu:%02lu  v%s", (unsigned long)(up / 86400), (unsigned long)((up % 86400) / 3600),
                 (unsigned long)((up % 3600) / 60), optstr(APP_VERSION));
        row(buf);
    }

#ifdef ARCH_ESP32
    // Light-sleep duty since boot: the field-visible power diagnostic. Healthy idle should show
    // a high percentage; a low number with the screen mostly "asleep" means something is holding
    // the CPU awake (e.g. the BLE wait_bluetooth_secs DARK-parking failure this row was born from).
    {
        const uint32_t upMs = millis();
        const uint32_t pct = upMs ? (uint32_t)(((uint64_t)lightSleepMsTotal * 100) / upMs) : 0;
        snprintf(buf, sizeof(buf), "Sleep %lu%%  %lu wakes", (unsigned long)pct, (unsigned long)lightSleepWakes);
        row(buf);
    }

    // Nap profile: awake time between sleeps (packet servicing). avg tells whether wakes are
    // lean; max exposes outliers (telemetry TX, flash saves). User sessions (>60s awake) excluded.
    if (lightSleepNapCount > 0) {
        snprintf(buf, sizeof(buf), "Nap avg %lums max %lums", (unsigned long)(lightSleepNapMsSum / lightSleepNapCount),
                 (unsigned long)lightSleepNapMsMax);
        row(buf);
    }
#endif

    // Memory: free heap / free PSRAM, each with the percentage of its total still free
    // (free-only numbers can't show how close to exhaustion we are). PSRAM in tenths of
    // a MB keeps the row narrow enough for the 240px tile alongside both percentages.
    {
        const uint32_t heapFree = memGet.getFreeHeap();
        const uint32_t heapTotal = memGet.getHeapSize();
        const uint32_t heapPct = heapTotal ? (uint32_t)(((uint64_t)heapFree * 100) / heapTotal) : 0;
        const uint32_t psramFree = memGet.getFreePsram();
        const uint32_t psramTotal = memGet.getPsramSize();
        if (psramTotal > 0) {
            const uint32_t pmTenths = ((psramFree / 1024) * 10) / 1024; // free PSRAM, tenths of a MB
            const uint32_t psramPct = (uint32_t)(((uint64_t)psramFree * 100) / psramTotal);
            snprintf(buf, sizeof(buf), "Heap %luk %lu%%  PSRAM %lu.%luM %lu%%", (unsigned long)(heapFree / 1024),
                     (unsigned long)heapPct, (unsigned long)(pmTenths / 10), (unsigned long)(pmTenths % 10),
                     (unsigned long)psramPct);
        } else {
            snprintf(buf, sizeof(buf), "Heap %luk free (%lu%%)", (unsigned long)(heapFree / 1024), (unsigned long)heapPct);
        }
        row(buf);
    }

    // Identity: node id + role
    snprintf(buf, sizeof(buf), "!%08lx %s", (unsigned long)myNodeInfo.my_node_num,
             DisplayFormatters::getDeviceRole(config.device.role));
    row(buf);
}

#endif
