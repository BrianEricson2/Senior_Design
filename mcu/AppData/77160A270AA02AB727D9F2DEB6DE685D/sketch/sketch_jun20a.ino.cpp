#line 1 "C:\\Users\\minec\\Downloads\\sketch_jun20a\\sketch_jun20a.ino"
#include <Arduino.h>
#include <WiFi.h>
#include <AsyncTCP.h>
#include <ESPAsyncWebServer.h>
#include <WiFiAP.h>

const char* esp_ap_ssid = "vDIg4qYsY2HZ";
const char* esp_ap_pw = "I6,mqr!Cs4ui";

String client_ap_ssid = "";
String client_ap_pw = "";
IPAddress ap_ip_addr = "";

bool end_ap_stage = false;
bool end_station_init_stage = false;

char front_page[] = R"(
<!DOCTYPE HTML> <html>
<head> <title>front_page</title> </head>
<body>
<form action=/get target="hidden-form">
<label for="ssid">SSID:</label>
<input type="text" name="ssid"><br>
Current ssid = %ssid%<br>
<label for="pswd">Password:</label>
<input type="text" name="pswd"><br>
Current password = %pswd%<br>
<input type="submit" value="Enter">
</form>
</body> </html>)";

AsyncWebServer async_web_server(80);

#line 34 "C:\\Users\\minec\\Downloads\\sketch_jun20a\\sketch_jun20a.ino"
void setup();
#line 42 "C:\\Users\\minec\\Downloads\\sketch_jun20a\\sketch_jun20a.ino"
String varRepl(const String& var);
#line 51 "C:\\Users\\minec\\Downloads\\sketch_jun20a\\sketch_jun20a.ino"
void ap_init_stage();
#line 82 "C:\\Users\\minec\\Downloads\\sketch_jun20a\\sketch_jun20a.ino"
void station_init_stage();
#line 109 "C:\\Users\\minec\\Downloads\\sketch_jun20a\\sketch_jun20a.ino"
void loop();
#line 34 "C:\\Users\\minec\\Downloads\\sketch_jun20a\\sketch_jun20a.ino"
void setup() {
  Serial.begin(9600);
  //ap_init_stage();
  client_ap_ssid = "Rialto_Resident";
  client_ap_pw = "rock144ancient";
  end_ap_stage = true;
}

String varRepl(const String& var){
  if(var == "ssid"){
    return client_ap_ssid;
  }
  if(var == "pswd"){
    return client_ap_pw;
  }
}

void ap_init_stage () {
  WiFi.mode(WIFI_AP);
  if(WiFi.softAP(esp_ap_ssid, esp_ap_pw)){
    ap_ip_addr = WiFi.softAPIP();
    Serial.print("AP IP Address: "); Serial.println(ap_ip_addr);
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
    end_ap_stage = (client_ap_ssid != "" && client_ap_pw != "");
    request->send(200,"text/text","OK");
  });
  async_web_server.begin();
}

void station_init_stage() {
  int counter = 0;
  if(end_ap_stage) {
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
            end_station_init_stage = true;
            return;
          }
          delay(500); Serial.print(" ... "); counter++;
        }
        Serial.println("Error connecting to WiFi. Please reenter WiFi info.");
        end_ap_stage = false;
        return;
      }
    }
  }
}

void loop() {
  if(end_ap_stage && (end_station_init_stage == false)) {
    station_init_stage();
  }
  /*Serial.println(ap_ip_addr);
  Serial.println(esp_ap_ssid);
  Serial.println(esp_ap_pw);
  Serial.println(client_ap_ssid);
  Serial.println(client_ap_pw);
  Serial.println(end_ap_stage);
  Serial.println(end_station_init_stage);*/
  delay(5000);
}

