#include <Arduino.h>
#include <WiFi.h>
#include <Wire.h>
#include <XPowersLib.h>
#include "Arduino_GFX_Library.h"
#include "HWCDC.h"
#include <lvgl.h>

HWCDC USBSerial;
XPowersAXP2101 PMU;

#define LCD_CS 12
#define LCD_SCLK 11
#define LCD_SDIO0 4
#define LCD_SDIO1 5
#define LCD_SDIO2 6
#define LCD_SDIO3 7

#define LCD_WIDTH 410
#define LCD_HEIGHT 502

#define BOOT_BUTTON 0

#define IIC_SDA 15
#define IIC_SCL 14

Arduino_DataBus *bus = new Arduino_ESP32QSPI(
    LCD_CS, LCD_SCLK, LCD_SDIO0, LCD_SDIO1, LCD_SDIO2, LCD_SDIO3);

Arduino_GFX *gfx = new Arduino_CO5300(
    bus,
    -1,
    0,
    LCD_WIDTH,
    LCD_HEIGHT,
    22,
    0,
    0,
    0);

bool screenOn = true;
bool pmuReady = false;

unsigned long lastActivity = 0;
unsigned long lastClockUpdate = 0;
unsigned long lastButtonPress = 0;
unsigned long lastBatteryUpdate = 0;
unsigned long lastLvglTick = 0;

const unsigned long IDLE_TIMEOUT = 10000;
const unsigned long DEBOUNCE_TIME = 300;
const unsigned long BATTERY_UPDATE_TIME = 30000;

int fakeHour = 2;
int fakeMinute = 37;
int fakeSecond = 0;

int batteryPercent = 0;
int batteryVoltage = 0;

static lv_color_t lv_buf[LCD_WIDTH * 40];
static lv_display_t *display;

lv_obj_t *mainScreen;
lv_obj_t *timeLabel;
lv_obj_t *countdownLabel;
lv_obj_t *batteryBar;
lv_obj_t *batteryPercentLabel;
lv_obj_t *batteryVoltageLabel;
lv_obj_t *statusLabel;

void my_disp_flush(lv_display_t *disp, const lv_area_t *area, uint8_t *px_map)
{
  uint32_t w = area->x2 - area->x1 + 1;
  uint32_t h = area->y2 - area->y1 + 1;

  gfx->draw16bitRGBBitmap(
      area->x1,
      area->y1,
      (uint16_t *)px_map,
      w,
      h);

  lv_display_flush_ready(disp);
}

void setupLVGL()
{
  lv_init();

  display = lv_display_create(LCD_WIDTH, LCD_HEIGHT);
  lv_display_set_flush_cb(display, my_disp_flush);

  lv_display_set_buffers(
      display,
      lv_buf,
      NULL,
      sizeof(lv_buf),
      LV_DISPLAY_RENDER_MODE_PARTIAL);
}

void updateTimeUI()
{
  char timeText[16];
  sprintf(timeText, "%02d:%02d:%02d", fakeHour, fakeMinute, fakeSecond);
  lv_label_set_text(timeLabel, timeText);
}

void updateCountdownUI()
{
  if (!screenOn)
    return;

  unsigned long now = millis();
  long remaining = (IDLE_TIMEOUT - (now - lastActivity)) / 1000;

  if (remaining < 0)
    remaining = 0;

  char countdownText[32];
  sprintf(countdownText, "Sleep in %lds", remaining);
  lv_label_set_text(countdownLabel, countdownText);
}

void updateBatteryUI()
{
  if (!pmuReady)
    return;

  batteryPercent = PMU.getBatteryPercent();
  batteryVoltage = PMU.getBattVoltage();

  if (batteryPercent < 0)
    batteryPercent = 0;
  if (batteryPercent > 100)
    batteryPercent = 100;

  lv_bar_set_value(batteryBar, batteryPercent, LV_ANIM_ON);

  char percentText[16];
  sprintf(percentText, "%d%%", batteryPercent);
  lv_label_set_text(batteryPercentLabel, percentText);

  char voltageText[32];
  sprintf(voltageText, "%d mV", batteryVoltage);
  lv_label_set_text(batteryVoltageLabel, voltageText);

  USBSerial.print("Battery: ");
  USBSerial.print(batteryPercent);
  USBSerial.print("% Voltage: ");
  USBSerial.print(batteryVoltage);
  USBSerial.println(" mV");
}

void createWatchUI()
{
  mainScreen = lv_screen_active();

  lv_obj_set_style_bg_color(mainScreen, lv_color_hex(0x05070A), 0);
  lv_obj_set_style_bg_opa(mainScreen, LV_OPA_COVER, 0);

  timeLabel = lv_label_create(mainScreen);
  lv_label_set_text(timeLabel, "02:37:00");
  lv_obj_set_style_text_color(timeLabel, lv_color_hex(0x22E6FF), 0);
  lv_obj_set_style_text_font(timeLabel, &lv_font_montserrat_14, 0);
  lv_obj_set_style_text_letter_space(timeLabel, 2, 0);
  lv_obj_set_style_transform_scale(timeLabel, 360, 0);
  lv_obj_align(timeLabel, LV_ALIGN_CENTER, 0, -120);

  countdownLabel = lv_label_create(mainScreen);
  lv_label_set_text(countdownLabel, "Sleep in 10s");
  lv_obj_set_style_text_color(countdownLabel, lv_color_hex(0x9CA3AF), 0);
  lv_obj_set_style_text_font(countdownLabel, &lv_font_montserrat_14, 0);
  lv_obj_align(countdownLabel, LV_ALIGN_CENTER, 0, -65);

  lv_obj_t *batteryCard = lv_obj_create(mainScreen);
  lv_obj_set_size(batteryCard, 310, 150);
  lv_obj_align(batteryCard, LV_ALIGN_CENTER, 0, 60);

  lv_obj_set_style_radius(batteryCard, 30, 0);
  lv_obj_set_style_bg_color(batteryCard, lv_color_hex(0x111827), 0);
  lv_obj_set_style_bg_opa(batteryCard, LV_OPA_COVER, 0);
  lv_obj_set_style_border_width(batteryCard, 2, 0);
  lv_obj_set_style_border_color(batteryCard, lv_color_hex(0x263244), 0);
  lv_obj_set_style_pad_all(batteryCard, 18, 0);

  lv_obj_t *title = lv_label_create(batteryCard);
  lv_label_set_text(title, "BATTERY");
  lv_obj_set_style_text_color(title, lv_color_hex(0x9CA3AF), 0);
  lv_obj_set_style_text_font(title, &lv_font_montserrat_14, 0);
  lv_obj_align(title, LV_ALIGN_TOP_LEFT, 0, 0);

  batteryPercentLabel = lv_label_create(batteryCard);
  lv_label_set_text(batteryPercentLabel, "--%");
  lv_obj_set_style_text_color(batteryPercentLabel, lv_color_hex(0xFFFFFF), 0);
  lv_obj_set_style_text_font(batteryPercentLabel, &lv_font_montserrat_14, 0);
  lv_obj_set_style_transform_scale(batteryPercentLabel, 230, 0);
  lv_obj_align(batteryPercentLabel, LV_ALIGN_TOP_RIGHT, -10, 0);

  batteryBar = lv_bar_create(batteryCard);
  lv_obj_set_size(batteryBar, 255, 30);
  lv_obj_align(batteryBar, LV_ALIGN_CENTER, 0, 20);
  lv_bar_set_range(batteryBar, 0, 100);
  lv_bar_set_value(batteryBar, 0, LV_ANIM_OFF);

  lv_obj_set_style_radius(batteryBar, 16, LV_PART_MAIN);
  lv_obj_set_style_bg_color(batteryBar, lv_color_hex(0x374151), LV_PART_MAIN);
  lv_obj_set_style_bg_opa(batteryBar, LV_OPA_COVER, LV_PART_MAIN);

  lv_obj_set_style_radius(batteryBar, 16, LV_PART_INDICATOR);
  lv_obj_set_style_bg_color(batteryBar, lv_color_hex(0x22C55E), LV_PART_INDICATOR);
  lv_obj_set_style_bg_opa(batteryBar, LV_OPA_COVER, LV_PART_INDICATOR);

  batteryVoltageLabel = lv_label_create(batteryCard);
  lv_label_set_text(batteryVoltageLabel, "---- mV");
  lv_obj_set_style_text_color(batteryVoltageLabel, lv_color_hex(0x9CA3AF), 0);
  lv_obj_set_style_text_font(batteryVoltageLabel, &lv_font_montserrat_14, 0);
  lv_obj_align(batteryVoltageLabel, LV_ALIGN_BOTTOM_LEFT, 0, 0);

  statusLabel = lv_label_create(mainScreen);
  lv_label_set_text(statusLabel, "BOOT = wake / reset timer");
  lv_obj_set_style_text_color(statusLabel, lv_color_hex(0x6B7280), 0);
  lv_obj_set_style_text_font(statusLabel, &lv_font_montserrat_14, 0);
  lv_obj_align(statusLabel, LV_ALIGN_BOTTOM_MID, 0, -30);

  updateTimeUI();
  updateCountdownUI();
  updateBatteryUI();
}

void turnScreenOff()
{
  lv_obj_add_flag(mainScreen, LV_OBJ_FLAG_HIDDEN);
  gfx->fillScreen(0x0000);

  screenOn = false;
  USBSerial.println("Screen off");
}

void turnScreenOn()
{
  screenOn = true;

  lastActivity = millis();
  lastClockUpdate = millis();
  lastBatteryUpdate = millis();

  lv_obj_clear_flag(mainScreen, LV_OBJ_FLAG_HIDDEN);

  updateTimeUI();
  updateCountdownUI();
  updateBatteryUI();

  lv_obj_invalidate(mainScreen);

  USBSerial.println("Screen on");
}

void handleButton()
{
  if (digitalRead(BOOT_BUTTON) == LOW)
  {
    if (millis() - lastButtonPress > DEBOUNCE_TIME)
    {
      lastButtonPress = millis();

      if (!screenOn)
      {
        turnScreenOn();
      }
      else
      {
        lastActivity = millis();
        updateCountdownUI();
        USBSerial.println("Activity reset");
      }
    }

    while (digitalRead(BOOT_BUTTON) == LOW)
    {
      delay(10);
    }
  }
}

void updateFakeClock()
{
  fakeSecond++;

  if (fakeSecond >= 60)
  {
    fakeSecond = 0;
    fakeMinute++;

    if (fakeMinute >= 60)
    {
      fakeMinute = 0;
      fakeHour++;

      if (fakeHour >= 24)
      {
        fakeHour = 0;
      }
    }
  }

  updateTimeUI();
  updateCountdownUI();
}

void setup()
{
  USBSerial.begin(115200);
  delay(1000);

  pinMode(BOOT_BUTTON, INPUT_PULLUP);

  WiFi.disconnect(true);
  WiFi.mode(WIFI_OFF);
  btStop();

  setCpuFrequencyMhz(80);

  Wire.begin(IIC_SDA, IIC_SCL);

  if (PMU.begin(Wire, AXP2101_SLAVE_ADDRESS, IIC_SDA, IIC_SCL))
  {
    pmuReady = true;
    USBSerial.println("AXP2101 found");
  }
  else
  {
    pmuReady = false;
    USBSerial.println("AXP2101 not found");
  }

  if (!gfx->begin())
  {
    USBSerial.println("Display failed");

    while (1)
    {
      delay(100);
    }
  }

  setupLVGL();
  createWatchUI();

  lastActivity = millis();
  lastClockUpdate = millis();
  lastBatteryUpdate = millis();
  lastLvglTick = millis();

  USBSerial.println("LVGL battery watch started");
}

void loop()
{
  unsigned long now = millis();

  lv_tick_inc(now - lastLvglTick);
  lastLvglTick = now;

  handleButton();

  if (screenOn && now - lastClockUpdate >= 1000)
  {
    lastClockUpdate = now;
    updateFakeClock();
  }

  if (screenOn && now - lastBatteryUpdate >= BATTERY_UPDATE_TIME)
  {
    lastBatteryUpdate = now;
    updateBatteryUI();
  }

  if (screenOn && now - lastActivity >= IDLE_TIMEOUT)
  {
    turnScreenOff();
  }

  if (screenOn)
  {
    lv_timer_handler();
  }

  delay(5);
}