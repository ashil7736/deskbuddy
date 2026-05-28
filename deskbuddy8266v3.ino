// ==================================================
// DESKBUDDY ESP8266 FINAL VERSION
// NODEMCU ESP8266 + SH1106 OLED
// ==================================================

#include <ESP8266WiFi.h>
#include <ESP8266WebServer.h>
#include <ESP8266HTTPClient.h>
#include <ArduinoJson.h>

#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SH110X.h>

#include <time.h>
#include <math.h>

#include <Fonts/FreeSans9pt7b.h>

// ==================================================
// DISPLAY CONFIG
// ==================================================

#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64

#define SDA_PIN D2
#define SCL_PIN D1
#define TOUCH_PIN D7

Adafruit_SH1106G display(
  SCREEN_WIDTH,
  SCREEN_HEIGHT,
  &Wire,
  -1
);

// ==================================================
// WIFI + WEATHER
// ==================================================

String wifiSsid    = "AG";
String wifiPass    = "123456789";

String apiKey      = "0cccca913e209caca4c7d0fa7e5c109d";

String city        = "Ernakulam";
String countryCode = "IN";

String tzString    = "IST-5:30";

const char* ntpServer = "pool.ntp.org";

// ==================================================
// WEB SERVER
// ==================================================

ESP8266WebServer configServer(80);

bool inConfigMode = false;

#define CONFIG_AP_SSID "DeskBuddy-Setup"
#define CONFIG_AP_PASS "12345678"

// ==================================================
// WEATHER DATA
// ==================================================

float temperature = 0;
String weatherMain = "Clear";
String weatherDesc = "Loading";

// ==================================================
// UI VARIABLES
// ==================================================

int currentPage = 0;

unsigned long lastWeatherUpdate = 0;

bool lastTouchState = LOW;
unsigned long lastTouchTime = 0;

bool sleeping = false;

unsigned long lastInteraction = 0;

const unsigned long SLEEP_TIME = 30000;

// ==================================================
// EYE SYSTEM
// ==================================================

struct Eye {

  float x, y;
  float w, h;

  float targetH;

  float pupilX, pupilY;
  float targetPupilX, targetPupilY;

  unsigned long nextBlinkTime;

  void init(float _x,float _y,float _w,float _h){

    x = _x;
    y = _y;

    w = _w;
    h = _h;

    targetH = _h;

    pupilX = 0;
    pupilY = 0;

    targetPupilX = 0;
    targetPupilY = 0;

    nextBlinkTime =
      millis() + random(2000,5000);
  }

  void update() {

    h += (targetH - h) * 0.2;

    pupilX +=
      (targetPupilX - pupilX) * 0.1;

    pupilY +=
      (targetPupilY - pupilY) * 0.1;
  }
};

Eye leftEye;
Eye rightEye;

// ==================================================
// WEATHER ICONS
// ==================================================

void drawWeatherIcon(String w, int x, int y) {

  if (w == "Clear") {

    display.drawCircle(x+8,y+8,7,SH110X_WHITE);

  } else if (w == "Rain") {

    display.fillCircle(x+8,y+6,5,SH110X_WHITE);

    display.drawLine(x+4,y+14,x+4,y+18,SH110X_WHITE);
    display.drawLine(x+8,y+14,x+8,y+18,SH110X_WHITE);
    display.drawLine(x+12,y+14,x+12,y+18,SH110X_WHITE);

  } else {

    display.fillCircle(x+6,y+8,5,SH110X_WHITE);
    display.fillCircle(x+12,y+8,5,SH110X_WHITE);

    display.fillRect(x+4,y+8,10,6,SH110X_WHITE);
  }
}

// ==================================================
// WEATHER API
// ==================================================

void getWeatherAndForecast() {

  if (WiFi.status() != WL_CONNECTED)
    return;

  WiFiClient client;
  HTTPClient http;

  String url =
    "http://api.openweathermap.org/data/2.5/weather?q=" +
    city + "," + countryCode +
    "&appid=" + apiKey +
    "&units=metric";

  http.begin(client, url);

  int httpCode = http.GET();

  if (httpCode > 0) {

    String payload = http.getString();

    DynamicJsonDocument doc(4096);

    DeserializationError error =
      deserializeJson(doc, payload);

    if (!error) {

      temperature =
        doc["main"]["temp"];

      weatherMain =
        doc["weather"][0]["main"].as<String>();

      weatherDesc =
        doc["weather"][0]["description"].as<String>();
    }
  }

  http.end();
}

// ==================================================
// EYES
// ==================================================

void drawEye(Eye &e) {

  int ix = (int)e.x;
  int iy = (int)e.y;

  int iw = (int)e.w;
  int ih = (int)e.h;

  display.fillRoundRect(
    ix,
    iy,
    iw,
    ih,
    8,
    SH110X_WHITE
  );

  int pw = iw / 3;
  int ph = ih / 3;

  int px =
    ix + iw/2 + e.pupilX - pw/2;

  int py =
    iy + ih/2 + e.pupilY - ph/2;

  display.fillRoundRect(
    px,
    py,
    pw,
    ph,
    3,
    SH110X_BLACK
  );
}

void updateEyes() {

  unsigned long now = millis();

  static unsigned long lastMove = 0;

  if (now - lastMove > 1200) {

    int moveX = random(-6,7);
    int moveY = random(-4,5);

    leftEye.targetPupilX = moveX;
    leftEye.targetPupilY = moveY;

    rightEye.targetPupilX = moveX;
    rightEye.targetPupilY = moveY;

    lastMove = now;
  }

  static bool blinking = false;
  static unsigned long blinkStart = 0;

  if (!blinking && now > leftEye.nextBlinkTime) {

    blinking = true;

    blinkStart = now;

    leftEye.targetH = 2;
    rightEye.targetH = 2;
  }

  if (blinking && now - blinkStart > 150) {

    leftEye.targetH = 36;
    rightEye.targetH = 36;

    blinking = false;

    leftEye.nextBlinkTime =
      now + random(2000,5000);
  }

  leftEye.update();
  rightEye.update();
}

void drawEmoPage() {

  updateEyes();

  drawEye(leftEye);
  drawEye(rightEye);
}

// ==================================================
// SLEEP EYES
// ==================================================

void drawSleepingEyes() {

  // Half closed left eye
  display.fillRoundRect(
    20,
    24,
    36,
    12,
    6,
    SH110X_WHITE
  );

  display.fillRect(
    20,
    18,
    36,
    10,
    SH110X_BLACK
  );

  // Half closed right eye
  display.fillRoundRect(
    72,
    24,
    36,
    12,
    6,
    SH110X_WHITE
  );

  display.fillRect(
    72,
    18,
    36,
    10,
    SH110X_BLACK
  );

  // Moving Z animation
  // Animated floating ZZZ
  int zOffset =
    (millis() / 300) % 14;

  display.setTextSize(1);

  display.setCursor(
    58,
    20 - zOffset
  );
  display.print("z");

  display.setCursor(
    68,
    14 - zOffset
  );
  display.print("Z");

  display.setCursor(
    80,
    8 - zOffset
  );
  display.print("z");
}

// ==================================================
// CLOCK PAGE
// ==================================================

void drawClock() {

  struct tm t;

  if (!getLocalTime(&t))
    return;

  display.setFont(&FreeSans9pt7b);

  char timeStr[12];

  int hour12 = t.tm_hour % 12;

  if (hour12 == 0)
    hour12 = 12;

  sprintf(
    timeStr,
    "%02d:%02d %s",
    hour12,
    t.tm_min,
    (t.tm_hour >= 12) ? "PM" : "AM"
  );

  int16_t x1, y1;
  uint16_t w, h;

  display.getTextBounds(
    timeStr,
    0,
    0,
    &x1,
    &y1,
    &w,
    &h
  );

  int x = (128 - w) / 2;

  display.setCursor(x,28);

  display.print(timeStr);

  display.setFont(NULL);

  char dateStr[20];

  sprintf(
    dateStr,
    "%02d/%02d/%04d",
    t.tm_mday,
    t.tm_mon + 1,
    t.tm_year + 1900
  );

  display.setCursor(30,50);

  display.print(dateStr);
}

// ==================================================
// WEATHER PAGE
// ==================================================

void drawWeatherPage() {

  display.setFont(&FreeSans9pt7b);

  display.setCursor(2,12);

  display.print(city);

  drawWeatherIcon(weatherMain,96,0);

  char tempStr[10];

  sprintf(
    tempStr,
    "%dC",
    (int)temperature
  );

  int16_t x1, y1;
  uint16_t w, h;

  display.getTextBounds(
    tempStr,
    0,
    0,
    &x1,
    &y1,
    &w,
    &h
  );

  int x = (128 - w) / 2;

  display.setCursor(x,34);

  display.print(tempStr);

  display.setFont(NULL);

  String shortDesc = weatherDesc;

  if (shortDesc.length() > 14)
    shortDesc = shortDesc.substring(0,14);

  display.setCursor(8,54);

  display.print(shortDesc);
}

// ==================================================
// BOOT ANIMATION
// ==================================================

void playBootAnimation() {

  for (int r=0;r<60;r+=4){

    display.clearDisplay();

    display.drawCircle(
      64,
      32,
      r,
      SH110X_WHITE
    );

    display.display();

    delay(15);
  }

  display.clearDisplay();

  display.setCursor(20,30);

  display.println("DeskBuddy");

  display.display();

  delay(1000);
}

// ==================================================
// CONFIG MODE
// ==================================================

void handleConfigRoot() {

  String html =
  "<html><body style='background:black;color:white;'>"
  "<h2>DeskBuddy Setup</h2>"
  "<form action='/save' method='POST'>"
  "<input name='ssid' placeholder='WiFi'><br><br>"
  "<input name='pass' placeholder='Password'><br><br>"
  "<input name='city' placeholder='City'><br><br>"
  "<button>Save</button>"
  "</form></body></html>";

  configServer.send(200,"text/html",html);
}

void handleConfigSave() {

  wifiSsid =
    configServer.arg("ssid");

  wifiPass =
    configServer.arg("pass");

  city =
    configServer.arg("city");

  configServer.send(
    200,
    "text/html",
    "<h2>Saved. Restarting...</h2>"
  );

  delay(2000);

  ESP.restart();
}

void startConfigPortal() {

  inConfigMode = true;

  WiFi.mode(WIFI_AP_STA);

  WiFi.softAP(
    CONFIG_AP_SSID,
    CONFIG_AP_PASS
  );

  configServer.on(
    "/",
    handleConfigRoot
  );

  configServer.on(
    "/save",
    HTTP_POST,
    handleConfigSave
  );

  configServer.begin();

  display.clearDisplay();

  display.setCursor(0,10);

  display.println("CONFIG MODE");

  display.println();

  display.println(CONFIG_AP_SSID);

  display.println();

  display.println("192.168.4.1");

  display.display();
}

// ==================================================
// SETUP
// ==================================================

void setup() {

  Serial.begin(115200);

  Wire.begin(
    SDA_PIN,
    SCL_PIN
  );

  pinMode(
    TOUCH_PIN,
    INPUT
  );

  display.begin(0x3C,true);

  display.clearDisplay();

  display.display();

  display.setTextColor(
    SH110X_WHITE
  );

  leftEye.init(
    18,14,36,36
  );

  rightEye.init(
    74,14,36,36
  );

  playBootAnimation();

  WiFi.begin(
    wifiSsid.c_str(),
    wifiPass.c_str()
  );

  display.clearDisplay();

  display.setCursor(0,20);

  display.println("Connecting WiFi");

  display.display();

  unsigned long startAttempt =
    millis();

  while (
    WiFi.status() != WL_CONNECTED &&
    millis() - startAttempt < 15000
  ) {

    delay(200);
  }

  if (WiFi.status() != WL_CONNECTED) {

    startConfigPortal();

    return;
  }

  configTime(
    0,
    0,
    ntpServer
  );

  setenv(
    "TZ",
    tzString.c_str(),
    1
  );

  tzset();

  getWeatherAndForecast();

  lastWeatherUpdate = millis();

  lastInteraction = millis();
}

// ==================================================
// LOOP
// ==================================================

void loop() {

  if (inConfigMode) {

    configServer.handleClient();

    return;
  }

  unsigned long now = millis();

  // Weather update
  if (
    now - lastWeatherUpdate >
    600000
  ) {

    getWeatherAndForecast();

    lastWeatherUpdate = now;
  }

  // Touch
  bool touchState =
    digitalRead(TOUCH_PIN);

  if (
    touchState == HIGH &&
    lastTouchState == LOW &&
    millis() - lastTouchTime > 300
  ) {

    sleeping = false;

    lastInteraction = millis();

    currentPage++;

    if (currentPage > 2)
      currentPage = 0;

    lastTouchTime = millis();
  }

  lastTouchState = touchState;

  // Sleep
  if (
    millis() - lastInteraction >
    SLEEP_TIME
  ) {

    sleeping = true;
  }

  display.clearDisplay();

  switch(currentPage){

    case 0:

      if (sleeping)
        drawSleepingEyes();
      else
        drawEmoPage();

      break;

    case 1:
      drawClock();
      break;

    case 2:
      drawWeatherPage();
      break;
  }

  display.display();
}
