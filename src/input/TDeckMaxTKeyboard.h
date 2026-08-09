#include "TCA8418KeyboardBase.h"

/**
 * @brief T-Deck Max BBQ10-style keyboard (TCA8418-scanned, 4x10 matrix), presented in the fork's
 * "T-keyboard" positional event model instead of finished ASCII.
 *
 * Each non-modifier key press queues a 2-byte event identical to what the custom UART T-keyboard
 * (MOD_UART_T_KEYBOARD) sends, so InkHUD's InputMenuApplet consumes both keyboards through the
 * same handleMenuTKey/handleBackgroundTKey path and the same mirrored on-screen boards:
 *
 *   byte 0 (modCode): 0x84 | 0x01 shift | 0x08 alt | 0x10 sym
 *   byte 1 (keyCode): 0x40 | row<<4 | col   for the three 10-wide typing rows (row 0..2)
 *                     0x72 = space, 0x71 = mic ("0" in sym mode / CIM toggle otherwise)
 *
 * Modifier semantics:
 *   - alt / shift are HOLD modifiers (like a PC keyboard): key-down raises the bit and the IME
 *     repaints to the Fn / UPPER / second-bopomofo-half / emoji board via a preview event; every
 *     typing key pressed while the modifier is held carries the bit; key-up drops it and a second
 *     preview event reverts the board. Nothing latches - release always returns to the base board.
 *   - sym is a tap-toggled MODE: one tap enters symbol mode (the 0x10 bit rides on every event)
 *     until tapped again. mic is likewise a mode toggle (CIM), handled IME-side via keyCode 0x71;
 *     while sym mode is on, mic instead types the literal '0' (the only digit 0 on this layout).
 *
 * The TCA8418 matrix is column-reversed relative to the printed layout (event index 0 is 'p'),
 * so col = 9 - matrixCol. Modifier keys (both shifts, sym, alt) queue only preview events.
 */
class TDeckMaxTKeyboard : public TCA8418KeyboardBase
{
  public:
    TDeckMaxTKeyboard();
    void reset(void) override;
    void trigger(void) override;
    void setBacklight(bool on) override;
    bool getBacklight() const { return backlightOn; } // for the alt+N toggle chord

    // Defensive reset of the held alt/shift bits (sym MODE deliberately persists). Called by the
    // IME on foreground/background flips and on the sleep->wake keyboard flush, so stale held
    // state can't re-map the next key (e.g. turning a background 'D' press into the alt+D tile
    // toggle). Safe against a physically-still-held key: handleRelease() is idempotent, and the
    // next key-down of the modifier simply re-raises the bit.
    void clearModifiers() { modFlags = 0; }

    // Gate the TCA8418's key-event interrupt (CFG.KE_IEN) to the interactive-nap window: enabled
    // on the screen-wake edge / at boot, disabled when the sleep indicator is stamped (see the
    // notifyScreenPower observer in extra_variants/t_deck_max/variant.cpp). Outside the window
    // the INT line physically cannot assert - a chip-level second barrier on top of the
    // window-gated GPIO wake arming, and it stops a pocket-pressed key from holding INT low
    // against the KB_IRQ_PIN pull-up. Key CAPTURE is unaffected (the FIFO needs no IEN); the
    // asleep drain-and-discard path keeps working as before. Idempotent (skips repeat I2C).
    void setKeyEventInterrupt(bool enable);

  protected:
    void pressed(uint8_t key) override;
    void released(void) override; // unused: release handling is key-aware, see handleRelease()

  private:
    void handleRelease(uint8_t key);              // key-up: drop a held modifier + preview revert
    static uint8_t modifierBitFor(uint8_t idx);   // 0x01 shift / 0x08 alt / 0 not-a-hold-modifier

    uint8_t modFlags; // alt/shift bits currently HELD down (0x01 shift / 0x08 alt)
    bool symMode;     // tap-toggled symbol-mode latch (0x10 on every event while set)
    bool kbIntEnabled = false;  // mirrors CFG.KE_IEN to skip redundant I2C writes
    bool backlightOn = false;   // mirrors the KB_BL pin for the toggle chord
};
