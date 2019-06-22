#define DIGITS 6
#define SDA_PIN D2
#define SCL_PIN D1
#define EEPROM_WIFI 0
#define I2C_DISPLAY_ADDRESS 1

#include <ArduinoJson.h>
#include <ESP8266WiFi.h>
#include <WiFiClientSecure.h>
#include <Wire.h>
#include <EEPROM.h>
#include <ESP8266WebServer.h>
#include "DNSServer.h"

IPAddress apIP(192, 168, 4, 1);
DNSServer dnsServer;
ESP8266WebServer webServer(80);
const char *portal = "<!DOCTYPE html><html lang=\"en\"> <head> <title>Mastermilo SubCounter </title> <meta charset=\"utf-8\"> <meta name=\"viewport\" content=\"width=device-width, initial-scale=1, maximum-scale=1, user-scalable=0\"> <style>body{font-family: sans-serif;}.header{font-size: 30px;}form > *{margin: 10px 0;}.instruction-1{font-size: 20px; font-weight: bold;}input{width: 100%; font-size: 2em;}</style> </head> <body> <div class=\"header\">Mastermilo SubCounter</div><form method=\"GET\" action=\"/save\"> <div class=\"instruction-1\">Hoi Emiel en Dirk! Als je hier even jullie Wi-Fi gegevens invullen dan kan de SubCounter live laten zien hoeveel abonnees jullie hebben! Je kan ook een hotspot van een telefoon gebruiken.</div><input type=\"text\" name=\"ssid\" placeholder=\"Wi-Fi naam\"> <input type=\"text\" name=\"passphrase\" placeholder=\"Wachtwoord\"> <div class=\"instruction-2\"> Let op, allebei de velden zijn hoofdletter gevoelig.<br></div><input type=\"submit\" name=\"submit\" value=\"Sla het op enzo\"> </form> </body></html>";
const char *ok = "<!DOCTYPE html><html lang=\"en\"> <head> <title>Mastermilo SubCounter </title> <meta charset=\"utf-8\"> <meta name=\"viewport\" content=\"width=device-width, initial-scale=1, maximum-scale=1, user-scalable=0\"> <style>body{font-family: sans-serif;}.header{font-size: 30px;}form > *{margin: 10px 0;}.instruction-1{font-size: 20px; font-weight: bold;}</style> </head> <body> <div class=\"header\">Nice, opgeslagen!</div><form> <div class=\"instruction-1\">Je moet nu het apparaat even uit en aan zetten en dan doet ie het als het goed is!</div></form> </body></html>";

const char* apiHost = "www.googleapis.com";
const unsigned short apiHttpsPort = 443;

// hgfedcba
const byte segments[] = {
  0x3F,
  0x06,
  0x5B,
  0x4F,
  0x66,
  0x6D,
  0x7D,
  0x07,
  0x7F,
  0x6F,
  0x77,
  0x7C,
  0x39,
  0x5E,
  0x79,
  0x71
};

byte values[DIGITS];
const byte connErr[] = { segments[0xC], segments[0], B10110111, segments[0xE], B01010000, B11010000 };
const byte jsonErr[] = { B00001110, B01101101, segments[0], B10110111, segments[0xE], B11010000 };
byte load[] = { B00111000, segments[0xA], segments[0xD], segments[0xE], B10110111, B10000000 };
const byte config[] = { segments[0xC], segments[0], B00110111, segments[0xF], segments[1], B01101111 };

String payload;

struct WifiCredentials {
  bool set;
  char ssid[50];
  char passphrase[50];
};

bool configMode = false;

WifiCredentials credentials;

void handlePage() {
  webServer.send(200, "text/html", portal);
}

void handleSave() {
  if (!webServer.hasArg("ssid") || !webServer.hasArg("passphrase")) {
    webServer.send(200, "text/html", "Er ging iets fout.\n");
    return;
  }

  String ssid = webServer.arg("ssid");
  String passphrase = webServer.arg("passphrase");

  ssid.toCharArray(credentials.ssid, 50);
  passphrase.toCharArray(credentials.passphrase, 50);
  credentials.set = true;
  EEPROM.put(EEPROM_WIFI, credentials);
  EEPROM.commit();

  webServer.send(200, "text/html", ok);
}

void setup() {
  pinMode(D6, INPUT_PULLUP);

  // reserve bytes in eeprom memory for wifi credentials
  EEPROM.begin(sizeof(WifiCredentials));

  EEPROM.get(EEPROM_WIFI, credentials);

  if (!digitalRead(D6)) {
    credentials.set = false;
    EEPROM.put(EEPROM_WIFI, credentials);
    EEPROM.commit();
  }

  if (!credentials.set) {
    configMode = true;
  }
  
  Wire.begin(SDA_PIN, SCL_PIN); //sda, scl

  delay(2000);

  if (configMode) {
    transmit(config);

    WiFi.mode(WIFI_AP); 
    WiFi.softAPConfig(apIP, apIP, IPAddress(255, 255, 255, 0));
    WiFi.softAP("Mastermilo SubCounter", NULL, 12, false, 4);
    dnsServer.start(53, "*", apIP);
    webServer.onNotFound(handlePage);
    webServer.on("/save", HTTP_GET, handleSave);
    webServer.begin();
  } else {
    transmit(load);

    WiFi.mode(WIFI_STA);
    WiFi.begin(credentials.ssid, credentials.passphrase);
    WiFi.setAutoConnect(true);
    WiFi.setAutoReconnect(true);

    load[5] = B00100000;
    while (WiFi.status() != WL_CONNECTED) {
      if (load[5] == 1) {
        load[5] = B00100000;
      } else {
        load[5] /= 2;
      }
      transmit(load);
      delay(200);
    }

    load[5] = B10000000;
  }
}

unsigned int pow10int(byte p) {
  unsigned int n = 1;
  for (byte i = 0; i < p; i ++) {
    n *= 10;
  }
  return n;
}

DynamicJsonDocument doc(2048);

void displayViews() {
  DeserializationError err = deserializeJson(doc, payload);
  if (err) {
    transmit(jsonErr);
    return;
  }

  JsonObject root = doc.as<JsonObject>();
  JsonArray items = root["items"].as<JsonArray>();
  JsonObject channel = items[0].as<JsonObject>();
  JsonObject statistics = channel["statistics"].as<JsonObject>();
  String subscribersStr = statistics["subscriberCount"].as<String>();

  uint32_t subscribers = subscribersStr.toInt();

  byte data[DIGITS] = {
    0, segments[0xC], segments[0], segments[0], B10111000, 0
  };

  if (subscribers < 1000000) {
    for (byte i = 0; i < DIGITS; i ++) {
      data[i] = segments[(subscribers % pow10int(DIGITS - i)) / pow10int(DIGITS - i - 1)];
    }
  }

  transmit(data);
}

void loop() {
  if (WiFi.status() == WL_CONNECTED && !configMode) {
    WiFiClientSecure client;
    if (!client.connect(apiHost, apiHttpsPort)) {
      transmit(connErr);
      return;
    }

    client.print(String("GET /youtube/v3/channels?part=statistics&id=UC3eSiX_rjJk2jg5EJSBcqBw&key=<key here> HTTP/1.1\r\n") +
      "Host: " + apiHost + "\r\n" +
      "User-Agent: Mastermilo View Display\r\n" +
      "Connection: close\r\n\r\n");

    while (client.connected()) {
      String line = client.readStringUntil('\n');
      if (line == "\r") {
        break;
      }
    }

    payload = client.readString();

    client.stop();

    displayViews();

    delay(10000);
  }

  if (configMode) {
    dnsServer.processNextRequest();
    webServer.handleClient();
  }
}

void transmit(const byte data[]) {
  bool diff = false;
  for (byte i = 0; i < DIGITS; i ++) {
    if (data[i] != values[i]) {
      diff = true;
      break;
    }
  }

  if (!diff) {
    return;
  }

  memcpy(values, data, DIGITS);

  Wire.beginTransmission(I2C_DISPLAY_ADDRESS);
  Wire.write(values, DIGITS);
  Wire.endTransmission();
}

