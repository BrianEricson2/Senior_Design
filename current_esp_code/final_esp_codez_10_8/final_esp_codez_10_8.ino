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
//AsyncWebServer main_server(80);

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

String server_url = "http://172.20.10.2:4000/api/pot/";
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

bool threshold_initialization_done = false;

// "Valid values should be positive values less than RTC slow clock period * (2 ^ RTC timer bitwidth)."
time_t sampling_period_us = 0;
float current_SMV = -1;
float dry_SMV = 3173.3255;
float wet_SMV = 1530.2288;
float SMV_frac = 0.0;
float SMV_threshold = -1;

unsigned int watering_duration_us = 0;
float water_lvl = -1;
float light_val_lux = -1;
unsigned int total_sunlight_cnt = 0;
unsigned int maximum_sunlight = 0;
bool max_sun_rx = false;
bool battery_level_low = false;
float battery_lvl = 0;
bool water_level_low = false;

// variables to hold display values for sensor_init_page
// String SMV_perc_disp = "";
// String max_sun_disp = "";
// String sampling_period_disp = "";
// String watering_duration_disp = "";

const int light_sensor_pwr_pin = 18; //GPIO18
const int pump_ctrl_pin = 16; //GPIO16
const int sms_power_pin = 19; //GPIO19
const int i2c_sda = 21; // i2c serial data = GPIO 21
const int i2c_scl = 22; // i2c serial clock = GPIO22
const int wls_power_pin = 23; //GPIO23
const int ledPin1     = 25; // pin 10 on esp32 ic
const int ledPin2     = 26; // pin 11 on esp32 ic
const int ledPin3     = 27; // pin 12 on esp32 ic
const int sms_data_pin = 32; //GPIO32
const int battery_lvl_pin = 34; //GPIO34
const int wls_signal = 35;

// use addr 0x23 if addr pin voltage is < 0.7*Vcc
// use addr 0x5C if addr pin voltage is > 0.7*Vcc
BH1750 light_sensor(0x23);

// ADC and calibration
const float vRef = 3.3;         // ADC reference voltage (ESP32)
const float correctionFactor = 1.03; // Tune this to match real voltmeter
const float R1 = 100.0; // kΩ (top)
const float R2 = 47.0;  // kΩ (bottom)

// Flash Memory
#define RW_MODE false
#define RO_MODE true
Preferences sys_pref;

/////////////////////////////////////////////////////////
/* LED States
    LED1    LED2    LED3    State
    0       0       0       Sleep or between states
    1       0       0       sensor_check
    0       1       0       AP State
    1       1       0       web_data_rx
    0       0       1       STA State
    1       0       1       web_data_tx
    0       1       1
    1       1       1
*/

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

  <div>Follow this url after entering Wi-Fi info: </div>
  <div class="value-box" id="pot_uuid">Loading...</div>

  <script>
    async function displayUUID () {
      response = await fetch('/pot_uuid_ap');
      document.getElementById('pot_uuid').textContent = await response.text();
    }
    setInterval(displayUUID, 2500);
    displayUUID();
  </script>

</body> </html>)";

void setup() {
  Serial.begin(9600);

  init_pins();

  esp_sleep_wakeup_cause_t wakeup_cause = esp_sleep_get_wakeup_cause();
  Serial.print("wakeup cause: "); Serial.println(wakeup_cause);

  if(wakeup_cause == ESP_SLEEP_WAKEUP_TIMER) {
    Serial.println("timer wakeup");
    wifi_init();
    setup_uuid();
  }
  else {
    Serial.println("power up");
    wifi_init();
    setup_uuid();
  }


  Serial.println("attempting to rx data from web server");
  // read threshold info set by user via web server
  // Ensure that we hold until web data is received
  while (!web_data_rx()) { delay(1000); }
  Serial.println("done with rx function");

  sensor_check(); //uncomment for production / hardware testing build
  //delay(5000); // example delay for simulation; mimic delay of sensor_check function

  Serial.println("attempting to tx data to web server");
  // send sensor data to user via web server
  web_data_tx();
  Serial.println("done with tx function");

  enter_deep_sleep(); // use for production
  /*if (1) { // use this block if not in production
    Serial.println("Entering deep-sleep mode.");
    esp_sleep_enable_timer_wakeup(sampling_period_us);
    esp_deep_sleep_start();
  }*/
}

String varRepl(const String& var){
  if(var == "ssid") {
    return client_ap_ssid;
  }
  if(var == "pswd") {
    return client_ap_pw;
  }
}

void init_pins () {
  Serial.println("Initializing pin modes.");
  pinMode(ledPin1, OUTPUT); digitalWrite(ledPin1, LOW);
  pinMode(ledPin2, OUTPUT); digitalWrite(ledPin2, LOW);
  pinMode(ledPin3, OUTPUT); digitalWrite(ledPin3, LOW);
  //digitalWrite(wls_power_pin,LOW);
  pinMode(pump_ctrl_pin,OUTPUT); digitalWrite(pump_ctrl_pin,LOW);
  pinMode(sms_power_pin,OUTPUT); digitalWrite(sms_power_pin,LOW);
  pinMode(light_sensor_pwr_pin,OUTPUT); digitalWrite(light_sensor_pwr_pin,LOW);
  pinMode(wls_power_pin,OUTPUT); digitalWrite(wls_power_pin,LOW);
  pinMode(sms_data_pin,INPUT);
  pinMode(battery_lvl_pin,INPUT);
  pinMode(wls_signal,INPUT);
  Wire.begin(); //Wire.begin(I2C_SDA, I2C_SCL);
  //light_sensor.begin();
  Serial.println("Initialized pin modes.");
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
      WiFi.mode(WIFI_STA);
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
  digitalWrite(ledPin2, HIGH);
  IPAddress set_ap_ip(192,168,4,1);
  IPAddress set_gateway(192,168,4,96);
  IPAddress set_subnet(255,255,255,0); 
  WiFi.mode(WIFI_AP_STA);
  if(WiFi.softAP(esp_ap_ssid, esp_ap_pw)){
    if (WiFi.softAPConfig (set_ap_ip, set_gateway, set_subnet) ) {
      Serial.println("successfully set AP IP addresses");
    } else {
      Serial.println("could not set AP IP addresses");
    }
    pot_ip_ap = WiFi.softAPIP();
  }
  else {
    Serial.println("Error creating AP.");
    while (true);
  }
  Serial.print("ESP AP IP Address: ");
  Serial.println(pot_ip_ap);

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

  async_web_server.on("/pot_uuid_ap", HTTP_GET, [](AsyncWebServerRequest *request){
    // You could replace this with a real sensor read
    String val;
    if(!end_ap_state) { val = "Input Wi-Fi info to receive url."; }
    else if(uuid == "") { val = "Waiting for pot UUID..."; }
    else { val = server_url + uuid; }
    request->send(200, "text/plain", val);
  });

  async_web_server.begin();

  while (!end_ap_state) {
    //Serial.print("ESP AP IP Address: "); Serial.println(pot_ip_ap);
    delay(200);
  }
  digitalWrite(ledPin2, LOW);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
void station_init_state() {
  digitalWrite(ledPin3, HIGH);
  int counter = 0;
  if(end_ap_state) {
    Serial.println("Initializing ESP32 in station mode.");
    if(client_ap_ssid != "" && client_ap_pw != "") {
      WiFi.begin(client_ap_ssid, client_ap_pw);
      while(counter < 20) {
        if(WiFi.isConnected()) {
          pot_ip_sta = WiFi.localIP();
          Serial.println("STA IP address: "); Serial.println(pot_ip_sta);
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
  digitalWrite(ledPin3, LOW);
}

// POT UUID; there should only be one
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
  digitalWrite(ledPin1, HIGH);
  digitalWrite(ledPin2, HIGH);
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
    digitalWrite(ledPin1, LOW);
    digitalWrite(ledPin2, LOW);
    return false;
  }
  else {
    Serial.println("GET command successful");
    payload = http_client.getString();
    Serial.print("payload: "); Serial.println(payload);
    http_client.end();
    deserializeJson(doc_rx, payload);

    //sampling_period_us = ((time_t) doc_rx["sampling_period"] * 60 * 1000000);
    sampling_period_us = (time_t) doc_rx["sampling_period"];
    watering_duration_us = doc_rx["watering_timer_useconds"];
    maximum_sunlight = doc_rx["maximum_sunlight"];
    SMV_frac = doc_rx["smv_percentage"];
    //dry_SMV = doc_rx["dry_smv"];
    //wet_SMV = doc_rx["wet_smv"];

    //return true;
    digitalWrite(ledPin1, LOW);
    digitalWrite(ledPin2, LOW);
    Serial.print("sampling_period_us: "); Serial.println(sampling_period_us);
    Serial.print("watering_duration_us: "); Serial.println(watering_duration_us);
    Serial.print("maximum_sunlight: "); Serial.println(maximum_sunlight);
    Serial.print("SMV_frac: "); Serial.println(SMV_frac);
    return (sampling_period_us > 0 && watering_duration_us > 0 && maximum_sunlight > 0 && SMV_frac > 0.0);
  }
}

bool web_data_tx () {
  digitalWrite(ledPin1, HIGH);
  digitalWrite(ledPin3, HIGH);
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
  http_client.end();
  Serial.print("http code for POST: "); Serial.println(http_code);
  if (http_code < 200 || http_code > 299) {
    Serial.println("POST command unsuccessful");
    digitalWrite(ledPin1, LOW);
    digitalWrite(ledPin3, LOW);
    return false;
  }
  else {
    Serial.println("POST command successful");
    digitalWrite(ledPin1, LOW);
    digitalWrite(ledPin3, LOW);
    return true;
  }
}

void enter_deep_sleep () {
  // deep sleep
  Serial.println("Entering deep-sleep mode.");
  //Serial.println("Disabling all wakeup sources.");
  esp_sleep_disable_wakeup_source(ESP_SLEEP_WAKEUP_ALL);

  //Serial.println("Enabling GPIO and timer wakeup sources.");
  esp_sleep_enable_timer_wakeup(sampling_period_us);
  //esp_sleep_enable_ext0_wakeup(RECONFIG_BUTTON, 0);
  delay(450);

  esp_deep_sleep_start();
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// SMV = soil moisture value
float get_SMV() {
  //Serial.print("starting get_SMV function");
  digitalWrite(sms_power_pin,HIGH); //Serial.println("513");
  delay(2500); //Serial.println("514");
  float SMV = 0; int N=10; //Serial.println("515");
  float SMV_temp = 0; //Serial.println("516");
  for (int i = 0; i < N; i++) {
    SMV_temp = analogRead(sms_data_pin); //Serial.println("518");
    SMV += SMV_temp; //Serial.println("519");
    if (wet_SMV > SMV_temp){
      wet_SMV = SMV_temp; //Serial.println("521");
    }
    if (dry_SMV < SMV_temp){
      dry_SMV = SMV_temp; //Serial.println("524");
    }
    delay(500);
  }

  digitalWrite(sms_power_pin,LOW); //Serial.println("529");
  current_SMV = SMV / N; //Serial.println("530");
  //Serial.print("SMV Value: "); Serial.println(SMV);
  return current_SMV;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
void water_plant() {
  Serial.println("Enabling power to plant");
  unsigned long current_time = micros();
  digitalWrite(pump_ctrl_pin, HIGH);
  digitalWrite(ledPin3, HIGH);

  // (max limit - buffer) - current_time
  //buffer needed because 
  unsigned long dist_from_limit = (4294967295 - 100000) - current_time;
  if(dist_from_limit > watering_duration_us) { //if watering duration will not exceed limit
    while((micros() - current_time) < watering_duration_us) {}
  }
  else{ // watering duration will exceed limit
    unsigned long stop_counter = watering_duration_us - dist_from_limit;
    while(micros() < stop_counter) {}
  }
  digitalWrite(pump_ctrl_pin, LOW);
  digitalWrite(ledPin3, LOW);
  
  Serial.println("Disabling power to plant");
  Serial.print("start time: "); Serial.println(current_time);
  Serial.print("finish time: "); Serial.println(micros());
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
float get_light_val () {
  // supply 3.3V to light sensor Vcc
  digitalWrite(light_sensor_pwr_pin,HIGH);
  float light_val = 0; int N=25;
  float temp = 0; int fail_cnt = 0;
  light_sensor.begin();

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
  // disable 3.3V power to light sensor
  digitalWrite(light_sensor_pwr_pin,LOW);
  return light_val;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
float read_light_sensor () {
  int sunlight_threshold = 10000;
  float new_light_val = get_light_val();
  unsigned int sunlight_interval = sampling_period_us/1000000;
  /*if ((new_light_val <= sunlight_threshold && light_val_lux >= sunlight_threshold) || (new_light_val >= sunlight_threshold && light_val_lux <= sunlight_threshold)) {
    total_sunlight_cnt += sunlight_interval / 2;
  }
  else if (new_light_val > sunlight_threshold && light_val_lux > sunlight_threshold) {
    total_sunlight_cnt += sunlight_interval;
  }
  light_val_lux = new_light_val;
  return light_val_lux;*/
  if (new_light_val > sunlight_threshold) {
    total_sunlight_cnt += sunlight_interval;
  }
  light_val_lux = new_light_val;
  return light_val_lux;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
void read_battery_level() {
  int raw_sum = 0; int N = 40;
  for (int i = 0; i < N; i++) {
    raw_sum = raw_sum + analogRead(battery_lvl_pin);
    delay(50);
  }
  float raw = raw_sum / N;
  Serial.print("Raw value at battery level pin: "); Serial.println(raw);
  float v_adc = raw * vRef / 4095.0;
  battery_lvl = v_adc * ((R1 + R2) / R2) * correctionFactor;
  Serial.print("Adjusted battery level value: "); Serial.print(battery_lvl); Serial.println("V");
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
void read_water_level() {
  //Serial.println("Reading Water Level");
  /*
  //digitalWrite(wls_power_pin,HIGH);
  //delay(3000);
  int N = 9; int temp = 0;
  for(int i = 0; i < N; i++) {
    if(digitalRead(wls_power_pin) == HIGH) {
      temp++;
    }
    else {
      temp--;
    }
    delay(500);
  }
  water_level_low = (temp < 0);
  //digitalWrite(wls_power_pin,LOW);
  Serial.print("Water Level is Low: "); Serial.println(water_level_low);
  */
  digitalWrite(wls_power_pin, HIGH);
  delay(200);
  water_level_low = !digitalRead(wls_signal);
  //Serial.print("wls signal = "); Serial.println(wls_signal);
  Serial.print("water_level_low = "); Serial.println(water_level_low);
  digitalWrite(wls_power_pin, LOW);
}

void sensor_check () {
  digitalWrite(ledPin1, HIGH);
  read_battery_level();
      if(battery_lvl > 6.25){
      //if(true) {
        battery_level_low = false;
        read_water_level();
        get_SMV();
        Serial.print("measured SMV: "); Serial.println(current_SMV);
        SMV_threshold = dry_SMV - (dry_SMV - wet_SMV)*SMV_frac;
                        //3173.3255 - (3173.3255 - 1530.2288)*SMV_frac
                        // = 3173.3255 - 1643.0967*SMV_frac
        if(!water_level_low) {
          Serial.println("Water level not low");
          if(current_SMV > SMV_threshold) {
            water_plant();
          }
        }
        else {
          // send message to server/client to refill water tank
          Serial.println("Water level low");
        }
        //Serial.println("////////////////////////////////////////////////////////////////////////////");
        read_light_sensor();
        if(total_sunlight_cnt >= maximum_sunlight) {
          // send message to server/client to cover plant
          max_sun_rx = true;
          Serial.println("Cover plant");
        }
        else {
          max_sun_rx = false;
        }
        //Serial.println("////////////////////////////////////////////////////////////////////////////");
      }
      else {
        // send message to server/client that battery is low
        battery_level_low = true;
        Serial.println("Battery low");
      }
  digitalWrite(ledPin1, LOW);
}

void loop() {
  // put your main code here, to run repeatedly:

}