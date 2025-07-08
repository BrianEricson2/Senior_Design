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
String client_ap_ssid = "Rialto_Resident";
String client_ap_pw = "rock144ancient";

// state variables
bool end_ap_state = false;
bool end_station_init_state = false;
bool enter_sleep_state = false;
bool moisture_sensor_init = false;

// "Valid values should be positive values less than RTC slow clock period * (2 ^ RTC timer bitwidth)."
int sleep_duration_us = 30000000;
int current_SMV = -1;
float dry_SMV = -1;
float wet_SMV = -1;
float max_SMV_frac = 0.90;
float min_SMV_frac = 0.25;
float max_SMV_threshold = -1;
float min_SMV_threshold = -1;

unsigned int watering_duration = 0;
float water_lvl = -1;

//const int pump_pin = -1;
const int water_lvl_sensor_pin = 34;
const int moisture_sensor_pin = 35;


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
<label for="max_SMV_perc">maximum soil moisture threshold (percent):</label>
<input type="text" name="max_SMV_perc"><br>
Current max SMV threshold = %max_SMV_perc%<br><br>
<input type="submit" value="Enter">
</form>
</body> </html>)";


int serial_output = 255;

void setup() {
  Serial.begin(9600);

  //pinMode(pump_pin,OUTPUT);
  pinMode(water_lvl_sensor_pin,INPUT);
  pinMode(moisture_sensor_pin,INPUT);

  while (!end_ap_state || !end_station_init_state || !moisture_sensor_init || !enter_sleep_state) {
    end_ap_state = true;
    if(!end_ap_state) {
      ap_init_state();

      delay(500);

      //client_ap_ssid = "Rialto_Resident";
      //client_ap_pw = "rock144ancient";
      //end_ap_state = true;
    }
    else if(!end_station_init_state) {
      station_init_state();
      moisture_sensor_init = true;
      enter_sleep_state = true;
    }
    
    else if(!moisture_sensor_init) {
      //init_moisture_sensor();
      //delay(500);
    }
    //else if(!sleep_state) {
    //  Serial.println("Disabling all wakeup sources.");
    //  esp_sleep_disable_wakeup_source(ESP_SLEEP_WAKEUP_ALL);

    //  Serial.println("Enabling WiFi, WiFi Beacon (modem-sleep), and timer wakeup sources.");
    //  esp_sleep_enable_wifi_wakeup();
    //  esp_sleep_enable_wifi_beacon_wakeup();
    //  esp_sleep_enable_timer_wakeup(sleep_duration_us);
    //}
  }
  Serial.println("Begin main server");
  main_server.on("/", HTTP_GET, [](AsyncWebServerRequest *request){
    (*request).send(200,"text/html",main_page, varRepl); } );

  main_server.on("/get", HTTP_GET, [](AsyncWebServerRequest *request){
    if(request->hasParam("ssid")) {
      max_SMV_frac = (request->getParam("max_SMV_perc")->value()).toFloat()/100.0;
    }
    request->send(200, "text/html", front_page, varRepl);
  });
  main_server.begin();
}

// replaces html variable names with value of corresponding variable in ESP memory 
String varRepl(const String& var){
  if(var == "ssid"){
    return client_ap_ssid;
  }
  if(var == "pswd"){
    return client_ap_pw;
  }
  if(var == "max_SMV_perc"){
    return String((max_SMV_frac)*100.0);
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
    end_ap_state = (client_ap_ssid != "" && client_ap_pw != "");
    request->send(200, "text/html", front_page, varRepl);
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

// SMV = soil moisture value
float getSMV() {
  int temp = 0; int N=3;
  for (int i = 0; i < N; i++) {
    temp += analogRead(moisture_sensor_pin);
    delay(100);
  }
  float SMV = (1.0 * temp) / N;
  Serial.print("SMV value: "); Serial.println(SMV);
  return SMV;
}

void init_moisture_sensor() {
  main_server.begin();
  while (dry_SMV < 0) {
    main_server.on("/", HTTP_GET, [](AsyncWebServerRequest *request){
      (*request).send(200,"text/html",dry_SMV_sensor_page); } );
    main_server.on("/get", HTTP_GET, [](AsyncWebServerRequest *request){
      if(request->hasParam("submit_button")) {
        dry_SMV = 2;
        Serial.println(dry_SMV);
      }
      request->send(200, "text/html", wet_SMV_sensor_page);
    });
    delay(250);
  }

  while (wet_SMV < 0) {
    main_server.on("/", HTTP_GET, [](AsyncWebServerRequest *request){
      (*request).send(200,"text/html",wet_SMV_sensor_page); } );
    main_server.on("/get", HTTP_GET, [](AsyncWebServerRequest *request){
      if(request->hasParam("submit_button")) {
        wet_SMV = 2;
        Serial.println(wet_SMV);
      }
      request->send(200, "text/html", wet_SMV_sensor_page);
    });
    delay(100);
  }

  moisture_sensor_init = !(dry_SMV < 0 || wet_SMV < 0);
  //Serial.println(dry_SMV);
  //Serial.println(wet_SMV);
}

void water_plant() {
  unsigned long start_time = micros();
  //digitalWrite(pump_pin, HIGH);
  unsigned long dist_from_limit = 4294967295 - start_time;
  if(dist_from_limit > watering_duration) {
    while((micros() - start_time) < watering_duration) {}
  }
  else{
    unsigned long stop_counter = watering_duration - dist_from_limit;
    while(micros() < stop_counter) {}
  }
  //digitalWrite(pump_pin, LOW);
}

float get_water_lvl () {
  int temp = 0; int N=3;
  for (int i = 0; i < N; i++) {
    temp += analogRead(water_lvl_sensor_pin);
    delay(100);
  }
  float water_lvl = (1.0 * temp) / N;
  Serial.print("Water Level value: "); Serial.println(water_lvl);
  return water_lvl;
}

void loop() {
  Serial.println("Entering light-sleep mode.");
  //sleep_state = true;
  Serial.println("Disabling all wakeup sources.");
  esp_sleep_disable_wakeup_source(ESP_SLEEP_WAKEUP_ALL);

  Serial.println("Enabling WiFi, WiFi Beacon (modem-sleep), and timer wakeup sources.");
  esp_sleep_enable_wifi_wakeup();
  esp_sleep_enable_wifi_beacon_wakeup();
  esp_sleep_enable_timer_wakeup(sleep_duration_us);
  esp_light_sleep_start();
  esp_sleep_wakeup_cause_t wakeup_cause = esp_sleep_get_wakeup_cause();
  switch (wakeup_cause) {
    case ESP_SLEEP_WAKEUP_TIMER:
      Serial.println("woke up from timer");
      //current_SMV = getSMV();
      //if(current_SMV > min_SMV_threshold && current_SMV < max_SMV_threshold){
      //  water_plant();
      //}
      break;
    case ESP_SLEEP_WAKEUP_WIFI:
      Serial.println("woke up from wifi");
      break;
    default:
      break;
  }
  /*Serial.println(esp_ap_ip_addr);
  Serial.println(esp_ap_ssid);
  Serial.println(esp_ap_pw);
  Serial.println(client_ap_ssid);
  Serial.println(client_ap_pw);
  Serial.println(end_ap_state);
  Serial.println(end_station_init_state);*/
  delay(100);
}
