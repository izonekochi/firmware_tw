#ifdef MESHTASTIC_INCLUDE_INKHUD

/*

Device information applet, in the spirit of MUI's home screen: firmware, uptime, battery,
node counts, radio config, channel/air utilization, GPS state, and memory, on one page.

Redraws once a minute while foreground (via OSThread) so battery/utilization/uptime stay
fresh without burning e-ink refreshes in the background.

*/

#pragma once

#include "configuration.h"

#include "concurrency/OSThread.h"
#include "graphics/niche/InkHUD/Applet.h"

namespace NicheGraphics::InkHUD
{

class SystemInfoApplet : public Applet, public concurrency::OSThread
{
  public:
    SystemInfoApplet();

    void onRender(bool full) override;

  protected:
    int32_t runOnce() override;

  private:
    // --- Battery life estimator -------------------------------------------------------------
    // Ring buffer of (millis, soc%) samples at 5-minute cadence, up to a 6-hour window. The
    // discharge slope is a least-squares fit over the window, which averages out the 1%-step
    // SoC granularity and the irregular sampling caused by light sleep (millis() runs on
    // esp_timer, so timestamps keep advancing through LS). Any USB power, charge jump, or
    // millis wrap resets the window: the slope is only meaningful for a monotonic discharge.
    struct BattSample {
        uint32_t ms;
        uint8_t soc;
    };
    static constexpr uint8_t BATT_SAMPLES = 72;                       // 6h at 5-min cadence
    static constexpr uint32_t BATT_SAMPLE_INTERVAL_MS = 5 * 60 * 1000UL;
    static constexpr uint32_t BATT_MIN_SPAN_MS = 45 * 60 * 1000UL;    // fit needs >=45min of data

    void sampleBattery();
    // Returns true with the discharge rate (tenths of %/hour, positive) and the projected
    // minutes until 0%; false while charging / insufficient data / negligible slope.
    bool battEstimate(uint16_t &tenthsPerHour, uint32_t &minutesLeft);

    BattSample battSamples[BATT_SAMPLES];
    uint8_t battCount = 0;
    uint8_t battHead = 0;
    uint32_t lastBattSampleMs = 0;

#if defined(HAS_BQ27220)
    // --- Measured-drain recorder (BQ27220 coulomb counter, 10mOhm sense) ---------------------
    // RemainingCapacity baseline for the unplugged average-drain readout. The gauge integrates
    // through light sleep on battery power, so delta-mAh / delta-t is the TRUE average
    // consumption including naps - no fit window needed, unlike the SoC slope above. The window
    // is invalidated whenever USB power is seen (sampleBattery, which ticks every wake window
    // regardless of foreground) or capacity ever RISES (charged while we weren't looking).
    uint16_t rcBaselineMah = 0;
    uint32_t rcBaselineMs = 0; // 0 = no valid window open
#endif
};

} // namespace NicheGraphics::InkHUD

#endif
