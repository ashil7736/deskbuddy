
// ==================================================
// DESKBUDDY ESP8266 VERSION
// FULLY MODIFIED FOR NODEMCU ESP8266
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

#include <Fonts/FreeSansBold18pt7b.h>
#include <Fonts/FreeSansBold9pt7b.h>
#include <Fonts/FreeSans9pt7b.h>

// ==================================================
// DISPLAY CONFIG
// ==================================================

#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64

#define SDA_PIN D2
#define SCL_PIN D1
#define TOUCH_PIN D7

Adafruit_SH1106G display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, -1);

// ==================================================
// WIFI + WEATHER CONFIG
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
float feelsLike = 0;

int humidity = 0;

String weatherMain = "Clear";
String weatherDesc = "Loading";

struct ForecastDay {
  String dayName;
  int temp;
  String iconType;
};

ForecastDay fcast[3];

// ==================================================
// UI
// ==================================================

int currentPage = 0;

unsigned long lastWeatherUpdate = 0;
bool lastTouchState = LOW;
unsigned long lastTouchTime = 0;

// ==================================================
// EYE SYSTEM
// ==================================================

struct Eye {

  float x, y;
  float w, h;

  float targetX, targetY;
  float targetW, targetH;

  float velX, velY;
  float velW, velH;

  float pupilX, pupilY;
  float targetPupilX, targetPupilY;

  float pVelX, pVelY;

  float k = 0.12;
  float d = 0.60;

  float pk = 0.08;
  float pd = 0.50;

  bool blinking;

  unsigned long lastBlink;
  unsigned long nextBlinkTime;

  void init(float _x,float _y,float _w,float _h){

    x = targetX = _x;
    y = targetY = _y;

    w = targetW = _w;
    h = targetH = _h;

    pupilX = targetPupilX = 0;
    pupilY = targetPupilY = 0;

    nextBlinkTime = millis() + random(1000,4000);
  }

  void update() {

    float ax = (targetX - x) * k;
    float ay = (targetY - y) * k;

    float aw = (targetW - w) * k;
    float ah = (targetH - h) * k;

    velX = (velX + ax) * d;
    velY = (velY + ay) * d;

    velW = (velW + aw) * d;
    velH = (velH + ah) * d;

    x += velX;
    y += velY;

    w += velW;
    h += velH;

    float pax = (targetPupilX - pupilX) * pk;
    float pay = (targetPupilY - pupilY) * pk;

    pVelX = (pVelX + pax) * pd;
    pVelY = (pVelY + pay) * pd;

    pupilX += pVelX;
    pupilY += pVelY;
  }
};

Eye leftEye;
Eye rightEye;

// ==================================================
// SIMPLE WEATHER ICONS
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
// CONFIG PAGE
// ==================================================

void handleConfigRoot() {

  String html = R"rawliteral(

<!DOCTYPE html>
<html>

<head>

<meta name="viewport" content="width=device-width, initial-scale=1">

<style>

body{
background:#111;
color:white;
font-family:sans-serif;
padding:20px;
}

input{
width:100%;
padding:12px;
margin-top:10px;
}

button{
width:100%;
padding:14px;
margin-top:20px;
}

</style>

</head>

<body>

<h2>DeskBuddy Setup</h2>

<form action="/save" method="POST">

<input name="ssid" placeholder="WiFi Name">

<input name="pass" placeholder="WiFi Password">

<input name="city" placeholder="City">

<button type="submit">SAVE</button>

</form>

</body>
</html>

)rawliteral";

  configServer.send(200,"text/html",html);
}

void handleConfigSave() {

  wifiSsid = configServer.arg("ssid");
  wifiPass = configServer.arg("pass");

  city = configServer.arg("city");

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

  WiFi.softAP(CONFIG_AP_SSID, CONFIG_AP_PASS);

  configServer.on("/", handleConfigRoot);

  configServer.on("/save", HTTP_POST, handleConfigSave);

  configServer.begin();

  display.clearDisplay();

  display.setCursor(0,10);
  display.println("CONFIG MODE");

  display.println();
  display.println("Connect WiFi:");

  display.println(CONFIG_AP_SSID);

  display.println();
  display.println("192.168.4.1");

  display.display();
}

// ==================================================
// WEATHER
// ==================================================

void getWeatherAndForecast() {

  if (WiFi.status() != WL_CONNECTED) return;

  HTTPClient http;

  String url =
    "http://api.openweathermap.org/data/2.5/weather?q=" +
    city + "," + countryCode +
    "&appid=" + apiKey +
    "&units=metric";

  WiFiClient client;

http.begin(client, url);

  int httpCode = http.GET();

  if (httpCode == 200) {

    String payload = http.getString();

    DynamicJsonDocument doc(4096);

    deserializeJson(doc, payload);

    temperature =
      doc["main"]["temp"];

    feelsLike =
      doc["main"]["feels_like"];

    humidity =
      doc["main"]["humidity"];

    weatherMain =
      doc["weather"][0]["main"].as<String>();

    weatherDesc =
      doc["weather"][0]["description"].as<String>();
  }

  http.end();
}

// ==================================================
// DRAW EYES
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

  // Random eye movement
  static unsigned long lastMove = 0;

  if (now - lastMove > 1200) {

    int moveX = random(-6, 7);
    int moveY = random(-4, 5);

    leftEye.targetPupilX = moveX;
    leftEye.targetPupilY = moveY;

    rightEye.targetPupilX = moveX;
    rightEye.targetPupilY = moveY;

    lastMove = now;
  }

  // Blinking
  static bool blinking = false;
  static unsigned long blinkStart = 0;

  if (!blinking && now > leftEye.nextBlinkTime) {

    blinking = true;
    blinkStart = now;

    leftEye.targetH = 2;
    rightEye.targetH = 2;
  }

  // Open eyes after blink
  if (blinking && now - blinkStart > 150) {

    leftEye.targetH = 36;
    rightEye.targetH = 36;

    blinking = false;

    leftEye.nextBlinkTime =
      now + random(2000, 5000);
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
// CLOCK PAGE
// ==================================================

void drawClock() {

  struct tm t;

  if (!getLocalTime(&t)) return;

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

  display.setCursor(x,40);

  display.print(timeStr);
}

// ==================================================
// WEATHER PAGE
// ==================================================

void drawWeatherPage() {

  // Small font
  display.setFont(&FreeSans9pt7b);

  // City
  display.setCursor(2,12);
  display.print(city);

  // Icon
  drawWeatherIcon(weatherMain,96,0);

  // Temperature
  char tempStr[10];

  sprintf(tempStr, "%dC", (int)temperature);

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

  // Smaller bottom text
  display.setFont(NULL);

  // Limit long text
  String shortDesc = weatherDesc;

  if (shortDesc.length() > 14)
    shortDesc = shortDesc.substring(0,14);

  display.setCursor(8,54);
  display.print(shortDesc);
}

// ==================================================
// BOOT
// ==================================================

void playBootAnimation() {

  for (int r=0;r<60;r+=4){

    display.clearDisplay();

    display.drawCircle(64,32,r,SH110X_WHITE);

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
// SETUP
// ==================================================

void setup() {

  Serial.begin(115200);

  Wire.begin(SDA_PIN,SCL_PIN);

  pinMode(TOUCH_PIN, INPUT_PULLUP);

  display.begin(0x3C,true);

  display.clearDisplay();
  display.display();

  display.setTextColor(SH110X_WHITE);

  leftEye.init(18,14,36,36);
  rightEye.init(74,14,36,36);

  playBootAnimation();

  WiFi.begin(
    wifiSsid.c_str(),
    wifiPass.c_str()
  );

  display.clearDisplay();

  display.setCursor(0,20);

  display.println("Connecting WiFi");

  display.display();

  unsigned long startAttempt = millis();

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

  configTime(0,0,ntpServer);

  setenv("TZ", tzString.c_str(), 1);

  tzset();

  getWeatherAndForecast();

  lastWeatherUpdate = millis();
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

  if (now - lastWeatherUpdate > 600000) {

    getWeatherAndForecast();

    lastWeatherUpdate = now;
  }
bool touchState = digitalRead(TOUCH_PIN);

if (
  touchState == HIGH &&
  lastTouchState == LOW &&
  millis() - lastTouchTime > 300
) {

  currentPage++;

  if (currentPage > 2)
    currentPage = 0;

  lastTouchTime = millis();
}

lastTouchState = touchState;

  display.clearDisplay();

  switch(currentPage){

    case 0:
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

