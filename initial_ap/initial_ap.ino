/*********
  Parts of this code were selected/inspired from Rui Santos & Sara Santos - Random Nerd Tutorials
  https://RandomNerdTutorials.com/esp32-esp8266-input-data-html-form/
*********/


///////////////////////////////////////////////////////////////////////////////////////////////////////////////////
/* ARDUINO AND ESP LIBRARIES */
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////
#include <Arduino.h>
#include <WiFi.h>
#include <AsyncTCP.h>
#include <ESPAsyncWebServer.h>
#include <WiFiAP.h>
#include "esp_sleep.h"
#include <Wire.h>
#include <BH1750.h>

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////
/* GLOBAL VARIABLES */
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////

// async_web_server used when ESP32 is in AP mode, creating its own WiFi network
AsyncWebServer async_web_server(80);

// main_server used when ESP32 is in station mode, connected to user's WiFi network
AsyncWebServer main_server(80);

// SSID & password when ESP32 is in AP mode
// user connects to ESP32 AP to send SSID & password for personal WiFi
const char* esp_ap_ssid = "vDIg4qYsY2HZ";
const char* esp_ap_pw = "I6,mqr!Cs4ui";
IPAddress esp_ap_ip_addr = "";

// SSID & password for user's personal WiFi
// ESP will connect to this AP as station for length of use
//String client_ap_ssid = "";
//String client_ap_pw = "";
String client_ap_ssid = "";
String client_ap_pw = "";

// state variables
bool end_ap_state = false;
bool end_station_init_state = false;
bool enter_sleep_state = false;
bool moisture_sensor_init = false;
bool initial_sleep_done = false;

// "Valid values should be positive values less than RTC slow clock period * (2 ^ RTC timer bitwidth)."
int sleep_duration_us = 30000000;
int current_SMV = -1;
float dry_SMV = -1;
float wet_SMV = -1;
float SMV_frac = 0.25;
float SMV_threshold = -1;

unsigned int watering_duration = 0;
float water_lvl = -1;
float light_val_lux = 0;
unsigned int total_sunlight_cnt = 0;
float battery_lvl = 0;

//const int pump_pin = -1;
const int water_lvl_sensor_pin = 34;
const int sms_data_pin = 35;
const int sms_power_pin = 32;
const int light_sensor_pwr_pin = 23;
const int battery_lvl_pin = 5;

// use addr 0x23 if addr pin voltage is < 0.7*Vcc
// use addr 0x5C if addr pin voltage is > 0.7*Vcc
const BH1750 light_sensor(0x23);

// ADC and calibration
const float vRef = 3.3;         // ADC reference voltage (ESP32)
const float correctionFactor = 1.05; // Tune this to match real voltmeter


///////////////////////////////////////////////////////////////////////////////////////////////////////////////////
/* HTML PAGES/FILES */
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////

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



char dry_SMV_sensor_page[] = R"rawliteral(
<!DOCTYPE HTML> <html>
<head> <title>dry_SMV_sensor_page</title> </head>
<body>
<p>The moisture sensor must be held in the air to initialize value of dry soil (0% humidity).<br>
Press OK when moisture sensor is held in the air.</p>
<form action="/get">
<input type="submit" name="submit_button">
</form>
</body> </html>)rawliteral";

char wet_SMV_sensor_page[] = R"rawliteral(
<!DOCTYPE HTML> <html>
<head> <title>wet_SMV_sensor_page</title> </head>
<body>
<p>The moisture sensor must be held in water to initialize value of wet soil (100% humidity).<br>
Press OK when moisture sensor is held in the water.</p>
<form action="/get">
<input type="submit" name="submit_button">
</form>
</body> </html>)rawliteral";

char main_page[] = R"(
<!DOCTYPE HTML> <html>
<head> <title>main_page</title> </head>
<body>
<form action=/get target="_self">
<label for="SMV_perc">maximum soil moisture threshold (percent):</label>
<input type="text" name="SMV_perc"><br>
Current max SMV threshold = %SMV_perc%<br><br>
<input type="submit" value="Enter">
</form>
</body> </html>)";


int serial_output = 255;

void setup() {
  Serial.begin(9600);
  
  while (!end_ap_state || !end_station_init_state || !moisture_sensor_init || !enter_sleep_state) {
    if(!end_ap_state) {
      ap_init_state();

      delay(500);

      Serial.println("");
      Serial.println(client_ap_ssid);
      Serial.println(client_ap_pw);

      //client_ap_ssid = "Rialto_Resident";
      //client_ap_pw = "rock144ancient";
      //end_ap_state = true;
    }
    else if(!end_station_init_state) {
      station_init_state();
      moisture_sensor_init = true;
      enter_sleep_state = true;
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
  if(var == "SMV_perc"){
    return String((SMV_frac)*100.0);
  }
}

void ap_init_state () {
  WiFi.mode(WIFI_AP);
  if(WiFi.softAP(esp_ap_ssid, esp_ap_pw)){
    esp_ap_ip_addr = WiFi.softAPIP();
  }
  else {
    Serial.println("Error creating AP.");
    while (true);
  }
  if(serial_output & 1 != 0) {
    Serial.print("ESP AP IP Address: ");
    Serial.println(esp_ap_ip_addr);
    serial_output = serial_output & 254;
  }

  async_web_server.on("/", HTTP_GET, [](AsyncWebServerRequest *request){
    request->send(200,"text/html",front_page, varRepl); } );

  async_web_server.on("/get", HTTP_GET, [](AsyncWebServerRequest *request){
    if(request->hasParam("ssid")) {
      client_ap_ssid = request->getParam("ssid")->value();
    }
    if(request->hasParam("pswd")) {
      client_ap_pw = request->getParam("pswd")->value();
    }
    end_ap_state = (client_ap_ssid != "" && client_ap_pw != "");
    request->send(200, "text/html", front_page, varRepl);
  });

  async_web_server.begin();

  while (!end_ap_state) {
    //Serial.print("ESP AP IP Address: "); Serial.println(esp_ap_ip_addr);
    delay(200);
  }
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
        while(counter < 20) {
          if(WiFi.isConnected()) {
            Serial.println("ESP connected to WiFi in station mode.");
            Serial.println("IP address: "); Serial.println(WiFi.localIP());
            WiFi.setAutoReconnect(true);
            end_station_init_state = true;
            return;
          }
          else {
            delay(500); Serial.print(" ... "); counter++;
          }
        }
        Serial.println("Error connecting to WiFi. Please reenter WiFi info.");
        end_ap_state = false;
        return;
      }
    }
  }
}


void loop() {
  delay(100);
}
