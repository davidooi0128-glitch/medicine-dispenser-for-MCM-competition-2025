#include <ESP8266WiFi.h>
#include <ESP8266WebServer.h>
#include <SoftwareSerial.h>

// ---------------- Wi-Fi AP ----------------
const char *ssid = "Medical_Device_WiFi";
const char *password = "12345678";
ESP8266WebServer server(80);

// Link to Arduino Nano (ESP8266 board labels, NOT Nano pin numbers):
// ESP D5/GPIO14 = RX from Nano D3 TX (through a 5V-to-3.3V divider)
// ESP D6/GPIO12 = TX to Nano D2 RX
SoftwareSerial nanoSerial(3, 1); // RX, TX

struct DoseConfig {
  int startMinute;
  int endMinute;
  int reminderMinute;
  int solutionPulses;
  int tabletCount;
};

struct DoseLog {
  uint16_t startDay;
  uint8_t doseNumber;
  uint16_t startMinute;
  uint16_t finishDay;
  uint16_t finishMinute;
  uint8_t targetPills;
  uint8_t actualPills;
  uint16_t targetPulses;
  uint16_t actualPulses;
  bool completed;
};

const uint8_t MAX_DOSES = 3;
const uint8_t MAX_LOGS = 40;
DoseConfig doses[MAX_DOSES];
DoseLog logs[MAX_LOGS];
uint8_t logCount = 0;
int numDoses = 0;

bool configKnown = false;
bool deviceBusy = false;
uint16_t currentDay = 1;           // Runtime only; not saved to flash/EEPROM.
uint16_t currentMinute = 0;
unsigned long lastNanoMessage = 0;
unsigned long lastConfigRequest = 0;
unsigned long saveSentAt = 0;
String saveStatus = "idle";
String saveMessage = "No pending changes.";
String nanoRxLine = "";

// ---------------- UI: Home ----------------
const char HOME_HTML[] PROGMEM = R"rawliteral(
<!doctype html><html><head><meta name="viewport" content="width=device-width,initial-scale=1">
<title>Pill Ping | Dashboard</title><style>
:root{--ink:#172b3a;--muted:#687b88;--line:#e0e9ed;--paper:#f5f8fa;--teal:#147d78;--teal2:#e1f4f1;--blue:#244e68;--white:#fff;--red:#a43d3d}
*{box-sizing:border-box}body{margin:0;background:var(--paper);font-family:Inter,Arial,sans-serif;color:var(--ink)}.wrap{max-width:980px;margin:0 auto;padding:22px}
header{display:flex;align-items:center;justify-content:space-between;gap:12px;margin:6px 0 25px}.brand{display:flex;align-items:center;gap:12px}.logo{width:46px;height:46px;border-radius:14px;background:var(--teal);color:white;display:grid;place-items:center;font-size:25px;font-weight:700}.eyebrow{font-size:11px;letter-spacing:1.7px;font-weight:800;color:var(--teal);text-transform:uppercase}.brand h1{font-size:21px;margin:3px 0 0}.chip{padding:8px 11px;border-radius:99px;background:#e9eff2;color:var(--muted);font-size:12px;font-weight:700}.hero{background:linear-gradient(125deg,#183d55,#176d70);border-radius:24px;padding:27px;color:white;box-shadow:0 12px 28px #173f5515}.hero .eyebrow{color:#a8e7df}.hero h2{font-size:clamp(26px,5vw,38px);margin:10px 0 8px;letter-spacing:-.6px}.hero p{margin:0;color:#d7e8ec;line-height:1.55;max-width:600px}.statusline{display:flex;gap:10px;align-items:center;flex-wrap:wrap;margin-top:22px}.statuspill{background:#ffffff19;border:1px solid #ffffff28;border-radius:12px;padding:10px 13px;font-size:13px}.dot{display:inline-block;width:8px;height:8px;border-radius:50%;background:#94e0ad;margin-right:7px}.section-title{margin:28px 0 13px;font-size:16px}.tiles{display:grid;grid-template-columns:repeat(2,minmax(0,1fr));gap:15px}.tile{display:block;text-decoration:none;background:white;border:1px solid var(--line);border-radius:20px;padding:22px;color:var(--ink);transition:transform .16s,border-color .16s}.tile:hover{transform:translateY(-2px);border-color:#99c9c3}.tileicon{width:43px;height:43px;border-radius:13px;display:grid;place-items:center;background:var(--teal2);color:var(--teal);font-size:23px}.tile h3{font-size:19px;margin:17px 0 7px}.tile p{margin:0;color:var(--muted);line-height:1.5;font-size:14px}.go{display:inline-block;margin-top:17px;color:var(--teal);font-weight:800;font-size:13px}.note{margin-top:20px;color:var(--muted);font-size:12px;line-height:1.5}.foot{margin-top:28px;text-align:center;color:#8a9aa3;font-size:11px}
@media(max-width:600px){.wrap{padding:15px}.tiles{grid-template-columns:1fr}.hero{padding:22px}.chip{display:none}header{margin-bottom:17px}}
</style></head><body><main class="wrap">
<header><div class="brand"><div class="logo">P+</div><div><div class="eyebrow">Pill Ping · Prototype</div><h1>Medical Dispenser</h1></div></div><div class="chip">Local device dashboard</div></header>
<section class="hero"><div class="eyebrow">System overview</div><h2>Care, on schedule.</h2><p>Review the saved dose plan and dispensing history, or configure the next schedule from this device's local Wi-Fi network.</p><div class="statusline"><div class="statuspill"><span class="dot" id="dot"></span><span id="connection">Checking controller…</span></div><div class="statuspill" id="clock">Simulated Day 1 · 00:00</div><div class="statuspill" id="profile">Loading profile…</div></div></section>
<h2 class="section-title">Choose a workspace</h2><section class="tiles">
<a class="tile" href="/logs"><div class="tileicon">▤</div><h3>Data &amp; Logs</h3><p>View the saved configuration for each dose and the actual dispensing time, simulated day, tablet count, and solution pulses.</p><span class="go">OPEN DATA &amp; LOGS →</span></a>
<a class="tile" href="/config"><div class="tileicon">⚙</div><h3>Configuration</h3><p>Set up 1–3 daily doses, each dose window, reminder time, solution amount, and tablet quantity.</p><span class="go">CONFIGURE SCHEDULE →</span></a>
</section><p class="note">Prototype time is accelerated, not real-world time. Day and event logs are held in runtime memory and reset when the device is powered off or restarted. The schedule itself is stored by the Arduino Nano.</p><div class="foot">PILL PING · LOCAL NETWORK ONLY</div></main>
<script>
function fmt(m){m=Number(m)||0;return String(Math.floor(m/60)%24).padStart(2,'0')+':'+String(m%60).padStart(2,'0')}
async function refresh(){try{const r=await fetch('/api/state');const s=await r.json();document.getElementById('connection').textContent=s.online?(s.busy?'Controller connected · dispensing':'Controller connected'):'Waiting for controller';document.getElementById('dot').style.background=s.online?(s.busy?'#ffd078':'#94e0ad'):'#ff9b9b';document.getElementById('clock').textContent='Simulated Day '+s.day+' · '+fmt(s.minute);document.getElementById('profile').textContent=s.configKnown?(s.numDoses+' dose profile'+(s.numDoses===1?'':'s')):'Loading saved profile…'}catch(e){document.getElementById('connection').textContent='Web UI connected · controller not responding';document.getElementById('dot').style.background='#ff9b9b'}}refresh();setInterval(refresh,1500);
</script></body></html>
)rawliteral";

// ---------------- UI: Data & Logs ----------------
const char LOGS_HTML[] PROGMEM = R"rawliteral(
<!doctype html><html><head><meta name="viewport" content="width=device-width,initial-scale=1"><title>Pill Ping | Data & Logs</title><style>
:root{--ink:#172b3a;--muted:#687b88;--line:#e0e9ed;--paper:#f5f8fa;--teal:#147d78;--teal2:#e1f4f1;--white:#fff;--amber:#fff4df}
*{box-sizing:border-box}body{margin:0;background:var(--paper);font-family:Inter,Arial,sans-serif;color:var(--ink)}.wrap{max-width:1100px;margin:0 auto;padding:22px}header{display:flex;align-items:center;justify-content:space-between;gap:12px;margin:6px 0 25px}.brand{display:flex;align-items:center;gap:12px}.logo{width:42px;height:42px;border-radius:13px;background:var(--teal);color:white;display:grid;place-items:center;font-size:22px;font-weight:700}.eyebrow{font-size:11px;letter-spacing:1.6px;font-weight:800;color:var(--teal);text-transform:uppercase}h1{font-size:24px;margin:4px 0 0}h2{font-size:17px;margin:0 0 14px}.nav{display:flex;gap:8px;flex-wrap:wrap}.nav a,.btn{padding:10px 13px;border-radius:11px;border:1px solid var(--line);color:var(--ink);text-decoration:none;background:#fff;font-weight:700;font-size:13px}.nav a.active{background:var(--teal);color:#fff;border-color:var(--teal)}.sub{color:var(--muted);font-size:13px;margin:-14px 0 22px;line-height:1.55}.panel{background:#fff;border:1px solid var(--line);border-radius:18px;padding:20px;margin-bottom:17px}.topinfo{display:flex;flex-wrap:wrap;align-items:center;justify-content:space-between;gap:10px;margin-bottom:20px}.status{font-size:12px;background:var(--teal2);color:#176c68;padding:8px 11px;border-radius:99px;font-weight:800}.cards{display:grid;grid-template-columns:repeat(3,minmax(0,1fr));gap:12px}.dosecard{border:1px solid var(--line);border-radius:14px;padding:15px}.dosecard h3{margin:0 0 12px;font-size:15px}.kv{display:flex;justify-content:space-between;gap:10px;padding:8px 0;border-bottom:1px solid #f0f3f4;font-size:13px}.kv:last-child{border-bottom:0}.kv span:first-child{color:var(--muted)}.kv strong{text-align:right}.tablewrap{overflow-x:auto}table{width:100%;border-collapse:collapse;min-width:750px}th,td{text-align:left;padding:12px 11px;border-bottom:1px solid #edf1f3;font-size:13px;white-space:nowrap}th{color:var(--muted);font-size:11px;text-transform:uppercase;letter-spacing:.8px;background:#f8fafb}td{line-height:1.4}.pill{display:inline-block;padding:5px 8px;border-radius:99px;background:var(--teal2);color:#176c68;font-size:11px;font-weight:800}.pill.wait{background:var(--amber);color:#8b5d15}.empty{padding:28px 10px;text-align:center;color:var(--muted);font-size:14px}.foot{margin:25px 0 4px;text-align:center;color:#8a9aa3;font-size:11px}
@media(max-width:700px){.wrap{padding:15px}.cards{grid-template-columns:1fr}.panel{padding:15px}header{align-items:flex-start;flex-direction:column}.nav{width:100%}.nav a{flex:1;text-align:center}}
</style></head><body><main class="wrap"><header><div class="brand"><div class="logo">P+</div><div><div class="eyebrow">Pill Ping · Data Center</div><h1>Data &amp; Logs</h1></div></div><nav class="nav"><a href="/">Home</a><a class="active" href="/logs">Data &amp; Logs</a><a href="/config">Configuration</a></nav></header>
<p class="sub">The current saved dose plan is read back from the Arduino Nano. Dispensing history records the simulated day and time when D4 was pressed, plus the time the pump finished.</p>
<section class="panel"><div class="topinfo"><h2>Saved dose configuration</h2><span class="status" id="profileStatus">Syncing with controller…</span></div><div id="doseCards" class="cards"><div class="empty">Waiting for saved configuration…</div></div><div style="font-size:11px;line-height:1.5;color:#687b88;margin-top:13px">* Estimated volume = Hall pulse count / 150 mL. Actual delivery may vary with pump speed, tubing, liquid properties and calibration.</div></section>
<section class="panel"><div class="topinfo"><div><h2>Actual dispensing history</h2><div style="font-size:12px;color:#687b88" id="logSummary">Runtime-only log · newest events appear below</div></div><span class="status" id="sessionClock">Day 1 · 00:00</span></div><div class="tablewrap"><table><thead><tr><th>Runtime day</th><th>Dose</th><th>Started (D4 pressed)</th><th>Completed</th><th>Tablets</th><th>Solution</th><th>Status</th></tr></thead><tbody id="logsBody"><tr><td colspan="7" class="empty">No dispensing events yet.</td></tr></tbody></table></div></section>
<p class="sub">Clock is accelerated prototype time, not a real-world timestamp. Day numbering starts at Day 1 after power-up and is not saved to EEPROM. Logs are in RAM and clear on power loss/restart.</p><div class="foot">PILL PING · LOCAL NETWORK ONLY</div></main>
<script>
function fmt(m){m=Number(m)||0;return String(Math.floor(m/60)%24).padStart(2,'0')+':'+String(m%60).padStart(2,'0')}
function doseCard(d, i) {
  const pulses = Number(d.solutionPulses) || 0;
  const volumeMl = (pulses / 150).toFixed(2);

  return `<article class="dosecard">
    <h3>Dose ${i + 1}</h3>
    <div class="kv">
      <span>Time window</span>
      <strong>${fmt(d.startMinute)} – ${fmt(d.endMinute)}</strong>
    </div>
    <div class="kv">
      <span>Reminder</span>
      <strong>${fmt(d.reminderMinute)}</strong>
    </div>
    <div class="kv">
      <span>Solution</span>
      <strong>${pulses} pulses ≈ ${volumeMl} mL*</strong>
    </div>
    <div class="kv">
      <span>Tablets</span>
      <strong>${d.tabletCount}</strong>
    </div>
  </article>`;
}
function render(s){document.getElementById('profileStatus').textContent=s.online?(s.configKnown?(s.numDoses+' saved dose'+(s.numDoses===1?'':'s')):'Controller online · no profile read yet'):'Controller offline';document.getElementById('sessionClock').textContent='Day '+s.day+' · '+fmt(s.minute);const dc=document.getElementById('doseCards');dc.innerHTML=s.configKnown&&s.numDoses?s.doses.map(doseCard).join(''):'<div class="empty">No saved dose configuration is available yet. Open Configuration after the controller connects.</div>';const body=document.getElementById('logsBody');document.getElementById('logSummary').textContent=s.logs.length+' event'+(s.logs.length===1?'':'s')+' in this powered-on session';if(!s.logs.length){body.innerHTML='<tr><td colspan="7" class="empty">No dispensing events yet. A record is created when a dose starts.</td></tr>';return}body.innerHTML=s.logs.slice().reverse().map(l=>`<tr><td>Day ${l.startDay}</td><td>Dose ${l.doseNumber}</td><td>Day ${l.startDay}<br><strong>${fmt(l.startMinute)}</strong></td><td>${l.completed?'Day '+l.finishDay+'<br><strong>'+fmt(l.finishMinute)+'</strong>':'—'}</td><td>${l.completed?(l.actualPills+'/'+l.targetPills):('Pending / '+l.targetPills)}</td><td>${l.actualPulses}/${l.targetPulses} pulses</td><td><span class="pill ${l.completed?'':'wait'}">${l.completed?'Completed':'In progress'}</span></td></tr>`).join('')}
async function refresh(){try{const r=await fetch('/api/state');render(await r.json())}catch(e){document.getElementById('profileStatus').textContent='Unable to read controller state'}}refresh();setInterval(refresh,1500);
</script></body></html>
)rawliteral";

// ---------------- UI: Configuration ----------------
const char CONFIG_HTML[] PROGMEM = R"rawliteral(
<!doctype html><html><head><meta name="viewport" content="width=device-width,initial-scale=1"><title>Pill Ping | Configuration</title><style>
:root{--ink:#172b3a;--muted:#687b88;--line:#e0e9ed;--paper:#f5f8fa;--teal:#147d78;--teal2:#e1f4f1;--white:#fff;--red:#a43d3d}
*{box-sizing:border-box}body{margin:0;background:var(--paper);font-family:Inter,Arial,sans-serif;color:var(--ink)}.wrap{max-width:950px;margin:0 auto;padding:22px}header{display:flex;align-items:center;justify-content:space-between;gap:12px;margin:6px 0 25px}.brand{display:flex;align-items:center;gap:12px}.logo{width:42px;height:42px;border-radius:13px;background:var(--teal);color:white;display:grid;place-items:center;font-size:22px;font-weight:700}.eyebrow{font-size:11px;letter-spacing:1.6px;font-weight:800;color:var(--teal);text-transform:uppercase}h1{font-size:24px;margin:4px 0 0}h2{font-size:17px;margin:0 0 10px}.nav{display:flex;gap:8px;flex-wrap:wrap}.nav a{padding:10px 13px;border-radius:11px;border:1px solid var(--line);color:var(--ink);text-decoration:none;background:#fff;font-weight:700;font-size:13px}.nav a.active{background:var(--teal);color:#fff;border-color:var(--teal)}.sub{color:var(--muted);font-size:13px;line-height:1.6;margin:-14px 0 21px}.panel{background:#fff;border:1px solid var(--line);border-radius:18px;padding:21px;margin-bottom:17px}.toolbar{display:flex;gap:12px;align-items:center;justify-content:space-between;flex-wrap:wrap}.fieldlabel{font-size:12px;font-weight:800;color:var(--muted);display:block;margin-bottom:7px}select,input{font:inherit;color:var(--ink);background:white;border:1px solid #d6e1e5;border-radius:10px;padding:11px 12px;width:100%;min-height:43px}select:focus,input:focus{outline:2px solid #b3e1dc;border-color:var(--teal)}.selectbox{width:220px}.dosebox{border:1px solid var(--line);border-radius:15px;padding:17px;margin-top:15px}.dosebox h3{margin:0 0 15px;font-size:15px}.fields{display:grid;grid-template-columns:repeat(2,minmax(0,1fr));gap:14px}.field{min-width:0}.hint{font-size:11px;color:var(--muted);margin-top:6px;line-height:1.45}.btn{font:inherit;border:0;border-radius:11px;padding:13px 17px;background:var(--teal);color:white;font-weight:800;cursor:pointer}.btn:disabled{opacity:.45;cursor:not-allowed}.message{display:none;padding:13px 15px;border-radius:12px;margin:15px 0 0;font-size:13px;line-height:1.5;background:#edf3f5;color:var(--ink)}.message.show{display:block}.message.good{background:var(--teal2);color:#176c68}.message.bad{background:#fff0f0;color:var(--red)}.message.wait{background:#fff4df;color:#8b5d15}.statusrow{display:flex;flex-wrap:wrap;gap:9px;margin-top:12px}.tag{padding:7px 10px;border-radius:99px;background:#edf3f5;color:var(--muted);font-size:12px;font-weight:700}.tag.good{background:var(--teal2);color:#176c68}.foot{margin:25px 0 4px;text-align:center;color:#8a9aa3;font-size:11px}
@media(max-width:650px){.wrap{padding:15px}header{align-items:flex-start;flex-direction:column}.nav{width:100%}.nav a{flex:1;text-align:center}.panel{padding:15px}.fields{grid-template-columns:1fr}.selectbox{width:100%}}
</style></head><body><main class="wrap"><header><div class="brand"><div class="logo">P+</div><div><div class="eyebrow">Pill Ping · Schedule Setup</div><h1>Configuration</h1></div></div><nav class="nav"><a href="/">Home</a><a href="/logs">Data &amp; Logs</a><a class="active" href="/config">Configuration</a></nav></header>
<p class="sub">This form is pre-filled from the schedule currently saved on the Arduino Nano. Saving sends the new values to the controller, which validates and stores them in EEPROM. The simulated day and event log are not stored.</p>
<section class="panel"><div class="toolbar"><div><h2>Dose schedule</h2><div style="font-size:12px;color:#687b88">Set up to three dose windows per simulated day.</div></div><div class="selectbox"><label class="fieldlabel" for="doseCount">NUMBER OF DOSES</label><select id="doseCount" name="n"><option value="1">1 dose per day</option><option value="2">2 doses per day</option><option value="3">3 doses per day</option></select></div></div>
<form id="scheduleForm"><input type="hidden" id="nHidden" name="n" value="1"><div id="doseFields"></div><div class="statusrow"><span class="tag" id="controllerTag">Connecting to Arduino Nano…</span><span class="tag">Solution: 150 pulses ≈ 1 mL (calibrated estimate)</span></div><div id="message" class="message" role="status"></div><div style="margin-top:18px;display:flex;gap:10px;flex-wrap:wrap"><button class="btn" id="saveBtn" type="submit" disabled>Save schedule to controller</button><a class="btn" href="/logs" style="display:inline-block;text-decoration:none;background:#eaf0f2;color:#244252">View saved data</a></div></form></section>
<section class="panel"><h2>How the time window works</h2><div style="color:#687b88;font-size:13px;line-height:1.7">The D4 dispense button is accepted only between Start and End. The reminder time must fall inside that same window. The controller permits one successful dispense for a dose window; after the window ends, that dose is marked as missed until the next simulated day. Dose windows must be ordered and must not overlap.</div></section>
<div class="foot">PILL PING · LOCAL NETWORK ONLY</div></main>
<script>
let savedDoses=[];let stateReady=false;let configLoadedFromDevice=false;let userHasEdited=false;
function fmt(m){m=Number(m)||0;return String(Math.floor(m/60)%24).padStart(2,'0')+':'+String(m%60).padStart(2,'0')}
function toMin(t){if(!t||t.indexOf(':')<0)return 0;const a=t.split(':');return Number(a[0])*60+Number(a[1])}
function getDrafts(){const out=[];for(let i=1;i<=3;i++){const a=document.querySelector(`[name="start${i}"]`);const b=document.querySelector(`[name="end${i}"]`);const c=document.querySelector(`[name="remind${i}"]`);const d=document.querySelector(`[name="pulse${i}"]`);const e=document.querySelector(`[name="pills${i}"]`);if(a&&b&&c&&d&&e)out[i-1]={startMinute:toMin(a.value),endMinute:toMin(b.value),reminderMinute:toMin(c.value),solutionPulses:Number(d.value),tabletCount:Number(e.value)}}return out}
function field(label,name,type,value,min,max,hint){return `<div class="field"><label class="fieldlabel" for="${name}">${label}</label><input id="${name}" name="${name}" type="${type}" value="${value}" ${min!==null?`min="${min}"`:''} ${max!==null?`max="${max}"`:''} required><div class="hint">${hint||''}</div></div>`}
function renderFields(source,keepDraft){if(keepDraft){const d=getDrafts();d.forEach((x,i)=>{if(x)savedDoses[i]=x})}const n=Number(document.getElementById('doseCount').value);document.getElementById('nHidden').value=n;let html='';for(let i=0;i<n;i++){const d=savedDoses[i]||{startMinute:480+i*240,endMinute:540+i*240,reminderMinute:500+i*240,solutionPulses:160,tabletCount:1};html+=`<section class="dosebox"><h3>Dose ${i+1}</h3><div class="fields">${field('START TIME',`start${i+1}`,'time',fmt(d.startMinute),null,null,'Beginning of the permitted dispense window.')}${field('END TIME',`end${i+1}`,'time',fmt(d.endMinute),null,null,'The dose button is rejected outside this window.')}${field('REMINDER TIME',`remind${i+1}`,'time',fmt(d.reminderMinute),null,null,'Must be between the start and end times.')}${field('SOLUTION PULSES',`pulse${i+1}`,'number',d.solutionPulses,1,30000,'Calibration: 150 Hall pulses ≈ 1 mL. Verify actual output experimentally.')}${field('NUMBER OF TABLETS',`pills${i+1}`,'number',d.tabletCount,1,50,'Number of tablets to confirm through the laser sensor.')}</div></section>`}document.getElementById('doseFields').innerHTML=html}
function showMessage(text,kind){const el=document.getElementById('message');el.textContent=text;el.className='message show '+(kind||'')}
async function refresh(){try{const r=await fetch('/api/state');const s=await r.json();if(s.configKnown&&!configLoadedFromDevice&&!userHasEdited){savedDoses=s.doses||[];document.getElementById('doseCount').value=(s.numDoses>0?s.numDoses:1);renderFields(null,false);configLoadedFromDevice=true}stateReady=s.online&&s.configKnown;document.getElementById('controllerTag').textContent=!s.online?'Controller offline':(s.busy?'Dispensing in progress':(s.configKnown?'Controller connected · saved settings loaded':'Connected · waiting for saved settings'));document.getElementById('controllerTag').className='tag '+(stateReady?'good':'');document.getElementById('saveBtn').disabled=!stateReady||s.busy||s.saveStatus==='waiting';if(s.saveStatus==='waiting'){showMessage('Schedule sent. Waiting for the Arduino Nano to confirm that it was saved…','wait')}else if(s.saveStatus==='saved'){showMessage('Controller confirmed: schedule saved to EEPROM.','good')}else if(s.saveStatus==='timeout'){showMessage('No acknowledgement received. Check the serial wiring and controller power, then retry.','bad')}else if(s.saveStatus==='error'){showMessage(s.saveMessage||'The controller rejected this schedule.','bad')}}catch(e){document.getElementById('controllerTag').textContent='Cannot reach controller'}}
document.getElementById('doseCount').addEventListener('change',()=>{userHasEdited=true;renderFields(null,true)});
document.getElementById('scheduleForm').addEventListener('input',()=>{userHasEdited=true});
document.getElementById('scheduleForm').addEventListener('submit',async e=>{e.preventDefault();const n=Number(document.getElementById('doseCount').value);const drafts=getDrafts();if(drafts.length!==n){showMessage('Fill in all fields for each dose.','bad');return}for(let i=0;i<n;i++){const d=drafts[i];if(d.endMinute<=d.startMinute){showMessage(`Dose ${i+1}: End time must be later than start time. Overnight windows are not supported by this prototype.`, 'bad');return}if(d.reminderMinute<d.startMinute||d.reminderMinute>d.endMinute){showMessage(`Dose ${i+1}: Reminder must be inside the dose window.`,'bad');return}if(d.solutionPulses<1||d.solutionPulses>30000||d.tabletCount<1||d.tabletCount>50){showMessage(`Dose ${i+1}: Check the solution pulse and tablet quantities.`,'bad');return}if(i>0&&d.startMinute<=drafts[i-1].endMinute){showMessage('Dose windows must be ordered and must not overlap.','bad');return}}const params=new URLSearchParams(new FormData(e.target));document.getElementById('saveBtn').disabled=true;showMessage('Sending schedule to the Arduino Nano…','wait');try{const r=await fetch('/api/save',{method:'POST',headers:{'Content-Type':'application/x-www-form-urlencoded'},body:params.toString()});const out=await r.json();if(!out.ok){showMessage(out.message||'Schedule could not be sent.','bad')}else{showMessage('Schedule sent. Waiting for the Arduino Nano to confirm the EEPROM save…','wait')} }catch(err){showMessage('Network error while sending the schedule.','bad')}refresh()});
async function init(){await refresh();if(!document.getElementById('doseFields').children.length)renderFields(null,false)}init();setInterval(refresh,1500);
</script></body></html>
)rawliteral";

// ---------------- Helpers ----------------
String timeLabel(int minutes) {
  char buf[6];
  minutes = constrain(minutes, 0, 1439);
  snprintf(buf, sizeof(buf), "%02d:%02d", minutes / 60, minutes % 60);
  return String(buf);
}

bool isNanoOnline() {
  // During dispensing the Nano is intentionally busy with actuator/sensor loops.
  const unsigned long allowedSilence = deviceBusy ? 120000UL : 5000UL;
  return lastNanoMessage != 0 && (millis() - lastNanoMessage < allowedSilence);
}

void requestConfig() {
  nanoSerial.print(F("<GETCFG>"));
  lastConfigRequest = millis();
}

void requestStatus() {
  nanoSerial.print(F("<GETSTAT>"));
}

int parseCsv(const String &input, long values[], int maxValues) {
  int count = 0;
  int cursor = 0;
  while (cursor <= input.length()) {
    if (count >= maxValues) return -1;
    int comma = input.indexOf(',', cursor);
    int stop = (comma < 0) ? input.length() : comma;
    String token = input.substring(cursor, stop);
    token.trim();
    if (token.length() == 0) return -1;
    values[count++] = token.toInt();
    if (comma < 0) break;
    cursor = comma + 1;
  }
  return count;
}

void clearRuntimeLogs() {
  logCount = 0;
  deviceBusy = false;
  currentDay = 1;
  currentMinute = 0;
}

void setConfigFromNano(const String &payload) {
  long values[16];
  int count = parseCsv(payload, values, 16);
  if (count < 1) return;
  int incomingCount = (int)values[0];
  if (incomingCount < 0 || incomingCount > 3 || count != 1 + incomingCount * 5) return;

  DoseConfig incoming[3];
  int p = 1;
  for (int i = 0; i < incomingCount; i++) {
    incoming[i].startMinute = (int)values[p++];
    incoming[i].endMinute = (int)values[p++];
    incoming[i].reminderMinute = (int)values[p++];
    incoming[i].solutionPulses = (int)values[p++];
    incoming[i].tabletCount = (int)values[p++];
  }

  numDoses = incomingCount;
  for (int i = 0; i < 3; i++) {
    if (i < incomingCount) doses[i] = incoming[i];
    else {
      doses[i].startMinute = 0;
      doses[i].endMinute = 0;
      doses[i].reminderMinute = 0;
      doses[i].solutionPulses = 0;
      doses[i].tabletCount = 0;
    }
  }
  configKnown = true;
  if (saveStatus == "waiting") {
    saveStatus = "saved";
    saveMessage = "Controller confirmed that the schedule was saved.";
  }
}

void addLogStart(int day, int doseNo, int minute, int targetPills, int targetPulses) {
  if (logCount >= MAX_LOGS) {
    for (int i = 1; i < MAX_LOGS; i++) logs[i - 1] = logs[i];
    logCount = MAX_LOGS - 1;
  }
  DoseLog &entry = logs[logCount++];
  entry.startDay = constrain(day, 1, 65535);
  entry.doseNumber = constrain(doseNo, 1, 3);
  entry.startMinute = constrain(minute, 0, 1439);
  entry.finishDay = 0;
  entry.finishMinute = 0;
  entry.targetPills = constrain(targetPills, 0, 255);
  entry.actualPills = 0;
  entry.targetPulses = constrain(targetPulses, 0, 30000);
  entry.actualPulses = 0;
  entry.completed = false;
  deviceBusy = true;
}

void finishLog(int startDay, int doseNo, int startMinute, int finishDay, int finishMinute, int actualPills, int actualPulses) {
  for (int i = logCount - 1; i >= 0; i--) {
    DoseLog &entry = logs[i];
    if (!entry.completed && entry.startDay == startDay && entry.doseNumber == doseNo && entry.startMinute == startMinute) {
      entry.finishDay = constrain(finishDay, 1, 65535);
      entry.finishMinute = constrain(finishMinute, 0, 1439);
      entry.actualPills = constrain(actualPills, 0, 255);
      entry.actualPulses = constrain(actualPulses, 0, 30000);
      entry.completed = true;
      break;
    }
  }
  deviceBusy = false;
}

void processNanoLine(String line) {
  line.trim();
  if (!line.length()) return;
  lastNanoMessage = millis();

  if (line == "BOOT") {
    clearRuntimeLogs();
    configKnown = false;
    saveStatus = "idle";
    saveMessage = "Controller restarted; waiting for saved profile.";
    requestConfig();
    requestStatus();
    return;
  }

  if (line.startsWith("CFG,")) {
    setConfigFromNano(line.substring(4));
    return;
  }
  if (line.startsWith("STAT,")) {
    long values[3];
    int n = parseCsv(line.substring(5), values, 3);
    if (n == 2) {
      int nextDay = constrain((int)values[0], 1, 65535);
      if (currentDay > nextDay) {
        // A Nano-only restart resets the prototype day; keep history consistent with that session.
        clearRuntimeLogs();
      }
      currentDay = nextDay;
      currentMinute = constrain((int)values[1], 0, 1439);
    }
    return;
  }
  if (line.startsWith("DAY,")) {
    long values[3];
    int n = parseCsv(line.substring(4), values, 3);
    if (n == 2) {
      currentDay = constrain((int)values[0], 1, 65535);
      currentMinute = constrain((int)values[1], 0, 1439);
    }
    return;
  }
  if (line == "ACK,SAVED") {
    saveStatus = "saved";
    saveMessage = "Controller confirmed that the schedule was saved to EEPROM.";
    saveSentAt = 0;
    return;
  }
  if (line.startsWith("ACK,ERR,")) {
    saveStatus = "error";
    String reason = line.substring(8);
    if (reason == "OVERLAP") saveMessage = "Controller rejected the schedule because dose windows overlap or are not ordered.";
    else if (reason == "RANGE") saveMessage = "Controller rejected a time, pulse, or tablet quantity outside the supported range.";
    else if (reason == "COUNT") saveMessage = "Controller rejected the schedule field count.";
    else saveMessage = "Controller rejected the schedule format.";
    saveSentAt = 0;
    return;
  }
  if (line.startsWith("LOG,START,")) {
    long values[8];
    int n = parseCsv(line.substring(10), values, 8);
    if (n == 5) addLogStart((int)values[0], (int)values[1], (int)values[2], (int)values[3], (int)values[4]);
    return;
  }
  if (line.startsWith("LOG,DONE,")) {
    long values[10];
    int n = parseCsv(line.substring(9), values, 10);
    if (n == 7) finishLog((int)values[0], (int)values[1], (int)values[2], (int)values[3], (int)values[4], (int)values[5], (int)values[6]);
    return;
  }
}

void readNanoSerial() {
  while (nanoSerial.available()) {
    char c = (char)nanoSerial.read();
    if (c == '\n') {
      processNanoLine(nanoRxLine);
      nanoRxLine = "";
    } else if (c != '\r') {
      if (nanoRxLine.length() < 180) nanoRxLine += c;
      else nanoRxLine = "";
    }
  }
}

void handleStateApi() {
  String json;
  json.reserve(1800 + logCount * 140);
  json = "{\"online\":";
  json += isNanoOnline() ? "true" : "false";
  json += ",\"configKnown\":";
  json += configKnown ? "true" : "false";
  json += ",\"busy\":";
  json += deviceBusy ? "true" : "false";
  json += ",\"day\":"; json += currentDay;
  json += ",\"minute\":"; json += currentMinute;
  json += ",\"numDoses\":"; json += numDoses;
  json += ",\"saveStatus\":\""; json += saveStatus; json += "\"";
  json += ",\"saveMessage\":\""; json += saveMessage; json += "\"";
  json += ",\"doses\":[";
  for (int i = 0; i < numDoses; i++) {
    if (i) json += ',';
    json += "{\"startMinute\":"; json += doses[i].startMinute;
    json += ",\"endMinute\":"; json += doses[i].endMinute;
    json += ",\"reminderMinute\":"; json += doses[i].reminderMinute;
    json += ",\"solutionPulses\":"; json += doses[i].solutionPulses;
    json += ",\"tabletCount\":"; json += doses[i].tabletCount;
    json += '}';
  }
  json += "],\"logs\":[";
  for (int i = 0; i < logCount; i++) {
    if (i) json += ',';
    const DoseLog &l = logs[i];
    json += "{\"startDay\":"; json += l.startDay;
    json += ",\"doseNumber\":"; json += l.doseNumber;
    json += ",\"startMinute\":"; json += l.startMinute;
    json += ",\"finishDay\":"; json += l.finishDay;
    json += ",\"finishMinute\":"; json += l.finishMinute;
    json += ",\"targetPills\":"; json += l.targetPills;
    json += ",\"actualPills\":"; json += l.actualPills;
    json += ",\"targetPulses\":"; json += l.targetPulses;
    json += ",\"actualPulses\":"; json += l.actualPulses;
    json += ",\"completed\":"; json += l.completed ? "true" : "false";
    json += '}';
  }
  json += "]}";
  server.send(200, "application/json; charset=utf-8", json);
}

int timeToMinutes(String timeStr) {
  if (timeStr.length() != 5 || timeStr.charAt(2) != ':') return -1;
  int h = timeStr.substring(0, 2).toInt();
  int m = timeStr.substring(3, 5).toInt();
  if (h < 0 || h > 23 || m < 0 || m > 59) return -1;
  return h * 60 + m;
}

void sendSaveError(int statusCode, const String &message) {
  String body = "{\"ok\":false,\"message\":\"" + message + "\"}";
  server.send(statusCode, "application/json; charset=utf-8", body);
}

void handleSaveSchedule() {
  if (!isNanoOnline() || !configKnown) {
    sendSaveError(503, "Arduino Nano is not connected or its saved profile has not been read yet.");
    return;
  }
  if (deviceBusy) {
    sendSaveError(409, "A dose is currently dispensing. Wait until it completes before changing the schedule.");
    return;
  }
  if (saveStatus == "waiting") {
    sendSaveError(409, "The previous schedule is still awaiting controller confirmation.");
    return;
  }

  if (!server.hasArg("n")) {
    sendSaveError(400, "Choose how many doses to configure.");
    return;
  }
  int n = server.arg("n").toInt();
  if (n < 1 || n > 3) {
    sendSaveError(400, "Dose count must be from 1 to 3.");
    return;
  }

  DoseConfig proposed[3];
  for (int i = 0; i < n; i++) {
    String suffix = String(i + 1);
    if (!server.hasArg("start" + suffix) || !server.hasArg("end" + suffix) ||
        !server.hasArg("remind" + suffix) || !server.hasArg("pulse" + suffix) ||
        !server.hasArg("pills" + suffix)) {
      sendSaveError(400, "Fill in every field for every dose.");
      return;
    }
    proposed[i].startMinute = timeToMinutes(server.arg("start" + suffix));
    proposed[i].endMinute = timeToMinutes(server.arg("end" + suffix));
    proposed[i].reminderMinute = timeToMinutes(server.arg("remind" + suffix));
    proposed[i].solutionPulses = server.arg("pulse" + suffix).toInt();
    proposed[i].tabletCount = server.arg("pills" + suffix).toInt();

    if (proposed[i].startMinute < 0 || proposed[i].endMinute < 0 || proposed[i].reminderMinute < 0) {
      sendSaveError(400, "Use valid 24-hour times for Start, End, and Reminder.");
      return;
    }
    if (proposed[i].endMinute <= proposed[i].startMinute) {
      sendSaveError(400, "End time must be later than Start time. Overnight windows are not supported.");
      return;
    }
    if (proposed[i].reminderMinute < proposed[i].startMinute || proposed[i].reminderMinute > proposed[i].endMinute) {
      sendSaveError(400, "Reminder time must fall inside its dose window.");
      return;
    }
    if (proposed[i].solutionPulses < 1 || proposed[i].solutionPulses > 30000 ||
        proposed[i].tabletCount < 1 || proposed[i].tabletCount > 50) {
      sendSaveError(400, "Solution pulses must be 1–30000 and tablet quantity must be 1–50.");
      return;
    }
    if (i > 0 && proposed[i].startMinute <= proposed[i - 1].endMinute) {
      sendSaveError(400, "Dose windows must be ordered and must not overlap.");
      return;
    }
  }

  String frame = "<SET," + String(n);
  for (int i = 0; i < n; i++) {
    frame += "," + String(proposed[i].startMinute);
    frame += "," + String(proposed[i].endMinute);
    frame += "," + String(proposed[i].reminderMinute);
    frame += "," + String(proposed[i].solutionPulses);
    frame += "," + String(proposed[i].tabletCount);
  }
  frame += ">";
  nanoSerial.print(frame);
  saveStatus = "waiting";
  saveMessage = "Schedule sent; waiting for Arduino acknowledgement.";
  saveSentAt = millis();
  server.send(200, "application/json; charset=utf-8", "{\"ok\":true,\"message\":\"Schedule sent to controller. Waiting for save acknowledgement.\"}");
}

void handleHome() { server.send_P(200, "text/html; charset=utf-8", HOME_HTML); }
void handleLogs() { server.send_P(200, "text/html; charset=utf-8", LOGS_HTML); }
void handleConfig() { server.send_P(200, "text/html; charset=utf-8", CONFIG_HTML); }

void setup() {
  Serial.begin(115200); // USB diagnostics only; not used for Nano data.
  nanoSerial.begin(9600);
  WiFi.mode(WIFI_AP);
  WiFi.softAP(ssid, password);

  server.on("/", HTTP_GET, handleHome);
  server.on("/logs", HTTP_GET, handleLogs);
  server.on("/config", HTTP_GET, handleConfig);
  server.on("/api/state", HTTP_GET, handleStateApi);
  server.on("/api/save", HTTP_POST, handleSaveSchedule);
  server.onNotFound([]() { server.send(404, "text/plain", "Not found. Go to /"); });
  server.begin();

  requestConfig();
  requestStatus();
}

void loop() {
  readNanoSerial();
  server.handleClient();

  if (!configKnown && millis() - lastConfigRequest >= 2000UL) requestConfig();
  if (saveStatus == "waiting" && saveSentAt != 0 && millis() - saveSentAt > 10000UL) {
    saveStatus = "timeout";
    saveMessage = "No acknowledgement received from the Arduino Nano. Check serial wiring and controller power.";
    saveSentAt = 0;
  }
}
