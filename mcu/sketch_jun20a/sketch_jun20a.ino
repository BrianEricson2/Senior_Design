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

const int pump_pin = -1;
const int water_lvl_sensor_pin = 34;
const int sms_data_pin = 35;
const int sms_power_pin = 32;
const int light_sensor_pwr_pin = 23;
const int battery_lvl_pin = 5;

// use addr 0x23 if addr pin voltage is < 0.7*Vcc
// use addr 0x5C if addr pin voltage is > 0.7*Vcc
BH1750 light_sensor(0x23);

// ADC and calibration
const float vRef = 3.3;         // ADC reference voltage (ESP32)
const float correctionFactor = 1.05; // Tune this to match real voltmeter
const int R1 = 10000;
const int R2 = 5000;


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
<input type="submit" name="submit_button" value="OK">
</form>
</body> </html>)rawliteral";

char wet_SMV_sensor_page[] = R"rawliteral(
<!DOCTYPE HTML> <html>
<head> <title>wet_SMV_sensor_page</title> </head>
<body>
<p>The moisture sensor must be held in water to initialize value of wet soil (100% humidity).<br>
Press OK when moisture sensor is held in the water.</p>
<form action="/get">
<input type="submit" name="submit_button" value="OK">
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

  pinMode(pump_pin,OUTPUT);
  pinMode(water_lvl_sensor_pin,INPUT);
  pinMode(sms_data_pin,INPUT);
  pinMode(sms_power_pin,OUTPUT);
  pinMode(light_sensor_pwr_pin,OUTPUT);
  Wire.begin();
  light_sensor.begin(BH1750::UNCONFIGURED);

  while (!end_ap_state || !end_station_init_state || !moisture_sensor_init || !enter_sleep_state) {
    end_ap_state = true;
    if(!end_ap_state) {
      ap_init_state();

      delay(500);

      //Serial.println("");
      //Serial.println(client_ap_ssid);
      //Serial.println(client_ap_pw);

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

float get_wet_SMV() {
  digitalWrite(sms_power_pin,HIGH);
  delay(2500);
  float SMV_temp = 0; int N = 10;
  float wet_SMV = analogRead(sms_data_pin);

  Serial.println();
  for (int i = 0; i < N; i++) {
    delay(250);
    SMV_temp = analogRead(sms_data_pin);
    Serial.print(SMV_temp); Serial.print(" ");
    if (wet_SMV > SMV_temp)
      wet_SMV = SMV_temp;
  }
  Serial.println();

  digitalWrite(sms_power_pin,LOW);
  Serial.print("wet (minimum) SMV: "); Serial.println(wet_SMV);
  return wet_SMV;
}

float get_dry_SMV() {
  digitalWrite(sms_power_pin,HIGH);
  delay(2500);
  float SMV_temp = 0; int N = 10;
  float dry_SMV = analogRead(sms_data_pin);

  Serial.println();
  for (int i = 0; i < N; i++) {
    delay(500);
    SMV_temp = analogRead(sms_data_pin);
    Serial.print(SMV_temp); Serial.print(" ");
    if (dry_SMV < SMV_temp)
      dry_SMV = SMV_temp;
  }
  Serial.println();

  digitalWrite(sms_power_pin,LOW);
  Serial.print("dry (maximum) SMV: "); Serial.println(dry_SMV);
  return dry_SMV;
}

// SMV = soil moisture value
float get_SMV() {
  digitalWrite(sms_power_pin,HIGH);
  delay(2500);
  float SMV = 0; int N=10;
  float SMV_temp = 0;
  for (int i = 0; i < N; i++) {
    SMV_temp = analogRead(sms_data_pin);
    SMV += SMV_temp;
    if (SMV > SMV_temp)
      SMV = SMV_temp;
    if (SMV < SMV_temp)
      SMV = SMV_temp;
    delay(500);
  }

  digitalWrite(sms_power_pin,LOW);
  SMV = SMV / N;
  Serial.print("SMV Value: "); Serial.println(SMV);
  return SMV;
}

void init_moisture_sensor() {
  main_server.begin();
  while (dry_SMV < 0) {
    main_server.on("/", HTTP_GET, [](AsyncWebServerRequest *request){
      (*request).send(200,"text/html",dry_SMV_sensor_page); } );
    main_server.on("/get", HTTP_GET, [](AsyncWebServerRequest *request){
      if(request->hasParam("submit_button")) {
        dry_SMV = get_dry_SMV();
        Serial.println(dry_SMV);
      }
      request->send(200, "text/html", wet_SMV_sensor_page);
    });
    delay(500);
  }

  while (wet_SMV < 0) {
    main_server.on("/", HTTP_GET, [](AsyncWebServerRequest *request){
      (*request).send(200,"text/html",wet_SMV_sensor_page); } );
    main_server.on("/get", HTTP_GET, [](AsyncWebServerRequest *request){
      if(request->hasParam("submit_button")) {
        wet_SMV = get_wet_SMV();
        Serial.println(wet_SMV);
      }
      request->send(200, "text/html", wet_SMV_sensor_page);
    });
    delay(500);
  }

  moisture_sensor_init = !(dry_SMV < 0 || wet_SMV < 0);
  //Serial.println(dry_SMV);
  //Serial.println(wet_SMV);
}

void water_plant() {
  Serial.println("Enabling power to plant");
  unsigned long current_time = micros();
  digitalWrite(pump_pin, HIGH);
  // (max limit - buffer) - current_time
  //buffer needed because 
  unsigned long dist_from_limit = (4294967295 - 100000) - current_time;
  if(dist_from_limit > watering_duration) { //if watering duration will not exceed limit
    while((micros() - current_time) < watering_duration) {}
  }
  else{ // watering duration will exceed limit
    unsigned long stop_counter = watering_duration - dist_from_limit;
    while(micros() < stop_counter) {}
  }
  digitalWrite(pump_pin, LOW);
  Serial.println("Disabling power to plant");
  Serial.print("start time: "); Serial.println(current_time);
  Serial.print("finish time: "); Serial.println(micros());
}

float get_light_val () {
  // supply 3.3V to light sensor Vcc
  digitalWrite(light_sensor_pwr_pin,HIGH);
  delay(50);
  // configure light_sensor into continuous high resolution mode
  light_sensor.configure(BH1750::CONTINUOUS_HIGH_RES_MODE);
  delay(50);
  // light_val - return variable; stores total then divide by N to get average
  // N - number of measurements
  // temp - each individual measurement
  // fail_cnt - number of times measurementReady() fails
  float light_val = 0; int N=5;
  float temp = 0; int fail_cnt = 0;

  for(int i = 0; i < N; i++){

    while (!light_sensor.measurementReady()) {
      // waits at most 2.5s before returning 0 & outputting failure message to serial monitor
      delay(50); fail_cnt++;
      if(fail_cnt >= 50) {
        Serial.println("Measurement Ready failed for light sensor. Returning 0.");
        return 0;
      }
    }

    temp = light_sensor.readLightLevel(); //measurement

    // readLightLevel returns -1 or -2 if failure occurs
    if(temp > 0) {
      light_val += temp;
    }
    else { // failure
      // retry another measurement
      i = i-1;
    }
    delay(50);
  }

  light_val = light_val / N;
  Serial.print("Light Value: "); Serial.println(light_val);
  // power down light sensor
  light_sensor.configure(BH1750::UNCONFIGURED);
  delay(50);
  // disable 3.3V power to light sensor
  digitalWrite(light_sensor_pwr_pin,LOW);
  return light_val;
}

float read_light_sensor () {
  int sunlight_threshold = 10000;
  float new_light_val = get_light_val();
  unsigned int sunlight_interval = sleep_duration_us/100;
  if ((new_light_val <= sunlight_threshold && light_val_lux >= sunlight_threshold) || (new_light_val >= sunlight_threshold && light_val_lux <= sunlight_threshold)) {
    total_sunlight_cnt += sunlight_interval / 2;
  }
  /*if ((new_light_val <= sunlight_threshold) ^ (light_val_lux >= sunlight_threshold)) {
    total_sunlight_cnt += sunlight_interval / 2;
  }*/
  else if (new_light_val > sunlight_threshold && light_val_lux > sunlight_threshold) {
    total_sunlight_cnt += sunlight_interval;
  }
  light_val_lux = new_light_val;
  return light_val_lux;
}

void read_battery_level() {
  int raw = analogRead(battery_lvl_pin);
  float v_adc = raw * vRef / 4095.0;
  battery_lvl = v_adc * ((R1 + R2) / R2) * correctionFactor;
}

void loop() {
  esp_sleep_wakeup_cause_t wakeup_cause;
  if(initial_sleep_done) {
    wakeup_cause = esp_sleep_get_wakeup_cause();
    initial_sleep_done = true;
  }
  else {
    wakeup_cause = ESP_SLEEP_WAKEUP_UNDEFINED;
  }
  
  switch (wakeup_cause) {
    case ESP_SLEEP_WAKEUP_TIMER:
      Serial.println("\n\nwoke up from timer");
      read_battery_level();
      read_light_sensor();
      current_SMV = get_SMV();
      Serial.print("measured SMV: "); Serial.println(current_SMV);
      SMV_threshold = dry_SMV - (dry_SMV - wet_SMV)*SMV_frac;
      if(current_SMV > SMV_threshold){
        water_plant();
      }
      break;
    case ESP_SLEEP_WAKEUP_WIFI:
      Serial.println("woke up from wifi");
      break;
    default:
      Serial.println("Entering light-sleep mode.");
      Serial.println("Disabling all wakeup sources.");
      esp_sleep_disable_wakeup_source(ESP_SLEEP_WAKEUP_ALL);

      Serial.println("Enabling WiFi, WiFi Beacon (modem-sleep), and timer wakeup sources.");
      esp_sleep_enable_wifi_wakeup();
      esp_sleep_enable_wifi_beacon_wakeup();
      esp_sleep_enable_timer_wakeup(sleep_duration_us);
      esp_light_sleep_start();
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
