#include <WiFi.h>
#include <WiFiManager.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <AsyncTCP.h>
#include <ESPAsyncWebServer.h>
#include "connection_test.h"

// TODO: Cleaner setup code; less spaghetti
void setup() {
  WiFi.mode(WIFI_STA);
  Serial.begin(SERIAL_BAUD);
  pinMode(LED, OUTPUT);

  WiFiManager wm;

  bool res = wm.autoConnect("ESPDemo", "s3curep4ssw0rd");

  if(!res) {
    Serial.println("Failed to connect");
  }
  else {
    Serial.println("connected");
  }

  Serial.println("\nWiFi connected. IP: " + WiFi.localIP().toString());

  // Define webserver to handle incoming data in JSON format at /update
  server.on("/update", HTTP_POST, [](AsyncWebServerRequest *request) {}, NULL, UpdateSensorData);

  // Debug method for changing server IP for local development
  server.on("/set-server-ip", HTTP_POST, [](AsyncWebServerRequest *request) {}, NULL, UpdateServerIP);

  server.begin();
}

// loop will continuously send latest available data to server
void loop() {
  // Using JSON allows us to send data originally in native structure / class easily
  // No need for pesky byte-level encode / decode functions :)
  StaticJsonDocument<HTTP_OK> doc;
  doc["data_float"] = data.data_float;
  doc["data_double"] = data.data_double;
  doc["data_int"] = data.data_int;

  String json;
  serializeJson(doc, json);
  Serial.println("Sending JSON: " + json);

  if (WiFi.status() == WL_CONNECTED) {
    HTTPClient http;
    http.begin(server_url);
    http.addHeader("Content-Type", "application/json");

    int httpResponseCode = http.POST(json);

    if (httpResponseCode > 0) {
      // blink if we send so I can see it
      Serial.printf("POST success: %d\n", httpResponseCode);
      blinkLED(LED);
    } else {
      // reverse blink if failed to send + write to console
      Serial.printf("POST failed: %s\n", http.errorToString(httpResponseCode).c_str());
      doubleBlinkLED(LED);
    }

    http.end();
  } else {
    // No wifi == continuous LED ON
    Serial.println("WiFi not connected.");
    digitalWrite(LED, HIGH);
  }

  // wait between POST reqs
  delay(TEN_S);
}

// Blink whej send is successfull
void blinkLED(int pin) {
  digitalWrite(pin, HIGH);
  delay(THREE_HUNDRED_MS);
  digitalWrite(pin, LOW);
}

// Backwards blink for send error
void doubleBlinkLED(int pin) {
  digitalWrite(pin, HIGH);
  delay(THREE_HUNDRED_MS);
  digitalWrite(pin, LOW);
  delay(THREE_HUNDRED_MS);
  digitalWrite(pin, HIGH);
}

