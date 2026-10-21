
#include <WiFi.h>
#include <WebServer.h>
#include <Preferences.h>
#include "esp_netif.h"

WebServer server(80);
Preferences prefs;

String staSsid, staPass, apSsid, apPass;
int txIdx = 3;
bool natOn = false;
bool throttled = false;
float chipTemp = 0;
unsigned long lastTemp = 0;
unsigned long lastTry = 0;
unsigned long lostSince = 0;

const wifi_power_t TX_LEVELS[] = {
  WIFI_POWER_8_5dBm, WIFI_POWER_11dBm, WIFI_POWER_15dBm,
  WIFI_POWER_17dBm, WIFI_POWER_19_5dBm
};
const char *TX_NAMES[] = {
  "8.5 dBm (coolest, short range)", "11 dBm", "15 dBm",
  "17 dBm (default)", "19.5 dBm (max range, hottest)"
};
const int TX_COUNT = 5;

String htmlEsc(const String &s) {
  String o;
  for (size_t i = 0; i < s.length(); i++) {
    char c = s[i];
    if (c == '"') o += "&quot;";
    else if (c == '<') o += "&lt;";
    else if (c == '>') o += "&gt;";
    else if (c == '&') o += "&amp;";
    else if (c == '\'') o += "&#39;";
    else o += c;
  }
  return o;
}

String urlEnc(const String &s) {
  String o;
  char buf[4];
  for (size_t i = 0; i < s.length(); i++) {
    uint8_t c = (uint8_t)s[i];
    if ((c >= '0' && c <= '9') || (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') ||
        c == '-' || c == '_' || c == '.') {
      o += (char)c;
    } else {
      snprintf(buf, sizeof(buf), "%%%02X", c);
      o += buf;
    }
  }
  return o;
}

void loadSettings() {
  prefs.begin("ext", true);
  staSsid = prefs.getString("staS", "");
  staPass = prefs.getString("staP", "");
  apSsid  = prefs.getString("apS", "ESP32_Extender");
  apPass  = prefs.getString("apP", "12345678");
  txIdx   = prefs.getInt("tx", 3);
  prefs.end();
  if (txIdx < 0 || txIdx >= TX_COUNT) txIdx = 3;
}

void saveSettings() {
  prefs.begin("ext", false);
  prefs.putString("staS", staSsid);
  prefs.putString("staP", staPass);
  prefs.putString("apS", apSsid);
  prefs.putString("apP", apPass);
  prefs.putInt("tx", txIdx);
  prefs.end();
}

void applyPower() {
  if (throttled) {
    setCpuFrequencyMhz(80);
    WiFi.setTxPower(WIFI_POWER_8_5dBm);
  } else {
    setCpuFrequencyMhz(160);
    WiFi.setTxPower(TX_LEVELS[txIdx]);
  }
}

void checkTemp() {
  if (millis() - lastTemp < 5000) return;
  lastTemp = millis();
  float t = temperatureRead();
  if (isnan(t)) return;
  chipTemp = t;
  if (!throttled && chipTemp >= 70.0) {
    throttled = true;
    applyPower();
  } else if (throttled && chipTemp <= 60.0) {
    throttled = false;
    applyPower();
  }
}

void enableNat() {
  if (natOn) return;
  esp_netif_t *ap = esp_netif_get_handle_from_ifkey("WIFI_AP_DEF");
  if (!ap) return;
  esp_netif_dns_info_t dns;
  dns.ip.type = IPADDR_TYPE_V4;
  dns.ip.u_addr.ip4.addr = esp_ip4addr_aton("8.8.8.8");
  esp_netif_dhcps_stop(ap);
  esp_netif_set_dns_info(ap, ESP_NETIF_DNS_MAIN, &dns);
  uint8_t offer = 1;
  esp_netif_dhcps_option(ap, ESP_NETIF_OP_SET, ESP_NETIF_DOMAIN_NAME_SERVER, &offer, sizeof(offer));
  esp_netif_dhcps_start(ap);
  natOn = (esp_netif_napt_enable(ap) == ESP_OK);
}

void handleRoot() {
  String ssidVal = server.hasArg("s") ? server.arg("s") : staSsid;
  String h;
  h.reserve(4000);
  h += "<!doctype html><html><head><meta name=viewport content='width=device-width,initial-scale=1'>";
  h += "<title>WiFi Extender</title><style>body{font-family:sans-serif;max-width:420px;margin:20px auto;padding:0 12px}";
  h += "input,button,select{width:100%;padding:10px;margin:6px 0;box-sizing:border-box;font-size:16px}";
  h += "button{background:#2a7;color:#fff;border:0;border-radius:6px}</style></head><body>";
  h += "<h2>WiFi Extender</h2><p><b>Status:</b> ";
  if (WiFi.status() == WL_CONNECTED) {
    h += "Connected to " + htmlEsc(WiFi.SSID()) + " (IP " + WiFi.localIP().toString() + ")";
  } else {
    h += "Not connected";
  }
  h += "<br><b>NAT:</b> ";
  h += natOn ? "ON" : "OFF";
  h += "<br><b>Clients:</b> " + String(WiFi.softAPgetStationNum());
  h += "<br><b>Chip temp:</b> " + String(chipTemp, 1) + " C";
  if (throttled) h += " (TOO HOT - power reduced)";
  h += "<br><b>CPU:</b> " + String(getCpuFrequencyMhz()) + " MHz</p>";
  h += "<form method=POST action=/save>";
  h += "<h3>Main WiFi (internet source)</h3>";
  h += "<input name=ssid placeholder='WiFi name' value=\"" + htmlEsc(ssidVal) + "\">";
  h += "<input name=pass type=password placeholder='WiFi password (empty = keep saved)'>";
  h += "<h3>Extender WiFi (what you share)</h3>";
  h += "<input name=assid placeholder='Extender name' value=\"" + htmlEsc(apSsid) + "\">";
  h += "<input name=apass type=password placeholder='Extender password (min 8, empty = open)' value=\"" + htmlEsc(apPass) + "\">";
  h += "<h3>WiFi power (range vs heat)</h3><select name=tx>";
  for (int i = 0; i < TX_COUNT; i++) {
    h += "<option value=" + String(i) + (i == txIdx ? " selected" : "") + ">" + TX_NAMES[i] + "</option>";
  }
  h += "</select>";
  h += "<button type=submit>Save &amp; Restart</button></form>";
  h += "<form method=GET action=/scan><button type=submit style='background:#357'>Scan WiFi list</button></form>";
  h += "</body></html>";
  server.send(200, "text/html", h);
}

void handleScanStart() {
  WiFi.scanDelete();
  WiFi.scanNetworks(true);
  server.sendHeader("Location", "/scanresult");
  server.send(303, "text/plain", "");
}

void handleScanResult() {
  int n = WiFi.scanComplete();
  String h = "<!doctype html><html><head><meta name=viewport content='width=device-width,initial-scale=1'>";
  if (n == WIFI_SCAN_RUNNING) h += "<meta http-equiv=refresh content='3'>";
  h += "</head><body style='font-family:sans-serif;padding:0 12px'><h3>Nearby WiFi</h3>";
  if (n == WIFI_SCAN_RUNNING) {
    h += "<p>Scanning... please wait</p>";
  } else if (n < 0) {
    h += "<p>Scan failed. <a href=/scan>Try again</a></p>";
  } else {
    if (n == 0) h += "<p>No networks found.</p>";
    for (int i = 0; i < n; i++) {
      h += "<p><a href='/?s=" + urlEnc(WiFi.SSID(i)) + "'>" + htmlEsc(WiFi.SSID(i)) + "</a> (" + String(WiFi.RSSI(i)) + " dBm)</p>";
    }
    h += "<p><a href=/scan>Scan again</a></p>";
  }
  h += "<p><a href=/>Back</a></p></body></html>";
  server.send(200, "text/html", h);
}

void handleSave() {
  String newSsid = server.arg("ssid");
  String newPass = server.arg("pass");
  // empty password + same WiFi name = keep the saved password
  if (newPass.length() > 0 || newSsid != staSsid) staPass = newPass;
  staSsid = newSsid;
  apSsid = server.arg("assid");
  apPass = server.arg("apass");
  txIdx = server.arg("tx").toInt();
  if (txIdx < 0 || txIdx >= TX_COUNT) txIdx = 3;
  if (apSsid.length() == 0) apSsid = "ESP32_Extender";
  if (apPass.length() > 0 && apPass.length() < 8) apPass = "12345678";
  saveSettings();
  server.send(200, "text/html", "<meta name=viewport content='width=device-width'><h3>Saved. Restarting...</h3>"
                                "<p>Reconnect to the extender WiFi, then open 192.168.4.1</p>");
  delay(1500);
  ESP.restart();
}

void setup() {
  Serial.begin(115200);
  setCpuFrequencyMhz(160);
  delay(300);
  loadSettings();

  WiFi.mode(WIFI_AP_STA);
  WiFi.setAutoReconnect(true);   // WiFi retries by itself, no manual reconnect spam
  WiFi.softAP(apSsid.c_str(), apPass.length() ? apPass.c_str() : nullptr);
  if (staSsid.length()) WiFi.begin(staSsid.c_str(), staPass.c_str());
  applyPower();

  server.on("/", HTTP_GET, handleRoot);
  server.on("/save", HTTP_POST, handleSave);
  server.on("/scan", handleScanStart);
  server.on("/scanresult", handleScanResult);
  server.begin();
  Serial.println("Settings page: http://192.168.4.1");
}

void loop() {
  server.handleClient();
  checkTemp();

  if (WiFi.status() == WL_CONNECTED) {
    lostSince = 0;
    if (!natOn) {
      enableNat();
      Serial.printf("Connected, NAT %s, IP %s\n", natOn ? "ON" : "FAILED", WiFi.localIP().toString().c_str());
    }
  } else {
    natOn = false;
    if (lostSince == 0) lostSince = millis();
    // Safety net only: after 60 s without internet, and only when nobody is
    // connected to our hotspot, start a fresh connect attempt (it can briefly
    // interrupt the hotspot, so never do it while a phone is connected).
    if (staSsid.length() && millis() - lostSince > 60000 && millis() - lastTry > 60000 &&
        WiFi.softAPgetStationNum() == 0) {
      lastTry = millis();
      WiFi.begin(staSsid.c_str(), staPass.c_str());
    }
  }
  delay(2);
}
