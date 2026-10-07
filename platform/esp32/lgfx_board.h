// LovyanGFX setup for the GC9A01 panel and CST816 touch chip.
#pragma once
#define LGFX_USE_V1
#include <LovyanGFX.hpp>

#include "board_pins.h"

class LGFX : public lgfx::LGFX_Device {
  lgfx::Panel_GC9A01 panel_;
  lgfx::Bus_SPI bus_;
  lgfx::Light_PWM light_;
  lgfx::Touch_CST816S touch_;

 public:
  LGFX() {
    {
      auto cfg = bus_.config();
      cfg.spi_host = SPI2_HOST;
      cfg.spi_mode = 0;
      cfg.freq_write = 80000000;
      cfg.freq_read = 20000000;
      cfg.spi_3wire = true;
      cfg.use_lock = true;
      cfg.dma_channel = SPI_DMA_CH_AUTO;
      cfg.pin_sclk = DP_PIN_SCLK;
      cfg.pin_mosi = DP_PIN_MOSI;
      cfg.pin_miso = -1;
      cfg.pin_dc = DP_PIN_DC;
      bus_.config(cfg);
      panel_.setBus(&bus_);
    }
    {
      auto cfg = panel_.config();
      cfg.pin_cs = DP_PIN_CS;
      cfg.pin_rst = DP_PIN_RST;
      cfg.pin_busy = -1;
      cfg.panel_width = 240;
      cfg.panel_height = 240;
      cfg.offset_x = 0;
      cfg.offset_y = 0;
      cfg.offset_rotation = 0;
      cfg.readable = false;
      cfg.invert = true;
      cfg.rgb_order = false;
      cfg.dlen_16bit = false;
      cfg.bus_shared = false;
      panel_.config(cfg);
    }
    {
      auto cfg = light_.config();
      cfg.pin_bl = DP_PIN_BL;
      cfg.invert = false;
      cfg.freq = 44100;
      cfg.pwm_channel = 1;
      light_.config(cfg);
      panel_.setLight(&light_);
    }
    {
      auto cfg = touch_.config();
      cfg.x_min = 0;
      cfg.x_max = 239;
      cfg.y_min = 0;
      cfg.y_max = 239;
      cfg.pin_int = DP_PIN_TP_INT;
      cfg.pin_rst = DP_PIN_TP_RST;
      cfg.bus_shared = false;
      cfg.offset_rotation = 0;
      cfg.i2c_port = 0;
      cfg.i2c_addr = DP_TP_I2C_ADDR;
      cfg.pin_sda = DP_PIN_TP_SDA;
      cfg.pin_scl = DP_PIN_TP_SCL;
      cfg.freq = 400000;
      touch_.config(cfg);
      panel_.setTouch(&touch_);
    }
    setPanel(&panel_);
  }
};
