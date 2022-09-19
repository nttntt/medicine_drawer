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
    <table><tr><th>ELEMENT</th><th>VALUE</th></tr>
    <tr><td>Pattern</td><td><span class="value">%PATTERN%</span></td></tr>
    <tr><td>Brightness</td><td><span class="value">%BRIGHTNESS%</span></td></tr>
    <tr><td>Sensitivity</td><td><span class="value">%SENSITIVITY%</span></td></tr>
    </table>
    <input type="button" value="Mode" style="font-size:32px;" onclick="location.href='/?button=mode';"><br>
    <input type="button" value="Reset" style="font-size:32px;" onclick="location.href='/?button=reset';"><br>
    <input type="button" value="^" style="font-size:32px;" onclick="location.href='/?button=up';"><br>
    <input type="button" value="<" style="font-size:32px;" onclick="location.href='/?button=left';">
    <input type="button" value=">" style="font-size:32px;" onclick="location.href='/?button=right';"><br>
    <input type="button" value="V" style="font-size:32px;" onclick="location.href='/?button=down';">
<form action="/" method="get">
<input type="text" name="valueR"><br><input type="text" name="valueG"><br><input type="text" name="valueB">
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
  sprintf(numStr, "%d", 0);
  strHtml.replace("%PATTERN%", numStr);
  sprintf(numStr, "%d", 0);
  strHtml.replace("%BRIGHTNESS%", numStr);
  sprintf(numStr, "%d", 0);
  strHtml.replace("%SENSITIVITY%", numStr);
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
    if (server.arg("button").equals("mode"))
    {
      gTestHue = 60;
    }
    else if (server.arg("button").equals("up"))
    {
      gTestHue = gTestHue + 60;
    }
    
    else if (server.arg("button").equals("down"))
    {
      gTestHue = gTestHue - 60;
   
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
  connectToWifi();  // Wi-Fiルーターに接続する
  startMDNS();      // Multicast DNS
  startWebServer(); // WebServer
  startOTA();
  configTime(JST, 0, NTPServer1, NTPServer2); // NTPの設定

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
  Serial.println("success!");
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
