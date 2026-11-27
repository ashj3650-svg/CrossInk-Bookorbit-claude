#include "LightSleep.h"

#if CROSSINK_APP_INSTANT_WAKE

#include <Arduino.h>
#include <HalGPIO.h>
#include <Logging.h>

#ifndef SIMULATOR
#include <BoardConfig.h>
#include <InputWake.h>
#include <WiFi.h>
#include <driver/gpio.h>
#include <esp_err.h>
#include <esp_sleep.h>
#endif

namespace LightSleep {

#ifndef SIMULATOR

namespace {
// Called with every source armed by sleepFor(), on both its exits.
// The button's GPIO interrupt (InputWake's edge handler) is off for the sleep: armed as a
// level wake source, it would fire for as long as the waking press lasts and starve every
// task. disarm() gives it back its edge trigger.
void disarm(const gpio_num_t pin, const bool restoreEdgeInterrupt) {
  gpio_wakeup_disable(pin);
  esp_sleep_disable_wakeup_source(ESP_SLEEP_WAKEUP_ALL);
  gpio_set_intr_type(pin, GPIO_INTR_ANYEDGE);
  if (restoreEdgeInterrupt) gpio_intr_enable(pin);
}

// The battery latch keeps the board powered. Pins keep their levels through a light sleep
// unless told otherwise, but should anything switch the pads to their sleep settings the
// board would power off mid-sleep: hold the latch where holdPowerRails() put it.
void holdPowerLatch(const bool hold) {
  for (const int8_t latch : {BoardConfig::ACTIVE.power.latch0, BoardConfig::ACTIVE.power.latch1}) {
    if (latch < 0 || BoardConfig::latchConflictsWithBus(latch)) continue;
    if (hold) {
      gpio_hold_en(static_cast<gpio_num_t>(latch));
    } else {
      gpio_hold_dis(static_cast<gpio_num_t>(latch));
    }
  }
}
}  // namespace

Wake sleepFor(const uint32_t durationMs) {
  const int8_t powerPin = BoardConfig::ACTIVE.input.power;
  if (powerPin < 0 || durationMs == 0) return Wake::Failed;
  const auto pin = static_cast<gpio_num_t>(powerPin);
  const bool activeHigh = BoardConfig::ACTIVE.input.powerActiveHigh;

  // The caller stopped Wi-Fi already; a radio still up would keep the chip from sleeping.
  if (WiFi.getMode() != WIFI_MODE_NULL) {
    LOG_ERR("SLP", "Wi-Fi still on before light sleep; turning it off");
    WiFi.mode(WIFI_OFF);
  }

  // Nothing else may wake the chip: no deep-sleep source is armed this early, and the
  // disarm() on the way out leaves the next deep sleep with only the sources it arms.
  esp_sleep_disable_wakeup_source(ESP_SLEEP_WAKEUP_ALL);
  const bool edgeInterrupt = InputWake::watchesPin(powerPin);
  gpio_intr_disable(pin);
  // The button keeps the pull InputManager gave it, so the idle level stays defined; a
  // level (not edge) trigger is the only kind light sleep offers on GPIOs.
  const esp_err_t gpioErr = gpio_wakeup_enable(pin, activeHigh ? GPIO_INTR_HIGH_LEVEL : GPIO_INTR_LOW_LEVEL);
  const esp_err_t sourceErr = gpioErr == ESP_OK ? esp_sleep_enable_gpio_wakeup() : gpioErr;
  const esp_err_t timerErr =
      sourceErr == ESP_OK ? esp_sleep_enable_timer_wakeup(static_cast<uint64_t>(durationMs) * 1000ULL) : sourceErr;
  if (timerErr != ESP_OK) {
    LOG_ERR("SLP", "Light sleep could not be armed (%s)", esp_err_to_name(timerErr));
    disarm(pin, edgeInterrupt);
    return Wake::Failed;
  }

  holdPowerLatch(true);
  const esp_err_t sleepErr = esp_light_sleep_start();
  const esp_sleep_wakeup_cause_t cause = esp_sleep_get_wakeup_cause();
  holdPowerLatch(false);
  disarm(pin, edgeInterrupt);
  if (sleepErr != ESP_OK) {
    LOG_ERR("SLP", "Light sleep refused (%s)", esp_err_to_name(sleepErr));
    return Wake::Failed;
  }
  switch (cause) {
    case ESP_SLEEP_WAKEUP_GPIO:
      return Wake::PowerButton;
    case ESP_SLEEP_WAKEUP_TIMER:
      return Wake::Timer;
    default:
      return Wake::Other;
  }
}

bool isPowerButtonDown() { return gpio.isPowerButtonPhysicallyPressed(); }

bool verifyWakePress(const bool shortPressWakes, const uint16_t longHoldMs) {
  // The same debounce as a deep-sleep wake (HalGPIO::verifyPowerButtonWakeup), but timed
  // from now: millis() has been counting since the last reset, long before this press.
  constexpr unsigned long STABILITY_MS = 10;
  const unsigned long start = millis();
  const bool heldAtFirstSample = isPowerButtonDown();
  while (millis() - start < STABILITY_MS) {
    delay(1);
    gpio.update();
  }
  if (shortPressWakes) return true;
  if (!heldAtFirstSample || !isPowerButtonDown()) return false;
  while (millis() - start < longHoldMs) {
    delay(1);
    gpio.update();
    if (!isPowerButtonDown()) return false;
  }
  return true;
}

void waitForPowerButtonRelease() {
  while (isPowerButtonDown()) {
    delay(10);
  }
  gpio.update();
}

#else  // SIMULATOR

// The simulator has no chip to pause: wait on its buttons instead, the way its deep sleep
// does, but return here rather than relaunching the process.
Wake sleepFor(const uint32_t durationMs) {
  const unsigned long start = millis();
  while (millis() - start < durationMs) {
    gpio.update();
    if (gpio.isPressed(HalGPIO::BTN_POWER)) return Wake::PowerButton;
    delay(10);
  }
  return Wake::Timer;
}

bool isPowerButtonDown() {
  gpio.update();
  return gpio.isPressed(HalGPIO::BTN_POWER);
}

// Like the simulator's own deep-sleep wake check, which accepts every press.
bool verifyWakePress(bool, uint16_t) { return true; }

void waitForPowerButtonRelease() {
  while (isPowerButtonDown()) {
    delay(10);
  }
}

#endif

}  // namespace LightSleep

#endif
