#include <Arduino.h>
#include <SPI.h>
#include <Wire.h>
#include <WiFi.h>
#include <WiFiManager.h>
#include <ESPAsyncWebServer.h>
#include <AsyncTCP.h>
#include <Preferences.h>
#include "SPIFFS.h"
#include <FS.h>
#include <HTTPClient.h>
#include <Adafruit_BMP280.h>
#include <DHT.h>

#define BOOT_BUTTON_PIN 0
#define DHTPIN 4
#define DHTTYPE DHT11

String indoorIP = "";
Adafruit_BMP280 bmp;
DHT dht(DHTPIN, DHTTYPE);
Preferences preferences;
AsyncWebServer server(80);
const int LED_PIN = 12;
const int portalOpenTime = 300000;

void setup() {
  Serial.begin(115200);
  dht.begin();
  pinMode(BOOT_BUTTON_PIN, INPUT);

  Serial.println("");
  Serial.println("======OUTDOOR MODULE======");
  delay(100);

  connectWiFi();
  delay(100);

  if (SPIFFS.begin(true)) {
    Serial.println("file system = ok!");
  }
  if (!SPIFFS.begin(true)) {
    Serial.println("file system = error!");
  }
  delay(100);
  if (!bmp.begin(0x76)) {  // Initialize BMP280, I2C address 0x76
    Serial.println("BMP280 not found!");
  }
  delay(100);

  setupServer();
  delay(100);

  pinMode(LED_PIN, OUTPUT);
  digitalWrite(LED_PIN, LOW);

}
//////////////////////////////////////////////////////
void connectWiFi() {

  WiFiManager wm;

  WiFi.disconnect();
  delay(50);
  bool success = false;
  while (!success) {
    wm.setConfigPortalTimeout(60);
    success = wm.autoConnect("OUTDOOR");
    if (!success) {
      Serial.println("OUTDOOR MODULE");
      Serial.println("wifi connection failed!");
      Serial.println("AP IP - 192.168.4.1");
    }
  }

  Serial.print("wifi connected! ssid - ");
  Serial.println(WiFi.SSID());
  Serial.print("IP: ");
  Serial.println(WiFi.localIP());

  delay(1000);
}
//////////////////////////////////////////////////////
void setupServer() {
  preferences.begin("my-app", false);
  if (preferences.getString("indoorIP", "") != "") {
    Serial.println("INDOOR MODULE IP ALREADY EXIST");
    indoorIP = preferences.getString("indoorIP", "");
    Serial.print("INDOOR MODULE IP: ");
    Serial.println(indoorIP);

    Serial.println("trying to connected indoor module...");

  } else {
    Serial.println("indoor module's IP not found!");
    configPortal();
  }

}
//////////////////////////////////////////////////////
void configPortal() {
  preferences.begin("my-app", false);

  server.on("/", HTTP_GET, [](AsyncWebServerRequest * request) {
    request->send(SPIFFS, "/index.html", String(), false);
  });

  server.on("/Submit", HTTP_POST, [](AsyncWebServerRequest * request) {
    indoorIP = request->arg("indoorIP");
    preferences.putString("indoorIP", indoorIP);
    delay(500);
    Serial.println("trying to connected indoor module...");
    delay(500);
    Serial.println("restarting...");
    delay(500);
    ESP.restart();
  });

  server.serveStatic("/", SPIFFS, "/");
  server.begin();
  Serial.print("CONFIG PORTAL IP: ");
  Serial.println(WiFi.localIP());
  delay(portalOpenTime);
  Serial.println("restarting...");
  delay(500);
  ESP.restart();
}
//////////////////////////////////////////////////////
void onDemandConfig() {
  if (digitalRead(BOOT_BUTTON_PIN) == LOW) {
    Serial.println("ondemand config portal starting...");
    digitalWrite(LED_PIN, HIGH);
    configPortal();
  }
  delay(100);
}
//////////////////////////////////////////////////////

void loop() {

  //  float tem = dht.readTemperature();
  float hum = dht.readHumidity();
  float tem = bmp.readTemperature();
  float pre = bmp.readPressure() / 100.0F;
  float alt = bmp.readAltitude(1013.25);

  if (WiFi.status() == WL_CONNECTED) {

    HTTPClient http;
    http.begin(indoorIP);
    http.addHeader("Content-Type", "application/x-www-form-urlencoded");
    http.addHeader("Connection", "close");
    String postData = "tem=" + String(tem, 2) +
                      "&hum=" + String(hum, 0) +
                      "&pre=" + String(pre, 1) +
                      "&alt=" + String(alt, 0);

    Serial.println("Sending Data: " + postData);

    int httpResponseCode = http.POST(postData);
    String response = http.getString();

    Serial.print("HTTP code: " + String(httpResponseCode));
    if (httpResponseCode == 200) {
      Serial.print(", Send Successful!");
      digitalWrite(LED_PIN, HIGH);
      delay(300);
      digitalWrite(LED_PIN, LOW);
    }
    if (httpResponseCode == -1) {
      Serial.print(" Connection Failed!");
    }

    Serial.print(" Server Response: ");
    Serial.println(response);
    Serial.println("==========================================================");

    http.end();

  }

  delay(3000);

  onDemandConfig();

  if (WiFi.status() != WL_CONNECTED) {
    connectWiFi();
  }

}
