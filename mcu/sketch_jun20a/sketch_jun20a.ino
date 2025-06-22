/*********
  Parts of this code were selected/inspired from Rui Santos & Sara Santos - Random Nerd Tutorials
  https://RandomNerdTutorials.com/esp32-esp8266-input-data-html-form/
*********/

#include <Arduino.h>
#include <WiFi.h>
#include <AsyncTCP.h>
#include <ESPAsyncWebServer.h>
#include <WiFiAP.h>
#include "esp_sleep.h"

// SSID & password when ESP32 is in AP mode
// user connects to ESP32 AP to send SSID & password for personal WiFi
const char* esp_ap_ssid = "vDIg4qYsY2HZ";
const char* esp_ap_pw = "I6,mqr!Cs4ui";
IPAddress esp_ap_ip_addr = "";

// SSID & password for user's personal WiFi
// ESP will connect to this AP as station for length of use
String client_ap_ssid = "";
String client_ap_pw = "";

// state variables
bool end_ap_state = false;
bool end_station_init_state = false;
bool enter_sleep_state = false;
bool moisture_sensor_init = false;

// "Valid values should be positive values less than RTC slow clock period * (2 ^ RTC timer bitwidth)."
int sleep_duration_us = 0;
int current_moisture = -1;
int min_moisture = -1;
int max_moisture = -1;

const int pump_pin = -1;
const int moisture_sensor_pin = -1;

// html page; interfaces between ESP AP & user
// user defines SSID and password of personal WiFi network
char front_page[] = R"(
<!DOCTYPE HTML> <html>
<head> <title>front_page</title> </head>
<body>
<form action=/get target="_self">
<label for="ssid">SSID:</label>
<input type="text" name="ssid"><br>
Current ssid = %ssid%<br><br>
<label for="pswd">Password:</label>
<input type="text" name="pswd"><br>
Current password = %pswd%<br>
<input type="submit" value="Enter">
</form>
</body> </html>)";

char moisture_sensor_page[] = R"rawliteral(
<!DOCTYPE HTML> <html>
<head> <title>moisture_sensor_page</title> </head>
<body>
<p>The moisture sensor must be held in the air to initialize value of dry soil (0% humidity).<br>
Press OK when moisture sensor is held in the air.</p>
<input type="submit" value="OK">
<form action=/get target="_self">
<label for="ssid">SSID:</label>
<input type="text" name="ssid"><br>
Current ssid = %ssid%<br><br>
<label for="pswd">Password:</label>
<input type="text" name="pswd"><br>
Current password = %pswd%<br>
<input type="submit" value="Enter">
</form>
</body> </html>)rawliteral";

AsyncWebServer async_web_server(80);

void setup() {
  Serial.begin(9600);

  pinMode(pump_pin,OUTPUT);
  pinMode(moisture_sensor_pin,INPUT);

  while (!end_ap_state && !end_station_init_state && !moisture_sensor_init && !enter_sleep_state) {
    if(!end_ap_state) {
      ap_init_state();

      /*client_ap_ssid = "Rialto_Resident";
      client_ap_pw = "rock144ancient";
      end_ap_state = true;*/
    }
    else if(!end_station_init_state) {
      station_init_state();
    }
    else if(!moisture_sensor_init) {
      init_moisture_sensor();
    }
    else if(!sleep_state) {
      Serial.println("Disabling all wakeup sources.");
      esp_sleep_disable_wakeup_source(ESP_SLEEP_WAKEUP_ALL);

      Serial.println("Enabling WiFi, WiFi Beacon (modem-sleep), and timer wakeup sources.");
      esp_sleep_enable_wifi_wakeup();
      esp_sleep_enable_wifi_beacon_wakeup();
      esp_sleep_enable_timer_wakeup(sleep_duration_us);
    }
  }
  
}

// replaces html variable names with value of corresponding variable in ESP memory 
String varRepl(const String& var){
  if(var == "ssid"){
    return client_ap_ssid;
  }
  if(var == "pswd"){
    return client_ap_pw;
  }
}

void ap_init_state () {
  WiFi.mode(WIFI_AP);
  if(WiFi.softAP(esp_ap_ssid, esp_ap_pw)){
    esp_ap_ip_addr = WiFi.softAPIP();
    Serial.print("AP IP Address: "); Serial.println(esp_ap_ip_addr);
  }
  else {
    Serial.println("Error creating AP.");
    while (true);
  }

  async_web_server.on("/", HTTP_GET, [](AsyncWebServerRequest *request){
    (*request).send(200,"text/html",front_page, varRepl); } );

  async_web_server.on("/get", HTTP_GET, [](AsyncWebServerRequest *request){
    String ssid_temp;
    String pw_temp;
    if(request->hasParam("ssid")) {
      client_ap_ssid = request->getParam("ssid")->value();
    }
    if(request->hasParam("pswd")) {
      client_ap_pw = request->getParam("pswd")->value();
    }
    Serial.println(client_ap_ssid);
    Serial.println(client_ap_pw);
    end_ap_state = (client_ap_ssid != "" && client_ap_pw != "");
    request->send(200,"text/text","OK");
  });
  async_web_server.begin();
}

void station_init_state() {
  int counter = 0;
  if(end_ap_state) {
  Serial.println("Disconnecting ESP as AP.");
    if(WiFi.softAPdisconnect()){
      Serial.println("Initializing ESP32 in station mode.");
      WiFi.mode(WIFI_STA);
      if(client_ap_ssid != "" && client_ap_pw != "") {
        WiFi.begin(client_ap_ssid, client_ap_pw);
        while(counter < 5) {
          if(WiFi.isConnected()) {
            Serial.println("ESP connected to WiFi in station mode.");
            Serial.println("IP address: "); Serial.println(WiFi.localIP());
            WiFi.setAutoReconnect(true);
            end_station_init_state = true;
            return;
          }
          delay(500); Serial.print(" ... "); counter++;
        }
        Serial.println("Error connecting to WiFi. Please reenter WiFi info.");
        end_ap_state = false;
        return;
      }
    }
  }
}



void water_plant() {
  unsigned long start_time = micros();
  digitalWrite(pump_pin, HIGH);
  unsigned long dist_from_limit = 4294967295 - start_time;
  if(dist_from_limit > 15000000) {
    while((micros() - start_time) < 15000000) {}
  }
  else{
    unsigned long stop_counter = 15000000 - dist_from_limit;
    while(micros() < stop_counter) {}
  }
  digitalWrite(pump_pin, LOW);
}

void loop() {
  Serial.println("Entering light-sleep mode.");
  sleep_state = true;
  esp_light_sleep_start();
  esp_sleep_wakeup_cause_t wakeup_cause = esp_sleep_get_wakeup_cause();
  switch (wakeup_cause) {
    case ESP_SLEEP_WAKEUP_TIMER:
      current_moisture = analogRead(moisture_sensor_pin);
      if()
      water_plant();
      break;
    case ESP_SLEEP_WAKEUP_WIFI:
      break;
    default:
      break;
  }
  else if(sleep_state) {
    //wakeup occurred
  }
  /*Serial.println(esp_ap_ip_addr);
  Serial.println(esp_ap_ssid);
  Serial.println(esp_ap_pw);
  Serial.println(client_ap_ssid);
  Serial.println(client_ap_pw);
  Serial.println(end_ap_state);
  Serial.println(end_station_init_state);*/
  delay(5000);
}
