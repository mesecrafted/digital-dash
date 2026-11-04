#include <Arduino.h>
#define LGFX_USE_V1
#include <LovyanGFX.hpp>
#include "driver/twai.h"

// Define CAN pins to match your port
#define CAN_TX_PIN GPIO_NUM_32
#define CAN_RX_PIN GPIO_NUM_25

// Define RGB LED Pins (Common Anode: LOW = ON, HIGH = OFF)
#define LED_RED 22
#define LED_GREEN 16
#define LED_BLUE 17

// OBD-II Mode 1 PIDs
#define PID_CALC_LOAD 0x04
#define PID_COOLANT_TEMP 0x05
#define PID_MAP_PRESS 0x0B
#define PID_TIMING_ADV 0x0E
#define PID_FUEL_STATUS 0x03
#define PID_BATT_VOLT 0x42

// Telemetry Data Variables
float battVoltage = 0.0;
int coolantTemp = 0;
int mapPressure = 0;
float timingAdvance = 0.0;
float engineLoad = 0.0;
String fuelStatusStr = "Initializing...";
bool ecuConnected = false;
unsigned long lastResponseTime = 0;

// Polling sequence state machine
const uint8_t pidList[] = {PID_BATT_VOLT, PID_COOLANT_TEMP, PID_MAP_PRESS, PID_TIMING_ADV, PID_CALC_LOAD, PID_FUEL_STATUS};
const int numPids = 6;
int currentPidIndex = 0;
unsigned long lastPollTime = 0;
const unsigned long pollInterval = 100; // Poll a PID every 100ms

void setupCAN() {
  twai_general_config_t g_config = TWAI_GENERAL_CONFIG_DEFAULT(
      CAN_TX_PIN, CAN_RX_PIN, TWAI_MODE_NORMAL);
  twai_timing_config_t t_config = TWAI_TIMING_CONFIG_500KBITS();
  twai_filter_config_t f_config = TWAI_FILTER_CONFIG_ACCEPT_ALL();

  if (twai_driver_install(&g_config, &t_config, &f_config) == ESP_OK) {
    if (twai_start() == ESP_OK) {
      Serial.println("TWAI Driver started successfully!");
    } else {
      Serial.println("Failed to start TWAI driver.");
    }
  } else {
    Serial.println("Failed to install TWAI driver.");
  }
}

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
    t_cfg.y_min = 3800;
    t_cfg.y_max = 200;
    _touch_instance.config(t_cfg);
    _panel_instance.setTouch(&_touch_instance);

    setPanel(&_panel_instance);
  }
};

LGFX lcd;

void drawStaticLabels() {
  lcd.fillScreen(TFT_BLACK);

  // Title Header
  lcd.setTextColor(TFT_WHITE, TFT_BLACK);
  lcd.setTextSize(2);
  lcd.setCursor(20, 15);
  lcd.println("VEHICLE TELEMETRY DASH");

  // Static Field Labels (drawn once)
  lcd.setTextSize(2);
  lcd.setTextColor(TFT_DARKGREY, TFT_BLACK);
  
  lcd.setCursor(20, 80);
  lcd.print("Battery: ");

  lcd.setCursor(20, 140);
  lcd.print("Coolant Temp: ");

  lcd.setCursor(20, 200);
  lcd.print("MAP Pressure: ");

  lcd.setCursor(20, 260);
  lcd.print("Ign Timing: ");

  lcd.setCursor(20, 320);
  lcd.print("Calc Load: ");

  lcd.setCursor(20, 390);
  lcd.setTextSize(1);
  lcd.println("Fuel System Status:");
}

void updateTelemetryDisplay() {
  // 1. Connection Status Bar (Top)
  lcd.fillRect(20, 45, 280, 16, TFT_BLACK); // Clear old status text area
  lcd.setTextSize(1);
  lcd.setCursor(20, 45);
  if (ecuConnected) {
    lcd.setTextColor(TFT_GREEN, TFT_BLACK);
    lcd.print("STATUS: CONNECTED TO ECU");
  } else {
    lcd.setTextColor(TFT_RED, TFT_BLACK);
    lcd.print("STATUS: SEARCHING / NO SIGNAL...");
  }

  // 2. Update Dynamic Values (Overwriting text cleanly without full-screen clear)
  lcd.setTextSize(2);

  // Battery Voltage
  lcd.fillRect(140, 80, 160, 20, TFT_BLACK);
  lcd.setCursor(140, 80);
  lcd.setTextColor(TFT_YELLOW, TFT_BLACK);
  lcd.printf("%.1f V", battVoltage);

  // Coolant Temperature
  lcd.fillRect(180, 140, 120, 20, TFT_BLACK);
  lcd.setCursor(180, 140);
  lcd.setTextColor(TFT_CYAN, TFT_BLACK);
  lcd.printf("%d C", coolantTemp);

  // MAP Pressure
  lcd.fillRect(180, 200, 120, 20, TFT_BLACK);
  lcd.setCursor(180, 200);
  lcd.setTextColor(TFT_ORANGE, TFT_BLACK);
  lcd.printf("%d kPa", mapPressure);

  // Ignition Timing
  lcd.fillRect(160, 260, 140, 20, TFT_BLACK);
  lcd.setCursor(160, 260);
  lcd.setTextColor(TFT_MAGENTA, TFT_BLACK);
  lcd.printf("%.1f deg", timingAdvance);

  // Calculated Load
  lcd.fillRect(150, 320, 150, 20, TFT_BLACK);
  lcd.setCursor(150, 320);
  lcd.setTextColor(TFT_GREEN, TFT_BLACK);
  lcd.printf("%.1f %%", engineLoad);

  // Fuel System Status
  lcd.fillRect(20, 415, 280, 30, TFT_BLACK);
  lcd.setCursor(20, 415);
  lcd.setTextColor(TFT_WHITE, TFT_BLACK);
  lcd.print(fuelStatusStr);
}

void sendOBDRequest(uint8_t pid) {
  twai_message_t tx_frame;
  tx_frame.identifier = 0x7DF; // Standard OBD-II functional request ID
  tx_frame.extd = 0;
  tx_frame.data_length_code = 8;
  tx_frame.data[0] = 0x02;     // 2 data bytes follow
  tx_frame.data[1] = 0x01;     // Mode 1: Current powertrain diagnostic data
  tx_frame.data[2] = pid;      // Requested PID
  tx_frame.data[3] = 0x55;     // Padding
  tx_frame.data[4] = 0x55;
  tx_frame.data[5] = 0x55;
  tx_frame.data[6] = 0x55;
  tx_frame.data[7] = 0x55;

  twai_transmit(&tx_frame, pdMS_TO_TICKS(10));
}

void parseOBDResponse(twai_message_t &rx) {
  if (rx.identifier >= 0x7E8 && rx.identifier <= 0x7EF) {
    if (rx.data[1] == 0x41) { // Positive response to Mode 1
      uint8_t pid = rx.data[2];
      ecuConnected = true;
      lastResponseTime = millis();

      switch (pid) {
        case PID_BATT_VOLT:
          battVoltage = ((float)((rx.data[3] * 256) + rx.data[4])) / 1000.0;
          break;
        case PID_COOLANT_TEMP:
          coolantTemp = (int)rx.data[3] - 40;
          break;
        case PID_MAP_PRESS:
          mapPressure = (int)rx.data[3];
          break;
        case PID_TIMING_ADV:
          timingAdvance = ((float)rx.data[3] - 128.0) / 2.0;
          break;
        case PID_CALC_LOAD:
          engineLoad = ((float)rx.data[3] * 100.0) / 255.0;
          break;
        case PID_FUEL_STATUS:
          switch (rx.data[3]) {
            case 1: fuelStatusStr = "Open: Low Temp"; break;
            case 2: fuelStatusStr = "Closed: O2 Active"; break;
            case 4: fuelStatusStr = "Open: Open Loop / Fault"; break;
            case 8: fuelStatusStr = "Closed: O2 Fault"; break;
            default: fuelStatusStr = "Open / Other"; break;
          }
          break;
      }
    }
  }
}

void setup() {
  Serial.begin(115200);
  delay(500);

  lcd.init();
  delay(100);
  lcd.setRotation(0);
  lcd.setBrightness(128);

  drawStaticLabels(); // Draw static background layout once
  setupCAN();
}

void loop() {
  // 1. Sequentially request OBD-II PIDs
  if (millis() - lastPollTime >= pollInterval) {
    lastPollTime = millis();
    sendOBDRequest(pidList[currentPidIndex]);
    currentPidIndex = (currentPidIndex + 1) % numPids;
  }

  // 2. Read incoming CAN bus frames non-blocking
  twai_message_t rx_frame;
  while (twai_receive(&rx_frame, 0) == ESP_OK) {
    parseOBDResponse(rx_frame);
  }

  // 3. Timeout check for ECU connection status (loss of signal after 2 seconds)
  bool previousConnectionState = ecuConnected;
  if (millis() - lastResponseTime > 2000) {
    ecuConnected = false;
  }

  // Refresh telemetry numbers smoothly every 250ms without screen clearing
  static unsigned long lastRedraw = 0;
  if (millis() - lastRedraw >= 250 || (previousConnectionState != ecuConnected)) {
    lastRedraw = millis();
    updateTelemetryDisplay();
  }
}