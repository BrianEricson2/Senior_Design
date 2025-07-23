#include <WiFi.h>
#include <WiFiManager.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <AsyncTCP.h>
#include <ESPAsyncWebServer.h>
#include <functional>

// Keep track of server url in global cope
String server_url = "";

typedef enum {
  HTTP_OK = 200,
  HTTP_BAD_REQUEST = 400
} HTTPCodes_t;

// Pins
#define LED 2

// Locally Hosted server on ESP
#define SERVER_PORT 80
AsyncWebServer server(SERVER_PORT);

// Time delays
#define TEN_S 10000
#define THREE_HUNDRED_MS 300

// Serial baud rate
#define SERIAL_BAUD 115200

// Generic Sensor Data struct
struct SensorData {
  float data_float;
  double data_double;
  int data_int;
} data = {0, 0, 0};

// Definition of function for handling HTTP request to update sensor data
std::function<void(AsyncWebServerRequest*, uint8_t*, size_t, size_t, size_t)> UpdateSensorData =
  [](AsyncWebServerRequest *request, uint8_t *data_recv, size_t len, size_t index, size_t total)
  {
    StaticJsonDocument<HTTP_OK> doc;
    DeserializationError err = deserializeJson(doc, data_recv);
    if (err) {
      request->send(HTTP_BAD_REQUEST, "application/json", "{\"error\":\"Invalid JSON\"}");
      return;
    }

    // TODO: More Scalable way of defining data? So we don't need to look in 3 places across
    // 2 differen projects (server & esp) to change one field
    if (doc.containsKey("data_float")) data.data_float = doc["data_float"];
    if (doc.containsKey("data_double")) data.data_double = doc["data_double"];
    if (doc.containsKey("data_int")) data.data_int = doc["data_int"];

    Serial.println("Received data via POST:");
    serializeJsonPretty(doc, Serial);
    Serial.println();

    request->send(HTTP_OK, "application/json", "{\"status\":\"updated\"}");
  };

// Definition of function for handling HTTP request to update server IP address
std::function<void(AsyncWebServerRequest*, uint8_t*, size_t, size_t, size_t)> UpdateServerIP =
  [](AsyncWebServerRequest *request, uint8_t *data_recv, size_t len, size_t index, size_t total)
  {
    StaticJsonDocument<HTTP_OK> doc;
    DeserializationError err = deserializeJson(doc, data_recv);
    if (err) {
      request->send(HTTP_BAD_REQUEST, "application/json", "{\"error\":\"Invalid JSON\"}");
      return;
    }

    if (!doc.containsKey("server_ip")) {
      request->send(HTTP_BAD_REQUEST, "application/json", "{\"error\":\"Missing 'server_ip'\"}");
      return;
    }

    // TODO: Update all char* types to Strings
    const char* ip = doc["server_ip"];
    IPAddress parsed_ip;
    if (!parsed_ip.fromString(ip)) {
      request->send(HTTP_BAD_REQUEST, "application/json", "{\"error\":\"Invalid IP format\"}");
      return;
    }

    String raw_server_url = String(ip);
    Serial.print("Server IP set to: ");
    Serial.println(raw_server_url);
    server_url = "http://" + raw_server_url + ":3000/submit";
    Serial.println(server_url);

    request->send(HTTP_OK, "application/json", "{\"status\":\"server_url updated\"}");
  };

