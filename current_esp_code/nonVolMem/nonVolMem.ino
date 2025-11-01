#include <Preferences.h>
#include <nvs_flash.h>
#include <Arduino.h>
#include <WiFi.h>
#include <AsyncTCP.h>
#include <ESPAsyncWebServer.h>
#include <WiFiAP.h>

#define RW_MODE false
#define RO_MODE true
Preferences sys_pref;

// async_web_server used when ESP32 is in AP mode, creating its own WiFi network
AsyncWebServer async_web_server(80);

// main_server used when ESP32 is in station mode, connected to user's WiFi network
AsyncWebServer main_server(80);

const char* esp_ap_ssid = "vDIg4qYsY2HZ";
const char* esp_ap_pw = "I6,mqr!Cs4ui";
IPAddress pot_ip_ap = "";

String client_ap_ssid = "";
String client_ap_pw = "";
IPAddress pot_ip_sta = "";
bool end_ap_state = false;
bool end_station_init_state = false;

int serial_output = 255;

String varRepl(const String& var){
  if(var == "ssid") {
    return client_ap_ssid;
  }
  if(var == "pswd") {
    return client_ap_pw;
  }
}

void setup() {
  Serial.begin(9600);

  // use to reset flash
  if (1) {
    esp_err_t err;
    err = nvs_flash_erase();
    Serial.println(err);
    err = nvs_flash_init();
    Serial.println(err);
  }
  else {
    sys_pref.begin("genPrefs", RO_MODE);
    bool wifi_info_exist = sys_pref.isKey("wifiUnCL") && sys_pref.isKey("wifiPwCL");
    sys_pref.end();
    if(wifi_info_exist) {
      sys_pref.begin("genPrefs", RO_MODE);
      end_ap_state = true;
      //read from flash & store in local variable
      client_ap_ssid = sys_pref.getString("wifiUnCL");
      client_ap_pw = sys_pref.getString("wifiPwCL");
      sys_pref.end();
      //try to connect to client wifi
      // if data in flash is incorrect, end_ap_state is set false
      // if data in flash is correct, end_station_init_state becomes true
      station_init_state();
      delay(500);
    }

    while (!end_ap_state || !end_station_init_state) {
      if(!end_ap_state) {
        ap_init_state();
        delay(500);
      }
      else if(!end_station_init_state) {
        async_web_server.end();
        station_init_state();
        sys_pref.begin("genPrefs", RW_MODE);
        sys_pref.putString("wifiUnCL", client_ap_ssid);
        sys_pref.putString("wifiPwCL", client_ap_pw);
        sys_pref.end();
        delay(500);
      }
    }
  }
}

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
  if(serial_output & 1 != 0) {
    Serial.print("ESP AP IP Address: ");
    Serial.println(pot_ip_ap);
    serial_output = serial_output & 254;
  }

  //digitalWrite(ledPin, HIGH);
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
  //digitalWrite(ledPin, LOW);
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

void loop() {
  Serial.println("running main loop!!");
  delay(10000);
}
