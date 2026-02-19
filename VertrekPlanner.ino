#include <WiFi.h>
#include <WiFiManager.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <GxEPD2_BW.h>      // E-paper library
#include <Fonts/FreeSansBold18pt7b.h> // Mooier font voor vertrektijd
#include <Fonts/FreeSans9pt7b.h>     // Font voor kleine tekst
#include "time.h"
#include "storage.h"

// E-paper Pin Definities voor jouw C3 setup
#define EPD_CS    0
#define EPD_DC    1
#define EPD_RST   2
#define EPD_BUSY  5 // Het draadje naar pin 5
#define EPD_SCL   4
#define EPD_SDA   3

// Initialiseer SSD1680 display (200x200)
GxEPD2_BW<GxEPD2_154_D67, GxEPD2_154_D67::HEIGHT> display(GxEPD2_154_D67(EPD_CS, EPD_DC, EPD_RST, EPD_BUSY));

const char* ntpServer = "pool.ntp.org";
String scriptUrl; 
char van_adres[64];
char naar_adres[64];
bool shouldSaveConfig = false;
#define INFO if (Serial) Serial.printf

void saveConfigCallback() { shouldSaveConfig = true; }

// URL Encoder tool
String urlEncode(const char *s) {
  String str = s;
  String encodedString = "";
  for (int i = 0; i < str.length(); i++) {
    char c = str.charAt(i);
    if (isAlphaNumeric(c)) encodedString += c;
    else { encodedString += '%'; encodedString += String(c, HEX); }
  }
  return encodedString;
}

void enterDeepSleep(uint64_t seconds) {
  while (digitalRead(EPD_BUSY) == HIGH) {
    delay(10);
  }
  INFO("Slaap voor %llu seconden...\n", seconds);
  display.hibernate(); // Cruciaal voor e-paper levensduur!
  esp_sleep_enable_timer_wakeup(seconds * 1000000ULL);
  esp_deep_sleep_start();
}
// Functie om de datum-layout te tekenen
void toonDatumLayout(struct tm timeinfo) {
    INFO("toon datum\n");
    display.setFullWindow();
    display.firstPage();
    do {
        display.fillScreen(GxEPD_WHITE);
        display.setTextColor(GxEPD_BLACK);
        
        // 1. Dag en Maand bovenin (bijv. "19 FEB")
        display.setFont(&FreeSans9pt7b);
        char bovenkant[15];
        strftime(bovenkant, sizeof(bovenkant), "%d %b", &timeinfo);
        display.setCursor(50, 30); // Beetje uitlijnen naar wens
        display.print(bovenkant);

        // 2. Dagnaam in het midden (bijv. "DON")
        display.setFont(&FreeSansBold18pt7b); // Lekker groot font
        char dagNaam[5];
        strftime(dagNaam, sizeof(dagNaam), "%a", &timeinfo);
        // Simpele centrering voor 200x200
        display.setCursor(45, 110); 
        display.print(dagNaam);
        
    } while (display.nextPage());
}

void setup() {
  Serial.begin(115200);

  pinMode(EPD_CS, OUTPUT);
  pinMode(EPD_DC, OUTPUT);
  pinMode(EPD_RST, OUTPUT);
  pinMode(EPD_BUSY, INPUT);

  INFO("Start op\n");
  // 1. Initialiseer E-paper op C3 Hardware SPI
  SPI.begin(EPD_SCL, -1, EPD_SDA, EPD_CS);
  display.init(115200, true, 2, false);
  display.setRotation(3); 

  nvs_handle_t nvs = initNvs();
  String savedVan = getNonVolatile(nvs, "van_param");
  String savedNaar = getNonVolatile(nvs, "naar_param");
  strncpy(van_adres, savedVan.c_str(), sizeof(van_adres));
  strncpy(naar_adres, savedNaar.c_str(), sizeof(naar_adres));

  WiFiManager wm;
  wm.setSaveConfigCallback(saveConfigCallback);
  if (savedVan.isEmpty()) wm.resetSettings();

  WiFiManagerParameter custom_van("van", "Vertrek", van_adres, 64);
  WiFiManagerParameter custom_naar("naar", "Bestemming", naar_adres, 64);
  wm.addParameter(&custom_van);
  wm.addParameter(&custom_naar);

  if (!wm.autoConnect("VertrekTijdPlanner")) {
    delay(3000);
    ESP.restart();
  }

  if (shouldSaveConfig) {
    setNonVolatile(nvs, "van_param", custom_van.getValue());
    setNonVolatile(nvs, "naar_param", custom_naar.getValue());
  }

  String vanEncoded = urlEncode(custom_van.getValue());
  String naarEncoded = urlEncode(custom_naar.getValue());
  scriptUrl = "https://script.google.com/macros/s/AKfycbwDx259LxLCEk1udjipwHFzu6TmGXWtHFOgB17pChjpt1VowNhGMvsWk0sdbOh9Z3P8/exec?van=" + vanEncoded + "&naar=" + naarEncoded;

  configTime(0, 0, ntpServer);
  setenv("TZ", "CET-1CEST,M3.5.0,M10.5.0/3", 1);
  tzset();

  struct tm timeinfo;
  if (!getLocalTime(&timeinfo)) {
    enterDeepSleep(60);
  }

  esp_sleep_wakeup_cause_t wakeup_reason = esp_sleep_get_wakeup_cause();

  // Beslis of we moeten updaten of slapen
  if (wakeup_reason != ESP_SLEEP_WAKEUP_TIMER) {
    voerUpdateUit();
    delay(5000);
    enterDeepSleep(60);
  } else {
    bool isJuisteDag = (timeinfo.tm_wday >= 1 && timeinfo.tm_wday <= 5); // Ma-Vr
    if (isJuisteDag && timeinfo.tm_hour < 9) {
      if (!voerUpdateUit()) {
        toonDatumLayout(timeinfo);
      }
      enterDeepSleep(5 * 60); // Ververs elke 5 min tijdens de spits
    } else {
      toonDatumLayout(timeinfo);
      // Slaap tot volgende ochtend 06:00
      int secondenNu = (timeinfo.tm_hour * 3600) + (timeinfo.tm_min * 60);
      int doel = 6 * 3600;
      uint64_t slaap = (secondenNu < doel) ? (doel - secondenNu) : (86400 - secondenNu + doel);
      enterDeepSleep(slaap);
    }
  }
}

bool voerUpdateUit() {
    INFO("haal tijden op\n");
    HTTPClient http;
    http.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);
    http.begin(scriptUrl);
    
    int httpCode = http.GET();
    if (httpCode > 0) {
        INFO("Tijdsduur opgehaald\n");
        String payload = http.getString();
        StaticJsonDocument<300> doc;
        deserializeJson(doc, payload);

        const char* reistijdStr = doc["reistijd"]; 
        const char* vertrekStr = doc["vertrek"];   

        int reistijdMinuten = atoi(reistijdStr); 
        int vertrekUur = atoi(vertrekStr);
        int vertrekMin = atoi(vertrekStr + 3);

        struct tm timeinfo;
        getLocalTime(&timeinfo);
        int minutenOver = ((vertrekUur * 60) + vertrekMin) - ((timeinfo.tm_hour * 60) + timeinfo.tm_min);
        INFO("Reistijd: %d min\n", reistijdMinuten);
        INFO("Vertrek over %d min\n", minutenOver);

        // --- E-PAPER DRAWING ---
        display.setFullWindow();
        display.firstPage();
        do {
            display.fillScreen(GxEPD_WHITE);
            
            // Header bar
            display.fillRect(0, 0, 200, 40, GxEPD_BLACK);
            display.setFont(&FreeSans9pt7b);
            display.setTextColor(GxEPD_WHITE);
            display.setCursor(10, 25);
            display.printf("Reistijd: %d min", reistijdMinuten);

            // Grote vertrektijd in het midden
            display.setFont(&FreeSansBold18pt7b);
            display.setTextColor(GxEPD_BLACK);
            display.setCursor(45, 110);
            display.print(vertrekStr);

            // Onderste regel
            display.setFont(&FreeSans9pt7b);
            display.setCursor(35, 160);
            if(minutenOver > 0) {
              display.printf("Vertrek over %d min", minutenOver);
            }
            else display.print("Nu vertrekken!");

        } while (display.nextPage());
    } else {
      INFO("ERROR getting tijdsduur %d\n", httpCode);
    }
    http.end();
    return httpCode > 0;
}

void loop() {}