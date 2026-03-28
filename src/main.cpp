#include <Arduino.h>
#include <WiFi.h>
#include <WebServer.h>

// ============== DEFAULT PIN DEFINITIONS ==============
#define PIR_PIN       14
#define TRIG_PIN      33
#define ECHO_PIN      32

// ============== CONFIGURATION ==============
const char* ssid = "SmartClass_AP";
const char* password = "12345678";

const float LIGHT_WATTS  = 40.0;
const float FAN_WATTS    = 75.0;
const float COST_PER_KWH = 5.0;

WebServer server(80);

// ============== STATE VARIABLES ==============
bool relay1State    = false;
bool relay2State    = false;
bool pirDetected    = false;
bool autoMode       = true;
bool roomOccupied   = false;
float lastDistance   = -1.0;

// ============== CONFIGURABLE RELAY PINS ==============
int relay1Pin = 26;
int relay2Pin = 25;
String relay1Name = "Light";
String relay2Name = "Fan";

// ============== TIMING ==============
unsigned long lastPIRTrigger = 0;
unsigned long lastPowerCalc  = 0;
unsigned long bootTime       = 0;

const long vacantDelay   = 180000;
const long powerInterval = 1000;

// ============== POWER TRACKING ==============
float relay1OnSeconds = 0;
float relay2OnSeconds = 0;

// ============== EVENT LOG ==============
#define MAX_LOG_ENTRIES 15
String eventLog[MAX_LOG_ENTRIES];
int logIndex = 0;
int logCount = 0;

void addLog(String msg) {
    unsigned long sec = (millis() - bootTime) / 1000;
    int h = sec / 3600;
    int m = (sec % 3600) / 60;
    int s = sec % 60;
    char timeBuf[12];
    snprintf(timeBuf, sizeof(timeBuf), "%02d:%02d:%02d", h, m, s);
    eventLog[logIndex] = String(timeBuf) + " - " + msg;
    logIndex = (logIndex + 1) % MAX_LOG_ENTRIES;
    if (logCount < MAX_LOG_ENTRIES) logCount++;
}

// ============== SENSOR FUNCTIONS ==============

float readDistance() {
    digitalWrite(TRIG_PIN, LOW);
    delayMicroseconds(2);
    digitalWrite(TRIG_PIN, HIGH);
    delayMicroseconds(10);
    digitalWrite(TRIG_PIN, LOW);
    long dur = pulseIn(ECHO_PIN, HIGH, 30000);
    if (dur == 0) return -1;
    return dur * 0.034 / 2.0;
}

void initRelayPins() {
    pinMode(relay1Pin, OUTPUT);
    pinMode(relay2Pin, OUTPUT);
    digitalWrite(relay1Pin, HIGH);
    digitalWrite(relay2Pin, HIGH);
}

void setRelay1(bool state) {
    if (relay1State != state) {
        addLog(relay1Name + (state ? " ON" : " OFF"));
    }
    relay1State = state;
    digitalWrite(relay1Pin, state ? LOW : HIGH);
}

void setRelay2(bool state) {
    if (relay2State != state) {
        addLog(relay2Name + (state ? " ON" : " OFF"));
    }
    relay2State = state;
    digitalWrite(relay2Pin, state ? LOW : HIGH);
}

// ============== CHUNKED HTML SENDER ==============

void sendChunk(WiFiClient &client, const char* data, size_t len) {
    const size_t CHUNK = 512;
    size_t sent = 0;
    while (sent < len) {
        size_t toSend = min(CHUNK, len - sent);
        size_t written = client.write(data + sent, toSend);
        if (written == 0) {
            delay(5);
            written = client.write(data + sent, toSend);
            if (written == 0) break;
        }
        sent += written;
        delay(1);
    }
}

// ============== HTML PARTS ==============

const char HTML_PART1[] PROGMEM = R"rawliteral(
<!DOCTYPE html>
<html lang="en">
<head>
<meta charset="UTF-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>Smart Classroom</title>
<link rel="icon" href="data:,">
<style>
*{margin:0;padding:0;box-sizing:border-box}
body{background:linear-gradient(135deg,#0f0c29,#302b63,#24243e);color:#e2e8f0;
font-family:'Segoe UI',system-ui,sans-serif;min-height:100vh}

.hdr{background:rgba(15,23,42,0.85);backdrop-filter:blur(20px);
border-bottom:1px solid rgba(255,255,255,0.08);padding:14px 20px;
display:flex;justify-content:space-between;align-items:center;
position:sticky;top:0;z-index:100}
.hdr h1{font-size:1.3rem;font-weight:700}
.hdr h1 span{background:linear-gradient(90deg,#38bdf8,#818cf8);
-webkit-background-clip:text;-webkit-text-fill-color:transparent}
.hr{display:flex;align-items:center;gap:10px;font-size:0.8rem;color:#94a3b8}
.dot{width:9px;height:9px;border-radius:50%;background:#22c55e;
box-shadow:0 0 8px #22c55e;display:inline-block;animation:p 2s infinite}
@keyframes p{0%,100%{opacity:1}50%{opacity:0.4}}

.db{max-width:1400px;margin:16px auto;padding:0 14px;
display:grid;grid-template-columns:repeat(auto-fit,minmax(300px,1fr));gap:14px}
.fw{grid-column:1/-1}

.cd{background:rgba(30,41,59,0.65);backdrop-filter:blur(16px);
border:1px solid rgba(255,255,255,0.06);border-radius:14px;padding:18px;
transition:transform 0.2s,box-shadow 0.2s}
.cd:hover{transform:translateY(-2px);box-shadow:0 8px 32px rgba(0,0,0,0.3)}
.ct{font-size:0.75rem;text-transform:uppercase;letter-spacing:1.5px;
color:#64748b;margin-bottom:10px;display:flex;align-items:center;gap:8px}

.sg{display:grid;grid-template-columns:repeat(auto-fit,minmax(130px,1fr));gap:10px}
.si{background:rgba(15,23,42,0.5);border-radius:10px;padding:14px;text-align:center;
border:1px solid rgba(255,255,255,0.04)}
.si-i{font-size:1.6rem;margin-bottom:4px}
.si-v{font-size:1.5rem;font-weight:700;line-height:1.2}
.si-l{font-size:0.68rem;color:#94a3b8;margin-top:3px;text-transform:uppercase;letter-spacing:1px}
.ab .si-v{color:#38bdf8} .ag .si-v{color:#4ade80}
.ao .si-v{color:#fb923c} .ap .si-v{color:#a78bfa}
.ar .si-v{color:#f87171} .ac .si-v{color:#22d3ee}

.occ-present .si-v{color:#4ade80}
.occ-absent .si-v{color:#f87171}
.occ-present{border:1px solid rgba(34,197,94,0.3);background:rgba(34,197,94,0.08)}
.occ-absent{border:1px solid rgba(248,113,113,0.2);background:rgba(248,113,113,0.06)}

.rr{display:flex;align-items:center;justify-content:space-between;
background:rgba(15,23,42,0.5);border-radius:10px;padding:12px 16px;
margin-bottom:8px;border:1px solid rgba(255,255,255,0.04);flex-wrap:wrap;gap:8px}
.ri{display:flex;align-items:center;gap:10px}
.ri-i{font-size:1.4rem}
.ri-n{font-weight:600;font-size:0.9rem}
.ri-s{font-size:0.7rem;color:#64748b}
.rs{font-size:0.65rem;font-weight:700;text-transform:uppercase;letter-spacing:1px;
padding:3px 9px;border-radius:20px}
.ron{background:rgba(34,197,94,0.2);color:#4ade80;box-shadow:0 0 10px rgba(34,197,94,0.15)}
.rof{background:rgba(248,113,113,0.2);color:#f87171}

.bg{display:flex;gap:5px}
.bt{padding:7px 14px;border:none;border-radius:7px;font-size:0.75rem;
font-weight:600;cursor:pointer;transition:all 0.2s;text-transform:uppercase;letter-spacing:0.5px}
.bn{background:linear-gradient(135deg,#22c55e,#16a34a);color:#fff}
.bn:hover{box-shadow:0 4px 14px rgba(34,197,94,0.4);transform:scale(1.05)}
.bf{background:linear-gradient(135deg,#ef4444,#dc2626);color:#fff}
.bf:hover{box-shadow:0 4px 14px rgba(239,68,68,0.4);transform:scale(1.05)}
.bs{background:linear-gradient(135deg,#3b82f6,#2563eb);color:#fff}
.bs:hover{box-shadow:0 4px 14px rgba(59,130,246,0.4);transform:scale(1.05)}

.mt{display:flex;align-items:center;justify-content:center;gap:10px;margin-bottom:12px}
.tl{font-size:0.82rem;font-weight:600}
.sw{position:relative;width:48px;height:26px}
.sw input{opacity:0;width:0;height:0}
.sl{position:absolute;cursor:pointer;top:0;left:0;right:0;bottom:0;
background:#475569;border-radius:26px;transition:0.3s}
.sl:before{content:"";position:absolute;height:20px;width:20px;left:3px;bottom:3px;
background:#fff;border-radius:50%;transition:0.3s}
input:checked+.sl{background:linear-gradient(135deg,#38bdf8,#818cf8)}
input:checked+.sl:before{transform:translateX(22px)}
)rawliteral";

const char HTML_PART2[] PROGMEM = R"rawliteral(
.ax{display:flex;flex-direction:column;gap:7px}
.al{display:flex;align-items:center;gap:9px;padding:9px 12px;
border-radius:9px;font-size:0.78rem;font-weight:500;animation:si 0.3s ease}
@keyframes si{from{opacity:0;transform:translateX(-8px)}to{opacity:1;transform:translateX(0)}}
.aw{background:rgba(251,191,36,0.15);border:1px solid rgba(251,191,36,0.3);color:#fbbf24}
.ad{background:rgba(239,68,68,0.15);border:1px solid rgba(239,68,68,0.3);color:#f87171}
.ai{background:rgba(56,189,248,0.15);border:1px solid rgba(56,189,248,0.3);color:#38bdf8}
.ak{background:rgba(34,197,94,0.15);border:1px solid rgba(34,197,94,0.3);color:#4ade80}

.lb{background:rgba(2,6,23,0.6);border-radius:9px;padding:10px;
max-height:180px;overflow-y:auto;font-family:'Courier New',monospace;
font-size:0.72rem;line-height:1.7;color:#94a3b8;border:1px solid rgba(255,255,255,0.04)}
.lb::-webkit-scrollbar{width:3px}
.lb::-webkit-scrollbar-thumb{background:#334155;border-radius:2px}

.cc{position:relative;height:200px;width:100%}

.eg{display:grid;grid-template-columns:repeat(auto-fit,minmax(110px,1fr));gap:8px;margin-top:10px}
.ei{text-align:center;padding:8px;background:rgba(15,23,42,0.5);border-radius:9px}
.ev{font-size:1.1rem;font-weight:700;color:#fbbf24}
.el{font-size:0.65rem;color:#64748b;margin-top:2px;text-transform:uppercase}

.ft{text-align:center;padding:16px;font-size:0.7rem;color:#475569}

.modal-bg{display:none;position:fixed;top:0;left:0;width:100%;height:100%;
background:rgba(0,0,0,0.6);z-index:200;justify-content:center;align-items:center}
.modal-bg.show{display:flex}
.modal{background:rgba(30,41,59,0.95);backdrop-filter:blur(20px);
border:1px solid rgba(255,255,255,0.1);border-radius:16px;padding:24px;
max-width:480px;width:92%;animation:mIn 0.3s ease}
@keyframes mIn{from{opacity:0;transform:scale(0.9)}to{opacity:1;transform:scale(1)}}
.modal h3{margin-bottom:6px;font-size:1.05rem;color:#38bdf8}
.modal-sub{font-size:0.72rem;color:#64748b;margin-bottom:18px}

.cfg-card{background:rgba(15,23,42,0.5);border:1px solid rgba(255,255,255,0.06);
border-radius:12px;padding:16px;margin-bottom:12px}
.cfg-card-title{font-size:0.8rem;font-weight:700;color:#e2e8f0;margin-bottom:12px;
display:flex;align-items:center;gap:8px}
.cfg-card-title span{font-size:1.2rem}
.cfg-row{display:flex;align-items:center;gap:10px;margin-bottom:10px}
.cfg-row:last-child{margin-bottom:0}
.cfg-label{font-size:0.75rem;font-weight:600;color:#94a3b8;min-width:75px}

.cfg-select{background:rgba(30,41,59,0.9);border:1px solid rgba(255,255,255,0.15);
color:#e2e8f0;padding:8px 12px;border-radius:8px;font-size:0.8rem;
font-family:'Segoe UI',system-ui,sans-serif;cursor:pointer;
appearance:none;-webkit-appearance:none;
background-image:url("data:image/svg+xml,%3Csvg xmlns='http://www.w3.org/2000/svg' width='12' height='12' viewBox='0 0 24 24' fill='none' stroke='%2394a3b8' stroke-width='2'%3E%3Cpath d='M6 9l6 6 6-6'/%3E%3C/svg%3E");
background-repeat:no-repeat;background-position:right 10px center;
padding-right:30px;transition:all 0.2s;flex:1;min-width:0}
.cfg-select:hover{border-color:#38bdf8;background-color:rgba(56,189,248,0.08)}
.cfg-select:focus{outline:none;border-color:#38bdf8;box-shadow:0 0 0 3px rgba(56,189,248,0.15)}
.cfg-select option{background:#1e293b;color:#e2e8f0;padding:8px}

.cfg-preview{margin-top:14px;padding:12px;background:rgba(56,189,248,0.06);
border:1px solid rgba(56,189,248,0.15);border-radius:10px;
font-size:0.75rem;color:#38bdf8}
.cfg-preview-title{font-weight:700;margin-bottom:6px;font-size:0.7rem;
text-transform:uppercase;letter-spacing:1px;color:#64748b}
.cfg-preview-row{display:flex;justify-content:space-between;padding:3px 0}
.cfg-preview-val{font-weight:700;color:#e2e8f0}

.cfg-warn{margin-top:10px;padding:8px 12px;background:rgba(251,191,36,0.1);
border:1px solid rgba(251,191,36,0.2);border-radius:8px;font-size:0.72rem;
color:#fbbf24;display:none;align-items:center;gap:6px}

.modal-btns{display:flex;gap:8px;margin-top:16px;justify-content:flex-end}
.modal-btns .bt{padding:9px 18px}

.cfg-msg{margin-top:10px;padding:10px 14px;border-radius:8px;font-size:0.78rem;
font-weight:600;display:none;text-align:center;animation:si 0.3s ease}
.cfg-msg.ok{display:block;background:rgba(34,197,94,0.15);
border:1px solid rgba(34,197,94,0.3);color:#4ade80}
.cfg-msg.err{display:block;background:rgba(239,68,68,0.15);
border:1px solid rgba(239,68,68,0.3);color:#f87171}
</style>
</head>
<body>

<div class="hdr">
<h1>🎓 <span>Smart Classroom</span></h1>
<div class="hr"><span class="dot"></span><span>Online</span>
<span>|</span><span id="up">00:00:00</span></div>
</div>

<div class="db">

<div class="cd">
<div class="ct">📡 Live Sensors</div>
<div class="sg">
<div class="si occ-absent" id="occCard"><div class="si-i">👤</div><div class="si-v" id="occStatus">Absent</div><div class="si-l">Occupancy</div></div>
<div class="si ap"><div class="si-i">📏</div><div class="si-v" id="ds">--</div><div class="si-l">Dist cm</div></div>
<div class="si ao"><div class="si-i">🔌</div><div class="si-v" id="pw">0</div><div class="si-l">Watts</div></div>
<div class="si ar"><div class="si-i">⚡</div><div class="si-v" id="te">0</div><div class="si-l">Wh Used</div></div>
</div>
</div>

<div class="cd">
<div class="ct">🎮 Controls</div>
<div class="mt">
<span class="tl">Manual</span>
<label class="sw"><input type="checkbox" id="mg" checked onchange="tm()"><span class="sl"></span></label>
<span class="tl">Auto</span>
<span id="ml" style="margin-left:6px;font-size:0.72rem;color:#38bdf8">● AUTO</span>
</div>
<div class="rr">
<div class="ri"><span class="ri-i" id="r1icon">💡</span><div><div class="ri-n" id="r1name">Light</div><div class="ri-s" id="r1info">Pin 26 · 40W</div></div></div>
<span class="rs rof" id="s1">OFF</span>
<div class="bg"><button class="bt bn" onclick="cr(1,1)">ON</button><button class="bt bf" onclick="cr(1,0)">OFF</button></div>
</div>
<div class="rr">
<div class="ri"><span class="ri-i" id="r2icon">🌀</span><div><div class="ri-n" id="r2name">Fan</div><div class="ri-s" id="r2info">Pin 25 · 75W</div></div></div>
<span class="rs rof" id="s2">OFF</span>
<div class="bg"><button class="bt bn" onclick="cr(2,1)">ON</button><button class="bt bf" onclick="cr(2,0)">OFF</button></div>
</div>
<div style="text-align:center;margin-top:10px">
<button class="bt bs" onclick="openPortCfg()">⚙️ Configure Ports & Appliances</button>
</div>
</div>

<div class="cd">
<div class="ct">🔔 Alerts</div>
<div class="ax" id="ab"><div class="al ai">⏳ Waiting for data...</div></div>
</div>
)rawliteral";

const char HTML_PART3[] PROGMEM = R"rawliteral(
<div class="cd">
<div class="ct">⚡ Power Usage</div>
<div class="cc"><canvas id="pc"></canvas></div>
<div class="eg">
<div class="ei"><div class="ev" id="lw">0</div><div class="el" id="lw_label">Light Wh</div></div>
<div class="ei"><div class="ev" id="fw">0</div><div class="el" id="fw_label">Fan Wh</div></div>
<div class="ei"><div class="ev" id="tc">0.00</div><div class="el">Cost ₹</div></div>
<div class="ei"><div class="ev" id="av">0</div><div class="el">Avg W</div></div>
</div>
</div>

<div class="cd fw">
<div class="ct">📋 Event Log</div>
<div class="lb" id="lg">Starting...</div>
</div>

</div>
<div class="ft">Smart Classroom IoT &bull; ESP32</div>

<!-- ========== PORT CONFIG MODAL WITH DROPDOWNS ========== -->
<div class="modal-bg" id="portModal">
<div class="modal">
<h3>⚙️ Port & Appliance Configuration</h3>
<div class="modal-sub">Assign appliances to ESP32 GPIO pins using the dropdowns below</div>

<!-- RELAY 1 CONFIG CARD -->
<div class="cfg-card">
<div class="cfg-card-title"><span>1️⃣</span> Relay Channel 1</div>
<div class="cfg-row">
<span class="cfg-label">Appliance:</span>
<select class="cfg-select" id="cfg_r1name" onchange="updatePreview()">
<option value="Light">💡 Light / Bulb</option>
<option value="Fan">🌀 Fan</option>
<option value="AC">❄️ Air Conditioner</option>
<option value="Projector">📽️ Projector</option>
<option value="Heater">🔥 Heater</option>
<option value="Motor">⚙️ Motor / Pump</option>
<option value="LED Strip">🌈 LED Strip</option>
<option value="Exhaust">🌬️ Exhaust Fan</option>
<option value="Speaker">🔊 Speaker / Amp</option>
<option value="Charger">🔋 Charger Station</option>
<option value="SmartBoard">📺 Smart Board</option>
<option value="CCTV">📹 CCTV Camera</option>
<option value="Custom">🔧 Custom Device</option>
</select>
</div>
<div class="cfg-row">
<span class="cfg-label">GPIO Pin:</span>
<select class="cfg-select" id="cfg_r1pin" onchange="updatePreview()">
<option value="2">GPIO 2</option>
<option value="4">GPIO 4</option>
<option value="5">GPIO 5</option>
<option value="12">GPIO 12</option>
<option value="13">GPIO 13</option>
<option value="15">GPIO 15</option>
<option value="16">GPIO 16</option>
<option value="17">GPIO 17</option>
<option value="18">GPIO 18</option>
<option value="19">GPIO 19</option>
<option value="21">GPIO 21</option>
<option value="22">GPIO 22</option>
<option value="23">GPIO 23</option>
<option value="25">GPIO 25</option>
<option value="26" selected>GPIO 26</option>
<option value="27">GPIO 27</option>
</select>
</div>
</div>

<!-- RELAY 2 CONFIG CARD -->
<div class="cfg-card">
<div class="cfg-card-title"><span>2️⃣</span> Relay Channel 2</div>
<div class="cfg-row">
<span class="cfg-label">Appliance:</span>
<select class="cfg-select" id="cfg_r2name" onchange="updatePreview()">
<option value="Light">💡 Light / Bulb</option>
<option value="Fan" selected>🌀 Fan</option>
<option value="AC">❄️ Air Conditioner</option>
<option value="Projector">📽️ Projector</option>
<option value="Heater">🔥 Heater</option>
<option value="Motor">⚙️ Motor / Pump</option>
<option value="LED Strip">🌈 LED Strip</option>
<option value="Exhaust">🌬️ Exhaust Fan</option>
<option value="Speaker">🔊 Speaker / Amp</option>
<option value="Charger">🔋 Charger Station</option>
<option value="SmartBoard">📺 Smart Board</option>
<option value="CCTV">📹 CCTV Camera</option>
<option value="Custom">🔧 Custom Device</option>
</select>
</div>
<div class="cfg-row">
<span class="cfg-label">GPIO Pin:</span>
<select class="cfg-select" id="cfg_r2pin" onchange="updatePreview()">
<option value="2">GPIO 2</option>
<option value="4">GPIO 4</option>
<option value="5">GPIO 5</option>
<option value="12">GPIO 12</option>
<option value="13">GPIO 13</option>
<option value="15">GPIO 15</option>
<option value="16">GPIO 16</option>
<option value="17">GPIO 17</option>
<option value="18">GPIO 18</option>
<option value="19">GPIO 19</option>
<option value="21">GPIO 21</option>
<option value="22">GPIO 22</option>
<option value="23">GPIO 23</option>
<option value="25" selected>GPIO 25</option>
<option value="26">GPIO 26</option>
<option value="27">GPIO 27</option>
</select>
</div>
</div>

<!-- LIVE PREVIEW -->
<div class="cfg-preview" id="cfgPreview">
<div class="cfg-preview-title">📋 Configuration Preview</div>
<div class="cfg-preview-row"><span>Channel 1:</span><span class="cfg-preview-val" id="pv1">Light → GPIO 26</span></div>
<div class="cfg-preview-row"><span>Channel 2:</span><span class="cfg-preview-val" id="pv2">Fan → GPIO 25</span></div>
</div>

<!-- DUPLICATE PIN WARNING -->
<div class="cfg-warn" id="cfgWarn">⚠️ Both channels use the same GPIO pin! Please select different pins.</div>

<!-- STATUS MESSAGE -->
<div class="cfg-msg" id="cfgMsg"></div>

<!-- BUTTONS -->
<div class="modal-btns">
<button class="bt bf" onclick="closePortCfg()">✕ Cancel</button>
<button class="bt bn" id="saveBtn" onclick="savePortCfg()">💾 Save & Apply</button>
</div>
</div>
</div>

<script src="https://cdn.jsdelivr.net/npm/chart.js"></script>
<script>
const MP=30;
let lb=[],pd=[],ld=[],fd=[];
let ns=0,ps=0;

// Appliance icon mapping
const appIcons={
'Light':'💡','Fan':'🌀','AC':'❄️','Projector':'📽️',
'Heater':'🔥','Motor':'⚙️','LED Strip':'🌈','Exhaust':'🌬️',
'Speaker':'🔊','Charger':'🔋','SmartBoard':'📺','CCTV':'📹','Custom':'🔧'
};

function getIcon(name){return appIcons[name]||'🔌';}

const pc=new Chart(document.getElementById('pc'),{
type:'bar',
data:{labels:lb,datasets:[
{label:'Appl 1 W',data:ld,backgroundColor:'rgba(251,191,36,0.7)',borderRadius:3,barPercentage:0.5},
{label:'Appl 2 W',data:fd,backgroundColor:'rgba(56,189,248,0.7)',borderRadius:3,barPercentage:0.5},
{label:'Total W',data:pd,type:'line',borderColor:'#f87171',backgroundColor:'transparent',tension:0.4,borderWidth:2,pointRadius:0}
]},
options:{responsive:true,maintainAspectRatio:false,animation:false,
plugins:{legend:{labels:{color:'#94a3b8',font:{size:10}}}},
scales:{x:{stacked:true,ticks:{color:'#475569',maxTicksLimit:6,font:{size:8}},grid:{color:'rgba(255,255,255,0.03)'}},
y:{ticks:{color:'#475569',font:{size:9}},grid:{color:'rgba(255,255,255,0.03)'}}}}
});

function cr(r,s){fetch('/relay'+r+'?state='+(s?'on':'off'));}

function tm(){
let a=document.getElementById('mg').checked;
fetch('/mode?auto='+(a?'1':'0'));
document.getElementById('ml').innerHTML=a?'<span style="color:#38bdf8">● AUTO</span>':'<span style="color:#fb923c">● MANUAL</span>';
}

// ===== PORT CONFIG MODAL FUNCTIONS =====

function updatePreview(){
let n1=document.getElementById('cfg_r1name').value;
let p1=document.getElementById('cfg_r1pin').value;
let n2=document.getElementById('cfg_r2name').value;
let p2=document.getElementById('cfg_r2pin').value;

document.getElementById('pv1').innerText=getIcon(n1)+' '+n1+' → GPIO '+p1;
document.getElementById('pv2').innerText=getIcon(n2)+' '+n2+' → GPIO '+p2;

// Check duplicate pins
let warn=document.getElementById('cfgWarn');
let saveBtn=document.getElementById('saveBtn');
if(p1===p2){
warn.style.display='flex';
saveBtn.style.opacity='0.5';saveBtn.style.pointerEvents='none';
}else{
warn.style.display='none';
saveBtn.style.opacity='1';saveBtn.style.pointerEvents='auto';
}
}

function openPortCfg(){
document.getElementById('portModal').classList.add('show');
document.getElementById('cfgMsg').className='cfg-msg';
document.getElementById('cfgMsg').style.display='none';

fetch('/getports').then(r=>r.json()).then(d=>{
document.getElementById('cfg_r1name').value=d.r1name;
document.getElementById('cfg_r1pin').value=d.r1pin;
document.getElementById('cfg_r2name').value=d.r2name;
document.getElementById('cfg_r2pin').value=d.r2pin;
updatePreview();
});
}

function closePortCfg(){
document.getElementById('portModal').classList.remove('show');
}

function savePortCfg(){
let r1n=document.getElementById('cfg_r1name').value;
let r1p=document.getElementById('cfg_r1pin').value;
let r2n=document.getElementById('cfg_r2name').value;
let r2p=document.getElementById('cfg_r2pin').value;

if(r1p===r2p){return;}

let msg=document.getElementById('cfgMsg');
msg.innerText='⏳ Applying changes...';
msg.className='cfg-msg ai';msg.style.display='block';

fetch('/setports?r1name='+encodeURIComponent(r1n)+'&r1pin='+r1p+'&r2name='+encodeURIComponent(r2n)+'&r2pin='+r2p)
.then(r=>r.text()).then(t=>{
msg.innerText='✅ '+t;
msg.className='cfg-msg ok';
setTimeout(()=>{closePortCfg();},2000);
}).catch(e=>{
msg.innerText='❌ Failed to save! Check connection.';
msg.className='cfg-msg err';
});
}

// ===== ALERTS =====

function ga(d){
let h='';
if(d.occupied)h+='<div class="al ai">👤 Room Occupied - Presence Detected</div>';
else h+='<div class="al aw">🚪 Room Vacant - No Presence</div>';
if(d.r1)h+='<div class="al ai">'+getIcon(d.r1name)+' '+d.r1name+' ON</div>';
if(d.r2)h+='<div class="al ai">'+getIcon(d.r2name)+' '+d.r2name+' ON</div>';
if(d.powerNow==0)h+='<div class="al ak">🔋 Zero power draw</div>';
document.getElementById('ab').innerHTML=h||'<div class="al ak">✅ Normal</div>';
}

function fu(s){
let h=Math.floor(s/3600),m=Math.floor((s%3600)/60),sc=s%60;
return String(h).padStart(2,'0')+':'+String(m).padStart(2,'0')+':'+String(sc).padStart(2,'0');
}

// ===== MAIN POLLING =====

function poll(){
fetch('/status').then(r=>r.json()).then(d=>{
let occCard=document.getElementById('occCard');
let occStatus=document.getElementById('occStatus');
if(d.occupied){
occStatus.innerText='Present';
occCard.className='si occ-present';
}else{
occStatus.innerText='Absent';
occCard.className='si occ-absent';
}

document.getElementById('ds').innerText=d.dist>=0?d.dist.toFixed(1):'--';
document.getElementById('pw').innerText=d.powerNow.toFixed(0);
document.getElementById('te').innerText=d.totalWh.toFixed(1);
document.getElementById('up').innerText=fu(d.uptime);

document.getElementById('r1icon').innerText=getIcon(d.r1name);
document.getElementById('r2icon').innerText=getIcon(d.r2name);
document.getElementById('r1name').innerText=d.r1name;
document.getElementById('r2name').innerText=d.r2name;
document.getElementById('r1info').innerText='GPIO '+d.r1pin+' · '+d.lightW_rated+'W';
document.getElementById('r2info').innerText='GPIO '+d.r2pin+' · '+d.fanW_rated+'W';
document.getElementById('lw_label').innerText=d.r1name+' Wh';
document.getElementById('fw_label').innerText=d.r2name+' Wh';

let e1=document.getElementById('s1');
e1.innerText=d.r1?'ON':'OFF';e1.className='rs '+(d.r1?'ron':'rof');
let e2=document.getElementById('s2');
e2.innerText=d.r2?'ON':'OFF';e2.className='rs '+(d.r2?'ron':'rof');

document.getElementById('mg').checked=d.autoMode;
document.getElementById('ml').innerHTML=d.autoMode?'<span style="color:#38bdf8">● AUTO</span>':'<span style="color:#fb923c">● MANUAL</span>';

document.getElementById('lw').innerText=d.lightWh.toFixed(1);
document.getElementById('fw').innerText=d.fanWh.toFixed(1);
document.getElementById('tc').innerText=d.cost.toFixed(2);
ns++;ps+=d.powerNow;
document.getElementById('av').innerText=(ps/ns).toFixed(0);

pc.data.datasets[0].label=d.r1name+' W';
pc.data.datasets[1].label=d.r2name+' W';

let t=new Date().toLocaleTimeString();
if(lb.length>=MP){lb.shift();pd.shift();ld.shift();fd.shift();}
lb.push(t);
pd.push(d.powerNow);ld.push(d.lightW);fd.push(d.fanW);
pc.update('none');
ga(d);

if(d.logs&&d.logs.length){
let lh='';d.logs.forEach(l=>{lh+='<div>'+l+'</div>';});
document.getElementById('lg').innerHTML=lh;
}
}).catch(e=>{
document.getElementById('ab').innerHTML='<div class="al ad">❌ Connection lost!</div>';
});
}

setInterval(poll,2500);
setTimeout(poll,500);
</script>
</body>
</html>
)rawliteral";

// ============== WEB HANDLERS ==============

void handleRoot() {
    WiFiClient client = server.client();

    client.println("HTTP/1.1 200 OK");
    client.println("Content-Type: text/html; charset=utf-8");
    client.println("Connection: close");
    client.println("Cache-Control: no-cache");
    client.println();

    sendChunk(client, HTML_PART1, strlen_P(HTML_PART1));
    sendChunk(client, HTML_PART2, strlen_P(HTML_PART2));
    sendChunk(client, HTML_PART3, strlen_P(HTML_PART3));

    client.stop();
    server.send(200, "text/html", "");
}

void handleStatus() {
    unsigned long uptimeSec = (millis() - bootTime) / 1000;

    float lightWh = (relay1OnSeconds / 3600.0) * LIGHT_WATTS;
    float fanWh   = (relay2OnSeconds / 3600.0) * FAN_WATTS;
    float currentPower = (relay1State ? LIGHT_WATTS : 0) + (relay2State ? FAN_WATTS : 0);
    float cost = ((lightWh + fanWh) / 1000.0) * COST_PER_KWH;

    String json = "{";
    json += "\"occupied\":" + String(roomOccupied ? "true" : "false");
    json += ",\"dist\":" + String(lastDistance, 1);
    json += ",\"r1\":" + String(relay1State ? "true" : "false");
    json += ",\"r2\":" + String(relay2State ? "true" : "false");
    json += ",\"autoMode\":" + String(autoMode ? "true" : "false");
    json += ",\"powerNow\":" + String(currentPower, 1);
    json += ",\"lightW\":" + String(relay1State ? LIGHT_WATTS : 0, 1);
    json += ",\"fanW\":" + String(relay2State ? FAN_WATTS : 0, 1);
    json += ",\"lightW_rated\":" + String(LIGHT_WATTS, 0);
    json += ",\"fanW_rated\":" + String(FAN_WATTS, 0);
    json += ",\"totalWh\":" + String(lightWh + fanWh, 2);
    json += ",\"lightWh\":" + String(lightWh, 2);
    json += ",\"fanWh\":" + String(fanWh, 2);
    json += ",\"cost\":" + String(cost, 2);
    json += ",\"uptime\":" + String(uptimeSec);
    json += ",\"r1name\":\"" + relay1Name + "\"";
    json += ",\"r2name\":\"" + relay2Name + "\"";
    json += ",\"r1pin\":" + String(relay1Pin);
    json += ",\"r2pin\":" + String(relay2Pin);

    json += ",\"logs\":[";
    for (int i = 0; i < logCount; i++) {
        int idx = (logCount < MAX_LOG_ENTRIES) ? i : (logIndex + i) % MAX_LOG_ENTRIES;
        if (i > 0) json += ",";
        String safe = eventLog[idx];
        safe.replace("\"", "'");
        json += "\"" + safe + "\"";
    }
    json += "]";
    json += "}";

    server.send(200, "application/json", json);
}

void handleRelay1() {
    if (!autoMode) {
        bool s = server.arg("state") == "on";
        setRelay1(s);
    }
    server.send(200, "text/plain", autoMode ? "AUTO" : "OK");
}

void handleRelay2() {
    if (!autoMode) {
        bool s = server.arg("state") == "on";
        setRelay2(s);
    }
    server.send(200, "text/plain", autoMode ? "AUTO" : "OK");
}

void handleMode() {
    autoMode = server.arg("auto") == "1";
    addLog(autoMode ? "Switched AUTO" : "Switched MANUAL");
    server.send(200, "text/plain", "OK");
}

void handleGetPorts() {
    String json = "{";
    json += "\"r1name\":\"" + relay1Name + "\"";
    json += ",\"r1pin\":" + String(relay1Pin);
    json += ",\"r2name\":\"" + relay2Name + "\"";
    json += ",\"r2pin\":" + String(relay2Pin);
    json += "}";
    server.send(200, "application/json", json);
}

void handleSetPorts() {
    // Validate pin numbers
    int newPin1 = server.hasArg("r1pin") ? server.arg("r1pin").toInt() : relay1Pin;
    int newPin2 = server.hasArg("r2pin") ? server.arg("r2pin").toInt() : relay2Pin;

    // Safety: prevent duplicate pins
    if (newPin1 == newPin2) {
        server.send(400, "text/plain", "Error: Both channels cannot use the same GPIO pin!");
        return;
    }

    // Safety: prevent using reserved sensor pins
    int reserved[] = {PIR_PIN, TRIG_PIN, ECHO_PIN, 0, 1, 3, 6, 7, 8, 9, 10, 11};
    int resCount = sizeof(reserved) / sizeof(reserved[0]);
    for (int i = 0; i < resCount; i++) {
        if (newPin1 == reserved[i] || newPin2 == reserved[i]) {
            server.send(400, "text/plain", "Error: GPIO " + String(reserved[i]) + " is reserved for sensors!");
            return;
        }
    }

    // Turn off current relays before changing pins
    setRelay1(false);
    setRelay2(false);

    // Release old pins
    pinMode(relay1Pin, INPUT);
    pinMode(relay2Pin, INPUT);

    // Apply new values
    if (server.hasArg("r1name")) relay1Name = server.arg("r1name");
    if (server.hasArg("r2name")) relay2Name = server.arg("r2name");
    relay1Pin = newPin1;
    relay2Pin = newPin2;

    // Initialize new pins
    initRelayPins();

    addLog("Config: " + relay1Name + "→GPIO" + String(relay1Pin) + ", " + relay2Name + "→GPIO" + String(relay2Pin));

    server.send(200, "text/plain", "Saved! " + relay1Name + " → GPIO" + String(relay1Pin) + ", " + relay2Name + " → GPIO" + String(relay2Pin));
}

void handleFavicon() {
    server.send(204, "", "");
}

void handleNotFound() {
    server.send(404, "text/plain", "Not Found");
}

// ============== SETUP ==============

void setup() {
    Serial.begin(115200);
    bootTime = millis();

    pinMode(PIR_PIN, INPUT);
    pinMode(TRIG_PIN, OUTPUT);
    pinMode(ECHO_PIN, INPUT);

    initRelayPins();

    WiFi.softAP(ssid, password);
    delay(500);

    Serial.println("\n=== Smart Classroom System ===");
    Serial.print("WiFi AP: ");
    Serial.println(ssid);
    Serial.print("Dashboard: http://");
    Serial.println(WiFi.softAPIP());

    server.on("/", HTTP_GET, handleRoot);
    server.on("/status", HTTP_GET, handleStatus);
    server.on("/relay1", HTTP_GET, handleRelay1);
    server.on("/relay2", HTTP_GET, handleRelay2);
    server.on("/mode", HTTP_GET, handleMode);
    server.on("/getports", HTTP_GET, handleGetPorts);
    server.on("/setports", HTTP_GET, handleSetPorts);
    server.on("/favicon.ico", HTTP_GET, handleFavicon);
    server.onNotFound(handleNotFound);

    server.begin();
    Serial.println("Web server started!");

    addLog("System booted");
    addLog("WiFi AP: " + String(ssid));
}

// ============== LOOP ==============

void loop() {
    server.handleClient();

    // PIR - Presence Detection
    bool currentPir = digitalRead(PIR_PIN);
    if (currentPir && !pirDetected) {
        addLog("Presence detected");
    }
    if (currentPir) lastPIRTrigger = millis();
    pirDetected = currentPir;

    bool wasRecentlyOccupied = (millis() - lastPIRTrigger < vacantDelay);
    bool newOccupied = pirDetected || wasRecentlyOccupied;

    if (roomOccupied && !newOccupied) {
        addLog("Room now vacant");
    } else if (!roomOccupied && newOccupied) {
        addLog("Room now occupied");
    }
    roomOccupied = newOccupied;

    // Ultrasonic distance
    float dist = readDistance();
    lastDistance = dist;

    // Power tracking
    if (millis() - lastPowerCalc >= powerInterval) {
        float elapsed = (millis() - lastPowerCalc) / 1000.0;
        if (relay1State) relay1OnSeconds += elapsed;
        if (relay2State) relay2OnSeconds += elapsed;
        lastPowerCalc = millis();
    }

    // Auto control
    if (autoMode) {
        if (roomOccupied) {
            setRelay1(true);
            setRelay2(true);
        } else {
            setRelay1(false);
            setRelay2(false);
        }
    }

    delay(100);
}