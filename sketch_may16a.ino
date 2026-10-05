#include <Arduino_GFX_Library.h>
#include <WiFi.h>
#include <WebServer.h>
#include <Adafruit_NeoPixel.h>
#include <Preferences.h>
#include "Image.h"
#include "##YOUR PATH HERE##/libraries/Adafruit_GFX_Library/Fonts/FreeSansBold24pt7b.h"
#include "##YOUR PATH HERE##/libraries/Adafruit_GFX_Library/Fonts/FreeSansBold18pt7b.h"

// --- Display setup ---
#define TFT_BL 7
#define SCREEN_W  240
#define SCREEN_H  280

// --- LED setup ---
#define NUM_LEDS 24
#define LED_PIN_CIRCLE 6
#define LED_PIN_HEX    5
#define LED_MAX_BRIGHTNESS 0.7f
Adafruit_NeoPixel stripCircle(NUM_LEDS, LED_PIN_CIRCLE, NEO_GRB + NEO_KHZ800);
Adafruit_NeoPixel stripHex(NUM_LEDS, LED_PIN_HEX, NEO_GRB + NEO_KHZ800);

Arduino_DataBus *bus1 = new Arduino_HWSPI(9, 10, 12, 11);
Arduino_DataBus *bus2 = new Arduino_HWSPI(9,  1, 12, 11);
Arduino_DataBus *bus3 = new Arduino_HWSPI(9,  2, 12, 11);

Arduino_GFX *gfx1 = new Arduino_ST7789(bus1,  8, 0, true, 240, 280,  0,  20);
Arduino_GFX *gfx2 = new Arduino_ST7789(bus2, 17, 0, true, 172, 320, 34,  0);
Arduino_GFX *gfx3 = new Arduino_ST7789(bus3, 18, 0, true, 240, 280,  0,  20);

// --- PSRAM tint buffer ---
uint16_t* tintBuffer = nullptr;

// --- Preferences ---
Preferences prefs;

// --- Hotspot config ---
const char* ssid  = "DMask";
String apPassword = "dmask1234";

WebServer server(80);

// --- State ---
String currentSeries = "circle";
String marqueeText   = "0001";
String activeLED     = "circle";

unsigned long lastFrameTime   = 0;
unsigned long lastLedFadeTime = 0;

// --- Colour state — default DE2185 ---
uint16_t marqueeColour = (27 << 11) | (8 << 5) | 16;
uint8_t  ledR          = 0xDE;
uint8_t  ledG          = 0x21;
uint8_t  ledB          = 0x85;
float    ledBrightness = 1.0f;

// --- LED crossfade state ---
float ledCrossfade    = 1.0f;
float ledCrossfadeDir = 0.0f;

// --- Animation state ---
float animBrightness = 0.0f;
float animDir        = 1.0f;

// --- Image arrays ---
const unsigned char* heartFrame = gImage_WHNG1;
const unsigned char* circFrame  = gImage_WCNG1;
const unsigned char* hexFrame   = gImage_WHENG1;

// --- Forward declarations ---
void drawText();

// --- Web page ---
const char PAGE[] PROGMEM = R"rawliteral(
<!DOCTYPE html>
<html>
<head>
  <meta name="viewport" content="width=device-width, initial-scale=1">
  <title>DMask Control</title>
  <style>
    body { background:#111; color:#de2185; font-family:sans-serif; text-align:center; padding:30px; }
    h1 { font-size:2em; margin-bottom:30px; }
    h2 { font-size:1.2em; margin-top:30px; margin-bottom:10px; }
    button {
      background:#222; color:#de2185; border:2px solid #de2185;
      padding:15px 30px; margin:10px; font-size:1.2em;
      border-radius:8px; cursor:pointer; width:160px;
    }
    button.active { background:#de2185; color:#111; }
    input[type=text], input[type=password] {
      background:#222; color:#de2185; border:2px solid #de2185;
      padding:12px; font-size:1.1em; border-radius:8px;
      width:80%; margin-top:20px;
    }
    input[type=color] {
      width:80px; height:50px; border:2px solid #de2185;
      border-radius:8px; cursor:pointer; background:none;
      margin-top:20px;
    }
    input[type=range] {
      width:80%; accent-color:#de2185; margin-top:10px;
    }
    #sendBtn, #colourBtn, #passBtn {
      background:#de2185; color:#111; border:none;
      padding:12px 24px; font-size:1.1em;
      border-radius:8px; cursor:pointer; margin-top:10px;
    }
    #status { margin-top:20px; font-size:0.9em; opacity:0.7; }
    #brightVal { font-size:0.9em; margin-top:5px; opacity:0.7; }
    #charWarning { font-size:0.85em; margin-top:5px; min-height:1.2em; }
  </style>
</head>
<body>
  <h1>DMask Control</h1>

  <h2>Expression</h2>
  <div>
    <button id="btnHeart" onclick="setSeries('heart')">Heart</button>
    <button id="btnCircle" onclick="setSeries('circle')">Circle</button>
  </div>

  <h2>Display Text</h2>
  <input type="text" id="mtext" placeholder="Enter text..." maxlength="80"
    oninput="checkLength(this.value)">
  <div id="charWarning"></div>
  <br>
  <button id="sendBtn" onclick="sendText()">Send Text</button>

  <h2>Colour</h2>
  <input type="color" id="colourPicker" value="#de2185">
  <br>
  <button id="colourBtn" onclick="sendColour()">Apply Colour</button>

  <h2>LED Brightness</h2>
  <input type="range" id="brightSlider" min="0" max="100" value="100"
    oninput="sendBrightness(this.value)">
  <div id="brightVal">100%</div>

  <h2>LED Ring</h2>
  <div>
    <button id="btnCircleLED" onclick="setLED('circle')">Circle</button>
    <button id="btnHexLED" onclick="setLED('hex')">Hex</button>
  </div>

  <h2>Wi-Fi Password</h2>
  <input type="password" id="newPass" placeholder="New password (min 8 chars)" maxlength="32">
  <br>
  <button id="passBtn" onclick="setPassword()">Change Password</button>

  <div id="status"></div>

  <script>
    function checkLength(v) {
      const warn = document.getElementById('charWarning');
      if (v.length === 0) {
        warn.innerText = '';
      } else if (v.length <= 8) {
        warn.style.color = '#00cc66';
        warn.innerText = v.length + ' characters fit on one line';
      } else if (v.length <= 16) {
        warn.style.color = '#ffaa00';
        warn.innerText = v.length + ' characters will wrap to two lines';
      } else {
        warn.style.color = '#ff4444';
        warn.innerText = v.length + ' characters may be cut off';
      }
    }

    function setSeries(s) {
      fetch('/series?v=' + s)
        .then(r => r.text())
        .then(t => {
          document.getElementById('btnHeart').className  = s === 'heart'  ? 'active' : '';
          document.getElementById('btnCircle').className = s === 'circle' ? 'active' : '';
          document.getElementById('status').innerText = 'Series: ' + s;
        });
    }

    function sendText() {
      let txt = document.getElementById('mtext').value;
      fetch('/text?v=' + encodeURIComponent(txt))
        .then(r => r.text())
        .then(t => { document.getElementById('status').innerText = 'Text sent'; });
    }

    function sendColour() {
      let hex = document.getElementById('colourPicker').value;
      let r = parseInt(hex.slice(1,3), 16);
      let g = parseInt(hex.slice(3,5), 16);
      let b = parseInt(hex.slice(5,7), 16);
      fetch('/colour?r=' + r + '&g=' + g + '&b=' + b)
        .then(r => r.text())
        .then(t => { document.getElementById('status').innerText = 'Colour applied'; });
    }

    function sendBrightness(v) {
      document.getElementById('brightVal').innerText = v + '%';
      fetch('/brightness?v=' + v);
    }

    function setLED(s) {
      fetch('/ledselect?v=' + s)
        .then(r => r.text())
        .then(t => {
          document.getElementById('btnCircleLED').className = s === 'circle' ? 'active' : '';
          document.getElementById('btnHexLED').className    = s === 'hex'    ? 'active' : '';
          document.getElementById('status').innerText = 'LED ring: ' + s;
        });
    }

    function setPassword() {
      let p = document.getElementById('newPass').value;
      if (p.length < 8) {
        document.getElementById('status').innerText = 'Password must be at least 8 characters';
        return;
      }
      fetch('/setpassword?v=' + encodeURIComponent(p))
        .then(r => r.text())
        .then(t => {
          document.getElementById('status').innerText = 'Password changed — reconnect with new password';
          document.getElementById('newPass').value = '';
        });
    }

    fetch('/state').then(r=>r.json()).then(d=>{
      document.getElementById('btnHeart').className  = d.series === 'heart'  ? 'active' : '';
      document.getElementById('btnCircle').className = d.series === 'circle' ? 'active' : '';
      document.getElementById('mtext').value = d.text;
      document.getElementById('colourPicker').value = d.colour;
      document.getElementById('brightSlider').value = d.brightness;
      document.getElementById('brightVal').innerText = d.brightness + '%';
      document.getElementById('btnCircleLED').className = d.led === 'circle' ? 'active' : '';
      document.getElementById('btnHexLED').className    = d.led === 'hex'    ? 'active' : '';
      checkLength(d.text);
    });
  </script>
</body>
</html>
)rawliteral";

// --- INIT ---
void initTintBuffer() {
  tintBuffer = (uint16_t*)ps_malloc(SCREEN_W * SCREEN_H * 2);
}

void updateLEDs() {
  float clampedBrightness = ledBrightness * LED_MAX_BRIGHTNESS;
  uint8_t r = (uint8_t)(ledR * clampedBrightness);
  uint8_t g = (uint8_t)(ledG * clampedBrightness);
  uint8_t b = (uint8_t)(ledB * clampedBrightness);

  float activeAmt   = ledCrossfade;
  float inactiveAmt = 1.0f - ledCrossfade;

  uint8_t ar = (uint8_t)(r * activeAmt);
  uint8_t ag = (uint8_t)(g * activeAmt);
  uint8_t ab = (uint8_t)(b * activeAmt);

  uint8_t ir = (uint8_t)(r * inactiveAmt);
  uint8_t ig = (uint8_t)(g * inactiveAmt);
  uint8_t ib = (uint8_t)(b * inactiveAmt);

  Adafruit_NeoPixel &activeStrip   = (activeLED == "circle") ? stripCircle : stripHex;
  Adafruit_NeoPixel &inactiveStrip = (activeLED == "circle") ? stripHex : stripCircle;

  for (int i = 0; i < NUM_LEDS; i++) {
    activeStrip.setPixelColor(i, activeStrip.Color(ar, ag, ab));
    inactiveStrip.setPixelColor(i, inactiveStrip.Color(ir, ig, ib));
  }
  activeStrip.show();
  inactiveStrip.show();
}

void updateLedCrossfade() {
  if (ledCrossfadeDir == 0.0f) return;

  unsigned long now = millis();
  if (now - lastLedFadeTime < 20) return;
  lastLedFadeTime = now;

  ledCrossfade += ledCrossfadeDir * 0.05f;

  if (ledCrossfade >= 1.0f) { ledCrossfade = 1.0f; ledCrossfadeDir = 0.0f; }
  if (ledCrossfade <= 0.0f) { ledCrossfade = 0.0f; ledCrossfadeDir = 0.0f; }

  updateLEDs();
}

// --- DRAW FRAME ---
void drawFrame(float brightness) {
  if (!tintBuffer) return;

  const unsigned char* frame;
  if (activeLED == "hex") {
    frame = hexFrame;
  } else {
    frame = (currentSeries == "heart") ? heartFrame : circFrame;
  }

  uint16_t* src = (uint16_t*)frame;
  int totalPixels = SCREEN_W * SCREEN_H;

  uint32_t tr = ledR * brightness;
  uint32_t tg = ledG * brightness;
  uint32_t tb = ledB * brightness;

  for (int i = 0; i < totalPixels; i++) {
    uint16_t px = src[i];
    px = (px >> 8) | (px << 8);

    uint32_t r5 = (px >> 11) & 0x1F;
    uint32_t g6 = (px >> 5)  & 0x3F;
    uint32_t b5 = px & 0x1F;

    uint32_t lum = ((r5 << 3) + (g6 << 2) + (b5 << 3)) / 3;

    uint32_t nr = (lum * tr) >> 11;
    uint32_t ng = (lum * tg) >> 10;
    uint32_t nb = (lum * tb) >> 11;

    tintBuffer[i] = (nr << 11) | (ng << 5) | nb;
  }

  gfx1->draw16bitRGBBitmap(0, 0, tintBuffer, SCREEN_W, SCREEN_H);
  gfx3->draw16bitRGBBitmap(0, 0, tintBuffer, SCREEN_W, SCREEN_H);
}

// --- TEXT DRAW ---
void drawText() {
  gfx2->fillScreen(0x0000);
  gfx2->setTextColor(marqueeColour);
  gfx2->setTextSize(1);
  gfx2->setTextWrap(false);

  int midY = gfx2->height() / 2;
  int spacePos = marqueeText.indexOf(' ');

  if (spacePos == -1) {
    gfx2->setFont(&FreeSansBold24pt7b);
    int16_t x1, y1; uint16_t w, h;
    gfx2->getTextBounds(marqueeText, 0, 0, &x1, &y1, &w, &h);
    const GFXfont* font = (w <= (uint16_t)gfx2->width()) ? &FreeSansBold24pt7b : &FreeSansBold18pt7b;
    int lineH = (font == &FreeSansBold24pt7b) ? 44 : 32;
    gfx2->setFont(font);
    gfx2->getTextBounds(marqueeText, 0, 0, &x1, &y1, &w, &h);
    int x = (gfx2->width() - w) / 2;
    gfx2->setCursor(x, midY + (lineH / 2));
    gfx2->print(marqueeText);
  } else {
    String line1 = marqueeText.substring(0, spacePos);
    String line2 = marqueeText.substring(spacePos + 1);
    int lineH = 32;
    int startY = midY - (lineH / 2);

    gfx2->setFont(&FreeSansBold18pt7b);
    int16_t x1, y1; uint16_t w1, h1, w2, h2;
    gfx2->getTextBounds(line1, 0, 0, &x1, &y1, &w1, &h1);
    gfx2->setCursor((gfx2->width() - w1) / 2, startY);
    gfx2->print(line1);
    gfx2->getTextBounds(line2, 0, 0, &x1, &y1, &w2, &h2);
    gfx2->setCursor((gfx2->width() - w2) / 2, startY + lineH);
    gfx2->print(line2);
  }
}

// --- Web handlers ---
void handleRoot() { server.send(200, "text/html", PAGE); }

void handleSeries() {
  if (server.hasArg("v")) {
    String val = server.arg("v");
    if (val == "heart" || val == "circle") {
      currentSeries  = val;
      animBrightness = 0.0f;
      animDir        = 1.0f;
    }
  }
  server.send(200, "text/plain", "ok");
}

void handleText() {
  if (server.hasArg("v")) {
    marqueeText = server.arg("v");
    drawText();
  }
  server.send(200, "text/plain", "ok");
}

void handleColour() {
  if (server.hasArg("r") && server.hasArg("g") && server.hasArg("b")) {
    ledR = server.arg("r").toInt();
    ledG = server.arg("g").toInt();
    ledB = server.arg("b").toInt();
    uint8_t r565 = ledR >> 3;
    uint8_t g565 = ledG >> 2;
    uint8_t b565 = ledB >> 3;
    marqueeColour = (r565 << 11) | (g565 << 5) | b565;
    updateLEDs();
    drawText();
  }
  server.send(200, "text/plain", "ok");
}

void handleBrightness() {
  if (server.hasArg("v")) {
    ledBrightness = server.arg("v").toInt() / 100.0f;
    updateLEDs();
  }
  server.send(200, "text/plain", "ok");
}

void handleLedSelect() {
  if (server.hasArg("v")) {
    String val = server.arg("v");
    if ((val == "circle" || val == "hex") && val != activeLED) {
      activeLED = val;
      ledCrossfade = 0.0f;
      ledCrossfadeDir = 1.0f;
      animBrightness = 0.0f;
      animDir = 1.0f;
    }
  }
  server.send(200, "text/plain", "ok");
}

void handleSetPassword() {
  if (server.hasArg("v")) {
    String newPass = server.arg("v");
    if (newPass.length() >= 8) {
      apPassword = newPass;
      prefs.putString("password", apPassword);
      server.send(200, "text/plain", "ok");
      delay(500);
      WiFi.softAPdisconnect(true);
      WiFi.softAP(ssid, apPassword.c_str());
    } else {
      server.send(400, "text/plain", "Password must be at least 8 characters");
    }
  }
}

void handleState() {
  char colHex[8];
  sprintf(colHex, "#%02x%02x%02x", ledR, ledG, ledB);
  int brightnessInt = (int)(ledBrightness * 100);
  String json = "{\"series\":\"" + currentSeries + "\",\"text\":\"" + marqueeText +
                "\",\"colour\":\"" + String(colHex) +
                "\",\"brightness\":" + brightnessInt +
                ",\"led\":\"" + activeLED + "\"}";
  server.send(200, "application/json", json);
}

void setup() {
  Serial.begin(115200);

  pinMode(TFT_BL, OUTPUT);
  digitalWrite(TFT_BL, HIGH);

  prefs.begin("dmask", false);
  apPassword = prefs.getString("password", "dmask1234");

  stripCircle.begin();
  stripCircle.setBrightness(255);
  stripHex.begin();
  stripHex.setBrightness(255);
  updateLEDs();

  gfx1->begin(40000000UL);
  gfx1->fillScreen(0x0000);

  gfx2->begin(40000000UL);
  gfx2->setRotation(3);
  gfx2->fillScreen(0x0000);

  gfx3->begin(40000000UL);
  gfx3->fillScreen(0x0000);

  initTintBuffer();

  WiFi.softAP(ssid, apPassword.c_str());
  Serial.print("Hotspot IP: ");
  Serial.println(WiFi.softAPIP());

  server.on("/",            handleRoot);
  server.on("/series",      handleSeries);
  server.on("/text",        handleText);
  server.on("/colour",      handleColour);
  server.on("/brightness",  handleBrightness);
  server.on("/ledselect",   handleLedSelect);
  server.on("/setpassword", handleSetPassword);
  server.on("/state",       handleState);
  server.begin();

  drawFrame(0.0f);
  drawText();
}

void loop() {
  server.handleClient();

  unsigned long now = millis();

  if (now - lastFrameTime >= 30) {
    lastFrameTime = now;

    animBrightness += animDir * 0.02f;
    if (animBrightness >= 1.0f) { animBrightness = 1.0f; animDir = -1.0f; }
    if (animBrightness <= 0.0f) { animBrightness = 0.0f; animDir =  1.0f; }

    drawFrame(animBrightness);
    updateLEDs();
  }

  updateLedCrossfade();
}
