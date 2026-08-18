#include <WiFi.h>
#include <WebServer.h>
#include <ArduinoJson.h>
#include <LiquidCrystal_I2C.h>
#include <Preferences.h>
#include <time.h>

// ================== HARDWARE ==================
#define BUZZER_PIN       25   // active HIGH via transistor
#define ACK_BTN_PIN      26   // optional, tombol ACK ke GND
#define CFG_BTN_PIN      27   // tombol config paksa AP mode ke GND (hold 5 detik)

// ================== LCD ==================
LiquidCrystal_I2C lcd(0x27, 20, 4);

// ================== WEB ==================
WebServer server(80);
Preferences prefs;

// ================== CONFIG ==================
struct MasterConfig {
  String wifiSsid;
  String wifiPass;
  String siteName;
  bool valid;
};

MasterConfig cfg;
bool apMode = false;
String apSsid;

// ================== DATA ==================
struct SlaveState {
  bool active = false;
  bool online = false;
  String location = "UNSET";
  unsigned long lastSeenMs = 0;
  String lastEvent = "-";
  String lastTime = "-";
};

SlaveState slaves[4]; // index 1..3
String recentLog[50];
int logCount = 0;

int scrollIndex = 1;
unsigned long lastLcdUpdate = 0;
unsigned long lastScroll = 0;
bool buzzerSilenced = false;

// =====================================================
// UTIL
// =====================================================
String nowStr() {
  struct tm timeinfo;
  if (!getLocalTime(&timeinfo)) return "NO_TIME";
  char buf[24];
  strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S", &timeinfo);
  return String(buf);
}

void addLog(const String &msg) {
  if (logCount < 50) recentLog[logCount++] = msg;
  else {
    for (int i = 1; i < 50; i++) recentLog[i - 1] = recentLog[i];
    recentLog[49] = msg;
  }
}

int activeCount() {
  int c = 0;
  for (int i = 1; i <= 3; i++) if (slaves[i].active) c++;
  return c;
}

void updateBuzzer() {
  if (activeCount() > 0 && !buzzerSilenced) digitalWrite(BUZZER_PIN, HIGH);
  else digitalWrite(BUZZER_PIN, LOW);
}

void saveConfig(const String& ssid, const String& pass, const String& siteName) {
  prefs.begin("mastercfg", false);
  prefs.putString("ssid", ssid);
  prefs.putString("pass", pass);
  prefs.putString("site", siteName);
  prefs.putBool("valid", true);
  prefs.end();
}

void loadConfig() {
  prefs.begin("mastercfg", true);
  cfg.wifiSsid = prefs.getString("ssid", "");
  cfg.wifiPass = prefs.getString("pass", "");
  cfg.siteName = prefs.getString("site", "POS SECURITY");
  cfg.valid = prefs.getBool("valid", false);
  prefs.end();
}

void clearConfig() {
  prefs.begin("mastercfg", false);
  prefs.clear();
  prefs.end();
  cfg.wifiSsid = "";
  cfg.wifiPass = "";
  cfg.siteName = "POS SECURITY";
  cfg.valid = false;
}

// =====================================================
// LCD
// =====================================================
void lcdShowNormal() {
  int act = activeCount();

  lcd.clear();
  lcd.setCursor(0, 0);
  if (act > 0) lcd.print("!!! PANIC ALERT !!!");
  else         lcd.print("SYSTEM NORMAL");

  lcd.setCursor(0, 1);
  lcd.print("Site:");
  String s = cfg.siteName;
  if (s.length() > 14) s = s.substring(0, 14);
  lcd.print(s);

  lcd.setCursor(0, 2);
  if (act > 0) {
    int tries = 0;
    while (!slaves[scrollIndex].active && tries < 4) {
      scrollIndex++;
      if (scrollIndex > 3) scrollIndex = 1;
      tries++;
    }
    lcd.print("Lokasi: ");
    String loc = slaves[scrollIndex].location;
    if (loc.length() > 12) loc = loc.substring(0, 12);
    lcd.print(loc);
  } else {
    lcd.print("Aktif: 0");
  }

  lcd.setCursor(0, 3);
  lcd.print("S1:");
  lcd.print(slaves[1].active ? "ALM" : "OK ");
  lcd.print(" S2:");
  lcd.print(slaves[2].active ? "ALM" : "OK ");
  lcd.print(" S3:");
  lcd.print(slaves[3].active ? "ALM" : "OK ");
}

void lcdShowAPInfo() {
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("MASTER AP SETUP");
  lcd.setCursor(0, 1);
  lcd.print("SSID:");
  String ss = apSsid;
  if (ss.length() > 14) ss = ss.substring(0, 14);
  lcd.print(ss);
  lcd.setCursor(0, 2);
  lcd.print("IP: 192.168.4.1");
  lcd.setCursor(0, 3);
  lcd.print("Open via browser");
}

// =====================================================
// WEB PAGES
// =====================================================
void handleRootNormal() {
  String html = "<!doctype html><html><head><meta name='viewport' content='width=device-width,initial-scale=1'>";
  html += "<meta http-equiv='refresh' content='2'>";
  html += "<title>Panic Master</title>";
  html += "<style>body{font-family:Arial;margin:16px;} .card{border:1px solid #ccc;padding:10px;margin:8px 0;border-radius:8px;} .alm{background:#ffdddd;} .ok{background:#ddffdd;} button{padding:8px 12px;margin:4px;} a{display:inline-block;margin:8px 0;}</style>";
  html += "</head><body>";
  html += "<h2>PANIC MASTER DASHBOARD</h2>";
  html += "<p><b>Site:</b> " + cfg.siteName + "</p>";
  html += "<p><b>IP Master:</b> " + WiFi.localIP().toString() + "</p>";
  html += "<p><b>Active Alarm:</b> " + String(activeCount()) + "</p>";
  html += "<form action='/silence' method='post'><button>Silence Buzzer</button></form>";
  html += "<form action='/unsilence' method='post'><button>Unsilence</button></form>";
  html += "<form action='/resetall' method='post'><button>Reset All Alarm</button></form>";
  html += "<a href='/config'>WiFi/Config Master</a>";

  for (int i = 1; i <= 3; i++) {
    String cls = slaves[i].active ? "alm" : "ok";
    html += "<div class='card " + cls + "'>";
    html += "<b>Slave " + String(i) + "</b><br>";
    html += "Lokasi: " + slaves[i].location + "<br>";
    html += "Status: " + String(slaves[i].active ? "PANIC" : "NORMAL") + "<br>";
    html += "Online: " + String(slaves[i].online ? "YES" : "NO") + "<br>";
    html += "Last Event: " + slaves[i].lastEvent + "<br>";
    html += "Waktu: " + slaves[i].lastTime + "<br>";
    html += "</div>";
  }

  html += "<h3>Recent Log</h3><ul>";
  for (int i = logCount - 1; i >= 0; i--) html += "<li>" + recentLog[i] + "</li>";
  html += "</ul></body></html>";
  server.send(200, "text/html", html);
}

void handleConfigPage() {
  String html = "<!doctype html><html><head><meta name='viewport' content='width=device-width,initial-scale=1'><title>Master Config</title></head><body>";
  html += "<h3>Config Master</h3>";
  html += "<form method='POST' action='/saveconfig'>";
  html += "SSID WiFi:<br><input name='ssid' value='" + cfg.wifiSsid + "'><br>";
  html += "Password WiFi:<br><input name='pass' type='password' value='" + cfg.wifiPass + "'><br>";
  html += "Nama Site/Pos:<br><input name='site' value='" + cfg.siteName + "'><br><br>";
  html += "<button type='submit'>Simpan & Reboot</button>";
  html += "</form><br>";
  html += "<form method='POST' action='/clearconfig' onsubmit=\"return confirm('Hapus config?');\">";
  html += "<button type='submit'>Clear Config (Masuk AP Setup)</button>";
  html += "</form>";
  html += "</body></html>";
  server.send(200, "text/html", html);
}

void handleSaveConfig() {
  String ssid = server.arg("ssid");
  String pass = server.arg("pass");
  String site = server.arg("site");
  if (ssid.length() < 1 || site.length() < 1) {
    server.send(400, "text/plain", "SSID dan Nama Site wajib diisi");
    return;
  }

  saveConfig(ssid, pass, site);
  server.send(200, "text/html", "<h3>Tersimpan. Reboot...</h3>");
  delay(1000);
  ESP.restart();
}

void handleClearConfig() {
  clearConfig();
  server.send(200, "text/html", "<h3>Config dihapus. Reboot ke AP setup...</h3>");
  delay(1000);
  ESP.restart();
}

// =====================================================
// API EVENT DARI SLAVE
// =====================================================
void handleEvent() {
  if (!server.hasArg("plain")) {
    server.send(400, "application/json", "{\"ok\":false,\"msg\":\"No body\"}");
    return;
  }

  StaticJsonDocument<256> doc;
  DeserializationError err = deserializeJson(doc, server.arg("plain"));
  if (err) {
    server.send(400, "application/json", "{\"ok\":false,\"msg\":\"Bad JSON\"}");
    return;
  }

  int id = doc["slave_id"] | 0;
  String location = doc["location"] | "UNSET";
  String eventType = doc["event"] | "";

  if (id < 1 || id > 3) {
    server.send(400, "application/json", "{\"ok\":false,\"msg\":\"slave_id invalid\"}");
    return;
  }

  slaves[id].location = location;
  slaves[id].online = true;
  slaves[id].lastSeenMs = millis();
  slaves[id].lastEvent = eventType;
  slaves[id].lastTime = nowStr();

  if (eventType == "PANIC") {
    slaves[id].active = true;
    buzzerSilenced = false;
    addLog(nowStr() + " - PANIC S" + String(id) + " " + location);
  } else if (eventType == "CLEAR") {
    slaves[id].active = false;
    addLog(nowStr() + " - CLEAR S" + String(id) + " " + location);
  } else if (eventType == "HEARTBEAT") {
    // keep online only
  }

  updateBuzzer();
  server.send(200, "application/json", "{\"ok\":true}");
}

// =====================================================
// CONTROL
// =====================================================
void handleSilence() {
  buzzerSilenced = true;
  updateBuzzer();
  server.sendHeader("Location", "/");
  server.send(303);
}

void handleUnsilence() {
  buzzerSilenced = false;
  updateBuzzer();
  server.sendHeader("Location", "/");
  server.send(303);
}

void handleResetAll() {
  for (int i = 1; i <= 3; i++) slaves[i].active = false;
  buzzerSilenced = false;
  updateBuzzer();
  addLog(nowStr() + " - RESET ALL BY MASTER");
  server.sendHeader("Location", "/");
  server.send(303);
}

// =====================================================
// WIFI/AP
// =====================================================
bool connectSTA() {
  WiFi.mode(WIFI_STA);
  WiFi.begin(cfg.wifiSsid.c_str(), cfg.wifiPass.c_str());

  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("Connecting WiFi...");
  lcd.setCursor(0, 1);
  lcd.print(cfg.wifiSsid.substring(0, 20));

  int t = 0;
  while (WiFi.status() != WL_CONNECTED && t < 40) { // 20 detik
    delay(500);
    lcd.setCursor(t % 20, 2);
    lcd.print(".");
    t++;
  }

  if (WiFi.status() == WL_CONNECTED) {
    configTime(7 * 3600, 0, "pool.ntp.org", "time.nist.gov");
    addLog(nowStr() + " - WIFI CONNECTED " + WiFi.localIP().toString());
    return true;
  }
  return false;
}

void startAPMode() {
  apMode = true;
  WiFi.mode(WIFI_AP);
  uint32_t chip = (uint32_t)ESP.getEfuseMac();
  apSsid = "PANIC-MASTER-" + String((uint16_t)(chip & 0xFFFF), HEX);
  WiFi.softAP(apSsid.c_str(), "12345678");

  lcdShowAPInfo();
  addLog("AP MODE: " + apSsid + " IP:192.168.4.1");
}

void setupRoutes() {
  server.on("/", HTTP_GET, []() {
    if (apMode) handleConfigPage();
    else handleRootNormal();
  });

  server.on("/config", HTTP_GET, handleConfigPage);
  server.on("/saveconfig", HTTP_POST, handleSaveConfig);
  server.on("/clearconfig", HTTP_POST, handleClearConfig);

  server.on("/api/event", HTTP_POST, handleEvent);
  server.on("/silence", HTTP_POST, handleSilence);
  server.on("/unsilence", HTTP_POST, handleUnsilence);
  server.on("/resetall", HTTP_POST, handleResetAll);

  server.begin();
}

// =====================================================
// SETUP / LOOP
// =====================================================
void setup() {
  Serial.begin(115200);

  pinMode(BUZZER_PIN, OUTPUT);
  digitalWrite(BUZZER_PIN, LOW);

  pinMode(ACK_BTN_PIN, INPUT_PULLUP);
  pinMode(CFG_BTN_PIN, INPUT_PULLUP);

  lcd.init();
  lcd.backlight();
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("Booting Master...");

  loadConfig();

  // Cek tombol config saat boot (hold)
  unsigned long holdStart = millis();
  while (digitalRead(CFG_BTN_PIN) == LOW) {
    if (millis() - holdStart > 3000) {
      clearConfig();
      break;
    }
    delay(50);
  }

  // Jika belum ada config -> AP mode
  if (!cfg.valid || cfg.wifiSsid.length() == 0) {
    startAPMode();
    setupRoutes();
    return;
  }

  // Coba konek STA
  bool ok = connectSTA();
  if (!ok) {
    startAPMode();
  } else {
    apMode = false;
    lcd.clear();
    lcd.setCursor(0, 0);
    lcd.print("WiFi Connected");
    lcd.setCursor(0, 1);
    lcd.print(WiFi.localIP().toString());
    lcd.setCursor(0, 2);
    lcd.print(cfg.siteName.substring(0, 20));
  }

  setupRoutes();
}

void loop() {
  server.handleClient();

  // Tombol ACK fisik: tekan = silence buzzer
  static unsigned long ackStart = 0;
  bool ackPressed = (digitalRead(ACK_BTN_PIN) == LOW);
  if (ackPressed && ackStart == 0) ackStart = millis();
  if (!ackPressed && ackStart > 0) {
    if (millis() - ackStart > 50) {
      buzzerSilenced = true;
      updateBuzzer();
      addLog(nowStr() + " - SILENCE BY ACK BUTTON");
    }
    ackStart = 0;
  }

  // Tombol config runtime (hold 5 detik) => AP setup
  static unsigned long cfgHold = 0;
  bool cfgPressed = (digitalRead(CFG_BTN_PIN) == LOW);
  if (cfgPressed && cfgHold == 0) cfgHold = millis();
  if (!cfgPressed) cfgHold = 0;
  if (cfgPressed && (millis() - cfgHold > 5000)) {
    addLog(nowStr() + " - FORCE AP MODE");
    clearConfig();
    delay(300);
    ESP.restart();
  }

  // timeout online slave
  for (int i = 1; i <= 3; i++) {
    if (millis() - slaves[i].lastSeenMs > 30000) slaves[i].online = false;
  }

  // update LCD periodik
  if (millis() - lastLcdUpdate > 1000) {
    lastLcdUpdate = millis();

    if (!apMode) {
      if (millis() - lastScroll > 2000) {
        lastScroll = millis();
        scrollIndex++;
        if (scrollIndex > 3) scrollIndex = 1;
      }
      lcdShowNormal();
    }
  }

  updateBuzzer();
}
