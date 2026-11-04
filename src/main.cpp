#include <Arduino.h>
#define LGFX_USE_V1
#include <LovyanGFX.hpp>

// Define RGB LED Pins (Common Anode: LOW = ON, HIGH = OFF)
#define LED_RED 22
#define LED_GREEN 16
#define LED_BLUE 17

// Custom LovyanGFX configuration for the Freenove 4.0" ESP32-32E Display
class LGFX : public lgfx::LGFX_Device {
  lgfx::Panel_ST7796 _panel_instance;
  lgfx::Bus_SPI _bus_instance;
  lgfx::Light_PWM _light_instance;
  lgfx::Touch_XPT2046 _touch_instance;

public:
  LGFX(void) {
    auto cfg = _bus_instance.config();
    cfg.spi_host = HSPI_HOST;
    cfg.spi_mode = 0;
    cfg.freq_write = 16000000; // Stabilized lower frequency
    cfg.freq_read = 16000000;
    cfg.spi_3wire = false;
    cfg.use_lock = true;
    cfg.dma_channel = SPI_DMA_CH_AUTO;
    cfg.pin_sclk = 14;
    cfg.pin_mosi = 13;
    cfg.pin_miso = 12;
    cfg.pin_dc = 2;
    _bus_instance.config(cfg);
    _panel_instance.setBus(&_bus_instance);

    auto p_cfg = _panel_instance.config();
    p_cfg.pin_cs = 15;
    p_cfg.pin_rst = 4;    // Hardware reset pin enabled
    p_cfg.pin_busy = -1;
    p_cfg.panel_width = 320;
    p_cfg.panel_height = 480;
    p_cfg.offset_x = 0;
    p_cfg.offset_y = 0;
    p_cfg.offset_rotation = 0;
    p_cfg.dummy_read_pixel = 8;
    p_cfg.dummy_read_bits = 1;
    p_cfg.readable = true;
    p_cfg.invert = false;
    p_cfg.rgb_order = false;
    p_cfg.dlen_16bit = false;
    p_cfg.bus_shared = true;
    _panel_instance.config(p_cfg);

    auto l_cfg = _light_instance.config();
    l_cfg.pin_bl = 27;    // Verified backlight pin
    l_cfg.invert = false;
    l_cfg.freq = 44100;
    l_cfg.pwm_channel = 7;
    _light_instance.config(l_cfg);
    _panel_instance.setLight(&_light_instance);

    auto t_cfg = _touch_instance.config();
    t_cfg.pin_cs = 33;
    t_cfg.pin_mosi = 32;
    t_cfg.pin_miso = 39;
    t_cfg.pin_sclk = 25;
    t_cfg.pin_int = 36;
    t_cfg.bus_shared = true;
    t_cfg.spi_host = HSPI_HOST;
    t_cfg.freq = 2500000;
    t_cfg.x_min = 200;
    t_cfg.x_max = 3800;
    t_cfg.y_min = 200;
    t_cfg.y_max = 3800;
    _touch_instance.config(t_cfg);
    _panel_instance.setTouch(&_touch_instance);

    setPanel(&_panel_instance);
  }
};

LGFX lcd;

// LED States
bool redState = false;
bool greenState = false;
bool blueState = false;

void drawUI() {
  lcd.fillScreen(TFT_BLACK);

  // Title
  lcd.setTextColor(TFT_WHITE, TFT_BLACK);
  lcd.setTextSize(2);
  lcd.setCursor(40, 20);
  lcd.println("CYD RGB LED Controller");

  // Draw Red Button
  lcd.fillRect(40, 80, 240, 80, redState ? TFT_RED : 0x4000);
  lcd.drawRect(40, 80, 240, 80, TFT_WHITE);
  lcd.setTextColor(TFT_WHITE, redState ? TFT_RED : 0x4000);
  lcd.setCursor(110, 110);
  lcd.print(redState ? "RED: ON" : "RED: OFF");

  // Draw Green Button
  lcd.fillRect(40, 190, 240, 80, greenState ? TFT_GREEN : 0x0200);
  lcd.drawRect(40, 190, 240, 80, TFT_WHITE);
  lcd.setTextColor(TFT_WHITE, greenState ? TFT_GREEN : 0x0200);
  lcd.setCursor(95, 220);
  lcd.print(greenState ? "GREEN: ON" : "GREEN: OFF");

  // Draw Blue Button
  lcd.fillRect(40, 300, 240, 80, blueState ? TFT_BLUE : 0x0010);
  lcd.drawRect(40, 300, 240, 80, TFT_WHITE);
  lcd.setTextColor(TFT_WHITE, blueState ? TFT_BLUE : 0x0010);
  lcd.setCursor(100, 330);
  lcd.print(blueState ? "BLUE: ON" : "BLUE: OFF");
}

void updateHardwareLEDs() {
  digitalWrite(LED_RED, redState ? LOW : HIGH);
  digitalWrite(LED_GREEN, greenState ? LOW : HIGH);
  digitalWrite(LED_BLUE, blueState ? LOW : HIGH);
}

void setup() {
  Serial.begin(115200);
  delay(500); // Allow power rails to stabilize

  // Initialize LED pins
  pinMode(LED_RED, OUTPUT);
  pinMode(LED_GREEN, OUTPUT);
  pinMode(LED_BLUE, OUTPUT);
  digitalWrite(LED_RED, HIGH);
  digitalWrite(LED_GREEN, HIGH);
  digitalWrite(LED_BLUE, HIGH);

  // Initialize Display and Touch safely
  lcd.init();
  delay(100);
  lcd.setRotation(0);
  lcd.setBrightness(128);

  drawUI();
}

void loop() {
  int32_t x, y;
  if (lcd.getTouch(&x, &y)) {
    delay(200); // Debounce

    if (x >= 40 && x <= 280 && y >= 80 && y <= 160) {
      redState = !redState;
      updateHardwareLEDs();
      drawUI();
    }
    else if (x >= 40 && x <= 280 && y >= 190 && y <= 270) {
      greenState = !greenState;
      updateHardwareLEDs();
      drawUI();
    }
    else if (x >= 40 && x <= 280 && y >= 300 && y <= 380) {
      blueState = !blueState;
      updateHardwareLEDs();
      drawUI();
    }
  }
}