#include <Arduino.h>
#include <math.h>
#include <stdlib.h>
#include "dashboard.h"
#include "network.h"
#include "vibration.h"
#include "schedule.h"
#include "water_quality.h"

static unsigned long lastOccupancyHeartbeat = 0;
static unsigned long lastOccPrint = 0;
static String occupancySenderIP = "";
static int occupancyEventCount = 0;
static bool occupancyEventActive = false;
static float occupancyChangePercent = 0.0;
static String occupancyLastEventTime = "";

static const char DASHBOARD_HTML[] PROGMEM = R"HTML(
<!doctype html>
<html lang="en">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>Pool Plant Monitor</title>
<style>
:root{--bg:#f4f7fb;--card:#fff;--ink:#172033;--muted:#667085;--line:#e4e8ef;--ok:#157f3b;--warn:#b54708;--bad:#b42318;--info:#175cd3}
*{box-sizing:border-box}body{margin:0;font-family:Arial,Helvetica,sans-serif;background:var(--bg);color:var(--ink)}
header{background:#0b1f3a;color:#fff;padding:18px 22px;display:flex;justify-content:space-between;gap:18px;align-items:center;flex-wrap:wrap}
header h1{font-size:1.2rem;margin:0}.sub{font-size:.86rem;color:#c8d3e3;margin-top:4px}.pill{padding:7px 11px;border-radius:999px;background:#17385f;font-size:.82rem}
main{max-width:1100px;margin:22px auto;padding:0 16px 32px}.hero,.panel,.card{background:var(--card);border:1px solid var(--line);border-radius:14px;box-shadow:0 3px 14px rgba(16,24,40,.05)}
.hero{padding:20px;margin-bottom:16px;display:flex;justify-content:space-between;align-items:center;gap:16px}.hero h2{margin:0 0 5px;font-size:1.1rem}.status-big{font-weight:700;font-size:1.05rem}
.grid{display:grid;grid-template-columns:repeat(4,1fr);gap:14px;margin-bottom:16px}.card{padding:16px;min-height:126px}.label{font-size:.78rem;text-transform:uppercase;letter-spacing:.06em;color:var(--muted)}.value{font-size:1.7rem;font-weight:700;margin:12px 0 6px}.state{font-size:.88rem;font-weight:600}.muted{color:var(--muted)}
.panel{padding:18px;margin-bottom:16px}.panel h3{margin:0 0 14px;font-size:1rem}.metrics{display:grid;grid-template-columns:repeat(4,1fr);gap:10px}.metric{background:#f8fafc;border-radius:10px;padding:12px}.metric b{display:block;font-size:1.05rem;margin-top:5px}.advisor{border-left:4px solid var(--info)}
.ok{color:var(--ok)}.caution{color:var(--warn)}.warning{color:var(--bad)}.info{color:var(--info)}
footer{text-align:center;color:var(--muted);font-size:.78rem;margin-top:18px}
@media(max-width:800px){.grid,.metrics{grid-template-columns:repeat(2,1fr)}}@media(max-width:480px){.grid,.metrics{grid-template-columns:1fr}.hero{align-items:flex-start;flex-direction:column}}
</style>
</head>
<body>
<header><div><h1>Pool Plant Monitor</h1><div class="sub">Preventative Maintenance Advisor</div></div><div class="pill" id="clock">Loading RTC…</div></header>
<main>
  <section class="hero"><div><div class="label">Overall system status</div><h2 id="overallText">Reading sensors…</h2><div class="muted" id="overallDetail">Waiting for live data from the ESP32.</div></div><div class="status-big" id="overallBadge">● CONNECTING</div></section>

  <section class="grid">
    <div class="card"><div class="label">Occupancy module</div><div class="value" id="occValue">—</div><div class="state muted" id="occState">Waiting for camera node</div></div>
  </section>

  <section class="panel">
    <h3>Water quality <span class="state caution" id="wSim" hidden>SIMULATED</span></h3>
    <div class="muted" id="wMsg" hidden>Water data unavailable</div>
    <div class="metrics">
      <div class="metric"><span class="label">Temperature</span> <span class="state caution" id="tSim" hidden>SIMULATED</span><b id="tV">-</b>
        <div>Condition: <strong id="tC">-</strong></div><div>Availability: <span id="tA">-</span></div><div class="muted" id="tX"></div></div>
      <div class="metric"><span class="label">pH</span> <span class="state caution" id="pSim" hidden>SIMULATED</span><b id="pV">-</b>
        <div>Condition: <strong id="pC">-</strong></div><div>Availability: <span id="pA">-</span></div><div class="muted" id="pK"></div><div class="caution" id="pB" hidden>Possible breach, verifying</div><div class="muted" id="pX"></div></div>
      <div class="metric"><span class="label">Turbidity</span> <span class="state caution" id="uSim" hidden>SIMULATED</span><b id="uV">-</b>
        <div>Condition: <strong id="uC">-</strong></div><div>Availability: <span id="uA">-</span></div><div class="muted" id="uX"></div></div>
    </div>
  </section>

  <section class="panel">
    <h3>Water preferences</h3>
    <div class="metrics">
      <div class="metric"><label class="label" for="inLow">Temperature minimum (°C)</label><input id="inLow" type="number" step="0.1" style="width:100%"></div>
      <div class="metric"><label class="label" for="inHigh">Temperature maximum (°C)</label><input id="inHigh" type="number" step="0.1" style="width:100%"></div>
      <div class="metric"><label class="label" for="inPh">Preferred pH</label><input id="inPh" type="number" step="0.01" style="width:100%"></div>
      <div class="metric"><button id="saveBtn" type="button">Save</button></div>
    </div>
    <p class="muted" style="margin-bottom:0" id="cfgMsg">A blank field is left unchanged. Preferences do not change the pH operating limits.</p>
  </section>

  <section class="panel">
    <h3>Pump condition monitoring</h3>
    <div class="metrics">
      <div class="metric"><span class="label">Current RMS</span><b id="rms">—</b></div>
      <div class="metric"><span class="label">Baseline RMS</span><b id="baseline">—</b></div>
      <div class="metric"><span class="label">Caution threshold</span><b id="cautionT">—</b></div>
      <div class="metric"><span class="label">Warning threshold</span><b id="warningT">—</b></div>
    </div>
    <p style="margin-bottom:0">Condition: <strong id="vibStatus">—</strong></p>
  </section>

  <section class="panel advisor">
    <h3>Preventative maintenance advisor</h3>
    <div id="advice">Collecting data…</div>
  </section>

  <section class="panel">
    <h3>System notes</h3>
    <div class="muted">The dashboard intentionally shows unconnected water-quality channels as unavailable instead of displaying simulated values. Occupancy currently reports camera-node connectivity only; presence detection will be added once the non-identifying frame-difference algorithm is implemented.</div>
  </section>
  <footer>Local ESP32 dashboard • No cloud connection required</footer>
</main>
<script>
function clsForStatus(s){s=(s||'').toUpperCase();return s==='WARNING'?'warning':s==='CAUTION'?'caution':'ok'}
async function refresh(){
 try{
  const r=await fetch('/api/status',{cache:'no-store'}); const d=await r.json();
  document.getElementById('clock').textContent=d.rtc.time;
  const s=d.vibration.status;
  document.getElementById('rms').textContent=d.vibration.rms.toFixed(3)+' m/s²';
  document.getElementById('baseline').textContent=d.vibration.baseline.toFixed(3)+' m/s²';
  document.getElementById('cautionT').textContent=d.vibration.cautionThreshold.toFixed(3)+' m/s²';
  document.getElementById('warningT').textContent=d.vibration.warningThreshold.toFixed(3)+' m/s²';
  const vs=document.getElementById('vibStatus');vs.textContent=s;vs.className=clsForStatus(s);
  const online=d.occupancy.moduleOnline;
  document.getElementById('occValue').textContent=online?'ONLINE':'OFFLINE';
  const os=document.getElementById('occState');os.textContent=online?'Camera node connected; detection pending':'No recent heartbeat from camera node';os.className='state '+(online?'info':'muted');
  let headline='Pool plant operating normally',detail='No persistent pump-vibration fault is currently detected.',advice='No immediate pump maintenance action is indicated by the current vibration logic.',badge='● NORMAL';
  if(s==='CAUTION'){headline='Pump vibration is elevated';detail='The current RMS is above the caution threshold, but the persistent-warning condition has not yet been met.';advice='Continue monitoring. If elevated vibration persists or repeatedly returns, inspect mounting, alignment, looseness and pump operating conditions.';badge='● CAUTION'}
  if(s==='WARNING'){headline='Pump vibration warning';detail='The vibration history has met the configured persistent-warning rule.';advice='Inspect the pump or test rig before continued operation. Check for looseness, imbalance, misalignment or abnormal operating conditions rather than treating a single spike as a fault.';badge='● WARNING'}
  document.getElementById('overallText').textContent=headline;document.getElementById('overallDetail').textContent=detail;
  const ob=document.getElementById('overallBadge');ob.textContent=badge;ob.className='status-big '+clsForStatus(s);
  document.getElementById('advice').textContent=advice;
  try{renderWater(d.water)}catch(e2){wText('wMsg','Water data unavailable');document.getElementById('wMsg').hidden=false}
 }catch(e){document.getElementById('overallText').textContent='Dashboard connection lost';document.getElementById('overallDetail').textContent='The browser could not read /api/status.';document.getElementById('overallBadge').textContent='● OFFLINE';document.getElementById('overallBadge').className='status-big warning'}
}
function wText(id,t){document.getElementById(id).textContent=t}
function wCls(id,c){document.getElementById(id).className=c}
function nice(s){s=String(s==null?'':s).replace(/_/g,' ').toLowerCase();return s.charAt(0).toUpperCase()+s.slice(1)}
function condCls(s){s=(s||'').toUpperCase();if(s==='OK'||s==='IN_RANGE'||s==='CLEAR'||s==='ON_TARGET')return 'ok';if(s==='WARNING')return 'warning';if(s==='CAUTION'||s==='BELOW_RANGE'||s==='ABOVE_RANGE'||s==='CLOUDIER'||s==='OFF_TARGET')return 'caution';return 'muted'}
function availText(ch){var st=ch.state;if(st==='live')return 'Live';if(st==='stale')return 'Stale';if(st==='disabled')return 'Disabled'+(ch.reason?' ('+nice(ch.reason).toLowerCase()+')':'');return 'Unavailable'+(ch.reason?' ('+nice(ch.reason).toLowerCase()+')':'')}
function chan(p,ch,val,cond,condStatus){
 document.getElementById(p+'Sim').hidden=!ch.simulated;
 wText(p+'V',val);wText(p+'C',cond);wCls(p+'C',condCls(condStatus));wText(p+'A',availText(ch));
}
function renderWater(w){
 var msg=document.getElementById('wMsg');
 if(!w||!w.temperature||!w.ph||!w.turbidity){msg.hidden=false;return}
 msg.hidden=true;
 document.getElementById('wSim').hidden=!w.simulated;
 var t=w.temperature,p=w.ph,u=w.turbidity;
 chan('t',t,(t.state==='live'&&t.valueC!=null)?t.valueC.toFixed(1)+' °C':'-',nice(t.status),t.status);
 wText('tX',t.rangeSet?'Preferred range: '+t.low.toFixed(1)+' to '+t.high.toFixed(1)+' °C':'Preferred range: not set');
 var pst=p.status+((p.direction&&p.direction!=='OK')?' '+p.direction:'');
 chan('p',p,(p.state==='live'&&p.value!=null)?p.value.toFixed(2):'-',nice(pst)+((p.status==='UNAVAILABLE'&&p.statusReason)?' ('+nice(p.statusReason).toLowerCase()+')':''),p.status);
 wText('pK',p.calibration==='two_point'?'Calibration: two-point':p.calibration==='partial'?'Calibration: partly calibrated':'Calibration: not calibrated since restart');
 document.getElementById('pB').hidden=!p.possibleBreach;
 var pf=p.preferred||{};
 wText('pX',pf.status==='ON_TARGET'?'On target (preferred '+pf.value.toFixed(2)+')':pf.status==='OFF_TARGET'?'Off target (preferred '+pf.value.toFixed(2)+')':pf.status==='UNAVAILABLE'?'Preferred pH: unavailable':'Preferred pH: not set');
 var up=(u.state==='live'&&u.percentBelowBaseline!=null);
 chan('u',u,up?Math.abs(u.percentBelowBaseline).toFixed(1)+' %':'-',nice(u.status),u.status);
 wText('uX',u.baseline!=='set'?'No baseline since restart':up?(u.percentBelowBaseline>=0?'below the clear-water baseline':'clearer than the clear-water baseline'):'Clear-water baseline set');
}
function loadCfg(){fetch('/api/water/config',{cache:'no-store'}).then(function(r){return r.json()}).then(function(c){
 document.getElementById('inLow').value=c.tempSet?c.tempLow:'';document.getElementById('inHigh').value=c.tempSet?c.tempHigh:'';document.getElementById('inPh').value=c.phSet?c.phPref:'';
 }).catch(function(){wText('cfgMsg','Could not read the current preferences.')})}
async function saveCfg(){
 var b=new URLSearchParams();b.set('tempLow',document.getElementById('inLow').value);b.set('tempHigh',document.getElementById('inHigh').value);b.set('phPref',document.getElementById('inPh').value);
 try{
  var r=await fetch('/api/water/config',{method:'POST',headers:{'Content-Type':'application/x-www-form-urlencoded'},body:b.toString()});var d=await r.json();
  if(!d.ok)wText('cfgMsg',(d.errors||[]).join(' ')||'Not saved.');
  else if(!d.changed)wText('cfgMsg','No changes.');
  else wText('cfgMsg',d.saved?'Saved.':'Applied, but could not be saved to flash.');
  if(d.ok)loadCfg();
 }catch(e){wText('cfgMsg','Not saved: no response from the ESP32.')}
}
document.getElementById('saveBtn').addEventListener('click',saveCfg);
loadCfg();
setInterval(refresh,1000);refresh();
</script>
</body></html>
)HTML";

static String boolJson(bool v) { return v ? "true" : "false"; }

static void handleRoot() {
  networkServer().send_P(200, "text/html", DASHBOARD_HTML);
}

static void handleStatus() {
  bool occupancyOnline = lastOccupancyHeartbeat != 0 && (millis() - lastOccupancyHeartbeat < 10000);

  String json = "{";
  json += "\"vibration\":{";
  json += "\"status\":\"" + vibrationGetStatus() + "\",";
  json += "\"rms\":" + String(vibrationGetRMS(), 4) + ",";
  json += "\"baseline\":" + String(vibrationGetBaseline(), 4) + ",";
  json += "\"cautionThreshold\":" + String(vibrationGetCautionThreshold(), 4) + ",";
  json += "\"warningThreshold\":" + String(vibrationGetWarningThreshold(), 4);
  json += "},";
  json += "\"rtc\":{";
  json += "\"ready\":" + boolJson(scheduleIsReady()) + ",";
  json += "\"time\":\"" + scheduleGetTimeString() + "\"";
  json += "},";
  json += "\"occupancy\":{";
  json += "\"moduleOnline\":" + boolJson(occupancyOnline) + ",";
  json += "\"detectionImplemented\":false";
  json += "},";
  json += "\"water\":" + waterQualityStatusJson();
  json += "}";

  networkServer().sendHeader("Cache-Control", "no-store");
  networkServer().send(200, "application/json", json);
}

static void handleOccupancyHeartbeat() {
  lastOccupancyHeartbeat = millis();
  occupancySenderIP = networkServer().client().remoteIP().toString();

  int incomingCount = networkServer().arg("eventCount").toInt();
  occupancyEventActive = (networkServer().arg("eventActive") == "1");
  occupancyChangePercent = networkServer().arg("changePercent").toFloat();

  if (incomingCount < occupancyEventCount) {
    Serial.println("[Occ] camera restart detected");
    occupancyLastEventTime = "";
  }

  if (incomingCount > occupancyEventCount) {
    occupancyLastEventTime = scheduleGetTimeString();
    Serial.print("[Occ] event #");
    Serial.print(incomingCount);
    Serial.print(" at ");
    Serial.println(occupancyLastEventTime);
  }

  occupancyEventCount = incomingCount;

  if (millis() - lastOccPrint >= 5000) {
    lastOccPrint = millis();
    Serial.print("[Occ] online ip=");
    Serial.print(occupancySenderIP);
    Serial.print(" count=");
    Serial.print(occupancyEventCount);
    Serial.print(" active=");
    Serial.print(occupancyEventActive ? "1" : "0");
    Serial.print(" chg=");
    Serial.print(occupancyChangePercent, 1);
    Serial.print(" last=");
    Serial.println(occupancyLastEventTime);
  }

  networkServer().send(200, "application/json", "{\"ok\":true}");
}

// Strict decimal parse: digits, sign, point and exponent only; no empty text, NaN, inf or trailing text.
static bool parseStrictFloat(const String& s, float* out) {
  if (s.length() == 0) return false;
  for (unsigned int i = 0; i < s.length(); i++) {
    char c = s[i];
    if (!((c >= '0' && c <= '9') || c == '+' || c == '-' || c == '.' || c == 'e' || c == 'E')) return false;
  }
  char* end = nullptr;
  double d = strtod(s.c_str(), &end);
  if (end == s.c_str() || *end != '\0') return false;
  float f = (float)d;
  if (isnan(f) || isinf(f)) return false;
  *out = f;
  return true;
}

// Reads one form field. Blank means "unchanged" (present stays false); bad text adds an error.
static void readConfigField(const char* name, const char* label, bool* present, float* value, String* errors) {
  *present = false;
  String s = networkServer().arg(name);
  s.trim();
  if (s.length() == 0) return;
  if (!parseStrictFloat(s, value)) {
    *errors += String(label) + " is not a valid number.\n";
    return;
  }
  *present = true;
}

static void handleWaterConfigGet() {
  networkServer().sendHeader("Cache-Control", "no-store");
  networkServer().send(200, "application/json", waterQualityConfigJson());
}

static void handleWaterConfigPost() {
  String errors;
  bool hasLow, hasHigh, hasPh;
  float low = NAN, high = NAN, ph = NAN;
  readConfigField("tempLow", "Temperature minimum", &hasLow, &low, &errors);
  readConfigField("tempHigh", "Temperature maximum", &hasHigh, &high, &errors);
  readConfigField("phPref", "Preferred pH", &hasPh, &ph, &errors);
  if (!hasLow) low = NAN;
  if (!hasHigh) high = NAN;

  WaterConfigResult r = waterQualityApplyConfig(hasLow || hasHigh, low, high, hasPh, ph, &errors);

  String json = "{\"ok\":" + boolJson(r.ok) + ",\"changed\":" + boolJson(r.changed) + ",\"saved\":" + boolJson(r.saved) + ",\"errors\":[";
  bool first = true;
  int start = 0;
  while (start < (int)errors.length()) {
    int nl = errors.indexOf('\n', start);
    if (nl < 0) nl = errors.length();
    String line = errors.substring(start, nl);
    start = nl + 1;
    if (line.length() == 0) continue;
    line.replace("\\", "\\\\");
    line.replace("\"", "\\\"");
    if (!first) json += ",";
    json += "\"" + line + "\"";
    first = false;
  }
  json += "]}";

  networkServer().sendHeader("Cache-Control", "no-store");
  networkServer().send(r.ok ? 200 : 400, "application/json", json);
}

void dashboardSetup() {
  networkServer().on("/api/water/config", HTTP_GET, handleWaterConfigGet);
  networkServer().on("/api/water/config", HTTP_POST, handleWaterConfigPost);
  networkServer().on("/", HTTP_GET, handleRoot);
  networkServer().on("/api/status", HTTP_GET, handleStatus);
  networkServer().on("/api/occupancy/heartbeat", HTTP_GET, handleOccupancyHeartbeat);

  Serial.println("Dashboard routes registered.");
}

void dashboardUpdate() {
}

bool dashboardOccupancyOnline() {
  return lastOccupancyHeartbeat != 0 && (millis() - lastOccupancyHeartbeat < 10000);
}

int dashboardOccupancyEventCount() {
  return occupancyEventCount;
}

bool dashboardOccupancyEventActive() {
  return occupancyEventActive;
}

float dashboardOccupancyChangePercent() {
  return occupancyChangePercent;
}

String dashboardOccupancySenderIP() {
  return occupancySenderIP;
}

String dashboardOccupancyLastEventTime() {
  return occupancyLastEventTime;
}
