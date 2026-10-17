// LIFX Tile PSU replacement: ID signal generator for any ESP32 (basic build).
//
// The original Tile power supply puts a 0-3.3 V square wave on the yellow
// "ID" wire (pin 7), derived from the mains frequency. The leader tile only
// boots normally when that frequency is between 45 and 65 Hz; without it the
// whole set "breathes" red. This sketch makes the LEDC peripheral generate it.
//
// The waveform is produced in hardware, so loop() has nothing to do. There is
// deliberately no Wi-Fi and no OTA: the only way this ESP32 restarts is when
// the whole thing loses power, and then the tiles restart with it.
//
// This sketch is for the basic build, where the tiles' 24 V comes straight
// from the brick. If you built the MOSFET switch, use the ESPHome config: it
// has the start-up sequencing and crash guards that switching needs.
//
// Uses the ESP-IDF LEDC driver directly, so it builds unchanged on
// arduino-esp32 core 2.x and 3.x (the ESP32-C6 needs core 3.x).

#include <Arduino.h>
#include "driver/ledc.h"
#include "soc/soc_caps.h"

// 50 or 60 Hz both work (the tile accepts 45-65 Hz). The original PSU just
// followed the local mains frequency.
#ifndef ID_FREQUENCY_HZ
#define ID_FREQUENCY_HZ 50
#endif

// Pick a pin that is not a strapping pin on your chip. Override with
// -DID_PIN=<n> or edit here.
#if defined(ID_PIN)
static const int kIdPin = ID_PIN;
#elif defined(CONFIG_IDF_TARGET_ESP32)
static const int kIdPin = 25;  // ESP32 DevKit / WROOM-32
#elif defined(CONFIG_IDF_TARGET_ESP32C6)
static const int kIdPin = 18;
#else
static const int kIdPin = 4;  // ESP32-S2 / S3 / C3
#endif

// 14 bits works on every ESP32 variant. On the XTAL clock (40 MHz) or the
// classic ESP32's APB clock (80 MHz), 50 Hz divides exactly and 60 Hz comes
// out at 59.998 / 60.001 Hz. Either is far inside the tile's 45-65 Hz window.
static const ledc_timer_bit_t kIdResolution = LEDC_TIMER_14_BIT;
static const uint32_t kIdDuty50Percent = 1u << (14 - 1);

static bool start_id_signal() {
  ledc_timer_config_t timer = {};
  timer.speed_mode = LEDC_LOW_SPEED_MODE;  // the only mode every chip has
  timer.duty_resolution = kIdResolution;
  timer.timer_num = LEDC_TIMER_0;
  timer.freq_hz = ID_FREQUENCY_HZ;
#if SOC_LEDC_SUPPORT_XTAL_CLOCK
  timer.clk_cfg = LEDC_USE_XTAL_CLK;  // S2 / S3 / C3 / C6
#else
  timer.clk_cfg = LEDC_USE_APB_CLK;  // classic ESP32
#endif
  esp_err_t err = ledc_timer_config(&timer);
  if (err != ESP_OK) {
    Serial.printf("ledc_timer_config failed: %s\n", esp_err_to_name(err));
    return false;
  }

  // The duty is set in the channel config itself, so no later duty update
  // has to land (the classic ESP32 can drop one while another is pending).
  ledc_channel_config_t channel = {};
  channel.gpio_num = kIdPin;
  channel.speed_mode = LEDC_LOW_SPEED_MODE;
  channel.channel = LEDC_CHANNEL_0;
  channel.intr_type = LEDC_INTR_DISABLE;
  channel.timer_sel = LEDC_TIMER_0;
  channel.duty = kIdDuty50Percent;
  channel.hpoint = 0;
  err = ledc_channel_config(&channel);
  if (err != ESP_OK) {
    Serial.printf("ledc_channel_config failed: %s\n", esp_err_to_name(err));
    return false;
  }
  return true;
}

void setup() {
  // Start the signal before anything else so the tiles see it as early as
  // possible after power-on.
  bool ok = start_id_signal();

  Serial.begin(115200);
  delay(200);
  if (ok) {
    Serial.printf("LIFX Tile ID: %u Hz, 50%% duty on GPIO%d (timer reports %u Hz)\n",
                  (unsigned)ID_FREQUENCY_HZ, kIdPin,
                  (unsigned)ledc_get_freq(LEDC_LOW_SPEED_MODE, LEDC_TIMER_0));
  } else {
    Serial.println("LIFX Tile ID: failed to start the signal");
  }
}

void loop() {
  delay(1000);
}
