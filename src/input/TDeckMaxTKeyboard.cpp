#if defined(T_DECK_MAX) && defined(MOD_I2C_TCA8418_KEYBOARD)

#include "TDeckMaxTKeyboard.h"

#define _TCA8418_COLS 10
#define _TCA8418_ROWS 4

// Matrix indices ((eventCode - 1), row-major): bottom control row + modifiers.
// Rows 0..2 are the three 10-wide typing rows; see the column reversal in pressed().
constexpr uint8_t idxAlt = 29;    // rightmost key of typing row 2
constexpr uint8_t idxRShift = 30; // bottom row, matrix order: R_Shift sym space mic L_Shift
constexpr uint8_t idxSym = 31;
constexpr uint8_t idxSpace = 32;
constexpr uint8_t idxMic = 33;
constexpr uint8_t idxLShift = 34;

TDeckMaxTKeyboard::TDeckMaxTKeyboard() : TCA8418KeyboardBase(_TCA8418_ROWS, _TCA8418_COLS), modFlags(0), symMode(false) {}

// CFG register (0x01) bits we need; the base driver's file-local defines are not exported.
static constexpr uint8_t TCA8418_CFG_KE_IEN = 0x01; // key-event interrupt enable

void TDeckMaxTKeyboard::reset()
{
    TCA8418KeyboardBase::reset();
    // Enable the key-event interrupt for the boot-time interactive-nap window. The base reset()
    // never writes CFG, whose power-on default is ALL ZERO - so the TCA8418 never asserted its
    // INT line at all, and the interactive-nap keyboard wake (KB_IRQ_PIN LOW-level wake armed
    // in doLightSleep) could never fire - key events just sat in the FIFO until the next BOOT
    // wake replayed them. With KE_IEN set, a keypress pulls INT low (waking a nap; the line is
    // ONLY a wake source - never attachInterrupt it, see the variant file) and clearInt() in
    // trigger() releases it after the drain. The bit is window-scoped at runtime via
    // setKeyEventInterrupt() (off while the sleep indicator is shown).
    kbIntEnabled = false; // force the write-through below
    setKeyEventInterrupt(true);
    setBacklight(false);
}

// See the header comment: window-scoped CFG.KE_IEN gate.
void TDeckMaxTKeyboard::setKeyEventInterrupt(bool enable)
{
    if (enable == kbIntEnabled)
        return;
    kbIntEnabled = enable;
    writeRegister(TCA8418_REG_CFG, enable ? TCA8418_CFG_KE_IEN : 0);
    if (enable)
        clearInt(); // a stale pre-enable K_INT flag would assert INT for keys long since drained
}

// Drain the whole event FIFO each poll (the base class trigger only pops one event per call).
// KEY_EVENT_A is the FIFO head and pops on every read; registers B..J only peek deeper entries
// without popping (the TDeckProKeyboard drain reads A+i, which re-delivers events on fast typing).
// Bit 7 of the event code is press(1)/release(0); the low 7 bits carry the key number either way,
// which the hold-modifier model needs on RELEASE too (base released() is keyless, so we bypass it).
void TDeckMaxTKeyboard::trigger()
{
    uint8_t count = keyCount();
    if (count == 0)
        return;
    for (uint8_t i = 0; i < count; ++i) {
        uint8_t k = readRegister(TCA8418_REG_KEY_EVENT_A);
        uint8_t key = k & 0x7F;
        if (k & 0x80) {
            pressed(key);
        } else {
            handleRelease(key);
            state = Idle;
        }
    }

    // Release the INT line: the TCA8418 holds K_INT asserted (INT pin LOW) until INT_STAT is
    // written after the FIFO drains. Required by the interactive-nap keyboard wake - a stuck-LOW
    // line would instantly re-wake the level-triggered light sleep. A key that lands after this
    // clear re-asserts INT and wakes the next nap (while awake the poll picks it up regardless).
    clearInt();
}

// 0x01 for either shift, 0x08 for alt, 0 for anything that is not a hold modifier.
uint8_t TDeckMaxTKeyboard::modifierBitFor(uint8_t idx)
{
    if (idx == idxLShift || idx == idxRShift)
        return 0x01;
    if (idx == idxAlt)
        return 0x08;
    return 0;
}

void TDeckMaxTKeyboard::pressed(uint8_t key)
{
    if (state == Init || state == Busy)
        return;

    const uint8_t idx = key - 1;
    const uint8_t mrow = idx / 10;
    const uint8_t mcol = idx % 10;
    if (mrow >= _TCA8418_ROWS || mcol >= _TCA8418_COLS)
        return; // not a matrix key

    state = Held;

    // Effective modCode for any event emitted this press: held alt/shift bits + sticky sym mode
    const auto modCode = [this]() -> uint8_t { return 0x84 | modFlags | (symMode ? 0x10 : 0); };

    // alt / shift: HOLD modifiers. Key-down raises the bit and emits a preview event so the IME
    // switches the shown board NOW (alt->Fn, shift->UPPER, in CIM mode the ㄅ<->ㄚ half, in sym
    // mode the emoji board). Every typing key pressed while the modifier is held carries the bit;
    // key-up (handleRelease below) drops it and previews again, reverting the board.
    // Preview keyCode 0x3F: no 0x40 valid-key bit so it types nothing; non-zero so it dodges the
    // (0x84,0) CIM-toggle case; != 0x71 so it does not toggle CIM itself.
    const uint8_t holdBit = modifierBitFor(idx);
    if (holdBit) {
        modFlags |= holdBit;
        queueEvent(modCode());
        queueEvent(0x3F);
        return;
    }

    // sym: tap-toggled MODE (deliberately not hold: it swaps the whole typing layer, and one-handed
    // symbol entry needs the thumb free). One tap enters symbol mode -- the 0x10 bit rides on every
    // subsequent event so the IME keeps the symbol board up -- until sym is tapped again. (The IME
    // side drops CIM when a sym-flagged event arrives, so sym also switches straight from bopomofo
    // to symbols; mic re-enters CIM.)
    if (idx == idxSym) {
        symMode = !symMode;
        queueEvent(modCode());
        queueEvent(0x3F);
        return;
    }

    uint8_t keyCode;
    if (mrow < 3)
        keyCode = 0x40 | (mrow << 4) | (9 - mcol); // typing rows; matrix cols run p..q, so reverse
    else if (idx == idxSpace)
        keyCode = 0x72; // T-keyboard (3,2)
    else if (idx == idxMic)
        keyCode = 0x71; // T-keyboard (3,1): in sym mode the IME types a literal '0' (the only digit
                        // 0 on this layout); otherwise it toggles CIM (bopomofo) mode
    else
        return; // unused bottom-row position

    queueEvent(modCode());
    queueEvent(keyCode);
    // No modifier reset here: held alt/shift persist until their key-up arrives (handleRelease)
}

void TDeckMaxTKeyboard::released()
{
    // Unused: our trigger() routes releases to the key-aware handleRelease() instead (the base
    // class signature has no key argument, which the hold-modifier model needs).
}

// Key-up. Only alt/shift releases matter: drop the held bit and emit a preview event so the
// on-screen board reverts to the base layer the moment the modifier is let go. Idempotent when
// the bit is already clear (e.g. clearModifiers() ran while the key was physically held), and
// typing-key / sym / mic releases carry no state at all.
void TDeckMaxTKeyboard::handleRelease(uint8_t key)
{
    if (key == 0)
        return; // empty FIFO slot, not a key
    const uint8_t holdBit = modifierBitFor(key - 1);
    if (!holdBit || !(modFlags & holdBit))
        return;
    modFlags &= ~holdBit;
    queueEvent(0x84 | modFlags | (symMode ? 0x10 : 0));
    queueEvent(0x3F); // preview: repaint selKB only (see pressed())
}

void TDeckMaxTKeyboard::setBacklight(bool on)
{
    backlightOn = on;
    // Drive via LEDC exactly like the vendor examples (keypad.ino / factory.ino fade the LED
    // with analogWrite; no vendor code ever digitalWrites this pad). GPIO42 is the MTMS JTAG
    // pad on the ESP32-S3, and the plain pinMode+digitalWrite route was field-reported dead
    // here - the frontlight on the neighbouring JTAG pad (GPIO41) uses analogWrite and works.
    analogWrite(KB_BL_PIN, on ? 255 : 0);
    LOG_INFO("T-Deck Max keyboard backlight -> %s", on ? "on" : "off");
}

#endif // T_DECK_MAX && MOD_I2C_TCA8418_KEYBOARD
