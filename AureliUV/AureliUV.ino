/*
Kod skapat av Oliver för AureliUV
Koden används för AureliUV gymnasiearbete 2025-2026
Viktig Information om koppling!:
SDA/SCL Kopplingar MÅSTE vara ihop satta genom breadboard eller lödade pins!
OLED Skärm - 5V, SDA, SCL
GPS - 3.3V, RX, TX
UV Sensor - 3.3V, SDA, SCL
RTC - 3.3V, SDA, SCL (ANVÄND EJ 32K och SQW ORSAKAR PROBLEM!)
ARDUINO MÅSTE ANVÄNDAS MED EN SNABB DATA-USB KABEL - FUNGERAR EJ UTAN DATA-KABEL!
*/

#include <NMEAGPS.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <Wire.h>
#include <RtcDS3231.h>
#include <SparkFun_AS7331.h>

#define gpsPort Serial1
NMEAGPS gps;

#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
#define OLED_ADDR 0x3C

Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, -1);

SfeAS7331ArdI2C myUV;

RtcDS3231<TwoWire> Rtc(Wire);

unsigned long lastUpdate = 0;
const unsigned long interval = 5000;

// temporära koordinater som används för uppstart
const float approxLat = 56.0528;
const float approxLon = 12.6932;

// Erythemal Action Spectrum formula för UV-Skala
float calculateUVI(float uva, float uvb) {
  float uvi = (uvb * 0.04) + (uva * 0.002);
  if (uvi < 0) uvi = 0;
  return uvi;
}

// Error problemsökning för RTC
bool wasError(const char* errorTopic = "") {
  uint8_t error = Rtc.LastError();
  if (error != 0) {
    Serial.print("[");
    Serial.print(errorTopic);
    Serial.print("] Kommunikationsproblem med kablarna (");
    Serial.print(error);
    Serial.print(") : ");

    switch (error) {
      case Rtc_Wire_Error_None:
        Serial.println("(inget)");
        break;
      case Rtc_Wire_Error_TxBufferOverflow:
        Serial.println("buffer overflow problem");
        break;
      case Rtc_Wire_Error_NoAddressableDevice:
        Serial.println("ingen enhet svarade");
        break;
      case Rtc_Wire_Error_UnsupportedRequest:
        Serial.println("enhet supportar inte denna request");
        break;
      case Rtc_Wire_Error_Unspecific:
        Serial.println("ospecifierad problem");
        break;
      case Rtc_Wire_Error_CommunicationTimeout:
        Serial.println("kommunikation har timat ut");
        break;
    }
    return true;
  }
  return false;
}

void setup() {
  Wire.begin();
  Wire.setClock(100000);

  Serial.begin(115200);
  gpsPort.begin(9600);

  Serial.print("kompilerat: ");
  Serial.print(__DATE__);
  Serial.println(__TIME__);

  RtcDateTime compiled = RtcDateTime(__DATE__, __TIME__);
  printDateTime(compiled);
  Serial.println();

  Rtc.Begin();
  if (!Rtc.IsDateTimeValid()) {
    if (!wasError("setup IsDateTimeValid")) {
      Serial.println("RTC har förlorat självförtroende i databasen");
      Rtc.SetDateTime(compiled);
    }
  }
  
  if (!Rtc.GetIsRunning()) {
    if (!wasError("setup GetIsRunning")) {
      Serial.println("RTC är inte på, startar nu!");
      Rtc.SetIsRunning(true);
    }
  }

  RtcDateTime now = Rtc.GetDateTime();
  if (!wasError("setup GetDateTime")) {
    if (now < compiled) {
      Serial.println("RTC är äldre än compile tiden, uppdaterar DateTime");
      Rtc.SetDateTime(compiled);
    } else if (now > compiled) {
      Serial.println("RTC är nyare än compile tiden, detta är normalt");
    } else if (now == compiled) {
      Serial.println("RTC är samma som compile tiden, troligtvis normalt");
    }
  }

  Rtc.Enable32kHzPin(false);
  wasError("setup Enable32kHzPin");
  Rtc.SetSquareWavePin(DS3231SquareWavePin_ModeNone);
  wasError("setup SetSquareWavePin");

  if (!display.begin(SSD1306_SWITCHCAPVCC, OLED_ADDR)) {
    Serial.println(F("SSD1306 misslyckades"));
    while (true);
  }

  display.clearDisplay();
  display.setTextColor(SSD1306_WHITE);
  display.setTextSize(1);

  myUV.begin();
  delay(10);
  myUV.setGain(2);
  myUV.setConversionTime(50);
}

void drawDisplay(gps_fix fix, RtcDateTime now, RtcTemperature temp, float uvi) { //OLED Display kod
  float lat = fix.valid.location ? fix.latitude() : 56.0528;
  float lon = fix.valid.location ? fix.longitude() : 16.6932;

  char dtString[26];
  formatDateTime(now, dtString, sizeof(dtString));

  display.clearDisplay();
  display.setCursor(0, 0);

  display.print("LAT: ");
  display.println(lat, 4);

  display.print("LON: ");
  display.println(lon, 4);

  display.print("UV Index: ");
  display.println(uvi, 1);

  display.print("Temp: ");
  display.print(temp.AsFloatDegC(), 1);
  display.println(" C");

  display.println(dtString);

  display.setCursor(0, 54);
  if (fix.valid.location) display.print("GPS OK ");
  else display.print("GPS... ");
  display.print("|");
  if (uvi > 0) display.print("UV OK");
  else display.print("UV ?");

  display.display();
}
 
void loop() {
  static gps_fix fix;
  static unsigned long lastUpdate = 0;
  static float uvi = 0;

  // NEO-6M GPS, läser av GPS-koordinater
  while (gps.available(gpsPort)) {
    fix = gps.read();

    if (fix.valid.location) {
      Serial.print("GPS OK  Lat: ");
      Serial.print(fix.latitude(), 6);
      Serial.print("  Lon: ");
      Serial.println(fix.longitude(), 6);
    } else {
    Serial.println("GPS söker satelliter...");
    }

    if (fix.valid.satellites) {
      Serial.print("Satelliter: ");
      Serial.println(fix.satellites);
    }
  }

  RtcDateTime now = Rtc.GetDateTime();
  RtcTemperature temp = Rtc.GetTemperature();

  Serial.print("RTC tid: ");
  printDateTime(now);
  Serial.println();
  delay(1000);

  if (millis() - lastUpdate >= interval) {
    lastUpdate = millis();
    myUV.prepareMeasurement();
    delay(myUV.getConversionTimeMillis() + 5);
    myUV.readAllUV();
    float uva = myUV.getUVA();
    float uvb = myUV.getUVB();
    uvi = calculateUVI(uva, uvb);

    Serial.print("UVA: ");
    Serial.print(uva);
    Serial.print("  UVB: ");
    Serial.print(uvb);
    Serial.print("  UVI: ");
    Serial.println(uvi);
  }
  drawDisplay(fix, now, temp, uvi);
}

#define countof(a) (sizeof(a) / sizeof(a[0]))

void printDateTime(const RtcDateTime& dt) { // möjlighet för att printa ut tiden
  char datestring[26];

  snprintf_P(datestring, 
      countof(datestring),
      PSTR("%02u/%02u/%04u %02u:%02u:%02u"),
      dt.Month(),
      dt.Day(),
      dt.Year(),
      dt.Hour(),
      dt.Minute(),
      dt.Second() );
    Serial.print(datestring);
}

void formatDateTime(const RtcDateTime& dt, char* buffer, size_t len) { // formaterar tiden till en fungerande String
  snprintf_P(buffer, len,
  PSTR("%02u/%02u/%04u %02u:%02u:%02u"),
  dt.Month(),
  dt.Day(),
  dt.Year(),
  dt.Hour(),
  dt.Minute(),
  dt.Second());
}