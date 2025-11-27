#include <WiFi.h>
#include <ArduinoOSCWiFi.h>
#include "SSD1306Wire.h"
// #include "OLEDDisplayUi.h"
#include "DHT20.h"
#include <Adafruit_AHTX0.h>
#include <EncoderButton.h>

#include "FS.h"
#include <LittleFS.h>
#include <ArduinoJson.h>
#include <TimeLib.h>


#define XIAO_SDA D4
#define XIAO_SCL D5
#define GPIO_AVAILABLE_N 4
#define RELAY_OUT D6
#define SERVO_OUT D7
#define TRIG_OUT_1 D8
#define TRIG_OUT_2 D9
#define TRIG_OUT_3 D10

DHT20 DHT;
//          +--------------+
//  VDD ----| 1            |
//  SDA ----| 2    DHT20   |
//  GND ----| 3            |
//  SCL ----| 4            |
//          +--------------+

Adafruit_AHTX0 AHT;
//          +----------------+
//  VDD ----| RED            |
//  SDA ----| YELLOW AM2301B |
//  GND ----| BLACK          |
//  SCL ----| WHITE          |
//          +----------------+

SSD1306Wire display(0x3c, XIAO_SDA, XIAO_SCL);  // ADDRESS, SDA, SCL
// OLEDDisplayUi ui( &display );

EncoderButton ecbt(D1, D2, D0);

uint32_t lastPing, lastRead;
uint8_t mode = 0;

volatile uint8_t gpio_out_1 = 0;
uint8_t gpio_out_1_target = 1;
float gpio_out_1_range[] = {20.0, 30.0};
int8_t gpio_out_1_edge = 0;

volatile uint8_t gpio_out_2 = 0;
uint8_t gpio_out_2_target = 2;
float gpio_out_2_range[] = {60.0, 80.0};
int8_t gpio_out_2_edge = 0;

volatile uint8_t gpio_out_3 = 0;
uint8_t gpio_out_3_target = 0;
float gpio_out_3_range[] = {0.0, 0.0};
int8_t gpio_out_3_edge = 0;

// uint8_t relay_out = 0;
volatile uint8_t relay_out = 0;
uint8_t relay_target = 0;
int relay_test_interval_hours = 2;
uint8_t gpio_in_1 = 0;

String labels[] = { "NONE", "TEMPERATURE", "HUMIDITY" };
String labels_relay[] = { "REMOTE", "INTERNAL", "INTERNAL_ONLY" };

volatile bool connected, dht20_available, am2301_available = false;

float humid, temp;

// --
String ssid = "SSID";
String pw = "PASSWORD";
String osc_dest = "192.168.100.20";
String osc_tag_self = "/chamber_1";
//--

void onEncoder(EncoderButton& eb) {
  // Serial.println(eb.increment());
  // Serial.println(eb.position());

  if (eb.position() > GPIO_AVAILABLE_N + 1) {
    eb.resetPosition(0);
  } else if (eb.position() < 0) {
    eb.resetPosition(GPIO_AVAILABLE_N + 1);
  }
}

void onBtnClick(EncoderButton& eb) {
  // Serial.println(eb.clickCount());

  if (eb.clickCount() == 1) { // -- single click
    if (eb.position() == 1) {
      gpio_out_1 = (gpio_out_1 + 1) % 2;
    } else if (eb.position() == 2) {
      gpio_out_2 = (gpio_out_2 + 1) % 2;
    } else if (eb.position() == 3) {
      gpio_out_3 = (gpio_out_3 + 1) % 2;
    } else {
      relay_out = (relay_out + 1) % 2;
    }
  } else if (eb.clickCount() >= 2) { // -- double click
    if (eb.position() == 1) {
      gpio_out_1_target = (gpio_out_1_target + 1) % 3;
    } else if (eb.position() == 2) {
      gpio_out_2_target = (gpio_out_2_target + 1) % 3;
    } else if (eb.position() == 3) {
      gpio_out_3_target = (gpio_out_3_target + 1) % 3;
    } else if (eb.position() == 4) {
      relay_target = (relay_target + 1) % 3;
    }
  }
}

void onOscReceived(const OscMessage& m) {
    // Serial.print(m.remoteIP());
    // Serial.print(" ");
    // Serial.print(m.remotePort());
    // Serial.print(" ");
    // Serial.print(m.size());
    // Serial.print(" ");
    // Serial.print(m.address());
    // Serial.print(" ");
    // Serial.print(m.arg<int>(0));
    // Serial.print(" ");
    // Serial.print(m.arg<float>(1));
    // Serial.print(" ");
    // Serial.print(m.arg<String>(2));
    // Serial.println();

  if (m.address() == "/cmd/pump" && m.size() > 0) {
    if (relay_target < 2) relay_out = m.arg<int>(0);
  }

  if (m.address() == "/sync" && m.size() > 2) {
    setTime(m.arg<int>(0), m.arg<int>(1), m.arg<int>(2), 0, 0, 0);
  }
}

void writeFile(String filename, String message){
  File file = LittleFS.open(filename, "w");
  if(!file){
    Serial.println("writeFile -> failed to open file for writing");
    return;
  }
  if(file.print(message)){
    Serial.println("File written");
  } else {
    Serial.println("Write failed");
  }
  file.close();
}

String readFile(String filename){
  File file = LittleFS.open(filename);
  if(!file){
    Serial.println("Failed to open file for reading");
    return "";
  }

  String fileText = "";
  while(file.available()){
    fileText = file.readString();
  }
  file.close();
  return fileText;
}


bool readConfig(String config_filename = "/config.json") {
  String file_content = readFile(config_filename);

  int config_file_size = file_content.length();
  Serial.println("Config file size: " + String(config_file_size));

  if(config_file_size > 1024) {
    Serial.println("Config file too large");
    return false;
  } else {
    Serial.println(file_content);
  }

  StaticJsonDocument<1024> doc;
  auto error = deserializeJson(doc, file_content);
  if ( error ) {
    Serial.println("Error interpreting config file");
    return false;
  }

  // TODO:
  const String _ssid = doc["ssid"];
  const String _pw = doc["pw"];
  const String _osc_dest = doc["osc_dest"];
  const String _osc_tag_self = doc["osc_tag_self"];
  const uint8_t _go1t = doc["gpio_out_1_target"];
  const uint8_t _go2t = doc["gpio_out_2_target"];
  const uint8_t _go3t = doc["gpio_out_3_target"];

  ssid = _ssid;
  pw = _pw;
  osc_dest = _osc_dest;
  osc_tag_self = _osc_tag_self;

  gpio_out_1_target = _go1t;
  gpio_out_1_range[0] = doc["gpio_out_1_range"][0];
  gpio_out_1_range[1] = doc["gpio_out_1_range"][1];
  gpio_out_2_target = _go2t;
  gpio_out_2_range[0] = doc["gpio_out_2_range"][0];
  gpio_out_2_range[1] = doc["gpio_out_2_range"][1];
  gpio_out_3_target = _go3t;
  gpio_out_3_range[0] = doc["gpio_out_3_range"][0];
  gpio_out_3_range[1] = doc["gpio_out_3_range"][1];
  // --

  return true;
}

bool saveConfig(String config_filename = "/config.json") {
  // StaticJsonDocument<1024> doc;

  // doc["ssid"] = ssid;
  // doc["pw"] = pw;
  // doc["osc_dest"] = osc_dest;

  // String tmp = "";
  // serializeJson(doc, tmp);
  // writeFile(config_filename, tmp);

  return true;
}

void onGotIp(WiFiEvent_t event, WiFiEventInfo_t info) {
  Serial.println("WiFi connected");
  Serial.println(IPAddress(info.got_ip.ip_info.ip.addr));

  connected = true;
}

void onWifiDisconnected(WiFiEvent_t event, WiFiEventInfo_t info) {
  connected = WiFi.isConnected();;

  if (!connected) {
    Serial.println("WiFi disconnected");
    WiFi.disconnect(true);
    WiFi.begin(ssid, pw);
  }
}

void setup()
{
  pinMode(D3, INPUT_PULLUP);
  pinMode(RELAY_OUT, OUTPUT);
  // pinMode(SERVO_OUT, OUTPUT);
  pinMode(TRIG_OUT_1, OUTPUT);
  pinMode(TRIG_OUT_2, OUTPUT);
  pinMode(TRIG_OUT_3, OUTPUT);

  digitalWrite(RELAY_OUT, 0);
  // digitalWrite(SERVO_OUT, 0);
  digitalWrite(TRIG_OUT_1, 0);
  digitalWrite(TRIG_OUT_2, 0);
  digitalWrite(TRIG_OUT_3, 0);

  Serial.begin(57600);

  Serial.print("DHT20 LIBRARY VERSION: ");
  Serial.println(DHT20_LIB_VERSION);
  Serial.println();

  if (!LittleFS.begin(false)) {
    Serial.println("LITTLEFS Mount failed");
    Serial.println("Did not find filesystem; starting format");

    if (!LittleFS.begin(true)) {
      Serial.println("LITTLEFS mount failed");
      Serial.println("Formatting not possible");
      return;
    }
  } else {
    if(readConfig() == false) {
      Serial.println("setup -> Could not read Config file -> initializing new file");
      if (saveConfig()) {
        Serial.println("setup -> Config file saved");
      }
    }
  }

  // Wire.begin();
  Wire.begin(XIAO_SDA, XIAO_SCL);

  dht20_available = DHT.begin();

  if (!dht20_available) am2301_available = AHT.begin();

  delay(1000);

  // ui.setTargetFPS(30);
  // ui.init();

  display.init();
  display.flipScreenVertically();
  display.setFont(ArialMT_Plain_10);

  ecbt.setEncoderHandler(onEncoder);
  ecbt.setClickHandler(onBtnClick);

  WiFi.onEvent(onWifiDisconnected, WiFiEvent_t::ARDUINO_EVENT_WIFI_STA_DISCONNECTED);
  WiFi.onEvent(onGotIp, WiFiEvent_t::ARDUINO_EVENT_WIFI_STA_GOT_IP);
  // WiFi.setAutoReconnect(false);

  WiFi.begin(ssid, pw);
  connected = true;

  int timeout = 0;

  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.println("Waiting connection..");

    display.setFont(ArialMT_Plain_10);
    display.setTextAlignment(TEXT_ALIGN_LEFT);
    display.drawStringMaxWidth(0, 0, 128, "Waiting connection..");
    display.display();

    timeout ++;
    if (timeout > 40) {
      connected = false;
      break;
    }
  }

  if (connected) {
    OscWiFi.subscribe(12000, "/cmd", onOscReceived);
    OscWiFi.subscribe(12000, "/sync", onOscReceived);
  }
}

void loop() {
  uint32_t elapsed = millis();
  time_t _n = now();
  int _h = hour(_n);
  int _m = minute(_n);

  if (dht20_available && elapsed - DHT.lastRead() >= 3000) {
    //  READ DATA, TODO:
    uint32_t start = micros();
    int status = DHT.read();
    uint32_t stop = micros();

    switch (status) {
    case DHT20_OK:
      humid = DHT.getHumidity();
      temp = DHT.getTemperature();
      if (connected) OscWiFi.send(osc_dest, 12000, osc_tag_self, humid, temp);
      break;
    case DHT20_ERROR_CHECKSUM:
      Serial.println("Checksum error");
      break;
    case DHT20_ERROR_CONNECT:
      Serial.println("Connect error");
      break;
    case DHT20_MISSING_BYTES:
      Serial.println("Missing bytes");
      break;
    case DHT20_ERROR_BYTES_ALL_ZERO:
      Serial.println("All bytes read zero");
      break;
    case DHT20_ERROR_READ_TIMEOUT:
      Serial.println("Read time out");
      break;
    case DHT20_ERROR_LASTREAD:
      Serial.println("Error read too fast");
      break;
    default:
      Serial.println("Unknown error");
      break;
    }
  }

  if (am2301_available && elapsed - lastRead >= 3000) {
    sensors_event_t t_humidity, t_temp;
    AHT.getEvent(&t_humidity, &t_temp);

    temp = t_temp.temperature;
    humid = t_humidity.relative_humidity;

    if (connected) OscWiFi.send(osc_dest, 12000, osc_tag_self, humid, temp);

    lastRead = millis();
  }

  if (elapsed - lastPing >= 5000) {
    lastPing = elapsed;
    if (connected) OscWiFi.send(osc_dest, 12000, osc_tag_self + "/ping", WiFi.localIP().toString());
  }

  display.clear();
  display.setFont(ArialMT_Plain_10);
  display.setTextAlignment(TEXT_ALIGN_LEFT);

  // TODO:
  switch (ecbt.position()) {
  case 0:
    if (dht20_available) {
      display.drawStringMaxWidth(0, 0, 128, "TEMPERATURE: " + String(temp, 2));
      display.drawStringMaxWidth(0, 12 * 1, 128, "HUMIDITY: " + String(humid, 2));
      display.drawStringMaxWidth(0, 12 * 2, 128, String(_h) + ":" + (_m < 10 ? "0" : "") + String(_m));
    } else {
      display.drawStringMaxWidth(0, 0, 128, "NO_SENSOR");
      display.drawStringMaxWidth(0, 12 * 1, 128, String(_h) + ":" + (_m < 10 ? "0" : "") + String(_m));
    }
    if (!connected) { display.drawStringMaxWidth(0, 12 * 3, 128, ":WIFI_CON_FAILED"); }
    break;
  case 1:
    display.drawStringMaxWidth(0, 0, 128, "GPIO_OUT_1: " + String(gpio_out_1));
    if (gpio_out_1_target > 0) {
      display.drawStringMaxWidth(0, 12 * 1, 128, "TARGET="  + labels[gpio_out_1_target]);
      display.drawStringMaxWidth(0, 12 * 2, 128, "LO=" +  String(gpio_out_1_range[0], 2) + ", HI=" + String(gpio_out_1_range[1], 2));
      display.drawStringMaxWidth(0, 12 * 3, 128, dht20_available ? (gpio_out_1_target == 1 ? String(temp, 2) : String(humid, 2)) : "NO_SENSOR");
    }
    break;
  case 2:
    display.drawStringMaxWidth(0, 0, 128, "GPIO_OUT_2: " + String(gpio_out_2));
    if (gpio_out_2_target > 0) {
      display.drawStringMaxWidth(0, 12 * 1, 128, "TARGET="  + labels[gpio_out_2_target]);
      display.drawStringMaxWidth(0, 12 * 2, 128, "LO=" +  String(gpio_out_2_range[0], 2) + ", HI=" + String(gpio_out_2_range[1], 2));
      display.drawStringMaxWidth(0, 12 * 3, 128, dht20_available ? (gpio_out_2_target == 1 ? String(temp, 2) : String(humid, 2)) : "NO_SENSOR");
    }
    break;
  case 3:
    display.drawStringMaxWidth(0, 0, 128, "GPIO_OUT_3: " + String(gpio_out_3));
    if (gpio_out_3_target > 0) {
      display.drawStringMaxWidth(0, 12 * 1, 128, "TARGET="  + labels[gpio_out_3_target]);
      display.drawStringMaxWidth(0, 12 * 2, 128, "LO=" +  String(gpio_out_3_range[0], 2) + ", HI=" + String(gpio_out_3_range[1], 2));
      display.drawStringMaxWidth(0, 12 * 3, 128, dht20_available ? (gpio_out_3_target == 1 ? String(temp, 2) : String(humid, 2)) : "NO_SENSOR");
    }
    break;
  case 4:
    display.drawStringMaxWidth(0, 0, 128, "RELAY_OUT: " + String(relay_out));
    display.drawStringMaxWidth(0, 12 * 1, 128, "TARGET="  + labels_relay[relay_target]);
    break;
  default:
    display.drawStringMaxWidth(0, 0, 128, connected ? ("SELF: " + WiFi.localIP().toString()) : ":WIFI_CON_FAILED");
    display.drawStringMaxWidth(0, 12 * 1, 128, "DEST: " + osc_dest);
    display.drawStringMaxWidth(0, 12 * 2, 128, "SELF_TAG: " + osc_tag_self);
    break;
  }
  display.display();
  // --

  ecbt.update();

  if (connected) OscWiFi.update();

  // -- GPIO OPs
  if (relay_target == 1) {
    if ((hour(_n) % relay_test_interval_hours) == 0 && minute(_n) < 3) {
      relay_out = 1;
    }
  }
  gpio_in_1 = digitalRead(D3);
  digitalWrite(RELAY_OUT, gpio_in_1 ? 0 : relay_out);

  // TODO:
  if (gpio_out_1_target == 1) {
    if (temp < gpio_out_1_range[0]) gpio_out_1_edge = -1;
    if (temp > gpio_out_1_range[1]) gpio_out_1_edge = 1;
    gpio_out_1 = ((temp < gpio_out_1_range[0] || temp < gpio_out_1_range[1]) && gpio_out_1_edge < 1);
  } else if (gpio_out_1_target == 2) {
    if (humid < gpio_out_1_range[0]) gpio_out_1_edge = -1;
    if (humid > gpio_out_1_range[1]) gpio_out_1_edge = 1;
    gpio_out_1 = ((humid < gpio_out_1_range[0] || humid < gpio_out_1_range[1]) && gpio_out_1_edge < 1);
  }

  if (gpio_out_2_target == 1) {
    if (temp < gpio_out_2_range[0]) gpio_out_2_edge = -1;
    if (temp > gpio_out_2_range[1]) gpio_out_2_edge = 1;
    gpio_out_2 = ((temp < gpio_out_2_range[0] || temp < gpio_out_2_range[1]) && gpio_out_2_edge < 1);
  } else if (gpio_out_2_target == 2) {
    if (humid < gpio_out_2_range[0]) gpio_out_2_edge = -1;
    if (humid > gpio_out_2_range[1]) gpio_out_2_edge = 1;
    gpio_out_2 = ((humid < gpio_out_2_range[0] || humid < gpio_out_2_range[1]) && gpio_out_2_edge < 1);
  }

  if (gpio_out_3_target == 1) {
    if (temp < gpio_out_3_range[0]) gpio_out_3_edge = -1;
    if (temp > gpio_out_3_range[1]) gpio_out_3_edge = 1;
    gpio_out_3 = ((temp < gpio_out_3_range[0] || temp < gpio_out_3_range[1]) && gpio_out_3_edge < 1);
  } else if (gpio_out_3_target == 2) {
    if (humid < gpio_out_3_range[0]) gpio_out_3_edge = -1;
    if (humid > gpio_out_3_range[1]) gpio_out_3_edge = 1;
    gpio_out_3 = ((humid < gpio_out_3_range[0] || humid < gpio_out_3_range[1]) && gpio_out_3_edge < 1);
  }

  digitalWrite(TRIG_OUT_1, gpio_out_1);
  digitalWrite(TRIG_OUT_2, gpio_out_2);
  digitalWrite(TRIG_OUT_3, gpio_out_3);
  // --
}
