#include "web.h"
#include "state.h"
#include "config.h"
#include "store.h"
#include "features.h"
#include "net.h"
#include <WiFi.h>
#include <WebServer.h>
#include <ArduinoJson.h>

static WebServer server(WEB_PORT);

// ---------------------------------------------------------------------------
static const char DASH_HTML[] PROGMEM = R"HTML(
<!doctype html><html><head><meta charset=utf-8>
<meta name=viewport content="width=device-width,initial-scale=1">
<title>HomeHub</title><style>
:root{color-scheme:dark}
body{font-family:system-ui,sans-serif;margin:0;background:#0f1115;color:#e6e6e6}
header{padding:12px 16px;background:#161a22;display:flex;justify-content:space-between;align-items:center;flex-wrap:wrap;gap:8px}
h1{font-size:1.1rem;margin:0}#meta{font-size:.8rem;color:#9aa4b2}
nav{display:flex;gap:4px;padding:8px 12px;background:#12151c;position:sticky;top:0}
nav button{flex:1;padding:8px;border:0;border-radius:8px;background:#1c2230;color:#cdd5e0;font-size:.9rem}
nav button.on{background:#2b6cff;color:#fff}
main{padding:14px 16px;max-width:760px;margin:0 auto}
section{display:none}section.on{display:block}
.card{background:#161a22;border:1px solid #222a38;border-radius:12px;padding:12px 14px;margin:10px 0}
.row{display:flex;justify-content:space-between;align-items:center;gap:10px}
.dot{width:10px;height:10px;border-radius:50%;display:inline-block;margin-right:6px}
.up{background:#3ad07a}.down{background:#ff5470}.idle{background:#666}
.name{font-weight:600}.sub{font-size:.78rem;color:#9aa4b2;word-break:break-all}
button.act{background:#2b6cff;color:#fff;border:0;border-radius:8px;padding:8px 14px}
button.off{background:#333c4d}
textarea{width:100%;height:230px;background:#0c0e13;color:#cfe;border:1px solid #2a3446;border-radius:8px;padding:10px;font-family:ui-monospace,monospace;font-size:.82rem;box-sizing:border-box}
input[type=color]{width:52px;height:34px;border:0;background:none}
.msg{font-size:.8rem;color:#9aa4b2;margin-top:6px}
</style></head><body>
<header><h1>&#127968; HomeHub</h1><div id=meta>…</div></header>
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
<script>
function h(s){return (s||'').replace(/[&<>]/g,c=>({'&':'&amp;','<':'&lt;','>':'&gt;'}[c]))}
document.querySelectorAll('nav button').forEach(b=>b.onclick=()=>{
  document.querySelectorAll('nav button').forEach(x=>x.classList.remove('on'));
  document.querySelectorAll('section').forEach(x=>x.classList.remove('on'));
  b.classList.add('on');document.getElementById(b.dataset.t).classList.add('on');
});
function ago(s){if(s<0)return 'never';if(s<60)return s+'s ago';if(s<3600)return (s/60|0)+'m ago';return (s/3600|0)+'h ago';}
function refresh(){
 fetch('/api/status').then(r=>r.json()).then(s=>{
  var sd=s.sd.present?(s.sd.usedMB+'/'+s.sd.sizeMB+' MB'):'no card';
  document.getElementById('meta').innerHTML=h(s.ip)+' &middot; '+s.rssi+' dBm &middot; up '+
   (s.uptime/3600|0)+'h'+((s.uptime%3600)/60|0)+'m &middot; SD '+sd;
 });
 fetch('/api/nodes').then(r=>r.json()).then(j=>{
  document.getElementById('nodesList').innerHTML=(j.nodes||[]).map(n=>
   '<div class=card><div class=row><div><span class="dot '+(n.online?'up':'down')+'"></span>'+
   '<span class=name>'+h(n.name)+'</span></div><div class=sub>'+(n.code||'-')+'</div></div>'+
   '<div class=sub>'+h(n.url)+'</div><div class=sub>'+h(n.snippet)+'</div></div>').join('')||
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
function tog(i,s){fetch('/api/output',{method:'POST',headers:{'Content-Type':'application/x-www-form-urlencoded'},body:'idx='+i+'&state='+s}).then(refresh);}
function setLed(){var v=document.getElementById('led').value;var r=parseInt(v.substr(1,2),16),g=parseInt(v.substr(3,2),16),b=parseInt(v.substr(5,2),16);
 fetch('/api/led',{method:'POST',headers:{'Content-Type':'application/x-www-form-urlencoded'},body:'r='+r+'&g='+g+'&b='+b});}
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
  int idx = server.arg("idx").toInt();
  bool st = server.arg("state").toInt() != 0;
  controlSetOutput(idx, st);
  storeSaveConfig();  // remember the new state across reboots
  server.send(200, "text/plain", "ok");
}

static void apiLed() {
  G.ledR = constrain(server.arg("r").toInt(), 0, 255);
  G.ledG = constrain(server.arg("g").toInt(), 0, 255);
  G.ledB = constrain(server.arg("b").toInt(), 0, 255);
  controlApplyLed();
  storeSaveConfig();
  server.send(200, "text/plain", "ok");
}

static void apiConfigGet() { server.send(200, "application/json", storeConfigToJson()); }

static void apiConfigPost() {
  String body = server.hasArg("plain") ? server.arg("plain") : "";
  int dropped = 0;
  if (!storeConfigFromJson(body, &dropped)) { server.send(400, "text/plain", "invalid JSON"); return; }
  storeSaveConfig();
  featuresInit();  // re-apply output pins + LED from new config
  if (dropped) {
    server.send(200, "text/plain", "saved, but dropped " + String(dropped) +
                " output(s) on unsafe GPIO pins (allowed: 1-6, 9-13)");
  } else {
    server.send(200, "text/plain", "saved");
  }
}

void webBegin() {
  server.on("/", []() { server.send_P(200, "text/html", DASH_HTML); });
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
}

void webHandle() { server.handleClient(); }
