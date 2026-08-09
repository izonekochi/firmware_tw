/*

E-Ink display driver
    - GDEQ031T10
    - Controller: UC8253 (UC8175 family: shared 0x00 PSR, 0x10/0x13 image RAM,
      0x12 refresh, 0x50 CDI, 0x90/0x91/0x92 partial-window commands, 0x04/0x02
      power on/off, active-LOW BUSY)
    - Size: 3.1 inch
    - Resolution: 240px x 320px (native portrait)

    Unlike GDEW0102T4 (also a UC8175-family subclass) this panel does NOT need
    register LUTs: the GxEPD2 reference driver (GxEPD2_310_GDEQ031T10.cpp) drives
    both FULL and FAST refreshes from the controller's OTP waveforms, selecting
    speed via a temperature-force (CCSET/TSSET, 0xE0/0xE5) rather than by loading
    LUT registers (0x20-0x2C). We transcribe the same OTP-only sequences here; no
    LUT tables are transmitted. Register bytes below are copied verbatim from the
    vendored reference at:
        .pio/libdeps/t-deck-max/GxEPD2/src/gdeq/GxEPD2_310_GDEQ031T10.cpp
    (line numbers cited per-method in the .cpp) — no register values were invented.

    NOTE (needs on-hardware validation): the FAST (partial) refresh uses the
    panel's OTP partial waveform with a forced temperature index (0xE5 = 0x79) and
    a partial-mode CDI (0x50 = 0xD7). The darkness-vs-ghosting balance of that OTP
    waveform, and the FAST/FULL polling timings, can only be bench-tuned once real
    hardware is available. The BaseUI env (GxEPD2 path) on the same panel is the
    A/B reference.

*/

#pragma once

#ifdef MESHTASTIC_INCLUDE_NICHE_GRAPHICS

#include "configuration.h"

#include "./UC8175.h"

namespace NicheGraphics::Drivers
{

class GDEQ031T10 : public UC8175
{
  private:
    // Native panel orientation is portrait 240x320. Rotation is applied by InkHUD's
    // WindowManager, never here — the driver always renders in native orientation.
    //
    // BEZEL INSET: the T-Deck Max screen mask overlaps the outermost 1-2px of the active
    // area, so the logical surface reported to InkHUD is shrunk by insetX/insetY on EVERY
    // side (240x320 -> 236x316) and writeImage() re-packs each frame into the physical
    // panel raster, shifted by (insetX, insetY) with a white border. The logical row
    // stride stays 30 bytes (ceil(236/8) == 240/8), so only the bit-shift in writeImage
    // and the white border padding differ from a native-resolution driver.
    static constexpr uint16_t panelWidth = 240;
    static constexpr uint16_t panelHeight = 320;
    static constexpr uint16_t insetX = 2; // px hidden under the mask, left AND right
    static constexpr uint16_t insetY = 2; // px hidden under the mask, top AND bottom
    static constexpr uint16_t width = panelWidth - 2 * insetX;   // 236, reported to InkHUD
    static constexpr uint16_t height = panelHeight - 2 * insetY; // 316, reported to InkHUD
    static constexpr uint16_t panelRowBytes = panelWidth / 8;    // 30
    static constexpr UpdateTypes supported = (UpdateTypes)(FULL | FAST);

  public:
    GDEQ031T10();

  protected:
    void configCommon() override; // <- GxEPD2 _InitDisplay()
    void configFull() override;   // <- GxEPD2 _Update_Full()
    void configFast() override;   // <- GxEPD2 _Update_Part() (partial-mode entry)
    void writeOldImage() override;
    // Re-packs the logical (inset) frame into the physical 240x320 raster; see .cpp
    void writeImage(uint8_t command, const uint8_t *image) override;
    void detachFromUpdate() override;
    void finalizeUpdate() override;

  private:
    // Mirrors GxEPD2 _setPartialRamArea() (0x90 window) byte encoding.
    void setPartialWindow(uint16_t x, uint16_t y, uint16_t w, uint16_t h);
};

} // namespace NicheGraphics::Drivers

#endif // MESHTASTIC_INCLUDE_NICHE_GRAPHICS
