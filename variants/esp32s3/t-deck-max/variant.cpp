#include "variant.h"
#include "ExtensionIOXL9555.hpp"

extern ExtensionIOXL9555 io;

static void setExpandPin(uint8_t pin, uint8_t value)
{
    io.pinMode(pin, OUTPUT);
    io.digitalWrite(pin, value);
}

static void pulseExpandPinLow(uint8_t pin, uint32_t lowMs, uint32_t highMs)
{
    setExpandPin(pin, LOW);
    delay(lowMs);
    io.digitalWrite(pin, HIGH);
    delay(highMs);
}

void earlyInitVariant()
{
    pinMode(LORA_CS, OUTPUT);
    digitalWrite(LORA_CS, HIGH);
    pinMode(SDCARD_CS, OUTPUT);
    digitalWrite(SDCARD_CS, HIGH);
    pinMode(PIN_EINK_CS, OUTPUT);
    digitalWrite(PIN_EINK_CS, HIGH);
    pinMode(KB_IRQ_PIN, INPUT_PULLUP);
    pinMode(CST328_PIN_INT, INPUT_PULLUP);
    pinMode(PIN_EINK_BL, OUTPUT);
    analogWrite(PIN_EINK_BL, 0);

    io.begin(Wire, XL9555_SLAVE_ADDRESS0, SDA, SCL);
    setExpandPin(EXPANDS_MODEM_EN, LOW);
    setExpandPin(EXPANDS_MODEM_PWRKEY, LOW);
    setExpandPin(EXPANDS_LORA_EN, HIGH);
    setExpandPin(EXPANDS_LORA_SEL, HIGH);
    setExpandPin(EXPANDS_GPS_EN, HIGH);
    // BHI260AP 1.8V rail: OFF. HAS_BHI260AP is defined but NO driver in this build ever consumes
    // it (the chip was only ever logged by the boot I2C scan), so powering the rail was a
    // permanent ~0.2-1mA leak for zero function (1-day-battery audit). Drive HIGH again when an
    // IMU driver actually lands. Side effect: the boot I2C scan no longer reports 0x28.
    setExpandPin(EXPANDS_1V8_EN, LOW);
    setExpandPin(EXPANDS_DRV_EN, HIGH);
    setExpandPin(EXPANDS_AMP_EN, LOW);
    setExpandPin(EXPANDS_AUDIO_SEL, LOW);
    pulseExpandPinLow(EXPANDS_TOUCH_RST, 20, 60);
    pulseExpandPinLow(EXPANDS_KB_RST, 20, 60);
}
