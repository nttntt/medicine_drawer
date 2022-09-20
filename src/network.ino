/* HTMLページ */
const String strHtmlHeader = R"rawliteral(
<!DOCTYPE HTML>
<html>
  <head>
    <meta charset="utf-8">
    <meta name="viewport" content="width=device-width, initial-scale=1">
    <style>
      html { font-family: Helvetica; display: inline-block; margin: 0px auto;text-align: center;} 
      h1 {font-size:28px;} 
      body {text-align: center;} 
      table { border-collapse: collapse; margin-left:auto; margin-right:auto; }
      th { padding: 12px; background-color: #0000cd; color: white; border: solid 2px #c0c0c0; }
      tr { border: solid 2px #c0c0c0; padding: 12px; }
      td { border: solid 2px #c0c0c0; padding: 12px; }
      .value { color:blue; font-weight: bold; padding: 1px;}
    </style>
  </head>
)rawliteral";
const String strHtmlBody = R"rawliteral(
  <body>
    <h1>%PAGE_TITLE%</h1>
    <form action="/" method="get">
    <table>
    <tr><th></th><th>朝食後</th><th>昼食後</th><th>夕食後</th></tr>
    <tr><th>服用時間</th>
      <td><input type="text" name="hour1" value="%hour1%" size="5">:<input type="text" name="minute1" value="%minute1%" size="5"></td>
      <td><input type="text" name="hour2" value="%hour2%" size="5">:<input type="text" name="minute2" value="%minute2%" size="5"></td>
      <td><input type="text" name="hour3" value="%hour3%" size="5">:<input type="text" name="minute3" value="%minute3%" size="5"></td></tr>
    <tr><th>グループ1</th>
      <td><input type="text" name="11" value="%11%" size="5"></td><td><input type="text" name="12" value="%12%" size="5"></td><td><input type="text" name="13" value="%13%" size="5"></td></tr>
    <tr><th>グループ2</th>
      <td><input type="text" name="21" value="%21%" size="5"></td><td><input type="text" name="22" value="%22%" size="5"></td><td><input type="text" name="23" value="%23%" size="5"></td></tr>
    <tr><th>グループ3</th>
      <td><input type="text" name="31" value="%31%" size="5"></td><td><input type="text" name="32" value="%32%" size="5"></td><td><input type="text" name="33" value="%33%" size="5"></td></tr>
    <tr><th>グループ4</th>
      <td><input type="text" name="41" value="%41%" size="5"></td><td><input type="text" name="42" value="%42%" size="5"></td><td><input type="text" name="43" value="%43%" size="5"></td></tr>
    </table>
    <input type="submit" name="button" value="send">
    </form>
  </body></html>
)rawliteral";

/* HTTP レスポンス処理 */
void httpSendResponse(void)
{
  String strHtml = strHtmlHeader + strHtmlBody;
  char numStr[10];
  strHtml.replace("%PAGE_TITLE%", mDNS_NAME);
  sprintf(numStr, "%d", 1);
  strHtml.replace("%hour1%", numStr);
  sprintf(numStr, "%d", 0);
  strHtml.replace("%minute1%", numStr);
  sprintf(numStr, "%d", 0);
  strHtml.replace("%11%", numStr);
  // HTMLを出力する
  server.send(200, "text/html", strHtml);
}

/* HTTP リクエスト処理 */
void handleHtml(void)
{

  // 「/?button=○」のパラメータが指定されているかどうかを確認
  if (server.hasArg("button"))
  {
    // パラメータに応じて、LEDを操作
    if (server.arg("button").equals("1"))
    {
      gGroup = 1;
    }
    else if (server.arg("button").equals("2"))
    {
      gGroup = 2;
    }
    else if (server.arg("button").equals("3"))
    {
      gGroup = 3;
    }
    else if (server.arg("button").equals("4"))
    {
      gGroup = 4;
    }
    else if (server.arg("button").equals("0"))
    {
      gGroup = 0;
    }
  }
  // ページ更新
  httpSendResponse();
}

// 存在しないアドレスが指定された時の処理
void handleNotFound(void)
{
  server.send(404, "text/plain", "Not Found.");
}

/****************************< HTTP functions >****************************/
/* マルチタスクでHTTP & OTA待ち受け */
void htmlTask(void *pvParameters)
{

  while (true)
  {
    server.handleClient(); // HTTPをリスンする
    ArduinoOTA.handle();
    delay(1);
  }
}

/* Wi-Fiルーターに接続する */
void connectToWifi()
{
  Serial.print("Connecting to Wi-Fi ");
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  while (WiFi.status() != WL_CONNECTED)
  {
    delay(500);
    Serial.print(".");
  }
  Serial.println("");
  // モニターにローカル IPアドレスを表示する
  Serial.println("WiFi connected.");
  Serial.print("  *IP address: ");
  Serial.println(WiFi.localIP());
}

/* Multicast DNSを開始する */
void startMDNS()
{
  Serial.print("mDNS server instancing ");
  while (!MDNS.begin(mDNS_NAME))
  {
    Serial.print(".");
    delay(100);
  }
  Serial.println("");
}

void setTime()
{
  Serial.print("Ajust time from NTP..");
  configTime(JST, 0, NTPServer1, NTPServer2); // NTPの設定
  getLocalTime(&gTimeInfo);
  Serial.printf("%04d/%02d/%02d %02d:%02d:%02d\n", gTimeInfo.tm_year + 1900, gTimeInfo.tm_mon + 1, gTimeInfo.tm_mday, gTimeInfo.tm_hour, gTimeInfo.tm_min, gTimeInfo.tm_sec);
}

void startWebServer()
{
  server.on("/", handleHtml);
  server.onNotFound(handleNotFound);
  server.begin(); // HTTPサーバ開始
  Serial.println("WebServer Started.");
}

void startOTA()
{
  ArduinoOTA.setHostname(mDNS_NAME)
      .onStart([]()
               {
        String type;
        if (ArduinoOTA.getCommand() == U_FLASH)
          type = "sketch";
        else  // U_SPIFFS
          type = "filesystem";

        // NOTE: if updating SPIFFS this would be the place to unmount SPIFFS
        // using SPIFFS.end()
        Serial.println("Start updating " + type); })
      .onEnd([]()
             { Serial.println("\nEnd"); })
      .onProgress([](unsigned int progress, unsigned int total)
                  { Serial.printf("Progress: %u%%\r", (progress / (total / 100))); })
      .onError([](ota_error_t error)
               {
        Serial.printf("Error[%u]: ", error);
        if (error == OTA_AUTH_ERROR)
          Serial.println("Auth Failed");
        else if (error == OTA_BEGIN_ERROR)
          Serial.println("Begin Failed");
        else if (error == OTA_CONNECT_ERROR)
          Serial.println("Connect Failed");
        else if (error == OTA_RECEIVE_ERROR)
          Serial.println("Receive Failed");
        else if (error == OTA_END_ERROR)
          Serial.println("End Failed"); });
  ArduinoOTA.begin();
  Serial.println("OTA Started.");
}
