#ifndef WAKE_DONGLE_WEB_PAGE_H
#define WAKE_DONGLE_WEB_PAGE_H

// Config UI served at http://192.168.7.1/ (or http://picowake.local/).
// Single self-contained page; talks to /api/config and /api/scan.
static const char WEB_PAGE[] = R"rawhtml(<!doctype html>
<html><head><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1">
<title>PC Wake Dongle</title>
<style>
:root{color-scheme:dark}
body{font-family:system-ui,sans-serif;background:#111;color:#eee;max-width:640px;margin:2rem auto;padding:0 1rem}
h1{font-size:1.4rem}h1 small{color:#888;font-weight:normal;font-size:.7em}
h2{font-size:1.1rem}
table{width:100%;border-collapse:collapse;margin:1rem 0}
th,td{padding:.45rem .6rem;text-align:left;border-bottom:1px solid #333;font-size:.92rem}
tbody tr{cursor:pointer}tbody tr:hover{background:#222}tr.sel{background:#1e3a5f}
input[type=text]{background:#222;border:1px solid #444;color:#eee;padding:.45rem;border-radius:4px;font-family:monospace;width:13.5em}
button{background:#2563eb;border:0;color:#fff;padding:.5rem 1.1rem;border-radius:4px;cursor:pointer;font-size:1rem}
.row{display:flex;gap:1rem;align-items:center;flex-wrap:wrap;margin:.6rem 0}
#status{min-height:1.2em}
.warn{color:#facc15;font-size:.85rem}
.mono{font-family:monospace}
</style></head><body>
<h1>PC Wake Dongle <small id="ver"></small></h1>
<p>Wakes this PC when the configured Bluetooth LE device powers on.</p>
<div class="row">
  <label><input type="checkbox" id="en"> Wake enabled</label>
  <input type="text" id="mac" placeholder="AA:BB:CC:DD:EE:FF" maxlength="17">
  <button id="save">Save</button>
</div>
<div id="status"></div>
<h2>Nearby BLE devices</h2>
<p class="warn">Tip: power-cycle your gamepad while watching this list and pick the entry that
appears. Phones and some devices rotate random MAC addresses and will not work reliably.</p>
<table><thead><tr><th>Name</th><th>MAC</th><th>RSSI</th><th>Seen</th></tr></thead>
<tbody id="list"><tr><td colspan="4"><i>scanning&hellip;</i></td></tr></tbody></table>
<script>
const $=id=>document.getElementById(id);
const up=m=>m.toUpperCase().replace(/-/g,':');
async function loadCfg(){
  const c=await (await fetch('/api/config')).json();
  $('en').checked=!!c.enabled; $('mac').value=c.mac; $('ver').textContent=c.version;
}
async function poll(){
  try{
    const d=await (await fetch('/api/scan')).json();
    const tb=$('list'); tb.innerHTML='';
    if(!d.devices.length){tb.innerHTML='<tr><td colspan="4"><i>nothing seen yet&hellip;</i></td></tr>';return}
    d.devices.sort((a,b)=>b.rssi-a.rssi).forEach(x=>{
      const tr=document.createElement('tr');
      if(up(x.mac)===up($('mac').value)) tr.className='sel';
      tr.innerHTML=`<td>${x.name||'<i>(no name)</i>'}</td><td class="mono">${x.mac}</td><td>${x.rssi} dBm</td><td>${x.age}s ago</td>`;
      tr.onclick=()=>{$('mac').value=x.mac;poll()};
      tb.appendChild(tr);
    });
  }catch(e){}
}
$('save').onclick=async()=>{
  const mac=up($('mac').value.trim());
  const st=$('status');
  if(!/^([0-9A-F]{2}:){5}[0-9A-F]{2}$/.test(mac)){st.style.color='#f87171';st.textContent='Invalid MAC format';return}
  const body=`enabled=${$('en').checked?1:0}&mac=${encodeURIComponent(mac)}`;
  const r=await fetch('/api/config',{method:'POST',headers:{'Content-Type':'application/x-www-form-urlencoded'},body});
  st.style.color=r.ok?'#4ade80':'#f87171';
  st.textContent=r.ok?'Saved ✓':'Save failed';
  if(r.ok) loadCfg();
};
loadCfg();poll();setInterval(poll,2000);
</script></body></html>
)rawhtml";

#endif // WAKE_DONGLE_WEB_PAGE_H
