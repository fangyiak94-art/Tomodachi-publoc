// DIYMORE ESP32-C3 round touch board (ESP32-2424S012 clone).
// Verify every pin against the seller's demo sketch before the first flash.
#pragma once

#define DP_PIN_SCLK 6
#define DP_PIN_MOSI 7
#define DP_PIN_CS 10
#define DP_PIN_DC 2
#define DP_PIN_RST -1  // panel reset is tied to EN on most clones
#define DP_PIN_BL 3

#define DP_PIN_TP_SDA 4
#define DP_PIN_TP_SCL 5
#define DP_PIN_TP_INT 0
#define DP_PIN_TP_RST 1
#define DP_TP_I2C_ADDR 0x15

#define DP_PIN_BOOT 9  // BOOT button, active low

// Piezo buzzer: wire to a free pin on the SH1.0-4P port, or leave -1.
#ifndef DP_PIN_BUZZER
#define DP_PIN_BUZZER -1
#endif
