#include "net.h"
#include "state.h"
#include "config.h"
#include <WiFi.h>
#include <WebServer.h>
#include <DNSServer.h>
#include <Preferences.h>
#include "features.h"

static Preferences wifiPrefs;
static WebServer   portal(80);
static DNSServer   dns;

void ledStatus(uint8_t r, uint8_t g, uint8_t b) {
  rgbLedWrite(r, g, b);  // handles this board's swapped R/G channel order
}

bool netHasCreds() {
  wifiPrefs.begin("wifi", true);
  String s = wifiPrefs.getString("ssid", "");
  wifiPrefs.end();
  return s.length() > 0;
}

void netClearCreds() {
  wifiPrefs.begin("wifi", false);
  wifiPrefs.clear();
  wifiPrefs.end();
}

static void netSaveCreds(const String& ssid, const String& pass) {
  wifiPrefs.begin("wifi", false);
  wifiPrefs.putString("ssid", ssid);
  wifiPrefs.putString("pass", pass);
  wifiPrefs.end();
}

bool netConnectSTA(uint32_t timeoutMs) {
  wifiPrefs.begin("wifi", true);
  String ssid = wifiPrefs.getString("ssid", "");
  String pass = wifiPrefs.getString("pass", "");
  wifiPrefs.end();
  if (ssid.isEmpty()) return false;

  ledStatus(90, 55, 0);  // amber: connecting
  WiFi.persistent(false);
  WiFi.mode(WIFI_STA);
  WiFi.setHostname(MDNS_HOSTNAME);
  WiFi.begin(ssid.c_str(), pass.c_str());

  uint32_t start = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - start < timeoutMs) {
    delay(200);
  }
  if (WiFi.status() == WL_CONNECTED) {
    ledStatus(0, 80, 0);  // green: online
    return true;
  }
  ledStatus(120, 0, 0);   // red: failed
  return false;
}

// ---- Config portal (AP mode) ----------------------------------------------

static const char PORTAL_HTML[] PROGMEM = R"HTML(
<!doctype html><html><head><meta charset=utf-8>
<meta name=viewport content="width=device-width,initial-scale=1">
<title>HomeHub Setup</title>
<style>
body{font-family:system-ui,sans-serif;max-width:420px;margin:24px auto;padding:0 16px;background:#111;color:#eee}
h1{font-size:1.3rem}label{display:block;margin:14px 0 4px}
input,select,button{width:100%;padding:10px;font-size:1rem;border-radius:8px;border:1px solid #444;background:#1c1c1c;color:#eee;box-sizing:border-box}
button{background:#2b6;border:0;color:#031;font-weight:600;margin-top:18px}
small{color:#999}
</style></head><body>
<h1>HomeHub WiFi setup</h1>
<p><small>Pick your network and enter the password. The hub will reboot and join it.</small></p>
<form method=POST action=/save>
<label>Network</label>
<select name=ssid id=ssid></select>
<label>Password</label>
<input name=pass type=password placeholder="WiFi password">
<button type=submit>Save &amp; reboot</button>
</form>
<p><small id=st>Scanning…</small></p>
<script>
fetch('/scan').then(r=>r.json()).then(j=>{
  var s=document.getElementById('ssid');
  s.innerHTML='';
  (j.nets||[]).forEach(function(n){
    var o=document.createElement('option');o.value=n.ssid;
    o.textContent=n.ssid+' ('+n.rssi+' dBm)'+(n.lock?' \u{1F512}':'');
    s.appendChild(o);
  });
  document.getElementById('st').textContent=(j.nets||[]).length+' networks found';
});
</script></body></html>
)HTML";

static void handleRoot()  { portal.send_P(200, "text/html", PORTAL_HTML); }

static void handleScan() {
  int n = WiFi.scanNetworks();
  String out = "{\"nets\":[";
  for (int i = 0; i < n; i++) {
    if (i) out += ',';
    String ssid = WiFi.SSID(i);
    ssid.replace("\\", "\\\\"); ssid.replace("\"", "\\\"");
    out += "{\"ssid\":\"" + ssid + "\",\"rssi\":" + WiFi.RSSI(i) +
           ",\"lock\":" + (WiFi.encryptionType(i) == WIFI_AUTH_OPEN ? "false" : "true") + "}";
  }
  out += "]}";
  WiFi.scanDelete();
  portal.send(200, "application/json", out);
}

static void handleSave() {
  String ssid = portal.arg("ssid");
  String pass = portal.arg("pass");
  if (ssid.isEmpty()) { portal.send(400, "text/plain", "SSID required"); return; }
  netSaveCreds(ssid, pass);
  portal.send(200, "text/html",
    "<meta name=viewport content='width=device-width'><body style='font-family:sans-serif;background:#111;color:#eee'>"
    "<h2>Saved.</h2><p>Rebooting and joining <b>" + ssid + "</b>…</p>"
    "<p>Find the hub at <code>http://homehub.local/</code> on your network.</p></body>");
  delay(600);
  ESP.restart();
}

void netStartPortal() {
  G.apMode = true;
  ledStatus(0, 0, 120);  // blue: setup portal is up
  WiFi.mode(WIFI_AP);
  WiFi.softAP(AP_SSID);
  IPAddress ip = WiFi.softAPIP();  // 192.168.4.1
  dns.start(53, "*", ip);          // captive portal: resolve everything to us
  portal.on("/", handleRoot);
  portal.on("/scan", handleScan);
  portal.on("/save", HTTP_POST, handleSave);
  portal.onNotFound(handleRoot);   // captive-portal probes land on the form
  portal.begin();
}

void netHandlePortal() {
  dns.processNextRequest();
  portal.handleClient();
}
