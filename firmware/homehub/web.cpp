#include "web.h"
#include "state.h"
#include "config.h"
#include "store.h"
#include "features.h"
#include "net.h"
#include "favicon.h"
#include "logo.h"
#include "lock.h"
// Optional, gitignored. Defines WEB_USER / WEB_PASSWORD. See secrets.example.h.
#if __has_include("secrets.h")
  #include "secrets.h"
#endif
#ifndef WEB_USER
  #define WEB_USER "admin"
#endif
#include <WiFi.h>
#include <WebServer.h>
#include <ArduinoJson.h>

static WebServer server(WEB_PORT);

// Gate for every route that exposes state or accepts a change. Digest, so the
// password never crosses the wire in the clear.
//
// Without a WEB_PASSWORD this is a no-op and the UI stays open -- the same
// choice OTA makes. Failing closed would brick a first boot: the AP setup
// portal has to be reachable before any credential exists.
static bool requireAuth() {
#ifdef WEB_PASSWORD
  if (server.authenticate(WEB_USER, WEB_PASSWORD)) return true;
  server.requestAuthentication(DIGEST_AUTH, "HomeHub", "Authentication required");
  return false;
#else
  return true;
#endif
}

// ---------------------------------------------------------------------------
static const char DASH_HTML[] PROGMEM = R"HTML(
<!doctype html><html><head><meta charset=utf-8>
<meta name=viewport content="width=device-width,initial-scale=1">
<title>Three Oak Woods Home Hub</title>
<link rel=icon type=image/svg+xml href=/logo.svg>
<link rel=icon type=image/png href=/favicon.ico>
<link rel=preconnect href='https://fonts.googleapis.com'>
<link href='https://fonts.googleapis.com/css2?family=Nunito:wght@400;700;800&display=swap' rel=stylesheet>
<style>
/* Palette and type match the e-paper display at .50 so the two read as one
   system. Nunito is fetched by the browser, not the device; system-ui is the
   fallback when the viewer has no internet. */
:root{--green:#2C654B;--cream:#F9E7DF;--amber:#C8852A;--bark:#2B3A33;--parchment:#F5F1E6;--line:#e6ddca;--rule:#cfc6b3;color-scheme:light}
*{box-sizing:border-box}
body{font-family:'Nunito',system-ui,sans-serif;color:var(--bark);background:var(--parchment);margin:0;padding:0 16px 32px}
header{display:flex;align-items:center;gap:14px;padding:22px 0 8px;max-width:480px;margin:0 auto}
header img{width:56px;height:56px}
header h1{font-size:1.25em;font-weight:800;margin:0;line-height:1.1}
header .sub{color:var(--green);font-weight:700;font-size:.78em;letter-spacing:.05em;text-transform:uppercase}
#ver{font-size:.62em;font-weight:700;color:var(--green);background:var(--cream);border:1px solid var(--line);border-radius:6px;padding:1px 6px;margin-left:7px;vertical-align:middle}
#meta{background:var(--cream);border-left:4px solid var(--amber);border-radius:8px;padding:10px 14px;margin:6px auto 16px;max-width:480px;font-size:.8em;font-weight:700}
nav{display:flex;gap:8px;max-width:480px;margin:0 auto 14px}
nav button{flex:1;padding:9px 4px;border:1px solid var(--rule);border-radius:8px;background:#fff;color:var(--bark);font-family:inherit;font-weight:700;font-size:.86em;cursor:pointer}
nav button.on{background:var(--green);color:var(--cream);border-color:var(--green)}
main{max-width:480px;margin:0 auto}
section{display:none}section.on{display:block}
.card{background:#fff;border:1px solid var(--line);border-radius:12px;padding:14px 16px;margin:0 0 14px;box-shadow:0 1px 2px rgba(43,58,51,.06)}
.row{display:flex;justify-content:space-between;align-items:center;gap:10px}
.dot{width:10px;height:10px;border-radius:50%;display:inline-block;margin-right:7px;vertical-align:middle}
.up{background:var(--green)}.down{background:#B3402F}.idle{background:#b3ab98}
.name{font-weight:800}
.sub{font-size:.78em;color:#6d7a71;word-break:break-all;margin-top:3px}
a.name{color:var(--green);text-decoration:none}a.name:hover{text-decoration:underline}
button.act{background:var(--green);color:var(--cream);border:0;border-radius:8px;padding:9px 16px;font-family:inherit;font-weight:800;font-size:.9em;cursor:pointer}
button.act:hover{filter:brightness(1.08)}
button.off{background:#fff;color:var(--bark);border:1px solid var(--rule);font-weight:700}
textarea{width:100%;height:230px;background:#fff;color:var(--bark);border:1px solid var(--rule);border-radius:8px;padding:10px;font-family:ui-monospace,monospace;font-size:.8em}
input[type=color]{width:52px;height:34px;border:1px solid var(--rule);border-radius:8px;background:#fff;padding:2px}
.msg{font-size:.8em;color:var(--green);font-weight:700;margin-top:8px}
footer{text-align:center;color:var(--green);font-size:.78em;margin-top:4px;opacity:.85}
</style></head><body>
<header><img src=/logo.svg alt="Three Oak Woods" width=56 height=56>
<div><div class=sub>Three Oak Woods</div><h1>Home Hub<span id=ver></span></h1></div></header>
<div id=meta>&#8230;</div>
<nav>
<button data-t=nodes class=on>Nodes</button>
<button data-t=presence>Presence</button>
<button data-t=control>Control</button>
<button data-t=settings>Settings</button>
</nav>
<main>
<section id=nodes class=on><div id=nodesList></div></section>
<section id=presence><div id=presList></div></section>
<section id=control>
  <div class=card><div class=row><div><div class=name>Onboard RGB LED</div>
  <div class=sub>WS2812 on GPIO38</div></div>
  <input type=color id=led onchange="setLed()"></div></div>
  <div id=outList></div>
  <div class=msg id=ctlMsg></div>
</section>
<section id=settings>
  <div class=card>
  <p class=sub>Config JSON: <b>hosts</b> = Presence tab, <b>nodes</b> = Nodes tab,
  <b>outputs</b> = Control tab. Safe output GPIOs on this board: 1-6, 9-13.</p>
  <p class=sub><b>Save</b> writes this to the device (SD + NVS) and applies it
  immediately. <b>Revert</b> just reloads the saved config into this box,
  discarding your edits. Neither one reboots the device.</p>
  <textarea id=cfg></textarea>
  <div class=row style="margin-top:8px">
  <button class="act off" onclick=loadCfg()>Revert</button>
  <button class=act onclick=saveCfg()>Save</button></div>
  <div class=msg id=cfgMsg></div>
  </div>
</section>
</main>
<footer>Three Oak Woods &middot; Home Hub</footer>
<script>
function h(s){return (s||'').replace(/[&<>]/g,c=>({'&':'&amp;','<':'&lt;','>':'&gt;'}[c]))}
// h() is for text nodes and does not escape quotes -- attribute values need this.
function attr(s){return h(s).replace(/"/g,'&quot;')}
// Only http(s) becomes a link. Config comes from whoever can reach the UI, so
// this keeps javascript:/data: URLs out of an href regardless of who that is.
function safeUrl(u){return /^https?:\/\//i.test(u||'')?u:''}
document.querySelectorAll('nav button').forEach(b=>b.onclick=()=>{
  document.querySelectorAll('nav button').forEach(x=>x.classList.remove('on'));
  document.querySelectorAll('section').forEach(x=>x.classList.remove('on'));
  b.classList.add('on');document.getElementById(b.dataset.t).classList.add('on');
});
function ago(s){if(s<0)return 'never';if(s<60)return s+'s ago';if(s<3600)return (s/60|0)+'m ago';return (s/3600|0)+'h ago';}
function refresh(){
 fetch('/api/status').then(r=>r.json()).then(s=>{
  var sd=s.sd.present?(s.sd.usedMB+'/'+s.sd.sizeMB+' MB'):'no card';
  // Reported by the running firmware, so this is the real check that an OTA landed.
  document.getElementById('ver').textContent='v'+s.ver;
  document.getElementById('meta').innerHTML=h(s.ip)+' &middot; '+s.rssi+' dBm &middot; up '+
   (s.uptime/3600|0)+'h'+((s.uptime%3600)/60|0)+'m &middot; SD '+sd;
 });
 fetch('/api/nodes').then(r=>r.json()).then(j=>{
  document.getElementById('nodesList').innerHTML=(j.nodes||[]).map(function(n){
   var u=safeUrl(n.url);
   var title=u?'<a class=name href="'+attr(u)+'" target=_blank rel=noopener>'+h(n.name)+'</a>'
              :'<span class=name>'+h(n.name)+'</span>';
   return '<div class=card><div class=row><div><span class="dot '+(n.online?'up':'down')+'"></span>'+
   title+'</div><div class=sub>'+(n.code||'-')+'</div></div>'+
   '<div class=sub>'+h(n.url)+'</div><div class=sub>'+h(n.snippet)+'</div></div>';}).join('')||
   '<div class=card><div class=sub>No nodes configured. Add some in Settings.</div></div>';
 });
 fetch('/api/monitor').then(r=>r.json()).then(j=>{
  document.getElementById('presList').innerHTML=(j.hosts||[]).map(x=>
   '<div class=card><div class=row><div><span class="dot '+(x.online?'up':(x.since<0?'idle':'down'))+'"></span>'+
   '<span class=name>'+h(x.name)+'</span></div><div class=sub>'+
   (x.online?x.latency+' ms':ago(x.since))+'</div></div>'+
   '<div class=sub>'+h(x.host)+':'+x.port+'</div></div>').join('')||
   '<div class=card><div class=sub>No hosts configured. Add some in Settings.</div></div>';
 });
 fetch('/api/control').then(r=>r.json()).then(j=>{
  document.getElementById('led').value='#'+[j.led.r,j.led.g,j.led.b].map(v=>('0'+v.toString(16)).slice(-2)).join('');
  document.getElementById('outList').innerHTML=(j.outputs||[]).map((o,i)=>
   '<div class=card><div class=row><div><div class=name>'+h(o.name)+'</div>'+
   '<div class=sub>GPIO'+o.pin+'</div></div>'+
   '<button class="act '+(o.state?'':'off')+'" onclick="tog('+i+','+(o.state?0:1)+')">'+
   (o.state?'ON':'OFF')+'</button></div></div>').join('')||
   '<div class=card><div class=sub>No outputs configured. Add some in Settings.</div></div>';
 });
}
// These endpoints report persistence failures in the body. Dropping it on the
// floor made a failed save look identical to a good one, so surface it.
function ctlNote(t){document.getElementById('ctlMsg').textContent=(t&&t!='ok')?t:'';}
function tog(i,s){fetch('/api/output',{method:'POST',headers:{'Content-Type':'application/x-www-form-urlencoded'},body:'idx='+i+'&state='+s})
 .then(r=>r.text()).then(t=>{ctlNote(t);refresh();});}
function setLed(){var v=document.getElementById('led').value;var r=parseInt(v.substr(1,2),16),g=parseInt(v.substr(3,2),16),b=parseInt(v.substr(5,2),16);
 fetch('/api/led',{method:'POST',headers:{'Content-Type':'application/x-www-form-urlencoded'},body:'r='+r+'&g='+g+'&b='+b})
 .then(r=>r.text()).then(ctlNote);}
function loadCfg(){fetch('/api/config').then(r=>r.text()).then(t=>{try{document.getElementById('cfg').value=JSON.stringify(JSON.parse(t),null,2);}catch(e){document.getElementById('cfg').value=t;}document.getElementById('cfgMsg').textContent='';});}
function saveCfg(){fetch('/api/config',{method:'POST',headers:{'Content-Type':'application/json'},body:document.getElementById('cfg').value})
 .then(r=>r.text()).then(t=>{document.getElementById('cfgMsg').textContent=t;refresh();});}
refresh();loadCfg();setInterval(refresh,5000);
</script></body></html>
)HTML";

// ---------------------------------------------------------------------------
static void sendJson(JsonDocument& doc) {
  String out; serializeJson(doc, out);
  server.send(200, "application/json", out);
}

static void apiStatus() {
  if (!requireAuth()) return;
  JsonDocument d;
  d["name"] = FW_NAME; d["ver"] = FW_VERSION;
  d["mac"]  = WiFi.macAddress();
  d["ip"]   = WiFi.localIP().toString();
  d["rssi"] = WiFi.RSSI();
  d["uptime"] = (uint32_t)(millis() / 1000);
  d["heap"] = ESP.getFreeHeap();
  JsonObject sd = d["sd"].to<JsonObject>();
  sd["present"] = G.sdPresent;
  sd["sizeMB"]  = (uint32_t)G.sdSizeMB;
  sd["usedMB"]  = (uint32_t)G.sdUsedMB;
  sendJson(d);
}

static void apiMonitor() {
  if (!requireAuth()) return;
  Lock l;  // shared with the poll task
  JsonDocument d;
  JsonArray a = d["hosts"].to<JsonArray>();
  for (int i = 0; i < G.hostCount; i++) {
    MonHost& h = G.hosts[i];
    JsonObject o = a.add<JsonObject>();
    o["name"] = h.name; o["host"] = h.host; o["port"] = h.port;
    o["online"] = h.online; o["latency"] = h.latencyMs;
    o["since"] = h.lastSeenMs ? (int)((millis() - h.lastSeenMs) / 1000) : -1;
  }
  sendJson(d);
}

static void apiNodes() {
  if (!requireAuth()) return;
  Lock l;  // shared with the poll task
  JsonDocument d;
  JsonArray a = d["nodes"].to<JsonArray>();
  for (int i = 0; i < G.nodeCount; i++) {
    Node& n = G.nodes[i];
    JsonObject o = a.add<JsonObject>();
    o["name"] = n.name; o["url"] = n.url;
    o["online"] = n.online; o["code"] = n.httpCode; o["snippet"] = n.snippet;
  }
  sendJson(d);
}

static void apiControl() {
  if (!requireAuth()) return;
  Lock l;  // shared with the poll task
  JsonDocument d;
  JsonArray a = d["outputs"].to<JsonArray>();
  for (int i = 0; i < G.outputCount; i++) {
    JsonObject o = a.add<JsonObject>();
    o["name"] = G.outputs[i].name; o["pin"] = G.outputs[i].pin; o["state"] = G.outputs[i].state;
  }
  JsonObject led = d["led"].to<JsonObject>();
  led["r"] = G.ledR; led["g"] = G.ledG; led["b"] = G.ledB;
  sendJson(d);
}

static void apiOutput() {
  if (!requireAuth()) return;
  Lock l;  // shared with the poll task
  int idx = server.arg("idx").toInt();
  bool st = server.arg("state").toInt() != 0;
  controlSetOutput(idx, st);
  // The pin is already driven; only persistence can fail here, so say so rather
  // than reporting "ok" and letting the state quietly not survive a reboot.
  bool ok = storeSaveConfig();
  server.send(200, "text/plain", ok ? "ok" : "ok (NOT persisted - save failed)");
}

static void apiLed() {
  if (!requireAuth()) return;
  Lock l;  // shared with the poll task
  G.ledR = constrain(server.arg("r").toInt(), 0, 255);
  G.ledG = constrain(server.arg("g").toInt(), 0, 255);
  G.ledB = constrain(server.arg("b").toInt(), 0, 255);
  controlApplyLed();
  bool ok = storeSaveConfig();
  server.send(200, "text/plain", ok ? "ok" : "ok (NOT persisted - save failed)");
}

static void apiConfigGet() {
  if (!requireAuth()) return;
  server.send(200, "application/json", storeConfigToJson());
}

static void apiConfigPost() {
  if (!requireAuth()) return;
  Lock l;  // shared with the poll task
  String body = server.hasArg("plain") ? server.arg("plain") : "";
  int dropped = 0;
  if (!storeConfigFromJson(body, &dropped)) { server.send(400, "text/plain", "invalid JSON"); return; }
  bool nvsOk = false, sdOk = false;
  bool allOk = storeSaveConfig(&nvsOk, &sdOk);
  featuresInit();  // re-apply output pins + LED from new config

  // Never report a bare "saved" unless it really was. A config UI that lies
  // about persistence is worse than one that fails loudly.
  String msg;
  if (!nvsOk) {
    msg = "NOT SAVED - NVS write failed; this config will be lost on reboot";
  } else if (!allOk) {
    msg = "saved to NVS, but the SD write FAILED (card removed or full?)";
  } else {
    msg = "saved";
  }
  if (dropped) {
    msg += " | dropped " + String(dropped) + " output(s) on unsafe GPIO pins (allowed: 1-6, 9-13)";
  }
  server.send(nvsOk ? 200 : 500, "text/plain", msg);
}

void webBegin() {
  server.on("/", []() {
    if (!requireAuth()) return;
    server.send_P(200, "text/html", DASH_HTML);
  });
  // Branding assets stay public: they carry no state, and gating them makes the
  // 401 challenge fire for the icon before the page itself has authenticated.
  // Embedded in the firmware rather than read off the SD card, so the icon still
  // works with no card fitted. Cached hard -- it only changes on a reflash.
  server.on("/favicon.ico", []() {
    server.sendHeader("Cache-Control", "public, max-age=604800, immutable");
    server.send_P(200, "image/png", (PGM_P)FAVICON_PNG, FAVICON_PNG_LEN);
  });
  // Same badge as the e-paper display serves, so the two brand identically.
  server.on("/logo.svg", []() {
    server.sendHeader("Cache-Control", "public, max-age=604800, immutable");
    server.send_P(200, "image/svg+xml", (PGM_P)LOGO_SVG, LOGO_SVG_LEN);
  });
  server.on("/api/status",  apiStatus);
  server.on("/api/monitor", apiMonitor);
  server.on("/api/nodes",   apiNodes);
  server.on("/api/control", apiControl);
  server.on("/api/output",  HTTP_POST, apiOutput);
  server.on("/api/led",     HTTP_POST, apiLed);
  server.on("/api/config",  HTTP_GET,  apiConfigGet);
  server.on("/api/config",  HTTP_POST, apiConfigPost);
  server.onNotFound([]() { server.send(404, "text/plain", "not found"); });
  server.begin();
#ifdef WEB_PASSWORD
  Serial.printf("web UI: digest auth enabled (user \"%s\")\n", WEB_USER);
#else
  Serial.println("WARNING: web UI is UNAUTHENTICATED - anyone on this LAN can toggle outputs.");
  Serial.println("         Set WEB_PASSWORD in firmware/homehub/secrets.h (see secrets.example.h).");
#endif
}

void webHandle() { server.handleClient(); }
