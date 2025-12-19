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


#define CHAMBER_IO_VERSION "0.9.10"

#define XIAO_SDA D4
#define XIAO_SCL D5
#define FOLLOW_MODE_N 4
#define PAGE_AVAILABLE_N 5
#define CONFIG_AVAILABLE_N 6
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

EncoderButton ecbt(D1, D2, D0);

uint32_t lastPing, lastRead;

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

volatile uint8_t gpio_out_relay = 0;
uint8_t relay_target = 1;
int relay_test_interval_hours = 2;
uint8_t gpio_in_1 = 0;

// TODO:
int gpio_test_interval_hours = 2;
int gpio_offset_interval = 40;
// --

String labels[] = { "NONE", "TEMPERATURE", "HUMIDITY", "TIMER" };
String labels_relay[] = {"REMOTE", "TIMER", "MANUAL"};
String labels_config[] = {
  "GPIO_OUT_1 [ LO ]", "GPIO_OUT_1 [ HI ]",
  "GPIO_OUT_2 [ LO ]", "GPIO_OUT_2 [ HI ]",
  "GPIO_OUT_3 [ LO ]", "GPIO_OUT_3 [ HI ]"
};

enum ModeLabel { MODE_DEFAULT, MODE_CONFIG, MODE_ADMIN };
ModeLabel current_mode = MODE_DEFAULT;

enum SubModeLabel { SUBMODE_DEFAULT, SUBMODE_EDIT };
SubModeLabel current_submode = SUBMODE_DEFAULT;
uint8_t submodeEditIndex = 0;

enum FollowTarget { FOLLOW_T_NONE, FOLLOW_T_TEMP, FOLLOW_T_HUMID, FOLLOW_T_TIMER };

bool connected, dht20_available, am2301_available = false;

float humid, temp;

String ssid = "SSID";
String pw = "PASSWORD";
String osc_dest = "192.168.100.20";
String osc_tag_self = "/chamber_1";


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

void onOscReceived(const OscMessage &m) {
  // -- TODO:
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
    if (relay_target < 2)
      gpio_out_relay = m.arg<int>(0);
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

  // -- TODO:
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

bool saveConfig(bool partial = true,  String config_filename = "/config.json") {
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

  doc["gpio_out_1_range"][0] = gpio_out_1_range[0];
  doc["gpio_out_1_range"][1] = gpio_out_1_range[1];
  doc["gpio_out_2_range"][0] = gpio_out_2_range[0];
  doc["gpio_out_2_range"][1] = gpio_out_2_range[1];
  doc["gpio_out_3_range"][0] = gpio_out_3_range[0];
  doc["gpio_out_3_range"][1] = gpio_out_3_range[1];

  if (!partial) {
    // -- TODO:
  }

  String tmp = "";
  serializeJson(doc, tmp);
  writeFile(config_filename, tmp);

  return true;
}

void onEncoder(EncoderButton& eb) {
  // Serial.println(eb.increment());
  // Serial.println(eb.position());
  if (current_mode == MODE_ADMIN) return;

  if (current_mode == MODE_CONFIG) {
    if (current_submode == SUBMODE_DEFAULT) {
      if (eb.position() > CONFIG_AVAILABLE_N - 1) {
        eb.resetPosition(CONFIG_AVAILABLE_N - 1);
      } else if (eb.position() < 0) {
        eb.resetPosition(0);
      }
    }
  } else {
    if (eb.position() > PAGE_AVAILABLE_N) {
      eb.resetPosition(PAGE_AVAILABLE_N);
    } else if (eb.position() < 0) {
      eb.resetPosition(0);
    }
  }
}

void onLongPress(EncoderButton &eb) {
  if (current_mode == MODE_ADMIN) return;

  if (current_mode == MODE_DEFAULT) {
    eb.resetPosition(0);
    current_mode = MODE_CONFIG;
    current_submode = SUBMODE_DEFAULT;
  } else {
    saveConfig();
    delay(1000);
    eb.resetPosition(0);
    current_mode = MODE_DEFAULT;
  }
}

void onBtnClick(EncoderButton &eb) {
  // Serial.println(eb.clickCount());
  if (current_mode == MODE_ADMIN) return;

  if (current_mode == MODE_CONFIG) {
    configModeClickAction(eb);
  } else {
    defaultModeClickAction(eb);
  }
}

void configModeClickAction(EncoderButton &eb) {
  if (eb.clickCount() == 1) {
    // -- single click: change submode
    current_submode = (current_submode == SUBMODE_DEFAULT) ? SUBMODE_EDIT : SUBMODE_DEFAULT;
    if (current_submode == SUBMODE_EDIT) {
      eb.resetPosition(0);
    } else {
      eb.resetPosition(submodeEditIndex);
    }
  } else if (eb.clickCount() >= 2) {
    // -- double click: change value & save
    if (current_submode == SUBMODE_EDIT) {
      float diff = eb.position() * 0.1;
      switch (submodeEditIndex) {
      case 0:
        gpio_out_1_range[0] = gpio_out_1_range[0] + diff;
        break;
      case 1:
        gpio_out_1_range[1] = gpio_out_1_range[1] + diff;
        break;
      case 2:
        gpio_out_2_range[0] = gpio_out_2_range[0] + diff;
        break;
      case 3:
        gpio_out_2_range[1] = gpio_out_2_range[1] + diff;
        break;
      case 4:
        gpio_out_3_range[0] = gpio_out_3_range[0] + diff;
        break;
      case 5:
        gpio_out_3_range[1] = gpio_out_3_range[1] + diff;
      default:
        break;
      }
      eb.resetPosition(submodeEditIndex);
      current_submode = SUBMODE_DEFAULT;
    }
  }
}

void defaultModeClickAction(EncoderButton &eb) {
  if (eb.clickCount() == 1) {
    // -- single click: change the current output value
    if (eb.position() == 1) {
      gpio_out_1 = (gpio_out_1 + 1) % 2;
    } else if (eb.position() == 2) {
      gpio_out_2 = (gpio_out_2 + 1) % 2;
    } else if (eb.position() == 3) {
      gpio_out_3 = (gpio_out_3 + 1) % 2;
    } else {
      gpio_out_relay = (gpio_out_relay + 1) % 2;
    }
  } else if (eb.clickCount() >= 2) {
    // -- double click: change following target
    if (eb.position() == 1) {
      gpio_out_1_target = (gpio_out_1_target + 1) % FOLLOW_MODE_N;
    } else if (eb.position() == 2) {
      gpio_out_2_target = (gpio_out_2_target + 1) % FOLLOW_MODE_N;
    } else if (eb.position() == 3) {
      gpio_out_3_target = (gpio_out_3_target + 1) % FOLLOW_MODE_N;
    } else if (eb.position() == 4) {
      relay_target = (relay_target + 1) % 3;
    }
  }
}

void enableNetwork() {
  WiFi.onEvent(onWifiDisconnected, WiFiEvent_t::ARDUINO_EVENT_WIFI_STA_DISCONNECTED);
  WiFi.onEvent(onGotIp, WiFiEvent_t::ARDUINO_EVENT_WIFI_STA_GOT_IP);
  // WiFi.setAutoReconnect(false);

  WiFi.begin(ssid, pw);
  connected = true;

  int timeout = 0;

  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.println("Waiting connection..");

    display.clear();
    display.setFont(ArialMT_Plain_10);
    display.setTextAlignment(TEXT_ALIGN_LEFT);
    display.drawStringMaxWidth(0, 0, 128, "Waiting connection..");
    display.display();

    timeout++;
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

void adminModeLoop() {
  // -- TODO:
  display.clear();
  display.setFont(ArialMT_Plain_10);
  display.setTextAlignment(TEXT_ALIGN_LEFT);
  display.drawStringMaxWidth(0, 0, 128, "Current version doesn't support this mode");
  display.display();
}

void configModeLoop() {
  display.clear();
  display.setFont(ArialMT_Plain_10);
  display.setTextAlignment(TEXT_ALIGN_LEFT);
  float diff = 0.0;

  if (current_submode == SUBMODE_DEFAULT) {
    submodeEditIndex = constrain(ecbt.position(), 0, CONFIG_AVAILABLE_N - 1);
    display.drawStringMaxWidth(0, 0, 128, "SELECT CONFIG:");
  } else {
    diff = ecbt.position() * 0.1;
    display.drawStringMaxWidth(0, 0, 128, "EDIT CONFIG:");
  }

  float editTarget = gpio_out_1_range[0] + diff;
  switch (submodeEditIndex) {
  case 1:
    editTarget = gpio_out_1_range[1] + diff;
    break;
  case 2:
    editTarget = gpio_out_2_range[0] + diff;
    break;
  case 3:
    editTarget = gpio_out_2_range[1] + diff;
    break;
  case 4:
    editTarget = gpio_out_3_range[0] + diff;
    break;
  case 5:
    editTarget = gpio_out_3_range[1] + diff;
  default:
    break;
  }

  display.drawStringMaxWidth(0, 12 * 1, 128, labels_config[submodeEditIndex]);
  display.drawStringMaxWidth(0, 12 * 2, 128, String(editTarget));
  display.display();
}

void setup() {
  // pinMode(D3, INPUT_PULLUP);
  pinMode(D3, INPUT_PULLDOWN);
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

  Wire.begin(XIAO_SDA, XIAO_SCL);

  dht20_available = DHT.begin();

  if (!dht20_available) am2301_available = AHT.begin();

  delay(1000);

  display.init();
  display.flipScreenVertically();
  display.setFont(ArialMT_Plain_10);

  ecbt.setLongClickDuration(2000);
  ecbt.setEncoderHandler(onEncoder);
  ecbt.setClickHandler(onBtnClick);
  ecbt.setLongPressHandler(onLongPress);
  ecbt.update();

  if (ecbt.isPressed()) {
    display.setFont(ArialMT_Plain_10);
    display.setTextAlignment(TEXT_ALIGN_LEFT);
    display.drawStringMaxWidth(0, 0, 128, "Enter configuration mode..");
    display.display();
    delay(1000);
    current_mode = MODE_ADMIN;
  } else {
    enableNetwork();
  }
}

void loop() {
  uint32_t elapsed = millis();
  time_t _n = now();
  int _h = hour(_n);
  int _m = minute(_n);

  ecbt.update();

  if (current_mode == MODE_ADMIN) {
    adminModeLoop();
  } else if (current_mode == MODE_CONFIG) {
    configModeLoop();
  } else {
    if (dht20_available && elapsed - DHT.lastRead() >= 3000) {
      //  -- Reading & publishing sensor data, TODO:
      uint32_t start = micros();
      int status = DHT.read();
      uint32_t stop = micros();

      switch (status) {
      case DHT20_OK:
        humid = DHT.getHumidity();
        temp = DHT.getTemperature();
        if (connected)
          OscWiFi.send(osc_dest, 12000, osc_tag_self, humid, temp);
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

      if (connected)
        OscWiFi.send(osc_dest, 12000, osc_tag_self, humid, temp);

      lastRead = millis();
    }

    if (elapsed - lastPing >= 5000) {
      lastPing = elapsed;
      if (connected)
        OscWiFi.send(osc_dest, 12000, osc_tag_self + "/ping",
                     WiFi.localIP().toString());
    }

    if (connected) OscWiFi.update();
    // --

    // -- GPIO ops, TODO:
    gpio_in_1 = digitalRead(D3);

    if (relay_target == 1) {
      if ((hour(_n) % relay_test_interval_hours) == 0 && minute(_n) < 3) {
        gpio_out_relay = 1;
      }
    }

    // gpio_out_relay = gpio_in_1 ? 0 : gpio_out_relay;
    gpio_out_relay = !gpio_in_1 ? 0 : gpio_out_relay;

    if (gpio_out_1_target == FOLLOW_T_TEMP) {
      if (temp < gpio_out_1_range[0]) gpio_out_1_edge = -1;
      if (temp > gpio_out_1_range[1]) gpio_out_1_edge = 1;
      gpio_out_1 = ((temp < gpio_out_1_range[0] || temp < gpio_out_1_range[1]) && gpio_out_1_edge < 1);
    } else if (gpio_out_1_target == FOLLOW_T_HUMID) {
      if (humid < gpio_out_1_range[0]) gpio_out_1_edge = -1;
      if (humid > gpio_out_1_range[1]) gpio_out_1_edge = 1;
      gpio_out_1 = ((humid < gpio_out_1_range[0] || humid < gpio_out_1_range[1]) && gpio_out_1_edge < 1);
    } else if (gpio_out_1_target == FOLLOW_T_TIMER) {
      gpio_out_1 = ((hour(_n) % gpio_test_interval_hours) == 0 && minute(_n) < gpio_offset_interval);
    }

    if (gpio_out_2_target == FOLLOW_T_TEMP) {
      if (temp < gpio_out_2_range[0]) gpio_out_2_edge = -1;
      if (temp > gpio_out_2_range[1]) gpio_out_2_edge = 1;
      gpio_out_2 = ((temp < gpio_out_2_range[0] || temp < gpio_out_2_range[1]) && gpio_out_2_edge < 1);
    } else if (gpio_out_2_target == FOLLOW_T_HUMID) {
      if (humid < gpio_out_2_range[0]) gpio_out_2_edge = -1;
      if (humid > gpio_out_2_range[1]) gpio_out_2_edge = 1;
      gpio_out_2 = ((humid < gpio_out_2_range[0] || humid < gpio_out_2_range[1]) && gpio_out_2_edge < 1);
    } else if (gpio_out_2_target == FOLLOW_T_TIMER) {
      gpio_out_2 = ((hour(_n) % gpio_test_interval_hours) == 0 && minute(_n) < gpio_offset_interval);
    }

    if (gpio_out_3_target == FOLLOW_T_TEMP) {
      if (temp < gpio_out_3_range[0]) gpio_out_3_edge = -1;
      if (temp > gpio_out_3_range[1]) gpio_out_3_edge = 1;
      gpio_out_3 = ((temp < gpio_out_3_range[0] || temp < gpio_out_3_range[1]) && gpio_out_3_edge < 1);
    } else if (gpio_out_3_target == FOLLOW_T_HUMID) {
      if (humid < gpio_out_3_range[0]) gpio_out_3_edge = -1;
      if (humid > gpio_out_3_range[1]) gpio_out_3_edge = 1;
      gpio_out_3 = ((humid < gpio_out_3_range[0] || humid < gpio_out_3_range[1]) && gpio_out_3_edge < 1);
    } else if (gpio_out_3_target == FOLLOW_T_TIMER) {
      gpio_out_2 = ((hour(_n) % gpio_test_interval_hours) == 0 && minute(_n) < gpio_offset_interval);
    }

    digitalWrite(RELAY_OUT, gpio_out_relay);
    digitalWrite(TRIG_OUT_1, gpio_out_1);
    digitalWrite(TRIG_OUT_2, gpio_out_2);
    digitalWrite(TRIG_OUT_3, gpio_out_3);
    // --

    // -- Displays, TODO:
    display.clear();
    display.setFont(ArialMT_Plain_10);
    display.setTextAlignment(TEXT_ALIGN_LEFT);

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
      if (!connected) {
        display.drawStringMaxWidth(0, 12 * 3, 128, ":WIFI_CON_FAILED");
      }
      break;
    case 1:
      display.drawStringMaxWidth(0, 0, 128, "GPIO_OUT_1: " + String(gpio_out_1));
      if (gpio_out_1_target > FOLLOW_T_NONE) {
        display.drawStringMaxWidth(0, 12 * 1, 128, "TARGET=" + labels[gpio_out_1_target]);
        display.drawStringMaxWidth(0, 12 * 2, 128,
                                   "LO=" + String(gpio_out_1_range[0], 2) +
                                   ", HI=" + String(gpio_out_1_range[1], 2));
        display.drawStringMaxWidth(0, 12 * 3, 128,
                                   dht20_available
                                   ? (gpio_out_1_target == 1 ? String(temp, 2) : String(humid, 2))
                                   : "NO_SENSOR");
      }
      break;
    case 2:
      display.drawStringMaxWidth(0, 0, 128, "GPIO_OUT_2: " + String(gpio_out_2));
      if (gpio_out_2_target > FOLLOW_T_NONE) {
        display.drawStringMaxWidth(0, 12 * 1, 128, "TARGET=" + labels[gpio_out_2_target]);
        display.drawStringMaxWidth(0, 12 * 2, 128,
                                   "LO=" + String(gpio_out_2_range[0], 2) +
                                   ", HI=" + String(gpio_out_2_range[1], 2));
        display.drawStringMaxWidth(0, 12 * 3, 128,
                                   dht20_available
                                   ? (gpio_out_2_target == 1 ? String(temp, 2) : String(humid, 2))
                                   : "NO_SENSOR");
      }
      break;
    case 3:
      display.drawStringMaxWidth(0, 0, 128, "GPIO_OUT_3: " + String(gpio_out_3));
      if (gpio_out_3_target > FOLLOW_T_NONE) {
        display.drawStringMaxWidth(0, 12 * 1, 128, "TARGET=" + labels[gpio_out_3_target]);
        display.drawStringMaxWidth(0, 12 * 2, 128,
                                   "LO=" + String(gpio_out_3_range[0], 2) +
                                   ", HI=" + String(gpio_out_3_range[1], 2));
        display.drawStringMaxWidth(0, 12 * 3, 128,
                                   dht20_available
                                   ? (gpio_out_3_target == 1 ? String(temp, 2) : String(humid, 2))
                                   : "NO_SENSOR");
      }
      break;
    case 4:
      display.drawStringMaxWidth(0, 0, 128, "RELAY_OUT: " + String(gpio_out_relay));
      display.drawStringMaxWidth(0, 12 * 1, 128,
                                 "TARGET=" + labels_relay[relay_target]);
      break;
    default:
      display.drawStringMaxWidth(0, 0, 128,
                                 connected ? ("SELF: " + WiFi.localIP().toString())
                                 : ":WIFI_CON_FAILED");
      display.drawStringMaxWidth(0, 12 * 1, 128, "DEST: " + osc_dest);
      display.drawStringMaxWidth(0, 12 * 2, 128, "SELF_TAG: " + osc_tag_self);
      display.drawStringMaxWidth(0, 12 * 3, 128, "VERSION: " + String(CHAMBER_IO_VERSION));
      break;
    }

    if (gpio_out_relay) {
      display.fillRect(0, 12 * 4 + 6, 10, 10);
    } else {
      display.drawRect(0, 12 * 4 + 6, 10, 10);
    }

    if (gpio_out_1) {
      display.fillRect(12 * 2, 12 * 4 + 6, 10, 10);
    } else {
      display.drawRect(12 * 2, 12 * 4 + 6, 10, 10);
    }

    if (gpio_out_2) {
      display.fillRect(12 * 3, 12 * 4 + 6, 10, 10);
    } else {
      display.drawRect(12 * 3, 12 * 4 + 6, 10, 10);
    }

    if (gpio_out_3) {
      display.fillRect(12 * 4, 12 * 4 + 6, 10, 10);
    } else {
      display.drawRect(12 * 4, 12 * 4 + 6, 10, 10);
    }

    display.display();
    // --
  }
}
