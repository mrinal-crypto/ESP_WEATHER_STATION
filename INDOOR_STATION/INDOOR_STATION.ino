#include <Arduino.h>
#include <WiFi.h>
#include <WiFiManager.h>
#include "time.h"
#include <U8g2lib.h>
#include <string.h>
#include <FastLED.h>
#include <SPI.h>
#include <Wire.h>
#include <ArduinoJson.h>
#include <ESPAsyncWebServer.h>
#include "SPIFFS.h"
#include <FS.h>

#define BUZ 12
#define DATA_PIN 21
#define ADJUST_BRIGHTNESS 14
#define NUM_LEDS 4
#define CHIPSET WS2812
#define BRIGHTNESS 50
#define COLOR_ORDER GRB
#define WIFI_CONNECT_STATUS_LED 3
#define SENSOR_STATUS_LED 0
#define ALERT_STATUS_LED 1
#define TIME_STATUS_LED 2

#define HOURLY_ARRAY_LENGTH 48
#define DAILY_ARRAY_LENGTH 31
#define PRESSURE_CHANGE_THRESHOLD 5.0   // hPa change that triggers alert
#define TEMP_HIGH_THRESHOLD 40.0        // °C
#define TEMP_LOW_THRESHOLD 10.0         // °C
#define PRESSURE_STORM_THRESHOLD 985.0  // hPa
#define HUMIDITY_HIGH_THRESHOLD 85.0    // %
float lastPressure = 0;
unsigned long lastAlertTime = 0;
const unsigned long ALERT_COOLDOWN = 600000;  // 10 minutes between alerts

AsyncWebServer server(80);
CRGB leds[NUM_LEDS];

void tostring();
void wifiSignalQuality();
void connectWiFi();
void printLocalTime();
void welcomeMsg();
void clearLCD();

U8G2_ST7920_128X64_F_SW_SPI u8g2(U8G2_R0, 18, 23, 5, 22);  //for full buffer mode

const char *ntpServer = "pool.ntp.org";
const long gmtOffset_sec = 19800;
const int daylightOffset_sec = 0;
unsigned long previousMillis = 0;
const long buzzerDuration = 200;
uint8_t wifiRSSI = 0;
String ssid = "";
float temperature = 0.0;
float humidity = 0.0;
float pressure = 0.0;
float altitude = 0.0;

float tem[HOURLY_ARRAY_LENGTH] = { 0 };
float hum[HOURLY_ARRAY_LENGTH] = { 0 };
float pre[HOURLY_ARRAY_LENGTH] = { 0 };
int currentIndex = 0;
bool dataSaved = false;

int signalQuality[] = { 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99,
                        99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 98, 98, 98, 97, 97, 96, 96, 95, 95, 94, 93, 93, 92,
                        91, 90, 90, 89, 88, 87, 86, 85, 84, 83, 82, 81, 80, 79, 78, 76, 75, 74, 73, 71, 70, 69, 67, 66, 64,
                        63, 61, 60, 58, 56, 55, 53, 51, 50, 48, 46, 44, 42, 40, 38, 36, 34, 32, 30, 28, 26, 24, 22, 20,
                        17, 15, 13, 10, 8, 6, 3, 1, 1, 1, 1, 1, 1, 1, 1
                      };
/////////////////////////////////////////////////
void handlePostData(AsyncWebServerRequest *request) {

  Serial.println("\nReceived Sensor Data =>");
  //  int params = request->params();
  //  Serial.print("Received Parameters: ");
  //  Serial.println(params);
  //
  //  for (int i = 0; i < params; i++) {
  //    AsyncWebParameter* p = request->getParam(i);
  //    Serial.print(p->name());
  //    Serial.print(": ");
  //    Serial.println(p->value());
  //  }

  if (request->hasParam("tem", true) && request->hasParam("hum", true) && request->hasParam("pre", true) && request->hasParam("alt", true)) {

    String tem = request->getParam("tem", true)->value();
    String hum = request->getParam("hum", true)->value();
    String pre = request->getParam("pre", true)->value();
    String alt = request->getParam("alt", true)->value();

    temperature = tem.toFloat();
    humidity = hum.toFloat();
    pressure = pre.toFloat();
    altitude = alt.toFloat();

    Serial.println("Temperature:" + tem + " Humidity:" + hum + " Pressure:" + pre + " Altitude:" + alt);

    request->send(200, "text/plain", " OK");

    leds[SENSOR_STATUS_LED] = CRGB(0, 255, 0);
    FastLED.show();
    delay(300);
    leds[SENSOR_STATUS_LED] = CRGB(0, 0, 0);
    FastLED.show();

    //    for (int i = 255; i >= 0; i -= 5) {
    //      leds[SENSOR_STATUS_LED] = CRGB(0, i, 0);
    //      FastLED.show();
    //      delay(10);  // Small delay for smooth fading effect
    //    }

  } else {
    Serial.println("Missing Parameters!");
    request->send(400, "text/plain", "Error: Missing Parameters");

    for (int i = 0; i <= 255; i += 5) {
      leds[SENSOR_STATUS_LED] = CRGB(i, 0, 0);
      FastLED.show();
      delay(10);  // Small delay for smooth fading effect
    }
    for (int i = 255; i >= 0; i -= 5) {
      leds[SENSOR_STATUS_LED] = CRGB(i, 0, 0);
      FastLED.show();
      delay(10);  // Small delay for smooth fading effect
    }
  }
}
//////////////////////////////////////////////////
void loadSavedData(uint8_t fsx, uint8_t fsy) {

  clearLCD(fsx, fsy - 10, 128, 10);
  u8g2.setFont(u8g2_font_t0_11_tr);
  u8g2.drawStr(fsx, fsy, "file system-");
  u8g2.sendBuffer();

  if (SPIFFS.exists("/sensor_data.txt")) {
    File file = SPIFFS.open("/sensor_data.txt", FILE_READ);
    if (file) {
      int i = 0;
      while (file.available() && i < 48) {
        String line = file.readStringUntil('\n');
        sscanf(line.c_str(), "%f,%f,%f", &tem[i], &hum[i], &pre[i]);
        i++;
      }
      file.close();
      delay(100);
      Serial.println("hourly data loaded from SPIFFS");

      u8g2.setFont(u8g2_font_t0_11_tr);
      u8g2.drawStr(fsx + 77, fsy, "ok!");
      u8g2.sendBuffer();
    }
  } else {
    Serial.println("hourly data not exists");
    u8g2.setFont(u8g2_font_t0_11_tr);
    u8g2.drawStr(fsx + 77, fsy, "ok!");
    u8g2.sendBuffer();
  }
  delay(1000);
}
//////////////////////////////////////////////////
void handleGetData(AsyncWebServerRequest *request) {
  StaticJsonDocument<200> jsonDoc;

  jsonDoc["tem"] = String(temperature, 2);  //upto one decimal
  jsonDoc["hum"] = String(humidity, 0);
  jsonDoc["pre"] = String(pressure, 1);
  jsonDoc["alt"] = String(altitude, 0);

  String jsonResponse;
  serializeJson(jsonDoc, jsonResponse);

  request->send(200, "application/json", jsonResponse);
}
//////////////////////////////////////////////////
void handleHistoricalData(AsyncWebServerRequest *request) {
  File file = SPIFFS.open("/sensor_data.txt", "r");
  if (!file) {
    request->send(404, "application/json", "[]");
    return;
  }

  String json = "[";
  bool first = true;
  String line;
  while (file.available()) {
    line = file.readStringUntil('\n');
    if (!first) json += ",";
    json += "\"" + line + "\"";
    first = false;
  }
  json += "]";
  file.close();

  request->send(200, "application/json", json);
}
//////////////////////////////////////////////////
void saveHourlyData() {
  struct tm timeinfo;
  if (!getLocalTime(&timeinfo)) {
    Serial.println("Failed to obtain time");
    return;
  }

  // Calculate array index based on time (00:00=0, 00:30=1, 01:00=2, etc.)
  int index = (timeinfo.tm_hour * 2) + (timeinfo.tm_min >= 30 ? 1 : 0);

  // Only save at exactly XX:00 or XX:30
  if (timeinfo.tm_min == 0 || timeinfo.tm_min == 30) {
    if (!dataSaved) {
      // Save current values to arrays
      tem[index] = temperature;
      hum[index] = humidity;
      pre[index] = pressure;

      // Save arrays to SPIFFS
      File file = SPIFFS.open("/sensor_data.txt", FILE_WRITE);
      if (file) {
        for (int i = 0; i < HOURLY_ARRAY_LENGTH; i++) {
          file.printf("%.2f,%.2f,%.2f\n", tem[i], hum[i], pre[i]);
        }
        file.close();
        Serial.printf("Data saved at %02d:%02d - Index: %d\n",
                      timeinfo.tm_hour, timeinfo.tm_min, index);

        leds[ALERT_STATUS_LED] = CRGB(255, 255, 255);
        FastLED.show();
        delay(300);
        for (int i = 255; i >= 0; i -= 5) {
          leds[ALERT_STATUS_LED] = CRGB(0, i, 0);
          FastLED.show();
          delay(5);  // Small delay for smooth fading effect
        }

      }
      dataSaved = true;
    }
  } else {
    dataSaved = false;
  }
}
//////////////////////////////////////////////////
void playAlertPattern(int pattern) {
  switch (pattern) {
    case 1:  // Rapid pressure drop (storm warning)
      for (int i = 0; i < 3; i++) {

        leds[ALERT_STATUS_LED] = CRGB(0, 255, 0);
        FastLED.show();
        digitalWrite(BUZ, HIGH);
        delay(200);

        leds[ALERT_STATUS_LED] = CRGB(0, 0, 0);
        FastLED.show();
        digitalWrite(BUZ, LOW);
        delay(200);
      }
      break;

    case 2:  // High temperature alert
      for (int i = 0; i < 2; i++) {

        leds[ALERT_STATUS_LED] = CRGB(255, 0, 0);
        FastLED.show();
        digitalWrite(BUZ, HIGH);
        delay(500);

        leds[ALERT_STATUS_LED] = CRGB(0, 0, 0);
        FastLED.show();
        digitalWrite(BUZ, LOW);
        delay(200);
      }
      break;

    case 3:  // High humidity alert
      leds[ALERT_STATUS_LED] = CRGB(0, 0, 255);
      FastLED.show();
      digitalWrite(BUZ, HIGH);
      delay(1000);

      leds[ALERT_STATUS_LED] = CRGB(0, 0, 0);
      FastLED.show();
      digitalWrite(BUZ, LOW);
      break;
  }
}

////////////////////////////////////////////////
void checkWeatherAlerts() {
  unsigned long currentTime = millis();

  if (currentTime - lastAlertTime < ALERT_COOLDOWN) {
    //    Serial.println("Still in cooldown period");
    return;
  }

  bool shouldAlert = false;
  int alertPattern = 0;

  if (humidity >= HUMIDITY_HIGH_THRESHOLD) {
    Serial.printf("High humidity alert triggered: %.1f%% (threshold: %.1f%%)\n",
                  humidity, HUMIDITY_HIGH_THRESHOLD);
    shouldAlert = true;
    alertPattern = 3;
  }

  else if (temperature >= TEMP_HIGH_THRESHOLD) {
    Serial.printf("High temperature alert triggered: %.1f°C\n", temperature);
    shouldAlert = true;
    alertPattern = 2;
  }

  else if (lastPressure != 0 && abs(pressure - lastPressure) >= PRESSURE_CHANGE_THRESHOLD) {
    Serial.printf("Pressure change alert: %.1f to %.1f\n", lastPressure, pressure);
    shouldAlert = true;
    alertPattern = 1;
  }

  if (shouldAlert) {
    Serial.printf("Playing alert pattern: %d\n", alertPattern);
    playAlertPattern(alertPattern);
    lastAlertTime = currentTime;
  }

  lastPressure = pressure;
}
//////////////////////////////////////////////////////////
void checkHourlyChime() {
  struct tm timeinfo;
  if (!getLocalTime(&timeinfo)) {
    return;
  }

  // Check if it's at the start of an hour (00 minutes only)
  static int lastChimeHour = -1;  // Track last hour we chimed
  if (timeinfo.tm_min == 0 && timeinfo.tm_hour != lastChimeHour) {
    // Single beep for one second
    digitalWrite(BUZ, HIGH);
    leds[TIME_STATUS_LED] = CRGB(255, 255, 255); // White flash
    FastLED.show();
    delay(1500);
    digitalWrite(BUZ, LOW);
    leds[TIME_STATUS_LED] = CRGB(255, 0, 255); // Return to purple
    FastLED.show();

    lastChimeHour = timeinfo.tm_hour;  // Update last chime hour
    Serial.printf("Hourly chime at %02d:00\n", timeinfo.tm_hour);
  }

  // Reset lastChimeHour when minute changes from 0
  if (timeinfo.tm_min != 0) {
    lastChimeHour = -1;
  }
}
//////////////////////////////////////////////////////////

void tostring(char str[], int num) {
  int i, rem, len = 0, n;

  n = num;
  while (n != 0) {
    len++;
    n /= 10;
  }
  for (i = 0; i < len; i++) {
    rem = num % 10;
    num = num / 10;
    str[len - (i + 1)] = rem + '0';
  }
  str[len] = '\0';
}

/////////////////////////////////////////////////////

void connectWiFi(uint8_t cwx, uint8_t cwy) {
  clearLCD(0, 0, 128, 64);
  WiFiManager wm;
  u8g2.setFont(u8g2_font_t0_11_tr);
  u8g2.drawStr(cwx, cwy, "wifi conection-");
  u8g2.sendBuffer();
  WiFi.disconnect();
  delay(50);
  bool success = false;
  while (!success) {
    wm.setConfigPortalTimeout(60);
    success = wm.autoConnect("ESP.digiClock");
    if (!success) {

      clearLCD(cwx, cwy - 13, 128, 35);
      u8g2.clearBuffer();
      u8g2.setFont(u8g2_font_t0_11_tr);
      u8g2.drawStr(cwx, cwy, "wifi conection-");
      u8g2.drawStr(cwx + 94, cwy, "err!");
      u8g2.drawStr(cwx, cwy + 11, "AP-ESP.digiClock");
      u8g2.drawStr(cwx, cwy + 22, "IP - 192.168.4.1");
      u8g2.sendBuffer();
    }
  }

  clearLCD(cwx, cwy - 13, 128, 35);
  u8g2.setFont(u8g2_font_t0_11_tr);
  u8g2.drawStr(cwx, cwy, "wifi conection-");
  u8g2.drawStr(cwx + 94, cwy, "ok!");
  u8g2.sendBuffer();
  delay(1000);

  ssid = WiFi.SSID();
  if (strlen(ssid.c_str()) > 7) {
    String shortSSID = ssid.substring(0, 7);
    String wifiName = "ssid- " + shortSSID + "...";
    u8g2.setFont(u8g2_font_t0_11_tr);
    u8g2.drawStr(cwx, cwy + 11, wifiName.c_str());
    u8g2.sendBuffer();
  } else {
    String wifiName = "ssid- " + ssid;
    u8g2.setFont(u8g2_font_t0_11_tr);
    u8g2.drawStr(cwx, cwy + 11, wifiName.c_str());
    u8g2.sendBuffer();
  }
  delay(1000);

  wifiRSSI = WiFi.RSSI() * (-1);
  char str[10];
  tostring(str, signalQuality[wifiRSSI]);
  String wifiSignal = String(str) + "%";
  u8g2.setFont(u8g2_font_t0_11_tr);
  u8g2.drawStr(cwx + 99, cwy + 11, wifiSignal.c_str());
  u8g2.sendBuffer();
  delay(1000);

  String rawIP = WiFi.localIP().toString();  //toString () used for convert char to string
  String IPAdd = "IP- " + rawIP;
  u8g2.setFont(u8g2_font_t0_11_tr);
  u8g2.drawStr(cwx, cwy + 22, IPAdd.c_str());  //c_str() function used for convert string to const char *
  u8g2.sendBuffer();
  delay(2000);

}
///////////////////////////////////////////////////////////////////
void configTime(uint8_t ctx, uint8_t cty) {
  clearLCD(ctx, cty - 8, 128, 8);
  u8g2.setFont(u8g2_font_t0_11_tr);
  u8g2.drawStr(ctx, cty, "time server-");
  u8g2.sendBuffer();

  configTime(gmtOffset_sec, daylightOffset_sec, ntpServer);
  struct tm timeinfo;
  getLocalTime(&timeinfo);
  if (!getLocalTime(&timeinfo)) {
    //    clearLCD(ctx + 85, cty - 11, 43, 11);
    u8g2.setFont(u8g2_font_t0_11_tr);
    u8g2.drawStr(ctx + 77, cty, "err!");
    u8g2.sendBuffer();
  }
  if (getLocalTime(&timeinfo)) {
    //    clearLCD(ctx + 85, cty - 11, 43, 11);
    u8g2.setFont(u8g2_font_t0_11_tr);
    u8g2.drawStr(ctx + 77, cty, "ok!");
    u8g2.sendBuffer();
  }
  delay(1000);
}
///////////////////////////////////////////////////////////////////

void printLocalTime(uint8_t ltx, uint8_t lty) {

  struct tm timeinfo;
  getLocalTime(&timeinfo);

  if (!getLocalTime(&timeinfo)) {
    leds[TIME_STATUS_LED] = CRGB(0, 0, 0);
    FastLED.show();

    clearLCD(ltx, lty - 18, 128, 18);
    u8g2.setFont(u8g2_font_unifont_tr);
    u8g2.drawStr(ltx, lty, "Time Failed!");
    u8g2.sendBuffer();
  }

  if (getLocalTime(&timeinfo)) {

    leds[TIME_STATUS_LED] = CRGB(255, 0, 255);

    char timeStringBuff[10];
    char dateStringBuff[25];
    char secStringBuff[10];
    char ampmStringBuff[10];
    char wDayStringBuff[10];
    char mDayStringBuff[10];
    char mNameStringBuff[10];
    char monthStringBuff[10];
    char yearStringBuff[10];
    char dayOfYearStringBuff[10];
    char weakOfYearStringBuff[10];

    strftime(timeStringBuff, sizeof(timeStringBuff), "%I:%M", &timeinfo);
    strftime(secStringBuff, sizeof(secStringBuff), "%S", &timeinfo);
    strftime(ampmStringBuff, sizeof(ampmStringBuff), "%p", &timeinfo);
    strftime(yearStringBuff, sizeof(yearStringBuff), "%y", &timeinfo);
    strftime(dateStringBuff, sizeof(dateStringBuff), "Date: %d.%m.%y %a", &timeinfo);
    strftime(dayOfYearStringBuff, sizeof(dayOfYearStringBuff), "%jd", &timeinfo);
    strftime(weakOfYearStringBuff, sizeof(weakOfYearStringBuff), "%Ww", &timeinfo);

    /*
      %a Abbreviated weekday name.
      %A  Full weekday name.
      %b  Abbreviated month name.
      %B  Full month name.
      %c  Date/Time in the format of the locale.
      %C  Century number [00-99], the year divided by 100 and truncated to an integer.
      %d  Day of the month [01-31].
      %D  Date Format, same as %m/%d/%y.
      %e  Same as %d, except single digit is preceded by a space [1-31].
      %g  2 digit year portion of ISO week date [00,99].
      %F  ISO Date Format, same as %Y-%m-%d.
      %G  4 digit year portion of ISO week date. Can be negative.
      %h  Same as %b.
      %H  Hour in 24-hour format [00-23].
      %I  Hour in 12-hour format [01-12].
      %j  Day of the year [001-366].
      %m  Month [01-12].
      %M  Minute [00-59].
      %n  Newline character.
      %p  AM or PM string.
      %r  Time in AM/PM format of the locale. If not available in the locale time format, defaults to the POSIX time AM/PM format: %I:%M:%S %p.
      %R  24-hour time format without seconds, same as %H:%M.
      %S  Second [00-61]. The range for seconds allows for a leap second and a double leap second.
      %t  Tab character.
      %T  24-hour time format with seconds, same as %H:%M:%S.
      %u  Weekday [1,7]. Monday is 1 and Sunday is 7.
      %U  Week number of the year [00-53]. Sunday is the first day of the week.
      %V  ISO week number of the year [01-53]. Monday is the first day of the week. If the week containing January 1st has four or more days in the new year then it is considered week 1. Otherwise, it is the last week of the previous year, and the next year is week 1 of the new year.
      %w  Weekday [0,6], Sunday is 0.
      %W  Week number of the year [00-53]. Monday is the first day of the week.
      %x  Date in the format of the locale.
      %X  Time in the format of the locale.
      %y  2 digit year [00,99].
      %Y  4-digit year. Can be negative.
      %z  UTC offset. Output is a string with format +HHMM or -HHMM, where + indicates east of GMT, - indicates west of GMT, HH indicates the number of hours from GMT, and MM indicates the number of minutes from GMT.
      %Z  Time zone name.
      %%  % character.
    */

    /* display time */
    clearLCD(ltx, lty - 26, 79, 26);
    u8g2.setFont(u8g2_font_timB24_tn);
    u8g2.drawStr(ltx, lty, timeStringBuff);
    u8g2.sendBuffer();

    /* display second, am, pm */
    clearLCD(ltx + 80, lty - 26, 15, 26);
    u8g2.setFont(u8g2_font_t0_11_tr);
    u8g2.drawStr(ltx + 80, lty - 13, secStringBuff);
    u8g2.drawStr(ltx + 80, lty, ampmStringBuff);
    u8g2.sendBuffer();

    /* display year days and week no */
    u8g2.drawFrame(ltx + 96, 0, 32, 26);
    clearLCD(ltx + 97, lty - 25, 30, 24);
    u8g2.setFont(u8g2_font_t0_11_tr);
    u8g2.drawStr(ltx + 99, lty - 14, dayOfYearStringBuff);
    u8g2.drawStr(ltx + 99, lty - 3, weakOfYearStringBuff);
    u8g2.sendBuffer();

    /* display date */
    clearLCD(ltx, lty + 1, 128, 8);
    u8g2.setFont(u8g2_font_t0_11b_tr);
    u8g2.drawStr(ltx + 10, lty + 10, dateStringBuff);
    u8g2.sendBuffer();
  }
}

////////////////////////////////////////////////////////////
void printWeatherStatus(uint8_t wsx, uint8_t wsy) {

  char temHumAltBuffer[20];
  char preBuffer[15];

  sprintf(temHumAltBuffer, "%.2fC  %.0f%%  %.0fm", temperature, humidity, altitude);
  sprintf(preBuffer, "%.1fhPa", pressure);

  u8g2.drawFrame(wsx, wsy - 26, 128, 26);

  /* display tem, hum, alt */
  //  Serial.println(temHumAltBuffer);
  clearLCD(wsx + 1, wsy - 25, 124, 10);
  u8g2.setFont(u8g2_font_t0_11_tr);
  u8g2.drawStr(wsx + 3, wsy - 15, temHumAltBuffer);
  u8g2.sendBuffer();

  /* display pressure */
  //  Serial.print(preBuffer);
  clearLCD(wsx + 1, wsy - 12, 60, 10);
  u8g2.setFont(u8g2_font_t0_11_tr);
  u8g2.drawStr(wsx + 3, wsy - 4, preBuffer);
  u8g2.sendBuffer();

  /* display sky condition */
  clearLCD(wsx + 61, wsy - 13, 66, 11);
  u8g2.setFont(u8g2_font_t0_11_tr);
  if (pressure <= 0.0) {
    u8g2.drawStr(wsx + 65, wsy - 4, "Waitting..");
    u8g2.sendBuffer();
  } else if (pressure < 970) {
    //    Serial.println(" Cyclones");
    u8g2.drawStr(wsx + 65, wsy - 4, "Cyclones");
    u8g2.sendBuffer();
  } else if (pressure >= 1030.0) {
    //    Serial.println(" Clear Skies");
    u8g2.drawStr(wsx + 65, wsy - 4, "Clr. Skies");
    u8g2.sendBuffer();
  } else if (pressure >= 1020.0) {
    //    Serial.println(" Mostly Clear");
    u8g2.drawStr(wsx + 65, wsy - 4, "Mostly Clr");
    u8g2.sendBuffer();
  } else if (pressure >= 1013.0) {
    //    Serial.println(" Fair Weather");
    u8g2.drawStr(wsx + 65, wsy - 4, "Clr Wthr.");
    u8g2.sendBuffer();
  } else if (pressure >= 1005.0) {
    //    Serial.println(" Partly Cloudy");
    u8g2.drawStr(wsx + 65, wsy - 4, "Prt. Cldy");
    u8g2.sendBuffer();
  } else if (pressure >= 995.0) {
    //    Serial.println(" Cloudy");
    u8g2.drawStr(wsx + 65, wsy - 4, "Cloudy");
    u8g2.sendBuffer();
  } else if (pressure >= 985.0) {
    //    Serial.println(" Rainy");
    u8g2.drawStr(wsx + 65, wsy - 4, "Rainy");
    u8g2.sendBuffer();
  } else if (pressure >= 970.0) {
    //    Serial.println(" Heavy Rain");
    u8g2.drawStr(wsx + 65, wsy - 4, "Heavy Rain");
    u8g2.sendBuffer();
  }
}
////////////////////////////////////////////////////////////

void welcomeMsg() {

  u8g2.clearBuffer();
  u8g2.setFont(u8g2_font_ncenB18_tr);
  u8g2.drawStr(0, 22, "ESP32");
  u8g2.setFont(u8g2_font_ncenR12_tr);
  u8g2.drawStr(0, 40, "DIGI - CLOCK");
  u8g2.sendBuffer();
  u8g2.setFont(u8g2_font_t0_11_tr);
  u8g2.drawStr(2, 60, "developed by M.Maity");
  u8g2.sendBuffer();
  u8g2.clearBuffer();
}

//////////////////////////////////////////////

void clearLCD(const long x, uint8_t y, uint8_t wid, uint8_t hig) {
  /*  box wid is right x, box height is below y
      where font wid is right x, font height is upper y
  */
  u8g2.setDrawColor(0);
  u8g2.drawBox(x, y, wid, hig);
  u8g2.setDrawColor(1);
}

//////////////////////////////////////////////////
void setup() {
  Serial.begin(115200);
  pinMode(BUZ, OUTPUT);
  u8g2.begin();
  welcomeMsg();
  delay(3000);

  connectWiFi(2, 13);
  configTime(2, 45);
  if (!SPIFFS.begin(true)) {
    Serial.println("file system = error!");
    return;
  }
  loadSavedData(2, 56);

  u8g2.clearBuffer();
  FastLED.addLeds<CHIPSET, DATA_PIN, COLOR_ORDER>(leds, NUM_LEDS);
  FastLED.setMaxPowerInVoltsAndMilliamps(5, 500);
  FastLED.setBrightness(BRIGHTNESS);
  FastLED.clear();
  FastLED.show();

  delay(100);

  if (WiFi.status() == WL_CONNECTED) {
    Serial.println("\nWiFi Connected!");
    Serial.print("ESP32 Indoor Module IP Address: ");
    Serial.println(WiFi.localIP());

    server.on("/", HTTP_GET, [](AsyncWebServerRequest * request) {
      request->send(SPIFFS, "/index.html", "text/html");
    });

    server.on("/historical_data", HTTP_GET, handleHistoricalData);

    server.on("/style.css", HTTP_GET, [](AsyncWebServerRequest * request) {
      request->send(SPIFFS, "/style.css", "text/css");
    });

    server.on("/table.svg", HTTP_GET, [](AsyncWebServerRequest * request) {
      request->send(SPIFFS, "/table.svg", "image/svg+xml");
    });

    server.on("/update", HTTP_POST, handlePostData);

    server.on("/sensor.json", HTTP_GET, handleGetData);

    server.begin();
    Serial.println("HTTP Server Started");
  } else {
    Serial.println("wifi connection failed! HTTP server not start!");
  }
}

//////////////////////////////////////////////////////////////////
void loop() {

  if (WiFi.status() == WL_CONNECTED) {
    leds[WIFI_CONNECT_STATUS_LED] = CRGB(255, 64, 0);
    FastLED.show();
    saveHourlyData();
    printLocalTime(0, 26);
    printWeatherStatus(0, 64);
    checkWeatherAlerts();
    checkHourlyChime();
  } else {
    leds[TIME_STATUS_LED] = CRGB(0, 0, 0);
    leds[WIFI_CONNECT_STATUS_LED] = CRGB(0, 0, 0);
    FastLED.show();
    connectWiFi(2, 13);
    configTime(2, 45);
    clearLCD(0, 0, 128, 64);
  }
}
