#ifdef MESHTASTIC_INCLUDE_NICHE_GRAPHICS

#include "./GDEQ031T10.h"

#include <cstring>

using namespace NicheGraphics::Drivers;

// All register bytes below are transcribed verbatim from the vendored GxEPD2
// reference driver GxEPD2_310_GDEQ031T10.cpp (UC8253 controller). Line numbers
// cited per method. No LUT registers (0x20-0x2C) are written: this panel uses
// OTP full+partial waveforms, selected via forced temperature (0xE0/0xE5).

GDEQ031T10::GDEQ031T10() : UC8175(width, height, supported)
{
    // UC8175 ctor computes LOGICAL geometry: bufferRowSize = ((236-1)/8)+1 = 30 (same stride as
    // the 240px panel raster), bufferSize = 30*316 = 9480. The previousBuffer for the FAST
    // differential is logical too; writeImage() re-packs both into physical rasters on the fly.
    static_assert(((width - 1) / 8) + 1 == panelRowBytes, "inset must keep the logical stride equal to the panel stride");
}

// GxEPD2 _InitDisplay(), lines 348-363.
// After UC8175::reset() has already pulsed RST (equivalent to GxEPD2's _reset()),
// we issue the same PANEL SETTING (PSR, 0x00) sequence GxEPD2 uses on a fresh init.
// GxEPD2 does NOT set a resolution register (0x61) for this panel — the UC8253
// defaults to 240x320 from OTP — so neither do we.
void GDEQ031T10::configCommon()
{
    sendCommand(0x00); // PANEL SETTING (PSR)
    sendData(0x1e);    // soft reset value
    sendData(0x0d);
    delay(1);

    sendCommand(0x00); // PANEL SETTING (PSR)
    sendData(0x1f);    // KW: 3f, KWR: 2F, BWROTP: 0f, BWOTP: 1f
    sendData(0x0d);
}

// GxEPD2 _Update_Full(), lines 365-381 (useFastFullUpdate == true).
// FULL uses the OTP full waveform, with a forced temperature index to select the
// faster (~1015ms) waveform rather than the extended low-temperature one.
void GDEQ031T10::configFull()
{
    sendCommand(0xE0); // Cascade Setting (CCSET)
    sendData(0x02);    // TSFIX
    sendCommand(0xE5); // Force Temperature (TSSET)
    sendData(0x5A);    // 90 -> ~1015000us waveform

    sendCommand(0x50); // VCOM and data interval (CDI) - full value
    sendData(0x97);

    powerOn(); // inherited UC8175::powerOn() -> 0x04 + BUSY wait
}

// GxEPD2 _Update_Part(), lines 383-398, plus the partial-mode entry that GxEPD2
// performs in refresh()/writeImage() (0x91 partial-in, lines 290/93; 0x90 window
// via _setPartialRamArea, lines 313-326). InkHUD always writes the whole frame,
// so the partial window is the full screen. The forced temperature (0x79) selects
// the OTP partial waveform; combined with UC8175's differential old/new image
// writes (0x10 previous, 0x13 current) this yields the FAST refresh.
void GDEQ031T10::configFast()
{
    sendCommand(0x91); // partial in (PTIN)
    // 0x90 window = full PHYSICAL screen (0..239 x 0..319): writeImage always transmits
    // complete panel rasters (the bezel inset lives inside them as a white border).
    setPartialWindow(0, 0, panelWidth, panelHeight);

    sendCommand(0xE0); // Cascade Setting (CCSET)
    sendData(0x02);    // TSFIX
    sendCommand(0xE5); // Force Temperature (TSSET)
    sendData(0x79);    // 121

    sendCommand(0x50); // VCOM and data interval (CDI) - partial value
    sendData(0xD7);

    powerOn();
}

// GxEPD2 _setPartialRamArea(), lines 313-326. UC8253 byte encoding for the
// partial window (command 0x90). x/w are on byte boundaries; xe is rounded up to
// the last byte inclusive. GxEPD2 transmits single bytes for the horizontal
// bounds (panel width <= 240 fits in 8 bits) and 16-bit big-endian for vertical.
void GDEQ031T10::setPartialWindow(uint16_t x, uint16_t y, uint16_t w, uint16_t h)
{
    uint16_t xe = (x + w - 1) | 0x0007; // byte boundary inclusive (last byte)
    uint16_t ye = y + h - 1;
    x &= 0xFFF8; // byte boundary

    sendCommand(0x90); // partial window
    sendData((uint8_t)x);
    sendData((uint8_t)xe);
    sendData((uint8_t)(y / 256));
    sendData((uint8_t)(y % 256));
    sendData((uint8_t)(ye / 256));
    sendData((uint8_t)(ye % 256));
    sendData(0x01);
}

// Mirrors GDEW0102T4::writeOldImage.
// FULL: this panel refreshes most cleanly when the "old image" (0x10) is all white,
// so the OTP full waveform drives every pixel white -> new image (clears ghosting).
// FAST: differential update uses the previous frame as the old image.
void GDEQ031T10::writeOldImage()
{
    if (updateType == FULL) {
        sendCommand(0x10);

        // Buffered 0xFF (white) writes to avoid per-byte SPI transactions. NOTE: the panel
        // expects a full PHYSICAL raster (240x320 = 9600 bytes), not the logical inset size.
        const uint16_t chunkSize = 64;
        uint8_t ffBuf[chunkSize];
        memset(ffBuf, 0xFF, sizeof(ffBuf));

        uint32_t remaining = (uint32_t)panelRowBytes * panelHeight;
        while (remaining > 0) {
            uint16_t toSend = remaining > chunkSize ? chunkSize : static_cast<uint16_t>(remaining);
            sendData(ffBuf, toSend);
            remaining -= toSend;
        }
        return;
    }

    // FAST refresh uses differential data (previous frame as old image).
    if (previousBuffer)
        writeImage(0x10, previousBuffer);
    else
        writeImage(0x10, buffer);
}

// Transmit one frame, re-packing the logical (bezel-inset) raster into the physical panel
// raster on the fly: insetY white rows top and bottom, and every image row shifted right by
// insetX bits with a white (1) border filling the revealed edge pixels. Bit order is GxEPD2
// convention: MSB = leftmost pixel, 0xFF = white. The panel row and the logical row are both
// panelRowBytes wide (static_assert in the ctor), so the shift is a straight rolling carry;
// the final byte's padding bits (beyond insetX + width) are forced white since the logical
// buffer's unused trailing bits are undefined.
void GDEQ031T10::writeImage(uint8_t command, const uint8_t *image)
{
    sendCommand(command);

    uint8_t row[panelRowBytes];
    memset(row, 0xFF, sizeof(row));
    for (uint16_t y = 0; y < insetY; y++)
        sendData(row, sizeof(row)); // top border

    constexpr uint8_t tailWhiteMask = (uint8_t)((1 << (8 * panelRowBytes - (insetX + width))) - 1);
    for (uint16_t y = 0; y < height; y++) {
        const uint8_t *src = image + (uint32_t)y * bufferRowSize;
        uint8_t carry = 0xFF; // white carried into the first insetX pixels
        for (uint16_t b = 0; b < panelRowBytes; b++) {
            row[b] = (uint8_t)((carry << (8 - insetX)) | (src[b] >> insetX));
            carry = src[b];
        }
        row[panelRowBytes - 1] |= tailWhiteMask; // right border + undefined tail bits -> white
        sendData(row, sizeof(row));
    }

    memset(row, 0xFF, sizeof(row));
    for (uint16_t y = 0; y < insetY; y++)
        sendData(row, sizeof(row)); // bottom border
}

// Poll BUSY to detect completion. expectedDuration delays the first poll.
// Sanity-checked against the GxEPD2 header timing constants for this panel:
//   full_refresh_time  = 1100 ms
//   partial_refresh_time = 700 ms
// so we begin polling slightly before each expected completion. A hard 10s
// timeout in EInk::runOnce() covers any stall.
void GDEQ031T10::detachFromUpdate()
{
    switch (updateType) {
    case FAST:
        return beginPolling(50, 600);
    case FULL:
    default:
        return beginPolling(100, 1000);
    }
}

// Mirrors GDEW0102T4::finalizeUpdate: power off the panel drivers only. We do NOT
// issue the deep-sleep command (0x07) — keeping the controller out of deep sleep
// improves reliability of repeated FAST refreshes. Every update re-inits via a
// hardware reset in UC8175::update() anyway.
void GDEQ031T10::finalizeUpdate()
{
    powerOff();
}

#endif // MESHTASTIC_INCLUDE_NICHE_GRAPHICS
