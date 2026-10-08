#pragma once
// Web-app (PWA) incorporata nel firmware. Viene servita dall'ESP32 su http://192.168.4.1
// Nessuna risorsa esterna: funziona anche senza internet.
#include <pgmspace.h>

static const char WEB_MANIFEST[] PROGMEM = R"json({
 "name": "LiFePO4 Tester 24V",
 "short_name": "LiFePO4 Tester",
 "start_url": "/",
 "scope": "/",
 "display": "standalone",
 "orientation": "portrait",
 "background_color": "#101828",
 "theme_color": "#101828",
 "icons": [
  {"src": "/icon-192.png", "sizes": "192x192", "type": "image/png", "purpose": "any"},
  {"src": "/icon-512.png", "sizes": "512x512", "type": "image/png", "purpose": "any maskable"}
 ]
})json";

static const char WEB_INDEX[] PROGMEM = R"rawliteral(<!DOCTYPE html>
<html lang="it">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width, initial-scale=1, viewport-fit=cover">
<meta name="theme-color" content="#101828">
<meta name="mobile-web-app-capable" content="yes">
<link rel="manifest" href="/manifest.json">
<link rel="icon" href="/icon-192.png">
<link rel="apple-touch-icon" href="/icon-192.png">
<title>LiFePO4 Tester</title>
<style>
:root{--bg:#101828;--card:#182235;--card2:#1f2b42;--tx:#e6edf7;--mut:#8b9bb4;--grn:#34d399;--org:#fb923c;--red:#f87171;--blu:#60a5fa;--yel:#fbbf24;--line:#2a3750}
*{box-sizing:border-box;-webkit-tap-highlight-color:transparent}
html,body{margin:0;background:var(--bg);color:var(--tx);font:15px/1.4 system-ui,-apple-system,"Segoe UI",Roboto,sans-serif}
body{padding-bottom:calc(64px + env(safe-area-inset-bottom))}
header{position:sticky;top:0;z-index:5;background:var(--bg);border-bottom:1px solid var(--line);padding:calc(10px + env(safe-area-inset-top)) 16px 10px;display:flex;align-items:center;justify-content:space-between}
header h1{font-size:17px;margin:0;font-weight:600}
.dot{display:inline-block;width:10px;height:10px;border-radius:50%;background:var(--red);margin-right:6px;vertical-align:middle}
.dot.on{background:var(--grn);box-shadow:0 0 8px var(--grn)}
#conn{font-size:12px;color:var(--mut)}
main{padding:12px 12px 0}
.page{display:none}.page.act{display:block}
.grid{display:grid;grid-template-columns:1fr 1fr;gap:10px}
.card{background:var(--card);border-radius:14px;padding:12px 14px;min-width:0}
.card.big{grid-column:1/-1}
.lbl{font-size:12px;color:var(--mut);text-transform:uppercase;letter-spacing:.04em}
.val{font-size:30px;font-weight:600;font-variant-numeric:tabular-nums;line-height:1.15;margin-top:2px}
.val small{font-size:14px;color:var(--mut);font-weight:400;margin-left:3px}
.val.g{color:var(--grn)}.val.o{color:var(--org)}.val.b{color:var(--blu)}.val.y{color:var(--yel)}
.sub{font-size:12px;color:var(--mut);margin-top:4px}
.bar{height:10px;background:#0c1220;border-radius:6px;overflow:hidden;margin-top:8px}
.bar i{display:block;height:100%;background:linear-gradient(90deg,var(--grn),#10b981);border-radius:6px;transition:width .5s}
.bar i.low{background:linear-gradient(90deg,var(--red),#ef4444)}
canvas{width:100%;height:220px;display:block;border-radius:10px;background:#0c1220}
.legend{display:flex;gap:16px;font-size:12px;color:var(--mut);margin:6px 2px 0}
.legend b{display:inline-block;width:10px;height:3px;vertical-align:middle;margin-right:5px;border-radius:2px}
.alarms{display:flex;flex-wrap:wrap;gap:6px;margin:0 0 10px}
.alarm{background:#3b1d1d;color:#fca5a5;border:1px solid #7f1d1d;border-radius:8px;padding:4px 10px;font-size:12px;font-weight:600}
.alarm.warn{background:#3b2f10;color:#fde68a;border-color:#92400e}
nav{position:fixed;bottom:0;left:0;right:0;background:var(--card);border-top:1px solid var(--line);display:flex;padding-bottom:env(safe-area-inset-bottom);z-index:5}
nav button{flex:1;background:none;border:0;color:var(--mut);padding:10px 0 8px;font-size:12px;font-weight:600;cursor:pointer}
nav button .ic{display:block;font-size:20px;margin-bottom:2px}
nav button.act{color:var(--grn)}
h2{font-size:14px;color:var(--mut);text-transform:uppercase;letter-spacing:.05em;margin:18px 4px 8px;font-weight:600}
.row{display:flex;gap:10px;align-items:center;margin:8px 0}
.row label{flex:1;color:var(--tx);font-size:14px}
input[type=number],input[type=text],input[type=password],select{width:120px;background:#0c1220;border:1px solid var(--line);color:var(--tx);border-radius:8px;padding:9px 10px;font-size:15px}
input.wide{width:100%}
input[type=checkbox]{width:22px;height:22px;accent-color:var(--grn)}
button.btn{background:var(--card2);border:1px solid var(--line);color:var(--tx);border-radius:10px;padding:11px 16px;font-size:15px;font-weight:600;cursor:pointer;width:100%;margin-top:6px}
button.btn.p{background:var(--grn);color:#052e1c;border-color:var(--grn)}
button.btn.d{background:#7f1d1d;color:#fecaca;border-color:#991b1b}
button.btn.s{width:auto;padding:8px 12px;font-size:13px;margin:0}
button.btn:disabled{opacity:.45;cursor:default}
button.btn{transition:transform .08s,box-shadow .25s,background .25s,color .25s;position:relative}
button.btn.pressed{transform:scale(.95);filter:brightness(1.35)}
button.btn.sending{box-shadow:0 0 0 3px var(--yel);border-color:var(--yel)}
button.btn.ok{background:var(--grn)!important;color:#052e1c!important;border-color:var(--grn)!important;box-shadow:0 0 18px var(--grn)}
button.btn.err{background:var(--red)!important;color:#fff!important;border-color:var(--red)!important;box-shadow:0 0 18px var(--red)}
button.btn.ok::after{content:' ✔'}button.btn.err::after{content:' ✖'}
.btns{display:flex;gap:10px}.btns .btn{flex:1}
table{width:100%;border-collapse:collapse;font-size:14px}
td{padding:6px 4px;border-bottom:1px solid var(--line)}td:last-child{text-align:right;font-variant-numeric:tabular-nums;font-weight:600}
.state{display:inline-block;padding:3px 10px;border-radius:8px;font-size:12px;font-weight:700;background:var(--card2)}
.state.RUN{background:#064e3b;color:#6ee7b7}.state.DONE{background:#1e3a8a;color:#bfdbfe}.state.IDLE{color:var(--mut)}
.toast{position:fixed;left:50%;bottom:84px;transform:translateX(-50%);background:#0c1220;border:2px solid var(--grn);color:var(--tx);padding:12px 20px;border-radius:12px;font-size:16px;font-weight:600;opacity:0;transition:opacity .3s;pointer-events:none;z-index:9;max-width:90vw;box-shadow:0 4px 20px rgba(0,0,0,.6)}
.toast.err{border-color:var(--red)}
.toast.show{opacity:1}
.note{font-size:12px;color:var(--mut);margin:4px 4px 8px}
.seg{display:flex;gap:6px;margin-top:8px}.seg button{flex:1;background:var(--card2);border:1px solid var(--line);color:var(--mut);border-radius:8px;padding:7px 0;font-size:12px;font-weight:600;cursor:pointer}.seg button.act{color:var(--grn);border-color:var(--grn)}
.mono{font-family:ui-monospace,Menlo,Consolas,monospace;font-size:13px}
table.hist{font-size:12px}table.hist th{text-align:left;color:var(--mut);font-weight:600;padding:4px 3px;border-bottom:1px solid var(--line)}table.hist td{padding:5px 3px;text-align:right}table.hist td:first-child{text-align:left;font-weight:600}
table.hist tr.bad td{color:#fca5a5}table.hist tr.best td{color:#6ee7b7}table.hist a{color:var(--blu);text-decoration:none}
.tag{display:inline-block;padding:1px 6px;border-radius:6px;font-size:10px;font-weight:700;background:var(--card2)}.tag.bms{background:#7f1d1d;color:#fecaca}.tag.bmsok{background:#3b2f10;color:#fde68a}.tag.ok{background:#064e3b;color:#6ee7b7}
</style>
</head>
<body>
<header>
 <h1>🔋 LiFePO4 Tester 24V</h1>
 <div id="conn"><span class="dot" id="dot"></span><span id="conntx">—</span></div>
</header>
<main>

<!-- ================= MONITOR ================= -->
<section class="page act" id="p-mon">
 <div class="alarms" id="alarms"></div>
 <div class="grid">
  <div class="card"><div class="lbl">Tensione</div><div class="val g" id="v">--.--<small>V</small></div><div class="sub" id="vcell">-- V/cella</div></div>
  <div class="card"><div class="lbl">Corrente</div><div class="val o" id="i">--.--<small>A</small></div><div class="sub" id="idir">—</div></div>
  <div class="card"><div class="lbl">Potenza</div><div class="val b" id="p">---<small>W</small></div><div class="sub" id="mvsh">shunt -- mV</div></div>
  <div class="card"><div class="lbl">Temperatura</div><div class="val y" id="t">--<small>°C</small></div><div class="sub">NTC batteria</div></div>
  <div class="card big"><div class="lbl">Stato di carica (stimato)</div>
   <div class="val" id="soc">--<small>%</small></div>
   <div class="bar"><i id="socbar" style="width:0%"></i></div>
   <div class="sub" id="socsub">-- Ah residui su -- Ah</div></div>
  <div class="card"><div class="lbl">Scaricati</div><div class="val" id="ahout">--<small>Ah</small></div><div class="sub" id="whout">-- Wh</div></div>
  <div class="card"><div class="lbl">Caricati</div><div class="val" id="ahin">--<small>Ah</small></div><div class="sub" id="whin">-- Wh</div></div>
  <div class="card big">
   <canvas id="chart"></canvas>
   <div class="legend"><span><b style="background:var(--grn)"></b>Tensione [V]</span><span><b style="background:var(--org)"></b>Corrente [A]</span><span id="chspan" style="margin-left:auto">—</span></div>
   <div class="seg" id="chwin"><button data-w="600" onclick="setWin(600)">10 min</button><button data-w="3600" onclick="setWin(3600)">1 ora</button><button data-w="0" onclick="setWin(0)">Tutto (6 h)</button></div>
  </div>
  <div class="card"><div class="lbl">R interna</div><div class="val" id="rint">--<small>mΩ</small></div><div class="sub">da gradino di carico</div></div>
  <div class="card"><div class="lbl">Relè carico</div><div class="val" id="relay">--</div><div class="sub" id="up">uptime --</div></div>
 </div>
 <div class="btns" style="margin-top:12px">
  <button class="btn" onclick="cmd('/api/soc','value=100','SOC impostato al 100%')">SOC = 100%</button>
  <button class="btn" onclick="if(confirm('Azzerare i contatori Ah/Wh?'))cmd('/api/counters','cmd=reset','Contatori azzerati')">Azzera contatori</button>
 </div>
</section>

<!-- ================= TEST ================= -->
<section class="page" id="p-test">
 <div class="card">
  <div class="row"><label>Stato test</label><span class="state IDLE" id="tstate">IDLE</span></div>
  <div class="row"><label>Nome batteria</label><input type="text" id="tname" class="wide" placeholder="es. B03" maxlength="22"></div>
  <div class="row"><label>Tensione di stacco (cutoff)</label><input type="number" id="cutoff" step="0.1" min="16" max="28"> <span>V</span></div>
  <div class="row"><label>Capacità nominale</label><input type="number" id="cap" step="1" min="1"> <span>Ah</span></div>
  <div class="row"><label>Intervallo log</label><input type="number" id="logint" step="1" min="1" max="600"> <span>s</span></div>
  <div class="row"><label>Usa relè per staccare il carico</label><input type="checkbox" id="relay_en"></div>
  <div class="btns">
   <button class="btn p" id="bstart" onclick="startTest()">▶ Avvia test</button>
   <button class="btn d" id="bstop" onclick="if(confirm('Fermare il test?'))cmd('/api/test','cmd=stop','Test fermato')">■ Ferma</button>
  </div>
  <p class="note">Il test azzera i contatori, integra gli Ah erogati e termina da solo quando la tensione scende sotto il cutoff (se il relè è abilitato stacca il carico). Collega un carico costante (resistenza, carico elettronico) prima di avviare.</p>
 </div>
 <h2>Test in corso</h2>
 <div class="card">
  <table>
   <tr><td>Batteria</td><td id="t_name">--</td></tr>
   <tr><td>Durata</td><td id="t_el">--</td></tr>
   <tr><td>Ah erogati</td><td id="t_ah">--</td></tr>
   <tr><td>Wh erogati</td><td id="t_wh">--</td></tr>
   <tr><td>Corrente media</td><td id="t_iavg">--</td></tr>
   <tr><td>V iniziale / V minima</td><td id="t_v">--</td></tr>
   <tr><td>Stima capacità a fine test</td><td id="t_proj">--</td></tr>
   <tr><td>Righe di log</td><td id="t_logn">--</td></tr>
  </table>
 </div>
 <h2>Ultimo test completato</h2>
 <div class="card">
  <table>
   <tr><td>Batteria</td><td id="l_name">--</td></tr>
   <tr><td>Esito</td><td id="l_reason">--</td></tr>
   <tr><td>Capacità misurata</td><td id="l_ah">--</td></tr>
   <tr><td>Energia</td><td id="l_wh">--</td></tr>
   <tr><td>Durata</td><td id="l_dur">--</td></tr>
   <tr><td>Corrente media</td><td id="l_iavg">--</td></tr>
   <tr><td>V iniziale → V finale</td><td id="l_v">--</td></tr>
   <tr><td>R interna (DC)</td><td id="l_r">--</td></tr>
   <tr><td>% della nominale</td><td id="l_pct">--</td></tr>
  </table>
  <div class="btns" style="margin-top:10px">
   <button class="btn" onclick="window.open(base()+'/api/log','_blank')">⬇ Scarica CSV</button>
  </div>
 </div>
 <h2>Storico e confronto batterie</h2>
 <div class="card">
  <p class="note">Ordinato per Ah erogati. In rosso: BMS staccato <b>in anticipo</b> (V fine oltre 1,5 V sopra il cutoff) oppure meno del 90 % della migliore. "BMS a fine" in giallo è normale: a fine scarica il BMS può intervenire un istante prima del tester. "V fine" alta (23–25 V) = una cella debole. Il cestino 🗑 elimina solo quella riga (con conferma), la matita ✏ rinomina il test.</p>
  <div style="overflow-x:auto"><table class="hist" id="hist"><thead><tr><th>Batteria</th><th>Esito</th><th>Ah</th><th>%</th><th>V fine</th><th>R mΩ</th><th>Ø A</th><th>CSV</th><th></th></tr></thead><tbody></tbody></table></div>
  <div class="btns" style="margin-top:10px">
   <button class="btn" onclick="loadHist()">↻ Aggiorna</button>
   <button class="btn d" onclick="clearHist()">Cancella TUTTO lo storico</button>
  </div>
 </div>
 <h2>Grafico di confronto</h2>
 <div class="card">
  <div class="row"><label>Asse orizzontale</label><select id="cmpx" onchange="drawCmp()"><option value="ah">Ah erogati</option><option value="t">Tempo (ore)</option></select></div>
  <div class="row"><label>Asse verticale</label><select id="cmpy" onchange="drawCmp()"><option value="v">Tensione (V)</option><option value="i">Corrente (A)</option><option value="p">Potenza (W)</option><option value="dv">Calo di tensione ogni 10 min (V)</option></select></div>
  <canvas id="cmp" style="height:280px"></canvas>
  <div class="legend" id="cmpleg" style="flex-wrap:wrap;gap:8px 14px"></div>
  <button class="btn" onclick="loadCmp()">📈 Carica le curve dai CSV</button>
  <p class="note" id="cmpinfo">Una curva per test. Asse verticale: tensione (default), corrente, potenza, oppure "calo di tensione ogni 10 min" che evidenzia i crolli improvvisi come picchi. Le batterie sane arrivano più a destra (≈100 Ah) e scendono a 20 V; una curva che finisce presto e in alto (pallino rosso = BMS staccato in anticipo) indica una cella debole.</p>
 </div>
</section>

<!-- ================= IMPOSTAZIONI ================= -->
<section class="page" id="p-set">
 <h2>Calibrazione tensione</h2>
 <div class="card">
  <div class="row"><label>Sorgente misura</label><select id="vsrc"><option value="0">Partitore (ADC ESP32)</option><option value="1" id="vsrc1">Sensore I2C (più preciso)</option></select></div>
  <div class="row"><label>Letto ora: partitore / sensore I2C</label><span class="mono" id="vboth">-- / --</span></div>
  <div class="row"><label>Tensione reale (multimetro)</label><input type="number" id="vreal" step="0.01" placeholder="26.45"> <span>V</span></div>
  <button class="btn" onclick="cal('vgain',$('vreal').value)">Calibra guadagno tensione</button>
  <div class="row"><label>Guadagno / offset attuali</label><span class="mono" id="vcal">-- / --</span></div>
 </div>
 <h2>Calibrazione corrente (shunt 100 A / 75 mV)</h2>
 <div class="card">
  <div class="row"><label>Letto ora</label><span class="mono" id="inow">-- A (-- mV)</span></div>
  <button class="btn" onclick="if(confirm('Assicurati che NON scorra corrente nello shunt, poi conferma.'))cal('izero','')">1) Azzera con corrente = 0</button>
  <div class="row"><label>Corrente reale (pinza/multimetro)</label><input type="number" id="ireal" step="0.01" placeholder="10.00"> <span>A</span></div>
  <button class="btn" onclick="cal('igain',$('ireal').value)">2) Calibra guadagno corrente</button>
  <p class="note">La taratura usa la media degli ultimi ~10 secondi: aspetta che "media 10 s" sia stabile prima di premere. Vale sia in scarica sia in carica, il segno non importa.</p>
  <div class="row"><label>Inverti segno (se scarica appare negativa)</label><input type="checkbox" id="inv"></div>
  <div class="row"><label>Guadagno / offset attuali</label><span class="mono" id="ical">-- / --</span></div>
  <button class="btn" onclick="if(confirm('Ripristinare le calibrazioni di fabbrica?'))cal('reset','')">Reset calibrazioni</button>
 </div>
 <h2>Allarmi</h2>
 <div class="card">
  <div class="row"><label>Tensione massima</label><input type="number" id="vhigh" step="0.1"> <span>V</span></div>
  <div class="row"><label>Corrente massima</label><input type="number" id="imax" step="1"> <span>A</span></div>
  <div class="row"><label>NTC collegato su GPIO35</label><input type="checkbox" id="ntc_en"></div>
  <div class="row"><label>Temperatura massima</label><input type="number" id="tmax" step="1"> <span>°C</span></div>
 </div>
 <button class="btn p" onclick="saveCfg()">💾 Salva impostazioni</button>
 <h2>Relè carico (manuale)</h2>
 <div class="card"><div class="btns">
  <button class="btn" onclick="cmd('/api/relay','state=1','Relè chiuso')">Chiudi (carico ON)</button>
  <button class="btn" onclick="cmd('/api/relay','state=0','Relè aperto')">Apri (carico OFF)</button>
 </div></div>
 <h2>Wi-Fi di casa (opzionale)</h2>
 <div class="card">
  <p class="note">Se inserisci la rete di casa, l'ESP32 si collega anche lì e il telefono resta con internet. L'access point <b>LiFePO4-Tester</b> rimane sempre attivo.</p>
  <div class="row"><label>SSID</label><input type="text" id="ssid" class="wide" autocapitalize="off"></div>
  <div class="row"><label>Password</label><input type="password" id="pass" class="wide"></div>
  <div class="row"><label>Stato</label><span class="mono" id="stainfo">--</span></div>
  <button class="btn" onclick="saveWifi()">Salva e riavvia</button>
 </div>
 <h2>App</h2>
 <div class="card">
  <div class="row"><label>Indirizzo tester (vuoto = questo)</label><input type="text" id="addr" class="wide" placeholder="192.168.1.50"></div>
  <button class="btn" onclick="setAddr()">Usa questo indirizzo</button>
  <p class="note">Sul telefono: menu ⋮ di Chrome → «Aggiungi a schermata Home» per avere l'icona dell'app.</p>
  <div class="row"><label>Firmware</label><span class="mono" id="fw">--</span></div>
  <div class="row"><label>IP access point / IP casa</label><span class="mono" id="ips">--</span></div>
  <button class="btn d" onclick="if(confirm('Riavviare il tester?'))cmd('/api/reboot','','Riavvio…')">Riavvia ESP32</button>
 </div>
</section>
</main>

<nav>
 <button class="act" data-p="p-mon"><span class="ic">📊</span>Monitor</button>
 <button data-p="p-test"><span class="ic">⏱</span>Test</button>
 <button data-p="p-set"><span class="ic">⚙️</span>Impostazioni</button>
</nav>
<div class="toast" id="toast"></div>

<script>
const $=id=>document.getElementById(id);
const CELLS=8, MAXPTS=600;
let hist=[], cfgLoaded=false, lastOk=0, failN=0;
function base(){return localStorage.getItem('tester_addr')?('http://'+localStorage.getItem('tester_addr')):'';}
function toast(m,err){const t=$('toast');t.textContent=(err?'✖ ':'✔ ')+m;t.className='toast show'+(err?' err':'');clearTimeout(t._h);t._h=setTimeout(()=>t.classList.remove('show'),2500);}
// --- feedback pulsanti: pressione, invio, conferma dal tester ---
let lastBtn=null;
document.addEventListener('click',e=>{const b=e.target.closest('button.btn');if(!b||b.disabled)return;lastBtn=b;b.classList.remove('ok','err');b.classList.add('pressed');setTimeout(()=>b.classList.remove('pressed'),160);if(navigator.vibrate)try{navigator.vibrate(25);}catch(_){}},true);
function btnState(b,cls){if(!b)return;b.classList.remove('sending','ok','err');if(cls)b.classList.add(cls);if(cls==='ok'||cls==='err'){if(navigator.vibrate)try{navigator.vibrate(cls==='ok'?[30,40,30]:[120]);}catch(_){}setTimeout(()=>b.classList.remove(cls),1500);}}
async function withBtn(fn){const b=lastBtn;btnState(b,'sending');try{const r=await fn();btnState(b,'ok');return r;}catch(e){btnState(b,'err');throw e;}}
function f(x,d=2){return (x==null||isNaN(x))?'--':Number(x).toFixed(d);}
function hms(s){s=Math.floor(s);const h=Math.floor(s/3600),m=Math.floor(s%3600/60),c=s%60;return (h?h+'h ':'')+String(m).padStart(2,'0')+'m '+String(c).padStart(2,'0')+'s';}
document.querySelectorAll('nav button').forEach(b=>b.onclick=()=>{document.querySelectorAll('nav button').forEach(x=>x.classList.remove('act'));b.classList.add('act');document.querySelectorAll('.page').forEach(p=>p.classList.remove('act'));$(b.dataset.p).classList.add('act');if(b.dataset.p!=='p-mon'&&!cfgLoaded)loadCfg();if(b.dataset.p==='p-test')loadHist();});

async function post(url,body){const r=await fetch(base()+url,{method:'POST',headers:{'Content-Type':'application/x-www-form-urlencoded'},body:body||''});if(!r.ok)throw new Error(r.status);return r.text();}
async function cmd(url,body,msg){try{await withBtn(()=>post(url,body));if(msg)toast(msg);}catch(e){toast('Errore: '+e.message,true);}}
async function cal(type,val){if((type==='vgain'||type==='igain')&&!(parseFloat(val)>0)){toast('Inserisci il valore reale',true);btnState(lastBtn,'err');return;}await cmd('/api/cal','type='+type+'&actual='+encodeURIComponent(val||0),'Calibrazione aggiornata');loadCfg();}
function startTest(){const c=parseFloat($('cutoff').value);if(!(c>0)){toast('Imposta il cutoff',true);btnState(lastBtn,'err');return;}const n=$('tname').value.trim();if(!n){toast('Dai un nome alla batteria (es. B03)',true);btnState(lastBtn,'err');$('tname').focus();return;}if(!confirm('Avviare il test di "'+n+'"? I contatori verranno azzerati.'))return;saveCfg(true).then(()=>cmd('/api/test','cmd=start&name='+encodeURIComponent(n)+'&ts='+Math.floor(Date.now()/1000),'Test avviato'));}
const REASON={cutoff:'Cutoff raggiunto ✔',manual:'Fermato manualmente',noload:'Carico assente',bms:'BMS ha staccato ⚠'};
function dstr(ts){return ts?new Date(ts*1000).toLocaleString('it-IT',{day:'2-digit',month:'2-digit',hour:'2-digit',minute:'2-digit'}):'';}
let lastState='';
async function loadHist(){const b=(lastBtn&&lastBtn.textContent.indexOf('Aggiorna')>=0)?lastBtn:null;btnState(b,'sending');try{const h=await (await fetch(base()+'/api/history',{cache:'no-store'})).json();btnState(b,'ok');const cap=parseFloat($('cap').value)||100;
 h.sort((a,b)=>b.ah-a.ah);const best=h.length?h[0].ah:0;const cut=parseFloat($('cutoff').value)||20,EARLY=1.5;
 $('hist').querySelector('tbody').innerHTML=h.map((r,k)=>{const early=r.reason==='bms'&&r.vend>cut+EARLY;const bad=early||(best>0&&r.ah<0.9*best);const cls=bad?'bad':(k===0?'best':'');
  return '<tr class="'+cls+'"><td>'+(r.name||'?')+' <a href="#" title="rinomina" onclick="renTest('+r.id+',\''+String(r.name||'').replace(/[\\']/g,'')+'\');return false;" style="text-decoration:none">✏</a><br><span style="font-weight:400;color:var(--mut)">'+dstr(r.ts)+'</span></td><td><span class="tag '+(r.reason==='bms'?(early?'bms':'bmsok'):(r.reason==='cutoff'?'ok':''))+'">'+(r.reason==='bms'?(early?'BMS presto':'BMS a fine'):(r.reason==='cutoff'?'OK':r.reason))+'</span></td><td>'+f(r.ah,1)+'</td><td>'+f(r.ah/cap*100,0)+'</td><td>'+f(r.vend,1)+'</td><td>'+(r.rint?f(r.rint*1000,1):'--')+'</td><td>'+f(r.iavg,1)+'</td><td><a href="'+base()+'/api/log?id='+r.id+'" target="_blank">⬇</a></td><td><a href="#" title="elimina solo questo test" onclick="delTest('+r.id+',\''+String(r.name||'').replace(/[\\']/g,'')+'\',\''+dstr(r.ts)+'\');return false;" style="color:var(--red)">🗑</a></td></tr>';}).join('')||'<tr><td colspan="9" style="text-align:center;color:var(--mut)">nessun test</td></tr>';
 window._histN=h.length;
 }catch(e){btnState(b,'err');}}
// --- eliminazione singolo test / storico completo ---
async function renTest(id,name){const n=prompt('Nuovo nome per il test "'+name+'":',name);if(n===null)return;const v=n.trim();if(!v||v===name)return;await cmd('/api/history','cmd=rename&id='+id+'&name='+encodeURIComponent(v),'Rinominato in "'+v+'"');loadHist();}
async function delTest(id,name,date){if(!confirm('Eliminare SOLO il test "'+name+'" ('+date+')?\nGli altri test restano.'))return;await cmd('/api/history','cmd=delete&id='+id,'Test "'+name+'" eliminato');loadHist();}
function clearHist(){const n=window._histN||0;if(!n){toast('Lo storico è già vuoto',true);return;}if(!confirm('ATTENZIONE: stai per cancellare TUTTI i '+n+' test dello storico con i loro CSV.\nPer eliminarne uno solo usa il cestino sulla riga.\n\nCancellare tutto?'))return;if(!confirm('Confermi definitivamente? Non si può annullare.'))return;cmd('/api/history','cmd=clear','Storico cancellato');cmd('/api/log','cmd=clear','');setTimeout(loadHist,500);}
// --- grafico di confronto: una curva per ogni test con CSV disponibile ---
let cmpData=[];const PAL=['#34d399','#60a5fa','#fb923c','#f472b6','#fbbf24','#a78bfa','#f87171','#2dd4bf','#c084fc','#facc15','#4ade80','#38bdf8'];
async function loadCmp(){const b=lastBtn;btnState(b,'sending');$('cmpinfo').textContent='Scarico i CSV dal tester…';
 try{const h=await (await fetch(base()+'/api/history',{cache:'no-store'})).json();h.sort((a,b)=>b.ah-a.ah);cmpData=[];let k=0,miss=0;
  for(const r of h){try{const rs=await fetch(base()+'/api/log?id='+r.id,{cache:'no-store'});if(!rs.ok){miss++;continue;}const txt=await rs.text();const pts=[];
   for(const l of txt.split('\n')){if(!l||l[0]==='#'||l[0]==='t')continue;const c=l.split(',');if(c.length<5)continue;const t=+c[0],v=+c[1],i=+c[2],p=+c[3],ah=+c[4];if(v>5)pts.push({t,v,i,p,ah});}
   for(let k=0;k<pts.length;k++){let j=k;while(j>0&&pts[k].t-pts[j].t<600)j--;pts[k].dv=(k>0&&pts[k].t-pts[j].t>0)?(pts[j].v-pts[k].v)*600/(pts[k].t-pts[j].t):0;}
   if(pts.length>1)cmpData.push({name:r.name||('#'+r.id),reason:r.reason,early:(r.reason==='bms'&&r.vend>(parseFloat($('cutoff').value)||20)+1.5),ah:r.ah,pts,color:PAL[k++%PAL.length]});}catch(e){miss++;}}
  drawCmp();$('cmpinfo').textContent=cmpData.length+' curve caricate'+(miss?' ('+miss+' test senza CSV: il tester conserva gli ultimi 12)':'');btnState(b,cmpData.length?'ok':'err');}
 catch(e){btnState(b,'err');$('cmpinfo').textContent='Errore nel caricamento';}}
function drawCmp(){const c=$('cmp'),dpr=window.devicePixelRatio||1,W=c.clientWidth,H=c.clientHeight;if(c.width!==W*dpr){c.width=W*dpr;c.height=H*dpr;}
 const g=c.getContext('2d');g.setTransform(dpr,0,0,dpr,0,0);g.clearRect(0,0,W,H);const xm=$('cmpx').value,ym=$('cmpy').value;const Y=p=>ym==='v'?p.v:ym==='i'?p.i:ym==='p'?p.p:p.dv;const ylab={v:'V',i:'A',p:'W',dv:'V/10min'}[ym];
 $('cmpleg').innerHTML=cmpData.map(d=>'<span><b style="background:'+d.color+'"></b>'+d.name+' · '+f(d.ah,1)+' Ah'+(d.reason==='bms'?(d.early?' <span class="tag bms">BMS presto</span>':' <span class="tag bmsok">BMS a fine</span>'):'')+'</span>').join('');
 if(!cmpData.length){g.fillStyle='#8b9bb4';g.font='13px system-ui';g.textAlign='center';g.fillText('Premi "Carica le curve dai CSV"',W/2,H/2);return;}
 const X=p=>xm==='ah'?p.ah:p.t/3600;let xmax=0,vmin=1e9,vmax=-1e9;cmpData.forEach(d=>d.pts.forEach(p=>{xmax=Math.max(xmax,X(p));vmin=Math.min(vmin,Y(p));vmax=Math.max(vmax,Y(p));}));
 if(xmax<=0)xmax=1;if(ym==='v'){vmin=Math.floor((vmin-0.3)*2)/2;vmax=Math.ceil((vmax+0.3)*2)/2;if(vmax-vmin<1)vmax=vmin+1;}else{const m=(vmax-vmin)||1;vmin=ym==='dv'?Math.min(0,vmin-m*0.1):Math.max(0,vmin-m*0.1);vmax=vmax+m*0.1;if(vmax<=vmin)vmax=vmin+1;}
 const pl=40,pr=12,pt=10,pb=26,w=W-pl-pr,h=H-pt-pb;const sx=x=>pl+w*x/xmax,sy=v=>pt+h*(vmax-v)/(vmax-vmin);
 g.strokeStyle='#22304a';g.lineWidth=1;g.font='10px system-ui';g.fillStyle='#8b9bb4';g.textBaseline='middle';
 for(let k=0;k<=5;k++){const v=vmax-(vmax-vmin)*k/5,y=sy(v);g.beginPath();g.moveTo(pl,y);g.lineTo(pl+w,y);g.stroke();g.textAlign='right';g.fillText(v.toFixed(ym==='v'||ym==='dv'?1:0),pl-4,y);}
 g.textBaseline='top';g.textAlign='center';for(let k=0;k<=5;k++){const x=xmax*k/5,px=sx(x);g.beginPath();g.moveTo(px,pt);g.lineTo(px,pt+h);g.stroke();g.fillText(x.toFixed(xm==='ah'?0:1),px,pt+h+4);}
 g.fillText(xm==='ah'?'Ah erogati':'ore',pl+w/2,H-11);g.save();g.translate(10,pt+h/2);g.rotate(-Math.PI/2);g.textBaseline='middle';g.fillText(ylab,0,0);g.restore();
 cmpData.forEach(d=>{g.strokeStyle=d.color;g.lineWidth=2;g.beginPath();d.pts.forEach((p,k)=>{const x=sx(X(p)),y=sy(Y(p));k?g.lineTo(x,y):g.moveTo(x,y);});g.stroke();
  const e=d.pts[d.pts.length-1];g.beginPath();g.arc(sx(X(e)),sy(Y(e)),4,0,7);g.fillStyle=d.early?'#f87171':d.color;g.fill();if(d.early){g.strokeStyle='#fff';g.lineWidth=1;g.stroke();}});}
window.addEventListener('resize',drawCmp);
function setAddr(){const a=$('addr').value.trim();if(a)localStorage.setItem('tester_addr',a);else localStorage.removeItem('tester_addr');btnState(lastBtn,'ok');toast('Indirizzo salvato');cfgLoaded=false;}

async function loadCfg(){try{const c=await (await fetch(base()+'/api/config')).json();
 ['cutoff','cap','logint','vhigh','imax','tmax'].forEach(k=>$(k).value=c[k]);
 $('relay_en').checked=!!c.relay_en;$('ntc_en').checked=!!c.ntc_en;$('inv').checked=!!c.inv;$('vsrc').value=c.vsrc;$('ssid').value=c.ssid||'';
 $('vcal').textContent=f(c.vgain,4)+' / '+f(c.voff,3)+' V';$('ical').textContent=f(c.igain,4)+' / '+f(c.ioff,3)+' A';
 $('addr').value=localStorage.getItem('tester_addr')||'';cfgLoaded=true;}catch(e){toast('Config non caricata');}}
async function saveCfg(quiet){const b=new URLSearchParams();
 ['cutoff','cap','logint','vhigh','imax','tmax'].forEach(k=>b.set(k,$(k).value));
 b.set('relay_en',$('relay_en').checked?1:0);b.set('ntc_en',$('ntc_en').checked?1:0);b.set('inv',$('inv').checked?1:0);b.set('vsrc',$('vsrc').value);
 try{await withBtn(()=>post('/api/config',b.toString()));if(!quiet)toast('Impostazioni salvate');}catch(e){toast('Errore salvataggio',true);}}
async function saveWifi(){await cmd('/api/wifi','ssid='+encodeURIComponent($('ssid').value)+'&pass='+encodeURIComponent($('pass').value),'Salvato, riavvio in corso…');}

function render(s){
 $('v').innerHTML=f(s.v)+'<small>V</small>';$('vcell').textContent=f(s.v/CELLS,3)+' V/cella';
 $('i').innerHTML=f(Math.abs(s.i))+'<small>A</small>';$('idir').textContent=s.i>0.05?'▼ scarica':(s.i<-0.05?'▲ carica':'a riposo');
 $('p').innerHTML=f(Math.abs(s.p),0)+'<small>W</small>';$('mvsh').textContent='shunt '+f(s.mvsh,2)+' mV';
 $('t').innerHTML=(s.t==null?'--':f(s.t,1))+'<small>°C</small>';
 $('soc').innerHTML=f(s.soc,0)+'<small>%</small>';$('socbar').style.width=Math.max(0,Math.min(100,s.soc))+'%';$('socbar').className=s.soc<20?'low':'';
 $('socsub').textContent=f(s.ah_rem,1)+' Ah residui su '+f(s.cap,0)+' Ah';
 $('ahout').innerHTML=f(s.ah_out)+'<small>Ah</small>';$('whout').textContent=f(s.wh_out,0)+' Wh';
 $('ahin').innerHTML=f(s.ah_in)+'<small>Ah</small>';$('whin').textContent=f(s.wh_in,0)+' Wh';
 $('rint').innerHTML=(s.rint==null?'--':f(s.rint*1000,1))+'<small>mΩ</small>';
 $('relay').textContent=s.relay?'CHIUSO':'APERTO';$('up').textContent='uptime '+hms(s.up);
 $('vboth').textContent=f(s.vadc)+' V / '+(s.ina_ok?f(s.vina)+' V':'n.d.');$('inow').textContent=f(s.i_med,2)+' A media 10 s  (istantanea '+f(s.i,2)+')';
 $('fw').textContent='v'+s.fw+' · sensore '+s.sensor;if(s.sensor&&$('vsrc1').textContent.indexOf(s.sensor)<0)$('vsrc1').textContent=s.sensor+(s.sensor==='ADS1115'?' canale A2':' VBUS')+' (più preciso)';$('ips').textContent=s.wifi.ap_ip+' / '+(s.wifi.sta_ip||'—');
 $('stainfo').textContent=s.wifi.ssid?(s.wifi.sta_ip?('connesso '+s.wifi.ssid+' '+s.wifi.rssi+' dBm'):'non connesso a '+s.wifi.ssid):'non configurato';
 const A={vlow:'TENSIONE BASSA',vhigh:'TENSIONE ALTA',ihigh:'SOVRACORRENTE',thigh:'TEMPERATURA ALTA',ina:'SENSORE I2C NON TROVATO',range:'SHUNT FUORI SCALA',adc:'PARTITORE FUORI SCALA'};
 $('alarms').innerHTML=(s.alarm||[]).map(a=>'<span class="alarm">⚠ '+(A[a]||a)+'</span>').join('');
 const T=s.test;$('tstate').textContent=T.state;$('tstate').className='state '+T.state;
 $('bstart').disabled=T.state==='RUN';$('bstop').disabled=T.state!=='RUN';$('t_name').textContent=T.name||'--';
  if(lastState==='RUN'&&T.state==='DONE')loadHist();lastState=T.state;
 if(T.state!=='IDLE'){$('t_el').textContent=hms(T.elapsed);$('t_ah').textContent=f(T.ah,3)+' Ah';$('t_wh').textContent=f(T.wh,1)+' Wh';$('t_iavg').textContent=f(T.iavg)+' A';$('t_v').textContent=f(T.vstart)+' / '+f(T.vmin)+' V';$('t_logn').textContent=T.logn;
  let proj='--';if(T.state==='RUN'&&T.iavg>0.5&&s.v>T.cutoff&&T.vstart>T.cutoff){const frac=(T.vstart-s.v)/(T.vstart-T.cutoff);proj=frac>0.15?'≈ '+f(T.ah/frac,0)+' Ah (grezza)':'in attesa…';}$('t_proj').textContent=proj;}
 const L=s.last;if(L&&L.ah>0){$('l_name').textContent=(L.name||'--')+(L.ts?' · '+dstr(L.ts):'');$('l_reason').textContent=REASON[L.reason]||L.reason;$('l_ah').textContent=f(L.ah,2)+' Ah';$('l_wh').textContent=f(L.wh,0)+' Wh';$('l_dur').textContent=hms(L.dur);$('l_iavg').textContent=f(L.iavg)+' A';$('l_v').textContent=f(L.vstart)+' → '+f(L.vend)+' V';$('l_r').textContent=L.rint?f(L.rint*1000,1)+' mΩ':'--';$('l_pct').textContent=f(L.ah/s.cap*100,1)+' %';}
 hist.push({t:Date.now(),v:s.v,i:s.i});if(hist.length>MAXPTS)hist.shift();draw();
}
async function poll(){const ctl=new AbortController();const to=setTimeout(()=>ctl.abort(),1800);
 try{const r=await fetch(base()+'/api/status',{signal:ctl.signal,cache:'no-store'});const s=await r.json();render(s);failN=0;$('dot').classList.add('on');$('conntx').textContent=(localStorage.getItem('tester_addr')||location.host);}
 catch(e){failN++;if(failN>=2){$('dot').classList.remove('on');$('conntx').textContent='disconnesso';}}
 clearTimeout(to);setTimeout(poll,1000);}

// --- grafico persistente: la curva storica la tiene l'ESP32 (/api/trend, un punto ogni 20 s per 6 ore);
//     la pagina aggiunge solo la coda in diretta. Chiudendo e riaprendo l'app la curva c'e' ancora.
let trend=[],tmark=null,win=0;try{win=parseInt(localStorage.getItem('chartwin')||'0')||0;}catch(e){}
async function loadTrend(){try{const r=await (await fetch(base()+'/api/trend',{cache:'no-store'})).json();const now=Date.now(),tl=now-r.age*1000;
 trend=r.v.map((x,k)=>({t:tl-(r.n-1-k)*r.dt*1000,v:x/1000,i:r.i[k]/100}));tmark=r.tstart>=0?now-r.tstart*1000:null;draw();}catch(e){}}
function setWin(w){win=w;try{localStorage.setItem('chartwin',w);}catch(e){}document.querySelectorAll('#chwin button').forEach(b=>b.classList.toggle('act',+b.dataset.w===w));draw();}
function draw(){const c=$('chart'),dpr=window.devicePixelRatio||1,W=c.clientWidth,H=c.clientHeight;if(!W)return;if(c.width!==W*dpr){c.width=W*dpr;c.height=H*dpr;}
 const g=c.getContext('2d');g.setTransform(dpr,0,0,dpr,0,0);g.clearRect(0,0,W,H);
 const last=trend.length?trend[trend.length-1].t:0;let pts=trend.concat(hist.filter(p=>p.t>last));
 if(win>0){const tmin=Date.now()-win*1000;pts=pts.filter(p=>p.t>=tmin);}
 if(pts.length<2){g.fillStyle='#8b9bb4';g.font='12px system-ui';g.textAlign='center';g.fillText('in attesa dei dati…',W/2,H/2);return;}
 const pl=38,pr=38,pt=10,pb=18,w=W-pl-pr,h=H-pt-pb;
 let vmin=Infinity,vmax=-Infinity,imin=0,imax=0;for(const p of pts){if(p.v<vmin)vmin=p.v;if(p.v>vmax)vmax=p.v;if(p.i<imin)imin=p.i;if(p.i>imax)imax=p.i;}
 if(vmax-vmin<0.5){const m=(vmax+vmin)/2;vmin=m-0.25;vmax=m+0.25;}vmin-=0.05;vmax+=0.05;if(imax-imin<1){imax=imin+1;}imax+=0.5;if(imin<0)imin-=0.5;
 g.strokeStyle='#22304a';g.lineWidth=1;g.font='10px system-ui';g.textBaseline='middle';
 for(let k=0;k<=4;k++){const y=pt+h*k/4;g.beginPath();g.moveTo(pl,y);g.lineTo(pl+w,y);g.stroke();
  g.fillStyle='#34d399';g.textAlign='right';g.fillText((vmax-(vmax-vmin)*k/4).toFixed(1),pl-4,y);
  g.fillStyle='#fb923c';g.textAlign='left';g.fillText((imax-(imax-imin)*k/4).toFixed(imax-imin<10?1:0),pl+w+4,y);}
 const t0=pts[0].t,t1=pts[pts.length-1].t,span=Math.max(t1-t0,1000),mins=span/60000;$('chspan').textContent=mins<90?('ultimi '+Math.round(mins)+' min'):('ultime '+(mins/60).toFixed(1)+' ore');
 const X=t=>pl+w*(t-t0)/span;
 if(tmark&&tmark>=t0&&tmark<=t1){const x=X(tmark);g.strokeStyle='#60a5fa';g.setLineDash([4,3]);g.beginPath();g.moveTo(x,pt);g.lineTo(x,pt+h);g.stroke();g.setLineDash([]);g.fillStyle='#60a5fa';g.textAlign=x>pl+w-60?'right':'left';g.textBaseline='top';g.fillText('avvio test',x+(x>pl+w-60?-4:4),pt+2);g.textBaseline='middle';}
 g.lineWidth=2;g.strokeStyle='#34d399';g.beginPath();pts.forEach((p,k)=>{const y=pt+h*(vmax-p.v)/(vmax-vmin);k?g.lineTo(X(p.t),y):g.moveTo(X(p.t),y);});g.stroke();
 g.strokeStyle='#fb923c';g.beginPath();pts.forEach((p,k)=>{const y=pt+h*(imax-p.i)/(imax-imin);k?g.lineTo(X(p.t),y):g.moveTo(X(p.t),y);});g.stroke();
 const hm=t=>new Date(t).toLocaleTimeString('it-IT',{hour:'2-digit',minute:'2-digit'});g.fillStyle='#8b9bb4';g.textAlign='center';g.textBaseline='bottom';g.fillText(hm(t0),pl+20,H-2);g.fillText(hm(t1),pl+w-20,H-2);}
window.addEventListener('resize',draw);
if('serviceWorker' in navigator){navigator.serviceWorker.register('/sw.js').catch(()=>{});}
poll();
setWin(win);loadTrend();setInterval(loadTrend,30000);
</script>
</body>
</html>
)rawliteral";

static const char WEB_SW[] PROGMEM = R"js(const C='lifepo4-v1';
self.addEventListener('install',e=>{e.waitUntil(caches.open(C).then(c=>c.addAll(['/','/manifest.json','/icon-192.png','/icon-512.png'])));self.skipWaiting();});
self.addEventListener('activate',e=>{e.waitUntil(self.clients.claim());});
self.addEventListener('fetch',e=>{const u=new URL(e.request.url);if(u.pathname.startsWith('/api/'))return;
e.respondWith(fetch(e.request).then(r=>{const cp=r.clone();caches.open(C).then(c=>c.put(e.request,cp));return r;}).catch(()=>caches.match(e.request)));});
)js";
