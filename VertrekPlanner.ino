#include <WiFi.h>

#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include "time.h"

// Tijd instellingen
const char* ntpServer = "pool.ntp.org";
const long  gmtOffset_sec = 3600; // Nederland (Wintertijd = 3600, Zomertijd = 7200)
const int   daylightOffset_sec = 3600; // Zet op 3600 als het zomertijd is

String urlEncode(const char *s);
void voerUpdateUit();

// Instellingen
const char* ssid = "vanPutte";
const char* password = "vanputte";
String van = urlEncode("van Maanenstraat 24, Vlaardingen, Nederland");
String naar = urlEncode("Lange Kleiweg 12, Rijswijk, Nederland");
String scriptUrl = "https://script.google.com/macros/s/AKfycbwDx259LxLCEk1udjipwHFzu6TmGXWtHFOgB17pChjpt1VowNhGMvsWk0sdbOh9Z3P8/exec?van=" + van + "&naar=" + naar;
 
// OLED Display instellingen
#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
#define SDA_PIN 8
#define SCL_PIN 9
#define I2C_ADDR 0x3C

Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, -1);

String urlEncode(const char *s) {
  String str = s;
  String encodedString = "";
  char c;
  for (int i = 0; i < str.length(); i++) {
    c = str.charAt(i);
    if (isAlphaNumeric(c)) {
      encodedString += c;
    } else {
      encodedString += '%';
      encodedString += String(c, HEX);
    }
  }
  return encodedString;
}

void enterDeepSleep(uint64_t seconds) {
  Serial.printf("Slaap voor %llu seconden...\n", seconds);
  esp_sleep_enable_timer_wakeup(seconds * 1000000ULL);
  esp_deep_sleep_start();
}

void setup() {
  Serial.begin(115200);
  delay(1000);

  // 1. Scherm en I2C initialisatie
  Wire.begin(SDA_PIN, SCL_PIN);
  if(!display.begin(SSD1306_SWITCHCAPVCC, I2C_ADDR)) {
    Serial.println(F("SSD1306 mislukt"));
  }
  display.clearDisplay();
  display.setTextColor(WHITE);

  // 2. WiFi en Tijd
  WiFi.begin(ssid, password);
  while (WiFi.status() != WL_CONNECTED) { delay(500); Serial.print("."); }
  
  configTime(0, 0, ntpServer);
  setenv("TZ", "CET-1CEST,M3.5.0,M10.5.0/3", 1); // Nederland (Winter & Zomer)
  tzset();

  struct tm timeinfo;
  if (!getLocalTime(&timeinfo)) {
    Serial.println("Tijd ophalen mislukt");
    enterDeepSleep(60);
  }

  // 3. Check Wakeup Cause
  esp_sleep_wakeup_cause_t wakeup_reason = esp_sleep_get_wakeup_cause();

  if (wakeup_reason != ESP_SLEEP_WAKEUP_TIMER) {
    // --- SCENARIO A: HARD RESET (TEST MODE) ---
    Serial.println("Handmatige reset: Toon data en slaap 1 minuut...");
    voerUpdateUit(); 
    delay(5000); 
    enterDeepSleep(60); 
  } else {
    // --- SCENARIO B: WAKKER UIT TIMER ---
    
    // tm_wday: 2 = Dinsdag, 4 = Donderdag
    bool isJuisteDag = (timeinfo.tm_wday == 2 || timeinfo.tm_wday == 4);
    bool isVoorAchtUur = (timeinfo.tm_hour < 8);

    if (isJuisteDag && isVoorAchtUur) {
      // Het is een werkdag ochtend: Update het scherm
      voerUpdateUit();
      
      // Optioneel: Slaap telkens 10 minuten om de reistijd te verversen tot 08:00
      // Of laat hem gewoon aanstaan. Voor batterij is 10 min slaap beter:
      Serial.println("Werkdag: Ververs over 3 minuten.");
      enterDeepSleep(3 * 60); 
    } else {
      // Het is geen werkdag of het is al na 08:00: Slaap tot morgen 06:00
      Serial.println("Geen actie nodig. Slaap tot morgen 06:00.");
      display.ssd1306_command(SSD1306_DISPLAYOFF);
      
      int secondenNu = (timeinfo.tm_hour * 3600) + (timeinfo.tm_min * 60) + timeinfo.tm_sec;
      int doelSeconden = 6 * 3600; // 06:00 uur
      uint64_t slaapSeconden;

      if (secondenNu < doelSeconden) {
        slaapSeconden = doelSeconden - secondenNu;
      } else {
        slaapSeconden = (24 * 3600) - secondenNu + doelSeconden;
      }
      
      enterDeepSleep(slaapSeconden);
    }
  }
}
void voerUpdateUit() {
    Serial.println("executing script " + scriptUrl);
    HTTPClient http;
    http.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);
    http.begin(scriptUrl);
    
    int httpCode = http.GET();
    if (httpCode > 0) {
        String payload = http.getString();
        StaticJsonDocument<200> doc;
        deserializeJson(doc, payload);

        const char* reistijdStr = doc["reistijd"]; // bijv "25 min"
        const char* vertrekStr = doc["vertrek"];   // bijv "08:42"

        // 1. Haal getallen uit de strings
        int reistijdMinuten = atoi(reistijdStr); 
        int vertrekUur = atoi(vertrekStr);
        int vertrekMin = atoi(vertrekStr + 3); // slaat "08:" over

        // 2. Bereken minuten tot vertrek
        struct tm timeinfo;
        getLocalTime(&timeinfo);
        int nuInMinuten = (timeinfo.tm_hour * 60) + timeinfo.tm_min;
        int vertrekInMinuten = (vertrekUur * 60) + vertrekMin;
        int minutenOver = vertrekInMinuten - nuInMinuten;

        // 3. Display update
        display.clearDisplay();
        
        // Bovenste regel: Tijd: [tt] over: [m]
        display.setTextSize(2); // Groter lettertype (was 1)
        display.setCursor(0, 0);
        display.printf("T:%d ov:%d", reistijdMinuten, minutenOver);

        display.drawLine(0, 18, 127, 18, WHITE); // Lijn iets lager wegens grotere tekst

        // Middelste regel: Vertrektijd
        display.setTextSize(3);
        display.setCursor(19, 30); 
        display.print(vertrekStr);

        display.display();
    }
    http.end();
    delay(2000);
}
void loop() {
  delay(60000);
}