#include <ArduinoOTA.h>
#include <ESPmDNS.h>
#include <FastLED.h>
#include <WebServer.h>
#include <WiFi.h>

/* Function Prototype  */
void doInitialize(void);
void notification(void);
void changeStatus(void);
void checkStatus(void);
void checkSchedule(void);
void checkOpenAlart(void);

/* 基本属性定義 */
#define SPI_SPEED 115200 // SPI通信速度

/* 利用人数 */
//#define NUM_USER 1

/* 飲み忘れアラートまでの時間(S) */
#define FORGET_ALERT_TIME 30

/* 締め忘れアラートまでの時間(S) */
#define LEFTOPEN_ALERT_TIME 10

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
#define COLOR_ORDER GRB
#define NUM_LEDS 32
#define BRIGHTNESS 64

/* センサー関連 */
#define SW1_PIN 5

CRGB leds[NUM_LEDS];
CRGB groupColor[] = {CRGB::Red, CRGB::Blue, CRGB::Green, CRGB::Purple, CRGB::Yellow}; // グループのカラー設定
uint8_t gBrightness = BRIGHTNESS;
uint8_t gHue = 0; // rotating "base color" used by many of the patterns

uint8_t gGroup = 4;
uint8_t gChangeStatus = 0;
uint8_t gStatus = 0;
uint8_t gFlag = 0;
time_t gPrevTime;    //  チャタリング対策
time_t gNowTime;
struct tm gTimeInfo; //時刻を格納するオブジェクト

uint64_t gSchedule[4] = {10, 30, 10, 20};

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
  gNowTime = time(NULL);
  checkStatus();
  checkSchedule();
  checkOpenAlart();

  FastLED.delay(1000 / 30); // insert a delay to keep the framerate modest
  EVERY_N_MILLISECONDS(20)
  {
    gHue++; // slowly cycle the "base color" through the rainbow
  }
  //  displayData();
  // Serial.print();
}

/*****************************< Other functions >*****************************/
/*  初期化処理  */
void doInitialize()
{
  Serial.begin(SPI_SPEED);

  connectToWifi();  // Wi-Fiルーターに接続する
  startMDNS();      // Multicast DNS
  startWebServer(); // WebServer
  setTime();        // 初回の時刻合わせ
  startOTA();

  FastLED.addLeds<LED_TYPE, DATA_PIN, COLOR_ORDER>(leds, NUM_LEDS);
  FastLED.setBrightness(gBrightness);
  Serial.println("LED Controller start ...");

  pinMode(SW1_PIN, INPUT_PULLDOWN);
  attachInterrupt(SW1_PIN, changeStatus, CHANGE);
}

/*****************************< LED functions >*****************************/

void notification()
{
  FastLED.clear();
  if (!gGroup)
  {
    return;
  }
  uint8_t numUnit = NUM_LEDS / gGroup; // 一グループあたりのLED数
  for (uint8_t g = 0; g < gGroup; ++g)
  {
    for (uint8_t i = 0; i < numUnit; ++i)
    {
      if (gFlag & (1 << g))
      {
        leds[numUnit * g + i + !!(NUM_LEDS % gGroup)] = groupColor[g + 1]; // 3グループの時は余りが出るので両端のLEDは光らせない
      }
    }
  }
  FastLED.show();
}

void changeStatus()
{
  gChangeStatus = 1;
}

void checkStatus()
{
  static uint8_t sPrevStatus = 0;
  if (gChangeStatus)
  {
    uint8_t newStatus = digitalRead(SW1_PIN);
    Serial.print(newStatus);
    if (newStatus == sPrevStatus)
    {
      if (gNowTime - gPrevTime > 1)
      {
        if ((gStatus == 0 || gStatus == 2) && newStatus == 1) // リセットのルーチン作り終えたら|| gStatus == 2は消すこと
        {
          gStatus = 1;
        }
        else if (gStatus == 1 && newStatus == 0)
        {
          gStatus = 2;
        }
        else if (gStatus == 99 && newStatus == 0)
        {
          gStatus = 0;
          notification();
        }
        sPrevStatus = newStatus;
        gChangeStatus = 0;
        Serial.print(gNowTime);
        Serial.print(":");
        Serial.println(gStatus);
      }
    }
    else
    {
      sPrevStatus = newStatus;
      gPrevTime = gNowTime;
    }
  }
}

void checkOpenAlart()
{
  if (gStatus == 1 && (gNowTime - gPrevTime > LEFTOPEN_ALERT_TIME)) // 開いたまま時間経過（閉め忘れ=飲んではいる）
  {
    gStatus = 99;
    gFlag = 0;
    Serial.println("閉め忘れ");
  }
  if (gStatus == 0 && (gNowTime - gPrevTime > FORGET_ALERT_TIME)&&gFlag) // 通知があるのに一定時間空けていない（飲み忘れ）
  {
      gStatus = 100;
      Serial.println("飲み忘れ");
  }

  if (gStatus == 99)
  {
    for (uint8_t i = 0; i < NUM_LEDS; ++i)
    {
      leds[i] = ColorFromPalette(HeatColors_p, gHue + i, 255);
    }
  }
  FastLED.show();
}

void checkSchedule()
{
  uint8_t flag = 0;
  static uint64_t sReserveTime[4] = {gNowTime + gSchedule[0], gNowTime + gSchedule[1], gNowTime + gSchedule[2], gNowTime + gSchedule[3]};

  if (gNowTime > sReserveTime[0])
  {
    flag = 1;
  }
  if (gNowTime > sReserveTime[1])
  {
    flag = flag | 2;
  }
  if (gNowTime > sReserveTime[2])
  {
    flag = flag | 4;
  }
  if (gNowTime > sReserveTime[3])
  {
    flag = flag | 8;
  }
  if (flag != gFlag)
  {
    gFlag = flag;
    notification();
  }
}