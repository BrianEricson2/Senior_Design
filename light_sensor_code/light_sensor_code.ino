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

unsigned int total_sunlight_cnt = 0;
const int light_sensor_pwr_pin = 23;
int sleep_duration_us = 30000000;
float light_val_lux = 0;

// use addr 0x23 if addr pin voltage is < 0.7*Vcc
// use addr 0x5C if addr pin voltage is > 0.7*Vcc
BH1750 light_sensor(0x23);

void setup() {
  Serial.begin(9600);

  //pinMode(pump_pin,OUTPUT);
  pinMode(light_sensor_pwr_pin,OUTPUT);
  Wire.begin();
  light_sensor.begin(BH1750::UNCONFIGURED);
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
      
      // autoadjustment of measurement time based on lux value
      /*if(temp > 40000) { // direct sunlight
        light_sensor.setMTreg(32);
      }
      else if(temp > 10) { // average internal light
        light_sensor.setMTreg(70);
      }
      else { // darkness / dim light
        light_sensor.setMTreg(150);
      }*/
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

float light_sensor_check () {
  int sunlight_threshold = 10000;
  float new_light_val = get_light_val();
  unsigned int sunlight_interval = sleep_duration_us/100;
  if ((new_light_val <= sunlight_threshold && light_val_lux >= sunlight_threshold) || (new_light_val >= sunlight_threshold && light_val_lux <= sunlight_threshold)) {
    total_sunlight_cnt += sunlight_interval / 2;
  }
  /*if ((new_light_val <= sunlight_threshold) ^ (light_val_lux >= sunlight_threshold)) {
    total_sunlight_cnt += sunlight_interval / 2;
  }*/
  else if (new_light_val > sunlight_threshold) {
    total_sunlight_cnt += sunlight_interval;
  }
  return new_light_val;
}

void loop() {
  Serial.print("total_sunlight_cnt: ");
  Serial.println(total_sunlight_cnt);
  Serial.print("light sensor value: ");
  Serial.println(light_sensor_check());
  Serial.println();
  delay(5000);
}
