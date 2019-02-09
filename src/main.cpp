#include <FS.h>
#include <Arduino.h>
#include <ESP8266WiFi.h>          //https://github.com/esp8266/Arduino

// Wifi Manager
#include <DNSServer.h>
#include <ESP8266WebServer.h>
#include <WiFiManager.h>         //https://github.com/tzapu/WiFiManager

// HTTP requests
#include <ESP8266HTTPClient.h>

// OTA updates
#include <ESP8266httpUpdate.h>
// Blynk
#include <BlynkSimpleEsp8266.h>

// Debounce
#include <Bounce2.h> //https://github.com/thomasfredericks/Bounce2

// JSON
#include <ArduinoJson.h>          //https://github.com/bblanchon/ArduinoJson

// GPIO Defines
#define I2C_SDA 2 // D1 Orange
#define I2C_SCL 13

 // D2 Yellow
#define HW_RESET 15

// Debounce interval in ms
#define DEBOUNCE_INTERVAL 10

Bounce hwReset {Bounce()};

// Humidity/Temperature/Pressure
#include <Adafruit_Sensor.h>

//DHT22
#include <DHT.h>
#define DHTPIN 5     // Digital pin connected to the DHT sensor 
#define DHTTYPE    DHT22     // DHT 22 (AM2302)
DHT dht(DHTPIN, DHTTYPE);

#include <Wire.h>

//WS2812b
#include <WS2812FX.h>
#define LED_PIN    4  // digital pin used to drive the LED strip
#define LED_COUNT 1  // number of LEDs on the strip

WS2812FX ws2812fx = WS2812FX(LED_COUNT, LED_PIN, NEO_GRB + NEO_KHZ400);


// Use U8g2 for i2c OLED Lib
#include <SPI.h>
#include <U8g2lib.h>
U8G2_SSD1306_128X64_NONAME_F_SW_I2C u8g2(U8G2_R0, I2C_SCL, I2C_SDA, U8X8_PIN_NONE);
byte x {0};
byte y {0};

// Handy timers
#include <SimpleTimer.h>

// SW Serial
#include <SoftwareSerial.h>

SoftwareSerial swSer(12, 14, false, 256); // GPIO15 (TX) and GPIO13 (RX)

// CO2 SERIAL
#define DEBUG_SERIAL Serial
#define SENSOR_SERIAL swSer

byte cmd[9] = {0xFF,0x01,0x86,0x00,0x00,0x00,0x00,0x00,0x79};
unsigned char response[7];



// Blynk token
char blynk_token[33] {"3baf4d0c1746411699ce4faea43ca85e"};
char blynk_server[64] {"blynk-cloud.com"};
const uint16_t blynk_port {8442};

// Device Id
char device_id[17] = "co2meter";
const char fw_ver[17] = "0.1.4";

// Handy timer
SimpleTimer timer;

// Setup Wifi connection
WiFiManager wifiManager;

// Network credentials
String ssid { "ku_" +  String(ESP.getChipId())};
String pass {"ku_" + String(ESP.getFlashChipId()) };

//flag for saving data
bool shouldSaveConfig = false;

// Sensors data
int t {-100};
int h {-1};
int co2 {-1};
float tf {0};
float pf {0};
float hf {0};

char loader[4] {'.'};

//callback notifying the need to save config
void saveConfigCallback() {
        ws2812fx.setSegment(0,  0,  0, FX_MODE_BLINK_RAINBOW, 0x87CEFA, 1000, false); //Blinking
        ws2812fx.start();

                 DEBUG_SERIAL.println("Should save config");
        shouldSaveConfig = true;
}

void factoryReset() {
                 DEBUG_SERIAL.println("Resetting to factory settings");
        wifiManager.resetSettings();
        SPIFFS.format();
        ESP.reset();
}

void printString(String str) {
                 DEBUG_SERIAL.println(str);
}

void readCO2() {
        // CO2
        bool header_found {false};
        char tries {0};

        SENSOR_SERIAL.write(cmd, 9);
        memset(response, 0, 7);

        // Looking for packet start
        while(SENSOR_SERIAL.available() && (!header_found)) {
                if(SENSOR_SERIAL.read() == 0xff ) {
                        if(SENSOR_SERIAL.read() == 0x86 ) header_found = true;
                }
        }

        if (header_found) {
                SENSOR_SERIAL.readBytes(response, 7);

                byte crc = 0x86;
                for (char i = 0; i < 6; i++) {
                        crc+=response[i];
                }
                crc = 0xff - crc;
                crc++;

                if ( !(response[6] == crc) ) {
                                 DEBUG_SERIAL.println("CO2: CRC error: " + String(crc) + " / "+ String(response[6]));
                } else {
                        unsigned int responseHigh = (unsigned int) response[0];
                        unsigned int responseLow = (unsigned int) response[1];
                        unsigned int ppm = (256*responseHigh) + responseLow;
                        co2 = ppm;
                                 DEBUG_SERIAL.println("CO2:" + String(co2));
                }
        } else {
                         DEBUG_SERIAL.println("CO2: Header not found");
        }

}

void sendMeasurements() {
        // Read data
        // Temperature
        printString("Getting Temperature from BME280");
        tf = dht.readTemperature();
        t = static_cast<float>(tf);

        // Humidity
        printString("Getting Humidity from DHT22");
        hf = dht.readHumidity();
        h = static_cast<int>(hf);

        // CO2
        printString("Getting CO2");
        readCO2();

        // Send to server
        if (tf > 10 && tf < 50) {Blynk.virtualWrite(V1, tf);}
        if (h > 10 && h < 100)  {Blynk.virtualWrite(V2, h);}
        if (co2 > 400)          {Blynk.virtualWrite(V5, co2);}

        // Write to debug console
        printString("H: " + String(hf) + "%");
        printString("T: " + String(tf) + "C");
        printString("CO2: " + String(co2) + "ppm");

        //Will set WS2812b
        if (co2 > 1400 )              {ws2812fx.setSegment(0,  0,  0, FX_MODE_BREATH, 0xFF0000, 10, false);} // > 1400 ppm
        if (co2 > 1300 && co2 < 1400) {ws2812fx.setSegment(0,  0,  0, FX_MODE_BREATH, 0xFF4500, 10, false);} // > 1300 ppm
        if (co2 > 1200 && co2 < 1300) {ws2812fx.setSegment(0,  0,  0, FX_MODE_BREATH, 0xFFA500, 10, false);} // > 1200 ppm
        if (co2 > 1100 && co2 < 1200) {ws2812fx.setSegment(0,  0,  0, FX_MODE_BREATH, 0xFFA500, 10, false);} // > 1100 ppm
        if (co2 > 1000 && co2 < 1100) {ws2812fx.setSegment(0,  0,  0, FX_MODE_BREATH, 0xFFFF00, 10, false);} // > 1000 ppm
        if (co2 > 900  && co2 < 1000) {ws2812fx.setSegment(0,  0,  0, FX_MODE_BREATH, 0x32CD32, 10, false);} // >  900 ppm
        if (co2 > 800  && co2 < 900)  {ws2812fx.setSegment(0,  0,  0, FX_MODE_BREATH, 0x00FFFF, 10, false);} // >  800 ppm
        if (co2 < 800)                {ws2812fx.setSegment(0,  0,  0, FX_MODE_BREATH, 0xFF00FF, 10, false);} // <  800 ppm
}


void loading() {
        long unsigned int count {(millis() / 500) % 4};
        memset(loader, '.', count);
        memset(&loader[count], 0, 1);
}

void draw() {
        u8g2.clearBuffer();

        // CO2
        if (co2 > -1) {
                char co2a [5];
                sprintf (co2a, "%i", co2);

                u8g2.setFont(u8g2_font_inb19_mf);
                x = (128 - u8g2.getStrWidth(co2a))/2;
                y = u8g2.getAscent() - u8g2.getDescent();
                u8g2.drawStr(x, y, co2a);

                const char ppm[] {"ppm CO2"};
                u8g2.setFont(u8g2_font_6x12_mf);
                x = (128 - u8g2.getStrWidth(ppm)) / 2;
                y = y + 2 + u8g2.getAscent() - u8g2.getDescent();
                u8g2.drawStr(x, y, ppm);
        } else {
                loading();
                u8g2.setFont(u8g2_font_inb19_mf);
                x = (128 - u8g2.getStrWidth(loader)) / 2;
                y = u8g2.getAscent() - u8g2.getDescent();
                u8g2.drawStr(x, y, loader);
        }

        // Cycle other meauserments
        String measurement {"..."};
        const char degree {176};

        // Switch every 3 seconds
        switch((millis() / 3000) % 3) {
        case 0:
                if (t > -100) { measurement = "T: " + String(tf) + degree + "C"; }
                break;
        case 1:
                if (h > -1) { measurement = "H: " + String(h) + "%"; }
                break;
        }

        char measurementa [12];
        measurement.toCharArray(measurementa, 12);

        u8g2.setFont(u8g2_font_9x18_mf);
        x = (128 - u8g2.getStrWidth(measurementa))/2;
        y = 64 + u8g2.getDescent();
        u8g2.drawStr(x, y, measurementa);

        u8g2.sendBuffer();
}

void drawBoot(String msg = "Loading...") {
        u8g2.clearBuffer();
        u8g2.setFont(u8g2_font_9x18_mf);
        x = (128 - u8g2.getStrWidth(msg.c_str())) / 2;
        y = 32 + u8g2.getAscent() / 2;
        u8g2.drawStr(x, y, msg.c_str());
        u8g2.sendBuffer();
}

void drawConnectionDetails(String ssid, String pass, String url) {
        String msg {""};
        u8g2.clearBuffer();

        msg = "Connect to WiFi:";
        u8g2.setFont(u8g2_font_7x13_mf);
        x = (128 - u8g2.getStrWidth(msg.c_str())) / 2;
        y = u8g2.getAscent() - u8g2.getDescent();
        u8g2.drawStr(x, y, msg.c_str());

        msg = "net: " + ssid;
        x = (128 - u8g2.getStrWidth(msg.c_str())) / 2;
        y = y + 1 + u8g2.getAscent() - u8g2.getDescent();
        u8g2.drawStr(x, y, msg.c_str());

        msg = "pw: "+ pass;
        x = (128 - u8g2.getStrWidth(msg.c_str())) / 2;
        y = y + 1 + u8g2.getAscent() - u8g2.getDescent();
        u8g2.drawStr(x, y, msg.c_str());

        msg = "Open browser:";
        x = (128 - u8g2.getStrWidth(msg.c_str())) / 2;
        y = y + 1 + u8g2.getAscent() - u8g2.getDescent();
        u8g2.drawStr(x, y, msg.c_str());

        // URL
        // u8g2.setFont(u8g2_font_6x12_mf);
        x = (128 - u8g2.getStrWidth(url.c_str())) / 2;
        y = y + 1 + u8g2.getAscent() - u8g2.getDescent();
        u8g2.drawStr(x, y, url.c_str());

        u8g2.sendBuffer();
}

bool loadConfig() {
                 DEBUG_SERIAL.println("Load config...");
        File configFile = SPIFFS.open("/config.json", "r");
        if (!configFile) {
                         DEBUG_SERIAL.println("Failed to open config file");
                return false;
        }

        size_t size = configFile.size();
        if (size > 1024) {
                         DEBUG_SERIAL.println("Config file size is too large");
                return false;
        }

        // Allocate a buffer to store contents of the file.
        std::unique_ptr<char[]> buf(new char[size]);

        // We don't use String here because ArduinoJson library requires the input
        // buffer to be mutable. If you don't use ArduinoJson, you may as well
        // use configFile.readString instead.
        configFile.readBytes(buf.get(), size);

        StaticJsonBuffer<200> jsonBuffer;
        JsonObject &json = jsonBuffer.parseObject(buf.get());

        if (!json.success()) {
                         DEBUG_SERIAL.println("Failed to parse config file");
                return false;
        }

        // Save parameters
        strcpy(device_id, json["device_id"]);
        strcpy(blynk_server, json["blynk_server"]);
        strcpy(blynk_token, json["blynk_token"]);
}

void configModeCallback (WiFiManager *wifiManager) {
        String url {"http://192.168.4.1"};
        printString("Connect to WiFi:");
        printString("net: " + ssid);
        printString("pw: "+ pass);
        printString("Open browser:");
        printString(url);
        printString("to setup device");

        drawConnectionDetails(ssid, pass, url);
}

void setupWiFi() {
        //set config save notify callback
        wifiManager.setSaveConfigCallback(saveConfigCallback);

        // Custom parameters
        WiFiManagerParameter custom_device_id("device_id", "Device name", device_id, 16);
        WiFiManagerParameter custom_blynk_server("blynk_server", "Blynk server", blynk_server, 64);
        WiFiManagerParameter custom_blynk_token("blynk_token", "Blynk token", blynk_token, 34);
        wifiManager.addParameter(&custom_blynk_server);
        wifiManager.addParameter(&custom_blynk_token);
        wifiManager.addParameter(&custom_device_id);

        // wifiManager.setTimeout(180);
        wifiManager.setAPCallback(configModeCallback);

        if (!wifiManager.autoConnect(ssid.c_str(), pass.c_str())) {
                         DEBUG_SERIAL.println("failed to connect and hit timeout");
        }

        //save the custom parameters to FS
        if (shouldSaveConfig) {
                         DEBUG_SERIAL.println("saving config");
                DynamicJsonBuffer jsonBuffer;
                JsonObject &json = jsonBuffer.createObject();
                json["device_id"] = custom_device_id.getValue();
                json["blynk_server"] = custom_blynk_server.getValue();
                json["blynk_token"] = custom_blynk_token.getValue();


                File configFile = SPIFFS.open("/config.json", "w");
                if (!configFile) {
                                 DEBUG_SERIAL.println("failed to open config file for writing");
                }

                json.printTo(DEBUG_SERIAL);
                json.printTo(configFile);
                configFile.close();
                //end save
        }

        //if you get here you have connected to the WiFi
                 DEBUG_SERIAL.println("WiFi connected");

                 DEBUG_SERIAL.print("IP address: ");
                 DEBUG_SERIAL.println(WiFi.localIP());
}

// Virtual pin update FW
BLYNK_WRITE(V22) {
        if (param.asInt() == 1) {
                         DEBUG_SERIAL.println("Got a FW update request");

                char full_version[34] {""};
                strcat(full_version, device_id);
                strcat(full_version, "::");
                strcat(full_version, fw_ver);

                t_httpUpdate_return ret = ESPhttpUpdate.update("http://romfrom.space/get", full_version);
                switch (ret) {
                case HTTP_UPDATE_FAILED:
                                 DEBUG_SERIAL.println("[update] Update failed.");
                        break;
                case HTTP_UPDATE_NO_UPDATES:
                                 DEBUG_SERIAL.println("[update] Update no Update.");
                        break;
                case HTTP_UPDATE_OK:
                                 DEBUG_SERIAL.println("[update] Update ok.");
                        break;
                }

        }
}

// Virtual pin reset settings
BLYNK_WRITE(V23) {
        factoryReset();
}

void setup() {
        // Init serial ports
                 DEBUG_SERIAL.begin(115200);
        SENSOR_SERIAL.begin(9600);

        ws2812fx.init();
        ws2812fx.setBrightness(255);
        ws2812fx.start();

        // Init I2C interface
        Wire.begin(I2C_SDA, I2C_SCL);

        // Setup HW reset
        pinMode(HW_RESET,INPUT_PULLUP);
        hwReset.interval(DEBOUNCE_INTERVAL);
        hwReset.attach(HW_RESET);

        // Init display
        u8g2.begin();
        drawBoot();
        
        //DHT22 init
        dht.begin();


        

        // Init filesystem
        if (!SPIFFS.begin()) {
                         DEBUG_SERIAL.println("Failed to mount file system");
                ESP.reset();
        }

        // Setup WiFi
        drawBoot("WiFi...");
        setupWiFi();

        // Load config
        drawBoot();
        if (!loadConfig()) {
                         DEBUG_SERIAL.println("Failed to load config");
                factoryReset();
        } else {
                         DEBUG_SERIAL.println("Config loaded");
        }

        // Start blynk

        Blynk.config(blynk_token, blynk_server, blynk_port);
                 DEBUG_SERIAL.print("blynk server: ");
                 DEBUG_SERIAL.println(  blynk_server );
                 DEBUG_SERIAL.print("port: " );
                 DEBUG_SERIAL.println(  blynk_port );
                 DEBUG_SERIAL.print("token: " );
                 DEBUG_SERIAL.println(  blynk_token  );

        drawBoot("Connecting...");
                 DEBUG_SERIAL.println("Connecting to blynk...");
        while (Blynk.connect() == false) {
          delay(500);
                   DEBUG_SERIAL.println("Connecting to blynk...");
        }

        // Setup a function to be called every 10 second
       timer.setInterval(60000L, sendMeasurements);

        sendMeasurements();
}

void loop() {
        Blynk.run();
        timer.run();
        draw();
        ws2812fx.service();
        hwReset.update();
        if (hwReset.fell()) {
           factoryReset();
         }
}
