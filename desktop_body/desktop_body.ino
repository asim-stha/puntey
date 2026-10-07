// Desktop Body firmware — ESP32 (Arduino core 3.x)
// Libs: Adafruit SSD1306, Adafruit GFX, WebSockets (Markus Sattler)
#include <WiFi.h>
#include <WebServer.h>
#include <WebSocketsServer.h>
#include <LittleFS.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>

const char* SSID = "SK_72B0_2.4G";
const char* PASS = "HIB14@6291";

#define BUZ 18
const int IN[4] = {14, 27, 26, 25};          // ULN2003 IN1..IN4
Adafruit_SSD1306 d(128, 64, &Wire, -1);
WebServer http(80);
WebSocketsServer ws(81);

// ---------- state ----------
volatile int expr = 0;      // 0 neutral 1 happy 2 sad 3 angry 4 sleepy 5 surprised 6 love 7 think 8 music
volatile bool talking = false;
volatile int look = 0;

// ---------- stepper (28BYJ-48, 4096 half-steps/rev) ----------
const uint8_t SEQ[8][4] = {{1,0,0,0},{1,1,0,0},{0,1,0,0},{0,1,1,0},{0,0,1,0},{0,0,1,1},{0,0,0,1},{1,0,0,1}};
const float SPD = 4096.0 / 360.0;
long pos = 0, target = 0;
int ph = 0;
unsigned long lastUs = 0, lastMove = 0;
bool coils = false;

void stepperRun() {
  if (pos == target) {
    if (coils && millis() - lastMove > 300) { for (int i = 0; i < 4; i++) digitalWrite(IN[i], 0); coils = false; }
    return;
  }
  if (micros() - lastUs < 1800) return;
  lastUs = micros();
  int dir = target > pos ? 1 : -1;
  pos += dir; ph = (ph + dir + 8) % 8;
  for (int i = 0; i < 4; i++) digitalWrite(IN[i], SEQ[ph][i]);
  coils = true; lastMove = millis();
}

// ---------- music (passive buzzer) ----------
const int N_C4 = 262, N_D4 = 294, N_E4 = 330, N_F4 = 349, N_G4 = 392, N_A4 = 440;
struct Note { uint16_t f, ms; };
const Note twinkle[] = {{N_C4,400},{N_C4,400},{N_G4,400},{N_G4,400},{N_A4,400},{N_A4,400},{N_G4,800},{N_F4,400},{N_F4,400},{N_E4,400},{N_E4,400},{N_D4,400},{N_D4,400},{N_C4,800}};
const Note ode[] = {{N_E4,400},{N_E4,400},{N_F4,400},{N_G4,400},{N_G4,400},{N_F4,400},{N_E4,400},{N_D4,400},{N_C4,400},{N_C4,400},{N_D4,400},{N_E4,400},{N_E4,600},{N_D4,200},{N_D4,800}};
const Note jingle[] = {{N_E4,300},{N_E4,300},{N_E4,600},{N_E4,300},{N_E4,300},{N_E4,600},{N_E4,300},{N_G4,300},{N_C4,450},{N_D4,150},{N_E4,1000}};
const Note* SONG[] = {twinkle, ode, jingle};
const uint8_t LEN[] = {14, 15, 11};
int song = -1, ni = 0; unsigned long nt = 0; bool gap = false;

void playSong(int s) {
  ledcWriteTone(BUZ, 0);
  if (s < 0 || s > 2) { song = -1; if (expr == 8) expr = 1; return; }
  song = s; ni = 0; nt = 0; gap = false; expr = 8;
}
void musicRun() {
  if (song < 0 || millis() < nt) return;
  if (!gap) {
    if (ni >= LEN[song]) { playSong(-1); return; }
    ledcWriteTone(BUZ, SONG[song][ni].f);
    nt = millis() + SONG[song][ni].ms * 85 / 100; gap = true;
  } else {
    ledcWriteTone(BUZ, 0);
    nt = millis() + SONG[song][ni].ms * 15 / 100; ni++; gap = false;
  }
}

// ---------- face ----------
void thick(int x0, int y0, int x1, int y1) { for (int i = -1; i <= 1; i++) d.drawLine(x0, y0 + i, x1, y1 + i, 1); }
void heart(int cx, int cy) { d.fillCircle(cx-6, cy-3, 7, 1); d.fillCircle(cx+6, cy-3, 7, 1); d.fillTriangle(cx-13, cy, cx+13, cy, cx, cy+14, 1); }

void face() {
  unsigned long t = millis();
  d.clearDisplay();
  int lx = 34 + look, rx = 94 + look, y = 24;
  bool blink = (t % 4200) < 140;
  int h = blink ? 3 : 28;
  int e = expr;
  if (e == 0 || e == 2 || e == 3) {
    d.fillRoundRect(lx-14, y-h/2, 28, h, 6, 1);
    d.fillRoundRect(rx-14, y-h/2, 28, h, 6, 1);
    if (!blink && e == 2) {          // sad: outer-top cut
      d.fillTriangle(lx-15, y-15, lx, y-15, lx-15, y-2, 0);
      d.fillTriangle(rx+15, y-15, rx, y-15, rx+15, y-2, 0);
    }
    if (!blink && e == 3) {          // angry: inner-top cut
      d.fillTriangle(lx+15, y-15, lx, y-15, lx+15, y-2, 0);
      d.fillTriangle(rx-15, y-15, rx, y-15, rx-15, y-2, 0);
    }
  } else if (e == 1 || e == 8) {     // happy ^ ^
    thick(lx-12, y+8, lx, y-6); thick(lx, y-6, lx+12, y+8);
    thick(rx-12, y+8, rx, y-6); thick(rx, y-6, rx+12, y+8);
    if (e == 8) {                    // bobbing note
      int by = 44 + ((t / 200) % 2) * 3;
      d.fillCircle(112, by, 3, 1); d.drawLine(115, by, 115, by-10, 1); d.drawLine(115, by-10, 119, by-8, 1);
    }
  } else if (e == 4) {               // sleepy
    d.fillRoundRect(lx-14, y+2, 28, 6, 3, 1); d.fillRoundRect(rx-14, y+2, 28, 6, 3, 1);
    d.setTextColor(1); d.setTextSize(1 + (t / 700) % 2); d.setCursor(108, 4); d.print("z");
  } else if (e == 5) {               // surprised
    d.drawCircle(lx, y, 13, 1); d.drawCircle(rx, y, 13, 1);
    d.fillCircle(lx, y, 5, 1); d.fillCircle(rx, y, 5, 1);
  } else if (e == 6) {               // love
    heart(lx, y-2); heart(rx, y-2);
  } else if (e == 7) {               // think
    d.fillRoundRect(lx-8, y-16, 20, 20, 5, 1); d.fillRoundRect(rx-8, y-16, 20, 20, 5, 1);
    d.setTextColor(1); d.setTextSize(1); d.setCursor(100, 50);
    for (int i = 0; i < (int)((t / 400) % 4); i++) d.print(".");
  }
  // mouth
  if (talking) {
    int mh = 3 + ((t / 80) % 4) * 3;
    d.fillRoundRect(54, 50, 20, mh, 3, 1);
  } else if (e == 1 || e == 6 || e == 8) {
    d.drawLine(54, 50, 60, 55, 1); d.drawLine(60, 55, 68, 55, 1); d.drawLine(68, 55, 74, 50, 1);
  } else if (e == 2) {
    d.drawLine(54, 56, 60, 51, 1); d.drawLine(60, 51, 68, 51, 1); d.drawLine(68, 51, 74, 56, 1);
  } else if (e != 5) {
    d.fillRect(58, 54, 12, 2, 1);
  }
  d.display();
}

void dispTask(void*) { for (;;) { face(); vTaskDelay(50 / portTICK_PERIOD_MS); } }

// ---------- commands: "x:1" expr | "t:1" talk | "n:5" nudge deg | "r:45" rotate deg
// ---------- "p:90" absolute deg | "m:0" song (-1 stop) | "l:3" eye look
void handle(String s) {
  if (s.length() < 3) return;
  char c = s[0]; String a = s.substring(2); float f = a.toFloat();
  if (c == 'x') expr = constrain(a.toInt(), 0, 8);
  else if (c == 't') talking = a.toInt();
  else if (c == 'n' || c == 'r') target = pos + lround(f * SPD);
  else if (c == 'p') {
    long cur = ((pos % 4096) + 4096) % 4096;
    long want = lround(fmod(f + 360, 360) * SPD);
    long dl = want - cur;
    if (dl > 2048) dl -= 4096;
    if (dl < -2048) dl += 4096;
    target = pos + dl;
  }
  else if (c == 'm') playSong(a.toInt());
  else if (c == 'l') look = constrain(a.toInt(), -8, 8);
}

void onWs(uint8_t n, WStype_t type, uint8_t* p, size_t len) {
  if (type == WStype_TEXT) handle(String((char*)p));
}

// ---------- storage for AI memory + faces (readable from phone) ----------
void store() {
  http.sendHeader("Access-Control-Allow-Origin", "*");
  String k = http.arg("k");
  if (k != "mem" && k != "faces") { http.send(400, "text/plain", "bad"); return; }
  String p = "/" + k + ".json";
  if (http.method() == HTTP_POST) {
    File f = LittleFS.open(p, "w"); f.print(http.arg("plain")); f.close();
    http.send(200, "text/plain", "ok");
  } else {
    if (!LittleFS.exists(p)) { http.send(200, "application/json", k == "mem" ? "{}" : "[]"); return; }
    File f = LittleFS.open(p, "r"); http.streamFile(f, "application/json"); f.close();
  }
}

const char PAGE[] PROGMEM = R"HTML(<!doctype html><meta name=viewport content="width=device-width,initial-scale=1">
<body style="font:16px sans-serif;background:#111;color:#eee;padding:12px"><h3>Desktop Body</h3><div id=bx></div>
<script>
const w=new WebSocket('ws://'+location.hostname+':81');const s=c=>w.send(c);
const B=(t,c)=>{const e=document.createElement('button');e.textContent=t;e.style.cssText='margin:4px;padding:12px';e.onclick=()=>s(c);bx.append(e)};
['neutral','happy','sad','angry','sleepy','surprised','love','think'].forEach((n,i)=>B(n,'x:'+i));
B('<< 45','r:-45');B('45 >>','r:45');B('Front','p:0');B('Back','p:180');B('Spin','r:360');
B('Song1','m:0');B('Song2','m:1');B('Song3','m:2');B('Stop','m:-1');
</script><h4>AI memory</h4><textarea id=m rows=14 style="width:100%"></textarea><br><button onclick="sv()">Save</button>
<script>fetch('/store?k=mem').then(r=>r.text()).then(t=>m.value=t);function sv(){fetch('/store?k=mem',{method:'POST',body:m.value}).then(()=>alert('saved'))}</script>)HTML";

void setup() {
  for (int i = 0; i < 4; i++) pinMode(IN[i], OUTPUT);
  ledcAttach(BUZ, 2000, 8);
  LittleFS.begin(true);
  Wire.begin(21, 22); Wire.setClock(400000);
  d.begin(SSD1306_SWITCHCAPVCC, 0x3C);
  d.setTextColor(1); d.setTextSize(1);
  d.clearDisplay(); d.setCursor(0, 0); d.print("Connecting WiFi..."); d.display();
  WiFi.begin(SSID, PASS);
  while (WiFi.status() != WL_CONNECTED) delay(300);
  d.clearDisplay(); d.setCursor(0, 0); d.println("Desktop Body"); d.println(WiFi.localIP()); d.display();
  delay(2500);
  http.on("/", []() { http.send_P(200, "text/html", PAGE); });
  http.on("/store", HTTP_ANY, store);
  http.begin();
  ws.begin(); ws.onEvent(onWs);
  xTaskCreatePinnedToCore(dispTask, "disp", 4096, NULL, 1, NULL, 0);
}

void loop() {
  http.handleClient();
  ws.loop();
  stepperRun();
  musicRun();
}
