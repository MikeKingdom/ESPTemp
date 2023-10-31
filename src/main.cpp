//
// Board: Lolin Wemos D1 R2 & Mini
//
#include <OneWire.h>
#include <DallasTemperature.h> 
#include <ESP8266WiFi.h>
#include <WiFiClient.h>
#include <ESP8266WebServer.h>
#include <ESP8266mDNS.h>
#include <Arduino.h>

#include "config.h"

#define VERSION "1.0"

#define MOISTURE_AIR 720
#define MOISTURE_WATER 270

#define BLINK_THRESHOLD 500
#define SENSOR_INTERVAL 1000
#define BLINK_TIME 500

#define BUTTON_PIN D5
#define MOISTURE_PIN A0

// Replace with your network credentials
const char* ssid     = WIFI_SSID;
const char* password = WIFI_PASSWORD;

// Set web server port number to 80
ESP8266WebServer server(80);

// Variable to store the HTTP request
String header;

String instanceId = String(INSTANCE_ID);

//pin definitions
const int pin1wire = D3;

//ds18b20 stuff
const int ds18Resolution = 12;
int ds18count = 0;
unsigned int ds18delay;
unsigned long ds18lastreq;
OneWire oneWire(pin1wire);
DallasTemperature ds18(&oneWire);
float temperatureF[255];
float temperatureC[255];

unsigned long lastLedChange = 0;
int ledState = LOW;
int ledBlinking = 0;

unsigned long lastSensorRead = 0;
int moisture = 0;

void ds18setup() {
  ds18.begin();
  Serial.println();
  Serial.println();

  ds18count = ds18.getDeviceCount();
  for(byte i = 0; i < 255; i++) {
    temperatureC[i] = 0.0;
    temperatureF[i] = 0.0;
  }
  Serial.print("Device count");
  Serial.println(ds18count);
  //loop the devices detected and match them with sensors we care about
  for (byte i=0; i<ds18count; i++) {
    DeviceAddress taddr;
    if(ds18.getAddress(taddr, i)) {
      Serial.print("CRC device ");
      Serial.print(i);
      Serial.print(" = ");
      Serial.println(oneWire.crc8(taddr, 7));
      ds18.setResolution(taddr, ds18Resolution); //also set desired resolution
    }
  }
  ds18.setWaitForConversion(false); //this enables asyncronous calls
  ds18.requestTemperatures(); //fire off the first request
  ds18lastreq = millis();
  ds18delay = 750 / (1 << (12 - ds18Resolution)); //delay based on resolution
}

void readTemperature() {
  for(byte i=0; i<ds18count; i++) {
    temperatureC[i] = ds18.getTempCByIndex(i);
    temperatureF[i] = ds18.getTempFByIndex(i);

    Serial.print("Sensor: "); Serial.print(i);
    Serial.print(" TempC: "); Serial.print(temperatureC[i]);
    Serial.print(" TempF: "); Serial.print(temperatureF[i]);
    Serial.println();
  }
}
void toggleLed() {
  lastLedChange = millis();
  ledState = ledState == HIGH ? LOW : HIGH;
  digitalWrite(LED_BUILTIN, ledState);
}

float calcPercent(int moisture) {
  int normalized = moisture - MOISTURE_WATER;
  int normalizedMax = MOISTURE_AIR - MOISTURE_WATER;
  float percent = 100.0 - (normalized * 100.0 / (float) normalizedMax);

  return percent;
}

int readMoisture()
{
  int reading = analogRead(MOISTURE_PIN);
  Serial.print("Moisture = ");
  Serial.print(reading);
  Serial.print(" - ");
  Serial.print(calcPercent(reading));
  Serial.println("%");
  return reading;
}

void handleNotFound(){
  String message = "File Not Found\n\n";
  message += "URI: ";
  message += server.uri();
  message += "\nMethod: ";
  message += (server.method() == HTTP_GET)?"GET":"POST";
  message += "\nArguments: ";
  message += server.args();
  message += "\n";
  for (uint8_t i=0; i<server.args(); i++){
    message += " " + server.argName(i) + ": " + server.arg(i) + "\n";
  }
  server.send(404, "text/plain", message);
}


void handleMetrics() {
  int moisture = readMoisture();
  float moisturePercent = calcPercent(moisture);
  
  String m = "uptime_milliseconds_total{job=\"esp32-sensor\",instance=\"" + instanceId + ".kingdom.local\"}" + String(millis()) + "\n";
  m += "heap_free_bytes{job=\"esp32-sensor\",instance=\"" + instanceId + ".kingdom.local\"}" + String(ESP.getFreeHeap()) + "\n";
#ifdef MOISTURE
  m += "sensor_moisture_absolute{job=\"esp32-sensor\",instance=\"" + instanceId + ".kingdom.local\"}" + String(moisture) + "\n";
  m += "sensor_moisture_percent{job=\"esp32-sensor\",instance=\"" + instanceId + ".kingdom.local\"}" + String(moisturePercent) + "\n";
#endif
#ifdef TEMPERATURE
  for (int i = 0; i < ds18count; i++) {
    m += "sensor_temperature_c_" + String(i) + "{job=\"esp32-sensor\",instance=\"" + instanceId + ".kingdom.local\"}" + String(temperatureC[i]) + "\n";
    m += "sensor_temperature_f_" + String(i) + "{job=\"esp32-sensor\",instance=\"" + instanceId + ".kingdom.local\"}" + String(temperatureF[i]) + "\n";
  }
#endif

  server.send(200, "text/plain", m);
}

void handleRoot() {
  int moisture = readMoisture();
  
  String m = "<!DOCTYPE html><html>\n";
  m += "<head><meta name=\"viewport\" content=\"width=device-width, initial-scale=1\">\n";
  m += "<meta http-equiv=\"refresh\" content=\"5\">\n";
  m += "<link rel=\"icon\" href=\"data:,\">\n";
  m += "<body><h1>ESP8266 Sensors - " + instanceId + "</h1>\n";
  m += "<span class=\"version\">Version " + String(VERSION) + "</span>\n";
  #ifdef MOISTURE
  m += "<h2>Moisture is "
    + String(moisture)
    + " - "
    + String(calcPercent(moisture))
    + "%</h2>\n";
  #endif
  #ifdef TEMPERATURE
  for(int i = 0; i<ds18count; i++) {
    m += "<h2>Temperature("
      + String(i)
      + ") - "
      + String(temperatureF[i])
      + "&deg;F</h2>\n";
  }
  #endif

  m += "</body></html>\n\n";
  
  server.send(200, "text/html", m);
}

void setup() {
  Serial.begin(115200);
  pinMode(BUTTON_PIN, INPUT_PULLUP);
  pinMode(LED_BUILTIN, OUTPUT);
  
  //
  // Connect to Wi-Fi network with SSID and password
  Serial.print("Connecting to ");
  Serial.println(ssid);
  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid, password);
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  // Print local IP address and start web server
  Serial.println("");
  Serial.println("WiFi connected.");
  Serial.println("IP address: ");
  Serial.println(WiFi.localIP());

  //if (MDNS.begin(INSTANCE_ID)) {
  //  Serial.println("MDNS responder started");
  //}
  server.onNotFound(handleNotFound);
  server.on("/", handleRoot);
  server.on("/metrics", handleMetrics);
  server.begin();

#ifdef TEMPERATURE
  ds18setup();
#endif

  toggleLed();
}

void loop(){
  server.handleClient();

  unsigned long curTime = millis();

#ifdef TEMPERATURE
  if(curTime - ds18lastreq >= ds18delay) {
    readTemperature();
    ds18.requestTemperatures(); 
    ds18lastreq = millis();
  }
#endif

#ifdef MOISTURE
  if (curTime - lastSensorRead > SENSOR_INTERVAL) {
    moisture = readMoisture();
    lastSensorRead = curTime;
    if (moisture > BLINK_THRESHOLD) {
      ledBlinking = 1;
    } else {
      ledBlinking = 0;
      ledState = HIGH;
      digitalWrite(LED_BUILTIN, ledState);
    }
  }
#endif

  if (ledBlinking) {
    if (curTime - lastLedChange > BLINK_TIME) {
      toggleLed();
    }
  }
}
