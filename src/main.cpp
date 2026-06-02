#include <Arduino.h>

#include <Wire.h>

#include <lvgl.h>

#include "Arduino_GFX_Library.h"

#include "HWCDC.h"

HWCDC USBSerial;

#define LCD_CS 12

#define LCD_SCLK 11

#define LCD_SDIO0 4

#define LCD_SDIO1 5

#define LCD_SDIO2 6

#define LCD_SDIO3 7

#define LCD_WIDTH 410

#define LCD_HEIGHT 502

#define IIC_SDA 15

#define IIC_SCL 14

#define TOUCH_ADDR 0x38

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

static lv_color_t lv_buf[LCD_WIDTH * 40];

static lv_display_t *display;

static lv_indev_t *touch_indev;

lv_obj_t *counterLabel;

int counter = 0;

unsigned long lastTick = 0;

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

bool readTouch(uint16_t *x, uint16_t *y)

{

  Wire.beginTransmission(TOUCH_ADDR);

  Wire.write(0x02);

  if (Wire.endTransmission(false) != 0)

  {

    return false;
  }

  uint8_t data[5];

  if (Wire.requestFrom(TOUCH_ADDR, 5) != 5)

  {

    return false;
  }

  for (int i = 0; i < 5; i++)

  {

    data[i] = Wire.read();
  }

  uint8_t touched = data[0] & 0x0F;

  if (touched == 0)

  {

    return false;
  }

  uint16_t rawX = ((data[1] & 0x0F) << 8) | data[2];

  uint16_t rawY = ((data[3] & 0x0F) << 8) | data[4];

  *x = rawX;

  *y = rawY;

  if (*x >= LCD_WIDTH)

    *x = LCD_WIDTH - 1;

  if (*y >= LCD_HEIGHT)

    *y = LCD_HEIGHT - 1;

  return true;
}

void my_touch_read(lv_indev_t *indev, lv_indev_data_t *data)

{

  uint16_t x;

  uint16_t y;

  if (readTouch(&x, &y))

  {

    data->state = LV_INDEV_STATE_PRESSED;

    data->point.x = x;

    data->point.y = y;

    USBSerial.print("Touch x=");

    USBSerial.print(x);

    USBSerial.print(" y=");

    USBSerial.println(y);
  }

  else

  {

    data->state = LV_INDEV_STATE_RELEASED;
  }
}

void button_event_cb(lv_event_t *e)

{

  counter++;

  char buf[32];

  sprintf(buf, "Count: %d", counter);

  lv_label_set_text(counterLabel, buf);

  USBSerial.print("Counter: ");

  USBSerial.println(counter);
}

void createUI()

{

  lv_obj_t *screen = lv_screen_active();

  lv_obj_set_style_bg_color(screen, lv_color_hex(0x000000), 0);

  counterLabel = lv_label_create(screen);

  lv_label_set_text(counterLabel, "Count: 0");

  lv_obj_set_style_text_color(counterLabel, lv_color_hex(0xFFFFFF), 0);

  lv_obj_align(counterLabel, LV_ALIGN_TOP_MID, 0, 100);

  lv_obj_t *btn = lv_button_create(screen);

  lv_obj_set_size(btn, 220, 90);

  lv_obj_align(btn, LV_ALIGN_CENTER, 0, 0);

  lv_obj_add_event_cb(

      btn,

      button_event_cb,

      LV_EVENT_CLICKED,

      NULL);

  lv_obj_t *label = lv_label_create(btn);

  lv_label_set_text(label, "Tap Me");

  lv_obj_center(label);
}

void setup()

{

  USBSerial.begin(115200);

  delay(1000);

  Wire.begin(IIC_SDA, IIC_SCL);

  Wire.setClock(400000);

  if (!gfx->begin())

  {

    USBSerial.println("Display failed");

    while (1)

    {

      delay(100);
    }
  }

  gfx->fillScreen(0x0000);

  lv_init();

  display = lv_display_create(LCD_WIDTH, LCD_HEIGHT);

  lv_display_set_flush_cb(display, my_disp_flush);

  lv_display_set_buffers(

      display,

      lv_buf,

      NULL,

      sizeof(lv_buf),

      LV_DISPLAY_RENDER_MODE_PARTIAL);

  touch_indev = lv_indev_create();

  lv_indev_set_type(touch_indev, LV_INDEV_TYPE_POINTER);

  lv_indev_set_read_cb(touch_indev, my_touch_read);

  createUI();

  lastTick = millis();

  USBSerial.println("LVGL counter with FT3168 touch started");
}

void loop()

{

  unsigned long now = millis();

  lv_tick_inc(now - lastTick);

  lastTick = now;

  lv_timer_handler();

  delay(5);
}