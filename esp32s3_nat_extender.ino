 // ESP32 WiFi Extender (NAT) + Web Settings
// Settings page: connect to the extender WiFi, open http://192.168.4.1
#include <WiFi.h>
#include <WebServer.h>
#include <Preferences.h>
#include "esp_netif.h"

WebServer server(80);
Preferences prefs;

String staSsid, staPass, apSsid, apPass;
bool natOn = false;

String htmlEsc(const String &s) {
  String o;
  for (char c : s) {
    if (c == '"') o += "&quot;";
    else if (c == '<') o += "&lt;";
    else if (c == '&') o += "&amp;";
    else o += c;
  }
  return o;
}

void loadSettings() {
  prefs.begin("ext", true);
  staSsid = prefs.getString("staS", "");
  staPass = prefs.getString("staP", "");
  apSsid  = prefs.getString("apS", "ESP32_Extender");
  apPass  = prefs.getString("apP", "12345678");
  prefs.end();
}

void saveSettings() {
  prefs.begin("ext", false);
  prefs.putString("staS", staSsid);
  prefs.putString("staP", staPass);
  prefs.putString("apS", apSsid);
  prefs.putString("apP", apPass);
  prefs.end();
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
  String st = (WiFi.status() == WL_CONNECTED)
                ? "Connected to " + htmlEsc(WiFi.SSID()) + " (IP " + WiFi.localIP().toString() + ")"
                : "Not connected";
  String h = "<!doctype html><html><head><meta name=viewport content='width=device-width,initial-scale=1'>"
             "<title>WiFi Extender</title><style>body{font-family:sans-serif;max-width:420px;margin:20px auto;padding:0 12px}"
             "input,button{width:100%;padding:10px;margin:6px 0;box-sizing:border-box;font-size:16px}"
             "button{background:#2a7;color:#fff;border:0;border-radius:6px}</style></head><body>"
             "<h2>WiFi Extender</h2><p><b>Status:</b> " + st + "<br><b>NAT:</b> " + (natOn ? "ON" : "OFF") +
             "<br><b>Clients:</b> " + String(WiFi.softAPgetStationNum()) + "</p>"
             "<form method=POST action=/save>"
             "<h3>Main WiFi (internet source)</h3>"
             "<input name=ssid placeholder='WiFi name' value=\"" + htmlEsc(staSsid) + "\">"
             "<input name=pass type=password placeholder='WiFi password' value=\"" + htmlEsc(staPass) + "\">"
             "<h3>Extender WiFi (what you share)</h3>"
             "<input name=assid placeholder='Extender name' value=\"" + htmlEsc(apSsid) + "\">"
             "<input name=apass type=password placeholder='Extender password (min 8, empty = open)' value=\"" + htmlEsc(apPass) + "\">"
             "<button type=submit>Save &amp; Restart</button></form>"
             "<form method=POST action=/scan><button style='background:#357'>Scan WiFi list</button></form>"
             "</body></html>";
  server.send(200, "text/html", h);
}

void handleScan() {
  int n = WiFi.scanNetworks();
  String h = "<!doctype html><html><head><meta name=viewport content='width=device-width,initial-scale=1'></head>"
             "<body style='font-family:sans-serif'><h3>Nearby WiFi</h3>";
  for (int i = 0; i < n; i++) {
    h += htmlEsc(WiFi.SSID(i)) + " (" + String(WiFi.RSSI(i)) + " dBm)<br>";
  }
  h += "<p><a href=/>Back</a></p></body></html>";
  server.send(200, "text/html", h);
}

void handleSave() {
  staSsid = server.arg("ssid");
  staPass = server.arg("pass");
  apSsid = server.arg("assid");
  apPass = server.arg("apass");
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
  delay(300);
  loadSettings();

  WiFi.mode(WIFI_AP_STA);
  WiFi.softAP(apSsid.c_str(), apPass.length() ? apPass.c_str() : nullptr);
  if (staSsid.length()) WiFi.begin(staSsid.c_str(), staPass.c_str());

  server.on("/", HTTP_GET, handleRoot);
  server.on("/save", HTTP_POST, handleSave);
  server.on("/scan", HTTP_POST, handleScan);
  server.begin();
  Serial.println("Settings page: http://192.168.4.1");
}

unsigned long lastTry = 0;

void loop() {
  server.handleClient();
  if (WiFi.status() == WL_CONNECTED) {
    if (!natOn) {
      enableNat();
      Serial.printf("Connected, NAT %s, IP %s\n", natOn ? "ON" : "FAILED", WiFi.localIP().toString().c_str());
    }
  } else {
    natOn = false;
    if (staSsid.length() && millis() - lastTry > 10000) {
      lastTry = millis();
      WiFi.reconnect();
    }
  }
}
