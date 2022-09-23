#include <ArduinoOTA.h>
#include <ESPmDNS.h>
#include <FastLED.h>
#include <WebServer.h>
#include <WiFi.h>
#include <EEPROM.h>

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
#define DEVICE_NAME "medicine_drawer"
// 時間取得
#define JST 9 * 3600L
#define NTPServer1 "192.168.1.10"
#define NTPServer2 "time.google.com"

// Webサーバーオブジェクト
#define HTTP_PORT 80

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

WebServer server(HTTP_PORT);

uint8_t gGroup = 4;           // 投薬タイミングのグループ数
uint8_t gDrawerIsChanged = 0; // 引き出しの開け閉めフラグ
uint8_t gDrawerStatus = 0;    // 引き出しの状態
uint8_t gNoticeFlag = 0;      // 通知フラグ
uint8_t gAlartFlag = 0;       // 飲み忘れフラグ
time_t gDrawerMovedTime;      // 閉め忘れ対策
time_t gScheduledTime;        // 飲み忘れ対策
time_t gCurrentTime;          // 現在の時刻
struct tm gTimeInfo;          // 時刻を格納するオブジェクト

struct _save_data
{
  uint8_t hour[3];
  uint8_t minutes[3];
  uint8_t interval[4][3];
  time_t nextSchedule[4][3];
  char check[16];
};
struct _save_data data;
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
  startOTA();
  setTime();          // 初回の時刻合わせ
  setFirstSchedule(); // 初回の予約時間設定

  FastLED.addLeds<LED_TYPE, DATA_PIN, COLOR_ORDER>(leds, NUM_LEDS);
  FastLED.setBrightness(gBrightness);
  Serial.println("LED Controller start ...");

  pinMode(SW1_PIN, INPUT_PULLDOWN);
  attachInterrupt(SW1_PIN, changeStatus, CHANGE);

  gDrawerStatus = digitalRead(SW1_PIN);
}

void setFirstSchedule()
{
  EEPROM.begin(128);
  EEPROM.get(0, data);
  if (strcmp(data.check, DEVICE_NAME)) //保存データかチェックしてデータが無い場合0で初期化
  {
    for (uint8_t i = 0; i < 3; ++i)
    {
      data.hour[i] = 0;
      data.minutes[i] = 0;
    }
    for (uint8_t g = 0; g < 4; ++g)
    {
      for (uint8_t i = 0; i < 3; ++i)
      {
        data.interval[g][i] = 0;
      }
    }
    strncpy(data.check, DEVICE_NAME, 16); // 保存データの証にデバイス名を登録
  }
  // 予約時刻をtime_t形式でに変換
  gCurrentTime = time(NULL);
  for (uint8_t g = 0; g < 4; ++g)
  {
    for (uint8_t i = 0; i < 3; ++i)
    {
      if (data.interval[g][i]) // インターバルが0以外の時
      {
        getLocalTime(&gTimeInfo);
        struct tm tmpSchedule = {0, data.minutes[i], data.hour[i], gTimeInfo.tm_mday, gTimeInfo.tm_mon, gTimeInfo.tm_year}; // 今日の日付で予約時間を仮計算
        time_t tmpTime = mktime(&tmpSchedule);                                                                              // 仮予約時間をtime_tへ
        if (tmpTime < gCurrentTime)                                                                                         // 予約時間を過ぎていたら24時間後の予約に
        {
          tmpTime += 24 * 60 * 60;
        }
        data.nextSchedule[g][i] = tmpTime;
      }
      else
      {
        data.nextSchedule[g][i] = 0;
      }
    }
  }

  // /////////////////////////////////////////////
  gTimeInfo = *localtime(&data.nextSchedule[0][0]);
  Serial.printf("%04d/%02d/%02d %02d:%02d:%02d\n", gTimeInfo.tm_year + 1900, gTimeInfo.tm_mon + 1, gTimeInfo.tm_mday, gTimeInfo.tm_hour, gTimeInfo.tm_min, gTimeInfo.tm_sec);
  gTimeInfo = *localtime(&data.nextSchedule[1][1]);
  Serial.printf("%04d/%02d/%02d %02d:%02d:%02d\n", gTimeInfo.tm_year + 1900, gTimeInfo.tm_mon + 1, gTimeInfo.tm_mday, gTimeInfo.tm_hour, gTimeInfo.tm_min, gTimeInfo.tm_sec);
  gTimeInfo = *localtime(&data.nextSchedule[2][2]);
  Serial.printf("%04d/%02d/%02d %02d:%02d:%02d\n", gTimeInfo.tm_year + 1900, gTimeInfo.tm_mon + 1, gTimeInfo.tm_mday, gTimeInfo.tm_hour, gTimeInfo.tm_min, gTimeInfo.tm_sec);
  gTimeInfo = *localtime(&data.nextSchedule[3][0]);
  Serial.printf("%04d/%02d/%02d %02d:%02d:%02d\n", gTimeInfo.tm_year + 1900, gTimeInfo.tm_mon + 1, gTimeInfo.tm_mday, gTimeInfo.tm_hour, gTimeInfo.tm_min, gTimeInfo.tm_sec);

  data.nextSchedule[0][0] = 31;
  data.nextSchedule[1][0] = 31;
  data.nextSchedule[2][0] = 28;
  data.nextSchedule[3][0] = 19;
  // /////////////////////////////////////////////
  EEPROM.put(0, data);
  EEPROM.commit();
}

void setNextSchedule()
{

  getLocalTime(&gTimeInfo);
  struct tm tmpSchedule = {0, data.minutes[0], data.hour[0], gTimeInfo.tm_mday, gTimeInfo.tm_mon, gTimeInfo.tm_year}; // 今日の日付で予約時間を仮計算
  time_t tmpTime = mktime(&tmpSchedule);                                                                              // 仮予約時間をtime_tへ
  if (tmpTime < gCurrentTime)                                                                                         // 予約時間を過ぎていたら24時間後の予約に
  {
    tmpTime += 24 * 60 * 60;
  }

  data.nextSchedule[0][0] = tmpTime;
  tmpSchedule = *localtime(&tmpTime);
  gTimeInfo = tmpSchedule;
  Serial.printf("%04d/%02d/%02d %02d:%02d:%02d\n", gTimeInfo.tm_year + 1900, gTimeInfo.tm_mon + 1, gTimeInfo.tm_mday, gTimeInfo.tm_hour, gTimeInfo.tm_min, gTimeInfo.tm_sec);
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
      if (gAlartFlag & (1 << g))
      {
        if (i % 2)
        {
          leds[numUnit * g + i + !!(NUM_LEDS % gGroup)] = groupColor[g + 1]; // 飲み忘れの場合
        }
        else
        {
          leds[numUnit * g + i + !!(NUM_LEDS % gGroup)] = groupColor[0];
        }
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
      if (sCounter > 10) // チャタリング防止の10カウント待ち
      {
        if (gDrawerStatus == 0 && newStatus == 1) // 引き出しを開けたとき 飲み忘れフラグを通知フラグに戻してからクリア
        {
          gDrawerStatus = 1;
          gNoticeFlag = gNoticeFlag | gAlartFlag;
          gAlartFlag = 0;
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
    gAlartFlag = gAlartFlag | gNoticeFlag;
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
  static time_t sReserveTime[4] = {gCurrentTime + data.nextSchedule[0][0], gCurrentTime + data.nextSchedule[1][0], gCurrentTime + data.nextSchedule[2][0], gCurrentTime + data.nextSchedule[3][0]};

  if (gCurrentTime > sReserveTime[0])
  {
    gNoticeFlag = gNoticeFlag | 1;
    gScheduledTime = sReserveTime[0];
    sReserveTime[0] = gCurrentTime + data.nextSchedule[0][0];
  }
  if (gCurrentTime > sReserveTime[1])
  {
    gNoticeFlag = gNoticeFlag | 2;
    gScheduledTime = sReserveTime[1];
    sReserveTime[1] = gCurrentTime + data.nextSchedule[1][0];
  }
  if (gCurrentTime > sReserveTime[2])
  {
    gNoticeFlag = gNoticeFlag | 4;
    gScheduledTime = sReserveTime[2];
    sReserveTime[2] = gCurrentTime + data.nextSchedule[2][0];
  }
  if (gCurrentTime > sReserveTime[3])
  {
    gNoticeFlag = gNoticeFlag | 8;
    gScheduledTime = sReserveTime[3];
    sReserveTime[3] = gCurrentTime + data.nextSchedule[3][0];
  }

  displayNotice();
}