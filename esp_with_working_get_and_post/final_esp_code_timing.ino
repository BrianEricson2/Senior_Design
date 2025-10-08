#include <Arduino.h>
#include <WiFi.h>
#include <AsyncTCP.h>
#include <ESPAsyncWebServer.h>
#include <WiFiAP.h>
#include "esp_sleep.h"
#include <Wire.h>
#include <BH1750.h>
#include "time.h"
#include <Preferences.h>
#include <nvs_flash.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>

// async_web_server used when ESP32 is in AP mode, creating its own WiFi network
AsyncWebServer async_web_server(80);

// main_server used when ESP32 is in station mode, connected to user's WiFi network
AsyncWebServer main_server(80);

//HTTPClient http_client;

// SSID & password when ESP32 is in AP mode
// user connects to ESP32 AP to send SSID & password for personal WiFi
const char* esp_ap_ssid = "vDIg4qYsY2HZ";
const char* esp_ap_pw = "I6,mqr!Cs4ui";
IPAddress pot_ip_ap = "";

// SSID & password for user's personal WiFi
// ESP will connect to this AP as station for length of use
//String client_ap_ssid = "";
//String client_ap_pw = "";
String client_ap_ssid = "";
String client_ap_pw = "";
IPAddress pot_ip_sta = "";

String server_url = "";
String uuid = "";

// state variables
bool end_flash_state = false;
bool end_ap_state = false;
bool end_station_init_state = false;
bool enter_sleep_state = false;
bool moisture_sensor_init = false;
bool sensor_val_init = false;
bool initial_sensor_check_done = false;
bool operation_server_init = false;
bool leave_display = false;

bool get_dry_SMV_flag = false;
bool get_wet_SMV_flag = false;

// "Valid values should be positive values less than RTC slow clock period * (2 ^ RTC timer bitwidth)."
int sleep_duration_us = 10000000;
int sampling_period = 0;
float current_SMV = -1;
float dry_SMV = -1;
float wet_SMV = -1;
float SMV_frac = 0;
float SMV_threshold = -1;

unsigned int watering_duration_us = 0;
float water_lvl = -1;
float light_val_lux = -1;
unsigned int total_sunlight_cnt = 0;
unsigned int maximum_sunlight = 0;
float battery_lvl = 0;
bool water_level_low = false;

// variables to hold display values for sensor_init_page
String SMV_perc_disp = "";
String max_sun_disp = "";
String sampling_period_disp = "";
String watering_duration_disp = "";

const int water_lvl_pin = 36; //GPIO36 - pin 4
const int sms_data_pin = 34; //GPIO34 - pin 6
const int battery_lvl_pin = 35; //GPIO35 - pin 7
const int sms_power_pin = 16; //GPIO16 - pin 27
const int light_sensor_pwr_pin = 17; //GPIO17 - pin 28
const int pump_ctrl_pin = 18; //GPIO18 - pin 30
const int wls_power_pin = 23; //GPIO23 - pin 37
const int ledPin     = 2;    // Onboard blue LED

// use addr 0x23 if addr pin voltage is < 0.7*Vcc
// use addr 0x5C if addr pin voltage is > 0.7*Vcc
BH1750 light_sensor(0x23);

// ADC and calibration
const float vRef = 3.3;         // ADC reference voltage (ESP32)
const float correctionFactor = 1.11; // Tune this to match real voltmeter
const float R1 = 100.0; // kΩ (top)
const float R2 = 47.0;  // kΩ (bottom)

// Time Correction
time_t wakeup_time;
time_t go_sleep_time;

// Flash Memory
#define RW_MODE false
#define RO_MODE true
Preferences sys_pref;

struct timeval tv_now;
time_t before_wifi_time;
time_t after_wifi_time;
time_t prev_ntp_time;

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

void setup() {
  Serial.begin(9600);

  // get system time after waking up (time before connecting to wifi)
  gettimeofday(&tv_now, NULL);
  before_wifi_time = tv_now.tv_sec;
  Serial.print("before_wifi_time: "); Serial.println(before_wifi_time);

  esp_sleep_wakeup_cause_t wakeup_cause = esp_sleep_get_wakeup_cause();

  if(wakeup_cause == ESP_SLEEP_WAKEUP_TIMER) {
    Serial.println("timer wakeup");
  }
  else if(wakeup_cause == ESP_SLEEP_WAKEUP_EXT0) {
    Serial.println("ext wakeup");
    //end_flash_state = true;
    // possibly use RTC GPIO as a kind of reconfiguration (whereas the reset button is power on/off)
    // configure sensor thresholds / values for new plant
    // or reconfigure / delete data for existing plant
    // or select between existing plant configurations
  }
  else {
    Serial.print("other wakeup source: "); Serial.println(wakeup_cause);
  }

  //wifi_init(); // use for production
  station_init_state(); // hard code wifi info & use for simulation/testing
  while (WiFi.status() != WL_CONNECTED) {
    Serial.println("not connected to wifi");
    end_ap_state = true;
    station_init_state();
    delay(5000);
  }
  
  //time_correction(); // possibly use for production

  // Open server to accept requests to modify webserver IP
//  server.on("/set-server-ip", HTTP_POST, [](AsyncWebServerRequest *request) {}, NULL, UpdateServerIP);
//  server.begin();
  // Wait until server url is populated
//  while (server_url.length() == 0) { delay(5000); }
  delay(5000);
  setup_uuid();

  Serial.println("attempting to rx data from web server");
  // read threshold info set by user via web server
  // Ensure that we hold until web data is received
  while (!web_data_rx()) { delay(5000); }
  Serial.println("done with rx function");

  //sensor_check(); //uncomment for production / hardware testing build
  delay(5000); // example delay for simulation; mimic delay of sensor_check function

  Serial.println("attempting to tx data to web server");
  // send sensor data to user via web server
  web_data_tx();
  Serial.println("done with tx function");

  //enter_deep_sleep(); // use for production
  if (1) { // use this block if not in production
    Serial.println("Entering deep-sleep mode.");
    esp_sleep_enable_timer_wakeup(sleep_duration_us);
    esp_deep_sleep_start();
  }
}

String varRepl(const String& var){
  if(var == "ssid") {
    return client_ap_ssid;
  }
  if(var == "pswd") {
    return client_ap_pw;
  }
}

void wifi_init () {
  // check if wifi info exists in flash
  sys_pref.begin("genPrefs", RW_MODE);
  bool wifi_info_exist = sys_pref.isKey("wifiUnCL") && sys_pref.isKey("wifiPwCL");
  if(wifi_info_exist) {
    Serial.println("wifi_info_exist == true");
    //read from flash & store in local variable
    client_ap_ssid = sys_pref.getString("wifiUnCL");
    Serial.print("client_ap_ssid: "); Serial.println(client_ap_ssid);
    client_ap_pw = sys_pref.getString("wifiPwCL");
    Serial.print("client_ap_pw: "); Serial.println(client_ap_pw);

    // check if wifi info from flash is not empty
    end_ap_state = (client_ap_ssid != "" && client_ap_pw != "");

    // if wifi info exists in flash & is valid, attempt network connection
    if (end_ap_state) {
      //sys_pref.end();
      Serial.println("end_ap_state == true");
      station_init_state();
    }
  }

  // if wifi info is not in flash or is not valid, or connection is unsuccessful
  while (!end_ap_state | !end_station_init_state) {
    Serial.println("user must initialize wifi info");
    ap_init_state();
    station_init_state();
    sys_pref.putString("wifiUnCL", client_ap_ssid);
    sys_pref.putString("wifiPwCL", client_ap_pw);
    //sys_pref.end();
  }
  sys_pref.end();
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
void ap_init_state () {
  WiFi.mode(WIFI_AP);
  if(WiFi.softAP(esp_ap_ssid, esp_ap_pw)){
    pot_ip_ap = WiFi.softAPIP();
  }
  else {
    Serial.println("Error creating AP.");
    while (true);
  }
  Serial.print("ESP AP IP Address: ");
  Serial.println(pot_ip_ap);

  digitalWrite(ledPin, HIGH);
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
    //Serial.print("ESP AP IP Address: "); Serial.println(pot_ip_ap);
    delay(200);
  }
  digitalWrite(ledPin, LOW);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
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
            pot_ip_sta = WiFi.localIP();
            Serial.println("IP address: "); Serial.println(pot_ip_sta);
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

void time_correction () {
  sys_pref.begin("genPrefs", RW_MODE);

  // connect to ntp server, define timeinfo (temp) variable
  configTime(3600, 3600, "pool.ntp.org");
  struct tm timeinfo;

  // get time after connecting to wifi & ntp server
  gettimeofday(&tv_now, NULL);
  after_wifi_time = tv_now.tv_sec;
  Serial.print("after_wifi_time: "); Serial.println(after_wifi_time);

  // get time from ntp server, subtract time spent connecting to network/ntp server
  //    = time after wakeup from deep sleep
  getLocalTime(&timeinfo);
  wakeup_time = mktime (&timeinfo) - (after_wifi_time - before_wifi_time);
  Serial.print("wakeup_time: "); Serial.println(wakeup_time);
  if(sys_pref.isKey("prevNtpTime")) {
    prev_ntp_time = (time_t) sys_pref.getLong64("prevNtpTime");
    Serial.print("prev_ntp_time: "); Serial.println(prev_ntp_time);
    float time_corr_frac = ((float) sleep_duration_us / 1000000.0) / (wakeup_time - prev_ntp_time);
    Serial.print("time_corr_frac: "); Serial.println(time_corr_frac);
  }

  sys_pref.end();
}

bool setup_uuid() {
  sys_pref.begin("UUIDPref", RW_MODE);
  String tmp_uuid = sys_pref.getString("uuid", "");
  if (tmp_uuid.length() != 0) {
    uuid = tmp_uuid;
    sys_pref.end();
    return true;
  }
  HTTPClient http_client;
  uint16_t http_cnt = 0;
  String payload;
  DynamicJsonDocument doc_rx(512);
  String url = server_url + "genUUID";
  Serial.println("begin http_client at " + url);
  if (!http_client.begin(url)) {
    Serial.println("http.begin() failed!");
    return false;
  }
  //http_client.setURL(server_url + "genUUID");
  http_client.setFollowRedirects(HTTPC_FORCE_FOLLOW_REDIRECTS);
  Serial.printf("Free heap before GET: %d\n", ESP.getFreeHeap());
  int http_code = http_client.GET();
  Serial.print("http code for GET: "); Serial.println(http_code);
  if (http_code < 200 || http_code > 299) {
    Serial.println("GET command unsuccessful");
    sys_pref.end();
    return false;
  }
  else {
    Serial.println("GET command successful");
    payload = http_client.getString();
    http_client.end();
    deserializeJson(doc_rx, payload);

    Serial.println("Payload: " + payload);
    tmp_uuid = doc_rx["id"].as<String>();
    uuid = tmp_uuid;
    Serial.println("Our uuid is: " + uuid);
    sys_pref.putString("uuid", uuid);
    sys_pref.end();
    return true;
  }
}

bool web_data_rx () {
    // read web server
  HTTPClient http_client;
  uint16_t http_cnt = 0;
  String payload;
  DynamicJsonDocument doc_rx(512);
  Serial.println("begin http_client at 'server_url'");
  Serial.println("setting url to 'threshold_data_url'");
  http_client.begin(server_url + "plantdata/" + uuid);
  http_client.setFollowRedirects(HTTPC_FORCE_FOLLOW_REDIRECTS);
  int http_code = http_client.GET();
  //while (http_code/100 != 2 && http_cnt < 5) {
  //  http_code = http.GET();
  //}
  Serial.print("http code for GET: "); Serial.println(http_code);
  if (http_code < 200 || http_code > 299) {
    Serial.println("GET command unsuccessful");
    return false;
  }
  else {
    Serial.println("GET command successful");
    payload = http_client.getString();
    http_client.end();
    deserializeJson(doc_rx, payload);

    sampling_period = doc_rx["smpl_per"];
    watering_duration_us = doc_rx["wat_dur"];
    maximum_sunlight = doc_rx["max_sun"];
    SMV_frac = doc_rx["smv_perc"];
    dry_SMV = doc_rx["dry_smv"];
    wet_SMV = doc_rx["wet_smv"];
    return true;
  }
}

bool web_data_tx () {
  HTTPClient http_client;
  uint16_t http_cnt = 0;
  String payload;
  DynamicJsonDocument doc_tx(512);
  Serial.println("setting key-value pairs for doc_tx Json document");
  doc_tx["battery_level"] = battery_lvl;
  doc_tx["water_level_is_low"] = water_level_low;
  doc_tx["current_smv"] = current_SMV;
  doc_tx["lux_value"] = light_val_lux;
  doc_tx["total_sunlight"] = total_sunlight_cnt;
  doc_tx["dry_smv"] = dry_SMV;
  doc_tx["wet_smv"] = wet_SMV;
  Serial.println("serializing doc_tx json document into String payload");
  serializeJson(doc_tx, payload);
  Serial.println(payload);
  Serial.println("begin http_client at 'server_url'");
  http_client.begin(server_url);
  Serial.println("setting url to 'threshold_data_url'");
  http_client.setURL(server_url + "measurements/" + uuid);
  http_client.addHeader("Content-Type", "application/json");
  http_client.setFollowRedirects(HTTPC_FORCE_FOLLOW_REDIRECTS);
  int http_code = http_client.POST(payload);
  //while (http_code/100 != 2 && http_cnt < 5) {
  //  http_code = http_client.POST(payload);
  //}
  Serial.print("http code for POST: "); Serial.println(http_code);
  if (http_code < 200 || http_code > 299) {
    Serial.println("POST command unsuccessful");
    return false;
  }
  else {
    Serial.println("POST command successful");
    return true;
  }
  http_client.end();
}

void enter_deep_sleep () {
  // deep sleep
  Serial.println("Entering deep-sleep mode.");
  //Serial.println("Disabling all wakeup sources.");
  esp_sleep_disable_wakeup_source(ESP_SLEEP_WAKEUP_ALL);

  //Serial.println("Enabling GPIO and timer wakeup sources.");
  esp_sleep_enable_timer_wakeup(sleep_duration_us);
  //delay(2000);

  // connect to ntp server, define timeinfo (temp) variable
  configTime(3600, 3600, "pool.ntp.org");
  struct tm timeinfo;
  sys_pref.begin("genPrefs", RW_MODE);
  // get time before entering sleep; should be moved to just before sleep
  getLocalTime(&timeinfo);
  go_sleep_time = mktime (&timeinfo);
  sys_pref.putLong64("prevNtpTime", go_sleep_time);
  sys_pref.end();

  esp_deep_sleep_start();
}

void loop() {
  // put your main code here, to run repeatedly:

}
