/**
 * ESP8266 Cheerlights lamp.
*/
#include <EEPROM.h>
#include <ESP8266WiFi.h>
#include <ESP8266HTTPClient.h>
#include <WiFiClient.h>
#include <ESP8266WebServer.h>
#define FASTLED_ALLOW_INTERRUPTS 0
#include <FastLED.h>

// WS2812B data pin
#define LED_PIN 2

// WEB server configuration port
#define WEB_SERVER_PORT 80

// Configuration WIFI name
#define WIFI_AP_SSID "CheerlightsConfig"

// Configuration timeout (3 mins).
#define CONFIG_TIMEOUT 1000 * 60 * 3

// ThingSpeak API server
#define DEFAULT_TS_HOST "api.thingspeak.com"
#define DEFAULT_TS_PORT 80
#define DEFAULT_TS_CHANNEL 1417L
#define DEFAULT_TS_FIELD 2

// Magic for detecting empty (unconfigured) EEPROM
#define MAGIC 0xAB

// Error count for reconnect WIFI
#define ERR_COUNT_FOR_RECONNECT 30
#define HTTP_READ_TIMEOUT       10000
#define HTTP_CONNECT_TIMEOUT    5000

// Color change delay
#define COLOR_CHANGE_DELAY      100

// Min and max update frequency
#define MIN_UPDATE_FREQUENCY    5
#define MAX_UPDATE_FREQUENCY 7200


// EEPROM config structure
struct ClConfiguration {
  int magic;
  int updateFrequency;
  char apName [24];
  char password [48];
  char tsHost [40];
  unsigned int tsPort;
  unsigned long tsChannel;
  int tsField;
  bool smoothUpdate;
};

// Pixel color
struct PixelColor {
  int red;
  int green;
  int blue;
};

// Object for LED state
CRGB led;

// Configuration
ClConfiguration config;

// Configuration server; when exists
ESP8266WebServer *server = (ESP8266WebServer *) NULL;

// Configuration flag; is true if configuration has been executed
bool configExecuted = false;

// Last configuration interaction time for detecting timeout
long lastInteractionTime;

// Current lamp color
PixelColor actPixelColor;

// Requested lamp color
PixelColor reqPixelColor;


/**
 * Setup function.
*/
void setup() {
  Serial.begin (115200);

  // Turn WIFI OFF
  WiFi.mode (WIFI_OFF);
  delay (1000);

  // Setup FastLED
  FastLED.addLeds<NEOPIXEL, LED_PIN> (&led, 1).setCorrection (TypicalSMD5050);
  setColor (100, 0, 0);

  // Read config
  EEPROM.begin (sizeof (ClConfiguration));
  EEPROM.get (0, config);

  // Set defaults when magic is not match
  if (config.magic != MAGIC) {
    memset (&config, 0, sizeof (ClConfiguration));
    config.magic = MAGIC;
    config.updateFrequency = 15;
    updateConfigKey (config.tsHost, 40, DEFAULT_TS_HOST);
    config.tsPort = DEFAULT_TS_PORT;
    config.tsChannel = DEFAULT_TS_CHANNEL;
    config.tsField = DEFAULT_TS_FIELD;
    config.smoothUpdate = true;
  }

  // Run configuration AP
  WiFi.softAP (WIFI_AP_SSID);

  // Initializes color values
  memset (&actPixelColor, 0, sizeof (PixelColor));
  memset (&reqPixelColor, 0, sizeof (PixelColor));
  
  // ... and run HTTP server for setup; typical IP=192.168.4.1
  server = new ESP8266WebServer (WiFi.softAPIP(), WEB_SERVER_PORT);
  server->on ("/", wsHandleRoot);
  server->on ("/update", wsHandleUpdate);
  server->on ("/run", wsHandleRun);
  server->begin();

  lastInteractionTime = millis();

  delay (500);
  setColor (0, 100, 0);
}

/**
 * Sets color.
*/
void setColor (int r, int g, int b) {
  actPixelColor.red = r;
  actPixelColor.green = g;
  actPixelColor.blue = b;
  setColor();
}

/**
 * Sets color.
*/
void setColor() {
  led.r = actPixelColor.red;
  led.green = actPixelColor.green;
  led.blue = actPixelColor.blue;
  FastLED.show();
}

/**
 * Shows root of HTTP server; configuration screen.
*/
void wsHandleRoot() {
  setColor (0, 255, 0);
  yield();

  String resp = "<html><head><title>Cheerlights lamp configuration</title>";
  resp += "<meta name=\"viewport\" content=\"initial-scale=1.0, width = device-width, user-scalable = no\">";
  resp += "</head><body>";
  resp += "<h1>Cheerlights lamp configuration</h1>";
  resp += "<form method=\"post\" action=\"/update\" id=\"form\">";
  resp += "<table border=\"0\" cellspacing=\"0\" cellpadding=\"5\">";
  resp += "<tr><td>AP SSID:</td><td><input type=\"text\" name=\"apName\" value=\"" + String(config.apName) + "\" maxlength=\"24\"></td><td></td></tr>";
  resp += "<tr><td>AP Password:</td><td><input type=\"password\" name=\"password\" value=\"" + String(config.password) + "\" maxlength=\"48\"></td><td></td></tr>";
  resp += "<tr><td>Update frequency:</td><td><input type=\"text\" name=\"sampleFrequency\" value=\"" + String(config.updateFrequency) + "\"></td><td></td></tr>";
  resp += "<tr><td>Smooth update:</td><td><input type=\"checkbox\" name=\"smoothUpdate\" " + (config.smoothUpdate ? String("checked") : String("")) + "></td><td></td></tr>";
  resp += "<tr><td>ThingSpeak host:</td><td><input type=\"text\" name=\"tsHost\" value=\"" + String(config.tsHost) + "\" maxlength=\"40\"></td><td></td></tr>";
  resp += "<tr><td>ThingSpeak port:</td><td><input type=\"text\" name=\"tsPort\" value=\"" + String(config.tsPort) + "\"></td><td></td></tr>";
  resp += "<tr><td>ThingSpeak channel:</td><td><input type=\"text\" name=\"tsChannel\" value=\"" + String(config.tsChannel) + "\"></td><td></td></tr>";
  resp += "<tr><td>ThingSpeak field:</td><td><input type=\"text\" name=\"tsField\" value=\"" + String(config.tsField) + "\"></td><td></td></tr>";
  resp += "<tr><td colspan=\"3\" align=\"center\"><input type=\"submit\"></td></tr>";
  resp += "</table></form>";
  resp += "<p><a href=\"/run\">Start cheerlights...</a></p>";
  resp += "</body></html>";

  server->send (200, "text/html", resp);

  lastInteractionTime = millis();
}

/**
 * Helper routine; updates config key.
 *
 * @param c key.
 * @param len max length.
 * @param val value.
*/
void updateConfigKey (char *c, int len, String val) {
  memset (c, 0, len);
  sprintf (c, "%s", val.c_str());
}

/**
 * Handles configuration update. POST from main config form.
*/
void wsHandleUpdate() {
  setColor (0, 255, 0);
  yield();

  String apName = server->arg ("apName");
  String password = server->arg ("password");
  int updateFrequency = atoi (server->arg ("sampleFrequency").c_str());
  String tsHost = server->arg ("tsHost");
  unsigned int tsPort = atoi (server->arg ("tsPort").c_str());
  unsigned long tsChannel = atol (server->arg ("tsChannel").c_str());
  int tsField = atoi (server->arg ("tsField").c_str());
  bool smoothUpdate = server->arg ("smoothUpdate") == String("1");

  if (apName.length() > 1) {
    updateConfigKey (config.apName, 24, apName);
    updateConfigKey (config.password, 48, password);
    config.updateFrequency = constrain (updateFrequency, MIN_UPDATE_FREQUENCY, MAX_UPDATE_FREQUENCY);
    updateConfigKey (config.tsHost, 40, tsHost);
    config.tsPort = constrain (tsPort, 1, 65535);
    config.tsChannel = tsChannel;
    config.tsField = tsField;
    config.smoothUpdate = smoothUpdate;
  
    // store configuration
    EEPROM.begin (sizeof (ClConfiguration));
    EEPROM.put (0, config);
    EEPROM.end();
  
    String resp = "<script>window.alert ('Configuration updated...'); window.location.replace ('/');</script>";
    server->send (200, "text/html", resp);
  } else {
    server->send (200, "text/html", "");
  }

  lastInteractionTime = millis();
}

/**
 * Handles end configuring; start cheerlighting :)
*/
void wsHandleRun() {
  setColor (0, 255, 0);
  configExecuted = true;

  String resp = "<script>window.alert ('Cheerlights started...'); window.location.replace ('/');</script>";
  server->send (200, "text/html", resp);

  lastInteractionTime = millis();
}

/**
 * Reconects WIFI.
*/
void reconnectWifi() {
  Serial.println ("WiFi disconnected...");
  WiFi.disconnect();
  WiFi.mode (WIFI_STA);
  delay (2000);

  Serial.print ("Connecting to "); Serial.print (config.apName); Serial.print (' ');
  WiFi.begin (config.apName, config.password);
  delay (5000);
  while (WiFi.status() != WL_CONNECTED) {
    yield();
    setColor (100, 0, 100);
    delay(500);
    setColor (100, 0, 0);
    delay (500);
    Serial.print (".");
  }
  setColor (0, 100, 0);
  delay (500);
  Serial.println();
  Serial.println ("WiFi connected...");
}

/**
 * Updates color. Performs only one color step per once. Multiple calls
 * aproximates target color.
*/
void updateColor() {
  int redDiff, greenDiff, blueDiff;
  if (config.smoothUpdate) {
    redDiff = constrain (reqPixelColor.red - actPixelColor.red, -1, 1);
    greenDiff = constrain (reqPixelColor.green - actPixelColor.green, -1, 1);
    blueDiff = constrain (reqPixelColor.blue - actPixelColor.blue, -1, 1);
  } else {
    redDiff = reqPixelColor.red - actPixelColor.red;
    greenDiff = reqPixelColor.green - actPixelColor.green;
    blueDiff = reqPixelColor.blue - actPixelColor.blue;
  }
  
  if ((redDiff != 0) || (greenDiff != 0) || (blueDiff != 0)) {
    actPixelColor.red += redDiff;
    actPixelColor.green += greenDiff;
    actPixelColor.blue += blueDiff;
    
    setColor();
  }
}

/**
 * Main loop.
*/
void thingSpeakLoop() {
  // close configuration server
  server->close();

  // connect to configured WIFI AP
  reconnectWifi();

  // Number of errors
  int errorCount = 0;

  while (true) {
    // once per cycle update color to reqPixelColor
    updateColor();

    if (millis() - lastInteractionTime > (long)config.updateFrequency * 1000L) {
      if (WiFi.status() == WL_CONNECTED) {
        WiFiClient client;

        // connect to ThingSpeak
        if (client.connect (config.tsHost, config.tsPort)) {
          // send request
          String req = String("GET /channels/") + String(config.tsChannel) + String("/field/")
            + String (config.tsField) + String("/last.txt HTTP/1.1\r\n")
            + String("Host: ") + String (config.tsHost) + String ("\r\nConnection: close\r\n\r\n");
          client.print (req);
          Serial.println (req);

          bool isError = false;
          
          // wait HTTP_CONNECT_TIMEOUT for response
          unsigned long connectStartTime = millis();
          while (client.available() == 0) {
            if (millis() - connectStartTime > HTTP_CONNECT_TIMEOUT) {
              errorCount++;
              isError = true;
              break;
            }

            yield();
          }
          
          // read response lines
          unsigned long readStartTime = millis();
          while (!isError) {
            String resp = client.readStringUntil ('\n');

            if ((resp[0] == '#') && (resp.length() == 8)) {
              unsigned long color = strtoul (resp.c_str() + 1, NULL, 16);

              Serial.print ("Readed color: ");
              Serial.println (color, HEX);
              
              reqPixelColor.red =   (color & 0xFF0000) >> 16;
              reqPixelColor.green = (color & 0x00FF00) >>  8;
              reqPixelColor.blue =  (color & 0x0000FF);

              errorCount = 0;
              break;
            }

            if (millis() - readStartTime > HTTP_READ_TIMEOUT) {
              errorCount++;
              isError = true;
            }
          }

          // Disconnect client
          client.stop();
        } else {
          errorCount++;
        }
      } else {
        errorCount++;
      }

      // when error count reaches ERR_COUNT_FOR_RECONNECT, reconnect WiFi.
      if (errorCount > ERR_COUNT_FOR_RECONNECT) {
        errorCount = 0;
        reconnectWifi();
      }

      lastInteractionTime = millis();
    }
   
    yield();
    delay (COLOR_CHANGE_DELAY);
  }
}

/**
 * Loop function.
*/
void loop() {
  static unsigned long lastUpdate = millis();
  static bool state = false;
  
  if (configExecuted) {
    setColor (0, 0, 0);
    thingSpeakLoop();
  } else {
    boolean configValid = (strlen (config.apName) > 0);
    unsigned long curMillis = millis();

    if (!configValid || (curMillis - lastInteractionTime < CONFIG_TIMEOUT)) {

      // Handle HTTP requests
      yield();
      server->handleClient();

      if (curMillis - lastUpdate > 1000) {
        state = !state;

        if (state) {
          setColor (100, 100, 0);
        } else {
          setColor (0, 100, 100);
        }

        lastUpdate = curMillis;
      }
    } else {
      configExecuted = true; 
    }
  }
}


