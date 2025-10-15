#define FASTLED_ESP32_I2S true
#include <ArduinoOTA.h>
#include <ESPmDNS.h>
#include <FastLED.h>
#include <WebServer.h>
#include <WiFi.h>
#include <EEPROM.h>

/* Function Prototype  */
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
#define FORGET_ALERT_TIME 10

/* 締め忘れアラートまでの時間(S) */
#define LEFTOPEN_ALERT_TIME 10

// ルーター接続情報
#define WIFI_SSID "0856g"
#define WIFI_PASSWORD "nttnttntt"
// Multicast DNS名
#define DEVICE_NAME "medicine_drawer" // 16バイト以内
// 時間取得
#define JST 9 * 3600L
#define NTPServer1 "192.168.1.10"
#define NTPServer2 "time.google.com"

// Webサーバーオブジェクト
#define HTTP_PORT 80

/* LED関連 */
#define DATA_PIN 14
#define LED_TYPE WS2812B
#define COLOR_ORDER GRB
#define NUM_LEDS 32
#define BRIGHTNESS 64

/* センサー関連 */
#define SW1_PIN 27

CRGB leds[NUM_LEDS];
CRGB groupColor[] = {CRGB::Red, CRGB::Blue, CRGB::Green, CRGB::Purple, CRGB::Yellow}; // グループのカラー設定
uint8_t gBrightness = BRIGHTNESS;
uint8_t gHue = 0; // rotating "base color" used by many of the patterns

WebServer server(HTTP_PORT);

uint8_t gGroup = 0;           // 投薬タイミングのグループ数
uint8_t gDrawerIsChanged = 0; // 引き出しの開け閉めフラグ
uint8_t gDrawerStatus = 0;    // 引き出しの状態
uint8_t gNoticeFlag = 0;      // 通知フラグ
uint8_t gAlartFlag = 0;       // 飲み忘れフラグ
uint8_t gDayOfClinic = 0;     // 通院日
time_t gDrawerMovedTime;      // 最後に動かした時間　閉め忘れ対策
time_t gScheduledTime;        // 直近の投薬時間　飲み忘れ対策
time_t gCurrentTime;          // 現在の時刻
struct tm gTimeInfo;          // 時刻を格納するオブジェクト

struct _EEPROM_DATA // ESP32はtime_tが32bitの2038年問題あり　将来time_tを64bitで計算するとEEPROMの使用量は138バイトなので確保はそれ以上で
{
  uint8_t hour[3];
  uint8_t minutes[3];
  uint8_t interval[4][3];
  time_t nextSchedule[4][3];
  time_t dayOfClinic;
  char check[16];
};
struct _EEPROM_DATA data;

/*****************************************************************************
                            Predetermined Sequence
 *****************************************************************************/
void setup()
{
  Serial.begin(SPI_SPEED);

  connectToWifi();      // Wi-Fiルーターに接続するajust
  startMDNS();          // Multicast DNS
  startWebServer();     // WebServer
  startOTA();           // OTA
  ajustTime();          // 初回の時刻合わせ
  initializeSchedule(); // 初回の予約時間設定

  FastLED.addLeds<LED_TYPE, DATA_PIN, COLOR_ORDER>(leds, NUM_LEDS);
  FastLED.setBrightness(gBrightness);
  Serial.println("LED Controller start ...");

  pinMode(SW1_PIN, INPUT_PULLDOWN);
  attachInterrupt(SW1_PIN, changeStatus, CHANGE);

  gDrawerStatus = digitalRead(SW1_PIN);
}

void loop()
{
  server.handleClient(); // HTTPをリスンする
  ArduinoOTA.handle();   // OTA待ち受け

  gCurrentTime = time(NULL);
  checkStatus();
  checkSchedule();
  checkAlart();

  displayNotice();
  FastLED.show();

  FastLED.delay(1000 / 30); // insert a delay to keep the framerate modest
}

/*****************************< Other functions >*****************************/

void initializeSchedule()
{
  EEPROM.begin(256);
  EEPROM.get(0, data);
  if (strcmp(data.check, DEVICE_NAME)) //保存データかチェックしてデータが無い場合0で初期化
  {
    for (uint8_t i = 0; i < 3; ++i)
    {
      data.hour[i] = 0;
      data.minutes[i] = 0;

      for (uint8_t g = 0; g < 4; ++g)
      {
        data.interval[g][i] = 0;
        data.interval[g][i] = 0;
      }
    }
    data.dayOfClinic = 0;
    strncpy(data.check, DEVICE_NAME, 16); // データ保存の証にデバイス名を登録
  }

  gCurrentTime = time(NULL);
  getLocalTime(&gTimeInfo);
  // 今日の0:00の時間をtime_tで計算
  time_t midnightTime = gCurrentTime - gTimeInfo.tm_hour * 3600 - gTimeInfo.tm_min * 60 - gTimeInfo.tm_sec;
  time_t tmpSchedule;

  for (uint8_t g = 0; g < 4; ++g)
  {
    uint8_t groupExist = 0;
    for (uint8_t i = 0; i < 3; ++i)
    {
      if (data.interval[g][i]) // インターバルが0以外の時
      {
        groupExist++;
        if (data.nextSchedule[g][i] <= gCurrentTime) // 停電からの復帰などを想定して現在時刻より服薬予定時間が未来ならそのまま、過去なら24時間後にする
        {
          tmpSchedule = midnightTime + data.hour[i] * 3600 + data.minutes[i] * 60;
          if (tmpSchedule <= gCurrentTime) // 予約時間を過ぎていたら24時間後の予約に
          {
            tmpSchedule += 24 * 3600;
          }
          data.nextSchedule[g][i] = tmpSchedule;
        }
      }
      else
      {
        data.nextSchedule[g][i] = 0;
      }
    }
    if (groupExist)
    {
      gGroup++;
    }
  }
  EEPROM.put(0, data);
  EEPROM.commit();
  showSchedule();
  /*/ //test data///////////////////////////////////////////
  data.nextSchedule[0][0] = gCurrentTime + 3;
  data.nextSchedule[1][0] = gCurrentTime + 10;
  data.nextSchedule[2][0] = gCurrentTime + 28;
  data.nextSchedule[3][0] = gCurrentTime + 19;
  // ////////////////////////////////////////////*/
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
      uint8_t currentLed = numUnit * g + i + (gGroup == 3) * g; // 3グループの時は余りが出るのでグループの境界のLEDは光らせない
      if (gNoticeFlag & (1 << g))
      { // グループ1なら 0b0001 2なら0b0010 ... のフラグが立っているか調べる
        leds[currentLed] = groupColor[g + 1];
      }
      if (gDrawerStatus == 99)
      { // 閉め忘れの時は赤点滅
        leds[currentLed] = blend(leds[currentLed], groupColor[0], beatsin8(30, 0, 255));
      }
      else if (gAlartFlag & (1 << g))
      { // 飲み忘れの時は点滅
        leds[currentLed] = blend(groupColor[g + 1], CRGB::Black, beatsin8(30, 0, 255));
      }
    }
  }
  if (gDayOfClinic)
  {
    for (uint8_t i = 0; i < NUM_LEDS; i = i + 4)
    {
      leds[i] = ColorFromPalette(RainbowColors_p, i * 8 + gHue, 255);
    }
    gHue++;
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
      if (sCounter > 10) // チャタリング防止のカウント
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
        }
        else if (gDrawerStatus == 99 && newStatus == 0) // 閉め忘れを閉じた
        {
          gDrawerStatus = 0;
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
}

void checkSchedule()
{
  uint8_t isChanged = 0;
  getLocalTime(&gTimeInfo);
  // 今日の0:00の時間をtime_tで計算
  time_t midnightTime = gCurrentTime - gTimeInfo.tm_hour * 3600 - gTimeInfo.tm_min * 60 - gTimeInfo.tm_sec;

  for (uint8_t g = 0; g < 4; ++g)
  {
    for (uint8_t i = 0; i < 3; ++i)
    {
      if ((data.interval[g][i]) && (data.nextSchedule[g][i] <= gCurrentTime)) // インターバルが0以外で予約時間を超えた時
      {
        gNoticeFlag = gNoticeFlag | (uint8_t)(1 << g);
        gScheduledTime = gCurrentTime;
        data.nextSchedule[g][i] = midnightTime + data.hour[i] * 3600 + data.minutes[i] * 60 + data.interval[g][i] * 24 * 3600;
        isChanged = 1;
      }
    }
  }

  if (midnightTime == data.dayOfClinic)
  {
    gDayOfClinic = 1;
  }
  else
  {
    gDayOfClinic = 0;
  }

  if (isChanged)
  {
    showSchedule();
    EEPROM.begin(256);
    EEPROM.put(0, data);
    EEPROM.commit();
  }
}

void showSchedule()
{
  for (uint8_t g = 0; g < 4; ++g)
  {

    for (uint8_t i = 0; i < 3; ++i)
    {
      Serial.print(data.interval[g][i]);
      gTimeInfo = *localtime(&data.nextSchedule[g][i]);
      Serial.printf(" %04d/%02d/%02d %02d:%02d:%02d  ", gTimeInfo.tm_year + 1900, gTimeInfo.tm_mon + 1, gTimeInfo.tm_mday, gTimeInfo.tm_hour, gTimeInfo.tm_min, gTimeInfo.tm_sec);
    }

    Serial.println("");
  }
  gTimeInfo = *localtime(&data.dayOfClinic);
  Serial.printf("通院日 %04d/%02d/%02d %02d:%02d:%02d  ", gTimeInfo.tm_year + 1900, gTimeInfo.tm_mon + 1, gTimeInfo.tm_mday, gTimeInfo.tm_hour, gTimeInfo.tm_min, gTimeInfo.tm_sec);

  Serial.println("");
}