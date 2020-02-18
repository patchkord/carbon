/*
  To upload through terminal you can use: curl -F "image=@firmware.bin" esp8266-webupdate.local/update
*/

#define PIN_CLOCK   D1
#define PIN_DATA    D2

#define IMPLEMENTATION  FIFO
#define MQTT_VERSION    MQTT_VERSION_3_1_1
#define CONFIG_JSON     "/config.json"

#include "cppQueue.h"
#include "co2decoder.h"

#include <FS.h>
#include <ESP8266WiFi.h>
#include <ESP8266mDNS.h>
#include <ESP8266WebServer.h>


#include <WiFiUdp.h>
#include <ArduinoOTA.h>
#include <PubSubClient.h>
#include <ArduinoJson.h> // https://github.com/bblanchon/ArduinoJson

// Globals used for business logic only
String device_id = "";

// Wifi Params (overridden with values in data/config.json):
String wifi_ssid = "*****";
String wifi_password = "*****";

// MQTT: server host, port, username and password
int    mqtt_server_port =  1883;
String mqtt_server_host = "host";
String mqtt_user = "";
String mqtt_password = "";

// MQTT: topics
const char* MQTT_ROOT_TOPIC = "/devices/MT8060/";
const char* MQTT_HUM_SUBTOPIC = "/humidity";
const char* MQTT_TEMP_SUBTOPIC = "/temperature";
const char* MQTT_CO2_SUBTOPIC = "/co2";


WiFiClient wifi_client;
PubSubClient mqtt_client(wifi_client);

ESP8266WebServer web_server(80);

Queue queue(sizeof(co2message), 5, IMPLEMENTATION, true);


static void delay_fun(int time, void (*fun)(void))
{
  unsigned long timing = millis();

  while (millis() - timing < time)
  {
    fun();
    yield();
  }
}

/* Business logic */
static bool fetch_co2mesage(co2message* message)
{
  bool result = false;
  noInterrupts();
  result = queue.pop(message);
  interrupts();
  return result;
}

static bool push_co2mesage(co2message* message)
{
  noInterrupts();

  if (!queue.isFull())
    queue.push(message);

  interrupts();
}

static String esp_uid()
{
  char esp_id[16];

  // get ESP id
  sprintf(esp_id, "%08X", ESP.getChipId());
  return String(esp_id);
}

static String host_name() {
  byte mac[6];
  char mac_addr[32];
  WiFi.macAddress(mac);
  snprintf(mac_addr, sizeof(mac_addr), "%02x-%02x-%02x-%02x-%02x-%02x", mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);

  return "esp-" + String(mac_addr);
}

bool publish_data(const char* subtopic, String &value)
{
  bool result = false;
  String uid = (device_id == "") ? host_name() : device_id;
  String topic = MQTT_ROOT_TOPIC + uid + subtopic;

  if (mqtt_client.connected()) {
    Serial.print("Publishing to ");
    Serial.print(topic);
    Serial.print("...");
    result = mqtt_client.publish(topic.c_str(), value.c_str(), false);
    Serial.println(result ? "OK" : "FAIL");
  }
  return result;
}

static void OtaSetup()
{
  // default port number
  // ArduinoOTA.setPort(8266);

  ArduinoOTA.setHostname(host_name().c_str());

  // authentication
  // no authentication by default
  // ArduinoOTA.setPassword((const char*)"123");

  ArduinoOTA.onStart([]() {
    Serial.println("OTA Start");
    detachInterrupt(digitalPinToInterrupt(PIN_CLOCK));
  });

  ArduinoOTA.onEnd([]() {
    Serial.println("\nOTA End");
    attachInterrupt(digitalPinToInterrupt(PIN_CLOCK), interrupt, FALLING);
  });

  ArduinoOTA.onProgress([](unsigned int progress, unsigned int total) {
    Serial.printf("Progress: %u%%\r", (progress / (total / 100)));
  });

  ArduinoOTA.onError([](ota_error_t error) {
    Serial.printf("Error[%u]: ", error);

    if (error == OTA_AUTH_ERROR)
      Serial.println("Auth Failed");
    else if (error == OTA_BEGIN_ERROR)
      Serial.println("Begin Failed");
    else if (error == OTA_CONNECT_ERROR)
      Serial.println("Connect Failed");
    else if (error == OTA_RECEIVE_ERROR)
      Serial.println("Receive Failed");
    else if (error == OTA_END_ERROR)
      Serial.println("End Failed");
  });

  ArduinoOTA.begin();
}

void setup() {
  // initialize serial port
  Serial.begin(115200);

  Serial.println("");
  Serial.println("CO2 ESP reader");
  Serial.println("Booting");

  ConfigInit();
  WifiSetup();
  OtaSetup();

  WebServerSetup();
  MDNS.begin(host_name().c_str());
  MDNS.addService("http", "tcp", 80);
  Serial.printf("Ready! Open http://%s.local in your browser\n", host_name().c_str());

  // initialize pins
  pinMode(PIN_CLOCK, INPUT);
  pinMode(PIN_DATA, INPUT);
  attachInterrupt(digitalPinToInterrupt(PIN_CLOCK), interrupt, FALLING);
}

void NetLoop() {
  // verify network connection and reboot on failure
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("Restarting ESP...");
    ESP.restart();
  }

  MDNS.update();
  ArduinoOTA.handle();
  web_server.handleClient();
}

void loop() {
  uint32_t millis_start = millis();

  NetLoop();

  while (!mqtt_client.connected()) {
    mqtt_client.setServer(mqtt_server_host.c_str(), mqtt_server_port);
    Serial.print("Connecting MQTT...");

    if (mqtt_client.connect(esp_uid().c_str() , mqtt_user.c_str(), mqtt_password.c_str()))
      Serial.println("connected");
    else {
      Serial.print("failed with state ");
      Serial.println(mqtt_client.state());
      delay_fun(10000, []() {
        NetLoop();
      });
    }

    // restart ESP if we cannot connect for too long
    if ((millis () - millis_start) > 2 * 60000) {
      Serial.println ("Cannot connect to MQTT, restarting...");
      ESP.restart ();
    }
  }

  co2message message;
  if (fetch_co2mesage(&message) && message.checksum_is_valid)
  {
    String value;
    switch (message.type) {
      case HUMIDITY:
        value = String((double)message.value / 100, 2);
        publish_data(MQTT_HUM_SUBTOPIC, value);
        break;

      case TEMPERATURE:
        value = String((double)message.value / 16 - 273.15, 1);
        publish_data(MQTT_TEMP_SUBTOPIC, value);
        break;

      case CO2_PPM:
        value = String(message.value, DEC);
        publish_data(MQTT_CO2_SUBTOPIC, value);
        break;

      default:
        break;
    }
  }
}

ICACHE_RAM_ATTR void interrupt() {
  // falling edge, sample data
  bool data = (digitalRead(PIN_DATA) == HIGH);

  // feed it to the state machine
  if (co2process(millis(), data)) {
    co2message message;
    co2msg(&message);
    push_co2mesage(&message);
  }
}

void WifiSetup() {
  WiFi.mode(WIFI_STA);
  WiFi.begin(wifi_ssid.c_str(), wifi_password.c_str());

  while (WiFi.waitForConnectResult() != WL_CONNECTED) {
    Serial.println("Connection Failed! Rebooting...");
    delay(5000);
    ESP.restart();
  }

  Serial.print("IP address: ");
  Serial.println(WiFi.localIP());
  Serial.print("MAC: ");
  Serial.println(WiFi.macAddress());
}

File ConfigGetFile(String fileName) {
  File textFile;

  if (SPIFFS.exists(fileName)) {
    textFile = SPIFFS.open(fileName, "r");
  }
  return textFile;
}

void ConfigInit() {
  Serial.println("Starting SPIFFS");

  if (SPIFFS.begin()) {
    Serial.println("Mounted file system");

    // open json config file
    File jsonFile = ConfigGetFile(CONFIG_JSON);
    if (jsonFile) {
      StaticJsonDocument<1024> doc;
      DeserializationError error = deserializeJson(doc, jsonFile);
      if (error)
        Serial.println("Failed to parse json config");
      else
      {
        //serializeJson(doc, Serial);
        JsonObject object = doc.as<JsonObject>();

        wifi_ssid = object["wifi_ssid"].as<String>();
        wifi_password = object["wifi_password"].as<String>();

        mqtt_server_port = object["mqtt_server_port"].as<int>();
        mqtt_server_host = object["mqtt_server_host"].as<String>();

        mqtt_user = object["mqtt_user"].as<String>();
        mqtt_password = object["mqtt_password"].as<String>();

        device_id = object["device_id"].as<String>();
      }

      jsonFile.close();
    } else {
      Serial.println("Failed to open json config");
    }
  }
}

void handle_not_found() {
  String message = "Not Found\n\n";

  message += "URI: ";
  message += web_server.uri();
  message += "\nMethod: ";
  message += (web_server.method() == HTTP_GET) ? "GET" : "POST";
  message += "\nArguments: ";
  message += web_server.args();
  message += "\n";

  for (uint8_t i = 0; i < web_server.args(); i++) {
    message += " " + web_server.argName(i) + ": " + web_server.arg(i) + "\n";
  }

  web_server.send(404, "text/plain", message);
}

void handle_root() {
  const char* serverIndex = "<form method='POST' action='/update' enctype='multipart/form-data'><input type='file' name='update'><input type='submit' value='Update'></form>";

  web_server.sendHeader("Connection", "close");
  web_server.send(200, "text/html", serverIndex);
}

void fill_config_doc(JsonObject object)
{
  object["device_id"] = device_id;

  object["wifi_ssid"] = wifi_ssid;
  object["wifi_password"] = wifi_password;

  object["mqtt_server_port"] = mqtt_server_port;
  object["mqtt_server_host"] = mqtt_server_host;

  object["mqtt_user"] = mqtt_user;
  object["mqtt_password"] = mqtt_password;
}

void handle_config_read() {
  String output;

  StaticJsonDocument<1024> doc;
  JsonObject object = doc.to<JsonObject>();
  fill_config_doc(doc.to<JsonObject>());

  serializeJson(doc, output);
  web_server.sendHeader("Connection", "close");
  web_server.send(200, "text/json", output);
}

bool apply_param(String& string, JsonVariant value)
{
  if (!value.isNull()) {
    string = value.as<String>();
    return false;
  }
  return false;
}

void handle_config_apply() {
  web_server.sendHeader("Connection", "close");

  StaticJsonDocument<1024> doc;
  DeserializationError error = deserializeJson(doc, web_server.arg("plain"));
  if (error)
  {
    Serial.println(F("Failed to parse json request"));
    web_server.send(200, "text/json", "{\"status\":\"error\", \"error\": \"wrong params\"}" );
  }
  else
  {
    JsonObject object = doc.as<JsonObject>();
    JsonVariant port_value = object["mqtt_server_port"];

    if (!port_value.isNull())
      mqtt_server_port = port_value.as<int>();

    apply_param(mqtt_server_host, object["mqtt_server_host"]);
    apply_param(mqtt_user, object["mqtt_user"]);
    apply_param(mqtt_password, object["mqtt_password"]);
    apply_param(device_id, object["device_id"]);

    if (mqtt_client.connected()) {
      Serial.println(F("Connection MQTT is already exists, disconnecting"));
      mqtt_client.disconnect();
    }

    web_server.send(200, "text/json", "{\"status\":\"ok\"}" );
  }
}

void handle_config_commit() {
  web_server.sendHeader("Connection", "close");

  // open config file
  File jsonFile = SPIFFS.open(CONFIG_JSON, "w");
  if (jsonFile) {
    StaticJsonDocument<1024> doc;
    JsonObject object = doc.to<JsonObject>();
    fill_config_doc(doc.to<JsonObject>());

    if (serializeJson(doc, jsonFile)) {
      web_server.send(200, "text/json", "{\"status\":\"ok\"}" );
    }
    else {
      Serial.println(F("Failed to write to config file"));
      web_server.send(200, "text/json", "{\"status\":\"error\", \"error\":\"failed to write config file\"}" );
    }
    jsonFile.close();
  }
  else {
    web_server.send(200, "text/json", "{\"status\":\"error\", \"error\":\"failed to open config file\"}" );
  }
}

// Web server functionality:
void WebServerSetup() {
  web_server.on("/", HTTP_GET, handle_root);

  web_server.on("/update", HTTP_POST, []() {
    web_server.sendHeader("Connection", "close");
    web_server.send(200, "text/plain", (Update.hasError()) ? "FAIL" : "OK");
    ESP.restart();
  }, []() {
    HTTPUpload& upload = web_server.upload();

    if (upload.status == UPLOAD_FILE_START) {
      detachInterrupt(digitalPinToInterrupt(PIN_CLOCK));
      Serial.setDebugOutput(true);
      WiFiUDP::stopAll();
      Serial.printf("Update: %s\n", upload.filename.c_str());
      uint32_t maxSketchSpace = (ESP.getFreeSketchSpace() - 0x1000) & 0xFFFFF000;
      if (!Update.begin(maxSketchSpace)) { //start with max available size
        Update.printError(Serial);
      }
    } else if (upload.status == UPLOAD_FILE_WRITE) {
      if (Update.write(upload.buf, upload.currentSize) != upload.currentSize) {
        Update.printError(Serial);
      }
    } else if (upload.status == UPLOAD_FILE_END) {
      if (Update.end(true)) { //true to set the size to the current progress
        Serial.printf("Update Success: %u\nRebooting...\n", upload.totalSize);
      } else {
        Update.printError(Serial);
      }
      Serial.setDebugOutput(false);
      attachInterrupt(digitalPinToInterrupt(PIN_CLOCK), interrupt, FALLING);
    }
    yield();
  });

  web_server.on("/config/read", HTTP_GET,  handle_config_read);
  web_server.on("/config/apply", HTTP_POST, handle_config_apply);
  web_server.on("/config/commit", HTTP_POST, handle_config_commit);

  web_server.onNotFound(handle_not_found);

  Serial.println("Starting web server");
  web_server.begin();
}
