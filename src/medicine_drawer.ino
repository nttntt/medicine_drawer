#include <ArduinoOTA.h>
#include <ESPmDNS.h>
#include <FastLED.h>
#include <WebServer.h>
#include <WiFi.h>

/* Function Prototype */
void doInitialize(void);

/* 基本属性定義 */
#define SPI_SPEED 115200 // SPI通信速度

// ルーター接続情報
#define WIFI_SSID "0856g"
#define WIFI_PASSWORD "nttnttntt"
// Multicast DNS名
#define mDNS_NAME "medicine_drawer"
// 時間取得
#define JST 9 * 3600L
#define NTPServer1 "192.168.1.10"
#define NTPServer2 "time.google.com"

// Webサーバーオブジェクト
#define HTTP_PORT 80
WebServer server(HTTP_PORT);

/* LED関連 */

#define DATA_PIN 25
#define LED_TYPE WS2812B
#define COLOR_ORDER RGB
#define NUM_LEDS 32
#define BRIGHTNESS 64
#define FRAMES_PER_SECOND 30

CRGB leds[NUM_LEDS];

uint8_t gBrightness = BRIGHTNESS;
uint8_t gCurrentPatternNumber = 0; // Index number of which pattern is current
uint8_t gHue = 0;                  // rotating "base color" used by many of the patterns



uint8_t gTestHue=44;

/*****************************************************************************
                            Predetermined Sequence
 *****************************************************************************/
void setup()
{
  // 初期化処理
  doInitialize();
  // 通信関連のタスク起動
  xTaskCreatePinnedToCore(htmlTask, "htmlTask", 8192, NULL, 10, NULL, 0);
}

void loop()
{
  solid();

  FastLED.show();

  FastLED.delay(
      1000 / FRAMES_PER_SECOND); // insert a delay to keep the framerate modest
  EVERY_N_MILLISECONDS(20)
  {
    gHue++; // slowly cycle the "base color" through the rainbow
  }
  //  displayData();
}

/*****************************< Other functions >*****************************/
/*  初期化処理  */
void doInitialize()
{
  Serial.begin(SPI_SPEED);

  FastLED.addLeds<LED_TYPE, DATA_PIN, COLOR_ORDER>(leds, NUM_LEDS);
  FastLED.setBrightness(gBrightness);

  Serial.println("LED Controller start ...");
}

/*****************************< LED functions >*****************************/

void solid()
{
  // 普通の光
  static uint8_t sHue = 44;

  fill_solid(leds, NUM_LEDS, CHSV(gTestHue, 255, 255));
}
