#ifndef WAKE_DONGLE_WEB_PAGE_H
#define WAKE_DONGLE_WEB_PAGE_H

// Config UI served at http://10.7.7.107/ (or http://picowake.local/, best-effort).
// Single self-contained page; talks to /api/config and /api/scan.
//
// Save policy: discrete actions (add/delete device, enable toggles, LED
// toggle) autosave immediately; name edits are buffered behind the Save
// button so flash isn't rewritten per keystroke.
static const char WEB_PAGE[] = R"rawhtml(<!doctype html>
<html><head><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1">
<title>PC Wake Dongle</title>
<style>
:root{color-scheme:dark}
body{font-family:system-ui,sans-serif;background:#111;color:#eee;max-width:680px;margin:2rem auto;padding:0 1rem}
h1{font-size:1.4rem}h1 small{color:#888;font-weight:normal;font-size:.7em}
h2{font-size:1.05rem;margin-top:1.6rem}
table{width:100%;border-collapse:collapse;margin:.6rem 0}
th,td{padding:.4rem .5rem;text-align:left;border-bottom:1px solid #333;font-size:.92rem}
#nearby tbody tr{cursor:pointer}#nearby tbody tr:hover{background:#222}
tr.saved{opacity:.45}
input[type=text]{background:#222;border:1px solid #444;color:#eee;padding:.35rem;border-radius:4px;width:11em}
button{background:#2563eb;border:0;color:#fff;padding:.45rem 1rem;border-radius:4px;cursor:pointer;font-size:.95rem}
button:disabled{background:#333;color:#777;cursor:default}
button.del{background:#7f1d1d;padding:.25rem .6rem}
.row{display:flex;gap:1.2rem;align-items:center;flex-wrap:wrap;margin:.6rem 0}
#status{min-height:1.2em}
.warn{color:#facc15;font-size:.85rem}
.mono{font-family:monospace;font-size:.88rem}
.dirty{color:#facc15}
i{color:#888}
</style></head><body>
<h1>PC Wake Dongle <small id="ver"></small></h1>
<p>Wakes this PC when one of the saved Bluetooth LE devices powers on.</p>

<h2>Saved devices</h2>
<table><thead><tr><th>Wake</th><th>Name</th><th>MAC</th><th></th></tr></thead>
<tbody id="saved"></tbody></table>
<div class="row">
  <button id="save" disabled>Save names</button>
  <span id="status"></span>
</div>

<h2>Settings</h2>
<div class="row">
  <label><input type="checkbox" id="en"> Wake enabled (global)</label>
  <label><input type="checkbox" id="led"> Status LED</label>
</div>

<h2>Nearby BLE devices <i style="font-weight:normal;font-size:.8em">(click to add)</i></h2>
<p class="warn">Tip: power-cycle your gamepad while watching this list and pick the entry that
appears. Phones and some devices rotate random MAC addresses and will not work reliably.</p>
<table><thead><tr><th>Name</th><th>MAC</th><th>RSSI</th><th>Seen</th></tr></thead>
<tbody id="nearby"><tr><td colspan="4"><i>scanning&hellip;</i></td></tr></tbody></table>

<script>
const $=id=>document.getElementById(id);
const esc=s=>s.replace(/[&<>"']/g,c=>'&#'+c.charCodeAt(0)+';');
let devs=[],namesDirty=false;

function setStatus(msg,warn){const st=$('status');st.className=warn?'dirty':'';st.textContent=msg}
function markNamesDirty(){namesDirty=true;$('save').disabled=false;setStatus('unsaved name changes',true)}

async function save(){
  let body=`enabled=${$('en').checked?1:0}&led_off=${$('led').checked?0:1}`;
  devs.forEach(d=>{
    const name=d.name.replace(/[|]/g,' ').trim()||'Device';
    body+=`&dev=${encodeURIComponent(d.mac+'|'+(d.enabled?1:0)+'|'+name)}`;
  });
  const r=await fetch('/api/config',{method:'POST',headers:{'Content-Type':'application/x-www-form-urlencoded'},body});
  if(r.ok){namesDirty=false;$('save').disabled=true;setStatus('Saved ✓',false)}
  else setStatus('Save failed',true);
}

function renderSaved(){
  const tb=$('saved');tb.innerHTML='';
  if(!devs.length){tb.innerHTML='<tr><td colspan="4"><i>none yet — add one from the list below</i></td></tr>';return}
  devs.forEach((d,i)=>{
    const tr=document.createElement('tr');
    tr.innerHTML=`<td><input type="checkbox" ${d.enabled?'checked':''}></td>
<td><input type="text" maxlength="19" value="${esc(d.name)}"></td>
<td class="mono">${d.mac}</td>
<td><button class="del">&#10005;</button></td>`;
    tr.querySelector('input[type=checkbox]').onchange=e=>{d.enabled=e.target.checked?1:0;save()};
    tr.querySelector('input[type=text]').oninput=e=>{d.name=e.target.value;markNamesDirty()};
    tr.querySelector('button').onclick=()=>{devs.splice(i,1);renderSaved();save()};
    tb.appendChild(tr);
  });
}

function addDevice(mac,name){
  if(devs.some(d=>d.mac===mac))return;
  if(devs.length>=8){setStatus('limit: 8 devices',true);return}
  devs.push({mac,enabled:1,name:(name||'Device').substring(0,19)});
  renderSaved();save();
}

async function loadCfg(){
  const c=await (await fetch('/api/config')).json();
  $('en').checked=!!c.enabled;$('led').checked=!c.led_off;$('ver').textContent=c.version;
  devs=c.devices;namesDirty=false;$('save').disabled=true;setStatus('',false);
  renderSaved();
}

async function poll(){
  try{
    const d=await (await fetch('/api/scan')).json();
    const tb=$('nearby');tb.innerHTML='';
    if(!d.devices.length){tb.innerHTML='<tr><td colspan="4"><i>nothing seen yet&hellip;</i></td></tr>';return}
    d.devices.sort((a,b)=>b.rssi-a.rssi).forEach(x=>{
      const tr=document.createElement('tr');
      if(devs.some(s=>s.mac===x.mac))tr.className='saved';
      tr.innerHTML=`<td>${x.name?esc(x.name):'<i>(no name)</i>'}</td><td class="mono">${x.mac}</td><td>${x.rssi} dBm</td><td>${x.age}s ago</td>`;
      tr.onclick=()=>addDevice(x.mac,x.name);
      tb.appendChild(tr);
    });
  }catch(e){}
}

$('en').onchange=save;
$('led').onchange=save;
$('save').onclick=save;

loadCfg();poll();setInterval(poll,2000);
</script></body></html>
)rawhtml";

#endif // WAKE_DONGLE_WEB_PAGE_H
