#include <ArduinoOTA.h>
#include <ESPmDNS.h>
#include <FastLED.h>
#include <WebServer.h>
#include <WiFi.h>

/* Function Prototype  */
void doInitialize(void);
void displayNotice(void);
void changeStatus(void);
void checkStatus(void);
void checkSchedule(void);
void checkAlart(void);

/* 基本属性定義 */
#define SPI_SPEED 115200 // SPI通信速度

/* 利用人数 */
//#define NUM_USER 1

/* 飲み忘れアラートまでの時間(S) */
#define FORGET_ALERT_TIME 5

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

uint8_t gGroup = 4;           // 投薬タイミングのグループ数
uint8_t gDrawerIsChanged = 0; // 引き出しの開け閉めフラグ
uint8_t gDrawerStatus = 0;    // 引き出しの状態
uint8_t gNoticeFlag = 0;      // 通知フラグ
uint8_t gForgetFlag = 0;      // 飲み忘れフラグ
time_t gDrawerMovedTime;      // 閉め忘れ対策
time_t gScheduledTime;        // 飲み忘れ対策
time_t gCurrentTime;          // 現在の時刻
struct tm gTimeInfo;          // 時刻を格納するオブジェクト

uint64_t gSchedule[4] = {20, 31, 28, 19};

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
  gCurrentTime = time(NULL);
  checkStatus();
  checkSchedule();
  checkAlart();

  FastLED.show();

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

  gDrawerStatus = digitalRead(SW1_PIN);
}

/*****************************< LED functions >*****************************/

void displayNotice()
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
      if (gNoticeFlag & (1 << g))
      {
        leds[numUnit * g + i + !!(NUM_LEDS % gGroup)] = groupColor[g + 1]; // 3グループの時は余りが出るので両端のLEDは光らせない
      }
      if (gForgetFlag & (1 << g))
      {
        leds[numUnit * g + i + !!(NUM_LEDS % gGroup)] = groupColor[0]; // 飲み忘れの場合
      }
    }
  }
}

void changeStatus()
{
  gDrawerIsChanged = 1;
}

void checkStatus()
{
  static uint8_t sPrevStatus = 0;
  static uint8_t sCounter = 0;
  if (gDrawerIsChanged)
  {
    uint8_t newStatus = digitalRead(SW1_PIN);
    Serial.print(newStatus);
    if (newStatus == sPrevStatus)
    {
      sCounter++;
      if (sCounter > 10) // チャタリング防止の１秒待ち
      {
        if (gDrawerStatus == 0 && newStatus == 1) // 引き出しを開けたとき 飲み忘れフラグを通知フラグに戻してからクリア
        {
          gDrawerStatus = 1;
          gNoticeFlag = gNoticeFlag | gForgetFlag;
          gForgetFlag = 0;
        }
        else if (gDrawerStatus == 1 && newStatus == 0) // 引き出しを閉じたとき 通知フラグをクリア
        {
          gDrawerStatus = 0;
          gNoticeFlag = 0;

          displayNotice();
        }
        else if (gDrawerStatus == 99 && newStatus == 0) // 閉め忘れを閉じた
        {
          gDrawerStatus = 0;
          displayNotice();
        }
        gDrawerMovedTime = gCurrentTime;
        sPrevStatus = newStatus;
        gDrawerIsChanged = 0;

        Serial.print(":");
        Serial.println(gDrawerStatus);
      }
    }
    else
    {
      sPrevStatus = newStatus;
      sCounter = 0;
    }
  }
}

void checkAlart()
{
  if (gDrawerStatus == 1 && (gCurrentTime - gDrawerMovedTime > LEFTOPEN_ALERT_TIME)) // 開いたまま時間経過（閉め忘れ=飲んではいる）
  {
    gDrawerStatus = 99;
    gNoticeFlag = 0;
    Serial.println("閉め忘れ");
  }
  if (gDrawerStatus == 0 && (gCurrentTime - gScheduledTime > FORGET_ALERT_TIME) && gNoticeFlag) // 通知があるのに一定時間開けていない（飲み忘れ）
  {
    gForgetFlag = gForgetFlag | gNoticeFlag;
    gNoticeFlag = 0;
    Serial.println("飲み忘れ");
  }

  if (gDrawerStatus == 99)
  {
    for (uint8_t i = 0; i < NUM_LEDS; ++i)
    {
      leds[i] = ColorFromPalette(HeatColors_p, gHue + i, 255);
    }
  }
}

void checkSchedule()
{
  static uint64_t sReserveTime[4] = {gCurrentTime + gSchedule[0], gCurrentTime + gSchedule[1], gCurrentTime + gSchedule[2], gCurrentTime + gSchedule[3]};

  if (gCurrentTime > sReserveTime[0])
  {
    gNoticeFlag = gNoticeFlag | 1;
    gScheduledTime = sReserveTime[0];
    sReserveTime[0] = gCurrentTime + gSchedule[0];
  }
  if (gCurrentTime > sReserveTime[1])
  {
    gNoticeFlag = gNoticeFlag | 2;
    gScheduledTime = sReserveTime[1];
    sReserveTime[1] = gCurrentTime + gSchedule[1];
  }
  if (gCurrentTime > sReserveTime[2])
  {
    gNoticeFlag = gNoticeFlag | 4;
    gScheduledTime = sReserveTime[2];
    sReserveTime[2] = gCurrentTime + gSchedule[2];
  }
  if (gCurrentTime > sReserveTime[3])
  {
    gNoticeFlag = gNoticeFlag | 8;
    gScheduledTime = sReserveTime[3];
    sReserveTime[3] = gCurrentTime + gSchedule[3];
  }

  displayNotice();
}