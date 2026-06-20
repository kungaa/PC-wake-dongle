#ifndef WAKE_DONGLE_WEB_PAGE_H
#define WAKE_DONGLE_WEB_PAGE_H

// Config UI served at the dongle's IP (default http://10.7.7.107/, selectable
// in Settings) or http://picowake.local/ (best-effort).
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
/* One consistent look + height for every text input, password and select. */
input[type=text],input[type=password],select{background:#222;border:1px solid #444;color:#eee;
  padding:.4rem .5rem;border-radius:4px;font-size:.92rem;box-sizing:border-box;height:2.2rem}
/* Form fields: a fixed, shared control width so inputs/selects line up. The
   password sits in a .pwwrap (for the eye button) that takes the same width. */
.field input[type=text],.field select,.field .pwwrap{width:20em;max-width:100%;
  display:block;margin:.35rem 0}
/* ...but controls sharing a row with a button stay inline and flex to fill. */
.row input[type=text],.row input[type=password],.row select{display:inline-block;flex:1 1 12em;
  width:auto;margin:0}
/* Table name inputs stay compact (they sit inside narrow cells). */
table input[type=text]{width:11em;height:auto;margin:0}
#customip_wrap{margin-top:.5rem}
.hint{color:#888;font-size:.82rem;margin:.3rem 0}
.field{margin:1rem 0}.field .lbl{display:block;font-weight:600;margin-bottom:.35rem;font-size:.92rem}
#wol_wrap{border-left:2px solid #333;padding-left:.9rem;margin:.4rem 0}
button{background:#2563eb;border:0;color:#fff;padding:0 1rem;height:2.2rem;border-radius:4px;
  cursor:pointer;font-size:.92rem;vertical-align:middle}
button:disabled{background:#333;color:#777;cursor:default}
button.del{background:#7f1d1d;padding:.25rem .6rem;height:auto}
/* Tight rows for control+button pairs; looser only where checkboxes wrap. */
.row{display:flex;gap:.5rem;align-items:center;flex-wrap:wrap;margin:.5rem 0}
#status{min-height:1.2em}
.warn{color:#facc15;font-size:.85rem}
.mono{font-family:monospace;font-size:.88rem}
.dirty{color:#facc15}
i{color:#888}
a{color:#60a5fa}
h1 a{color:inherit;text-decoration:none}
h1 a:hover{color:#60a5fa}
/* Password field + show/hide eye toggle. Width comes from .field .pwwrap above
   so it matches the SSID input exactly; the input fills the wrap. */
.pwwrap{position:relative}
.pwwrap input[type=password],.pwwrap input[type=text]{width:100%;padding-right:2.4rem;margin:0}
.eye{position:absolute;right:.2rem;top:50%;transform:translateY(-50%);background:none;border:0;
  padding:.2rem .4rem;height:auto;font-size:1rem;cursor:pointer;color:#aaa}
footer{margin:2.5rem 0 1rem;text-align:center}
</style></head><body>
<h1><a href="https://github.com/kungaa/PC-wake-dongle" target="_blank" rel="noopener">PC Wake Dongle</a> <small id="ver"></small></h1>
<p>Wakes this PC when one of the saved Bluetooth LE devices powers on.</p>

<h2>Saved devices</h2>
<table><thead><tr><th>Wake</th><th>Name</th><th>MAC</th><th></th></tr></thead>
<tbody id="saved"></tbody></table>
<div class="row">
  <button id="save" disabled>Save names</button>
  <span id="status"></span>
</div>

<h2>Nearby BLE devices <i style="font-weight:normal;font-size:.8em">(click to add)</i></h2>
<p class="warn">Tip: power-cycle your gamepad while watching this list and pick the entry that
appears. Phones and some devices rotate random MAC addresses and will not work reliably.</p>
<table><thead><tr><th>Name</th><th>MAC</th><th>RSSI</th><th>Seen</th></tr></thead>
<tbody id="nearby"><tr><td colspan="4"><i>scanning&hellip;</i></td></tr></tbody></table>

<h2>Settings</h2>
<div class="row">
  <label><input type="checkbox" id="en"> Wake enabled (global)</label>
  <label><input type="checkbox" id="led"> Status LED</label>
</div>
<div class="field">
  <label class="lbl">Config page address</label>
  <select id="subnet"></select>
  <div id="customip_wrap" hidden>
    <input id="custom_ip" type="text" inputmode="decimal" placeholder="e.g. 10.20.30.107"
           pattern="\d{1,3}(\.\d{1,3}){3}">
    <button id="custom_ip_save">Save</button>
    <div class="hint">&#9888;&#65039; <b>Advanced.</b> Must be a <b>private</b> address
    (<code>10.x.x.x</code>, <code>172.16&ndash;31.x.x</code>, or <code>192.168.x.x</code>), and not a
    <code>.0</code>/<code>.255</code>. If you enter something unreachable the dongle falls back to the
    default address &mdash; you won't get locked out, but you may not land where you expected. The PC
    gets a DHCP lease in the same <code>/29</code> block.</div>
  </div>
</div>
<p id="subnetwarn" class="warn" hidden>Saved. <b>Unplug and replug the dongle</b>, then browse to
the new address above.</p>

<h2>Wake-on-LAN from Shutdown (S5, soft-off)</h2>
<div class="hint">Only a few motherboards (mainly ASRock &amp; MSI) can wake from full shutdown via USB.
Many more support <b>Wake-on-LAN</b> from S5, so this is the fallback for those boards: the dongle
joins your Wi-Fi and sends a WOL packet alongside the USB wake. Requires <b>"Wake on LAN" /
"Power On by PCIE" enabled in both the BIOS and the OS</b>, and <b>ErP/EuP disabled in the BIOS</b>
so the dongle stays powered in S5.</div>
<div class="row">
  <label><input type="checkbox" id="wol_en"> Enable wake from soft-off via Wake-on-LAN</label>
</div>
<div id="wol_wrap" hidden>
  <div class="field">
    <label class="lbl">Home Wi-Fi network</label>
    <div class="row">
      <select id="wifi_ssid_sel"><option value="">&mdash; pick a network &mdash;</option></select>
      <button id="wifi_scan" type="button">Scan</button>
    </div>
    <input id="wifi_ssid" type="text" placeholder="or type network name (SSID)">
    <span class="pwwrap"><input id="wifi_pass" type="password" placeholder="Wi-Fi password (leave blank to keep)">
      <button type="button" id="wifi_pass_eye" class="eye" title="Show/hide password" aria-label="Show password">&#128065;</button></span>
    <div class="row">
      <button id="wifi_save">Save Wi-Fi</button>
      <span id="wifi_stat" class="hint"></span>
    </div>
    <div class="hint">&#9888;&#65039; The dongle's radio is <b>2.4 GHz only</b> &mdash; pick a 2.4 GHz
    network (5 GHz / 6 GHz networks won't appear or connect).</div>
  </div>
  <div class="field">
    <label class="lbl">Target PC</label>
    <div class="row">
      <input id="wol_ip" type="text" inputmode="decimal" placeholder="PC IP, e.g. 192.168.1.50"
             pattern="\d{1,3}(\.\d{1,3}){3}">
      <button id="wol_resolve" type="button">Resolve MAC</button>
    </div>
    <input id="wol_mac" type="text" placeholder="PC MAC, e.g. AA:BB:CC:DD:EE:FF"
           pattern="([0-9A-Fa-f]{2}[:-]){5}[0-9A-Fa-f]{2}">
    <div class="row"><button id="wol_save">Save target</button><span id="wol_stat" class="hint"></span></div>
    <div class="hint">Enter the PC's LAN IP while it is <b>on</b> and hit <b>Resolve MAC</b> to fill the
    MAC automatically, or type the MAC yourself.</div>
  </div>
</div>

<script>
const $=id=>document.getElementById(id);
const esc=s=>s.replace(/[&<>"']/g,c=>'&#'+c.charCodeAt(0)+';');
let devs=[],namesDirty=false,savedSubnet=0;

function setStatus(msg,warn){const st=$('status');st.className=warn?'dirty':'';st.textContent=msg}
function markNamesDirty(){namesDirty=true;$('save').disabled=false;setStatus('unsaved name changes',true)}
function toggleCustomIp(){
  const sel=$('subnet');
  $('customip_wrap').hidden=(+sel.value!==sel.options.length-1);
}
function toggleWol(){$('wol_wrap').hidden=!$('wol_en').checked}

async function save(){
  // The SSID typed field overrides the picker if non-empty.
  const ssid=($('wifi_ssid').value.trim()||$('wifi_ssid_sel').value);
  let body=`enabled=${$('en').checked?1:0}&led_off=${$('led').checked?0:1}&subnet=${$('subnet').value}`+
    `&custom_ip=${encodeURIComponent($('custom_ip').value.trim())}`+
    `&wol_enabled=${$('wol_en').checked?1:0}`+
    `&wifi_ssid=${encodeURIComponent(ssid)}`+
    `&wifi_pass=${encodeURIComponent($('wifi_pass').value)}`+
    `&wol_target_ip=${encodeURIComponent($('wol_ip').value.trim())}`+
    `&wol_target_mac=${encodeURIComponent($('wol_mac').value.trim())}`;
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
  const sel=$('subnet');sel.innerHTML='';
  (c.subnets||[]).forEach((s,i)=>{
    const o=document.createElement('option');o.value=i;o.textContent='http://'+s+'/'+(i?'':' (default)');
    sel.appendChild(o);
  });
  const customOpt=document.createElement('option');
  customOpt.value=(c.subnets||[]).length;customOpt.textContent='Custom…';
  sel.appendChild(customOpt);
  savedSubnet=c.subnet||0;sel.value=savedSubnet;$('subnetwarn').hidden=true;
  if(c.custom_ip&&c.custom_ip!=='0.0.0.0')$('custom_ip').value=c.custom_ip;
  toggleCustomIp();
  // Wake-on-LAN block.
  $('wol_en').checked=!!c.wol_enabled;
  $('wifi_ssid').value=c.wifi_ssid||'';
  $('wifi_pass').placeholder=c.wifi_pass_set?'leave blank to keep current':'Wi-Fi password';
  if(c.wol_target_ip&&c.wol_target_ip!=='0.0.0.0')$('wol_ip').value=c.wol_target_ip;
  if(c.wol_target_mac&&c.wol_target_mac!=='00:00:00:00:00:00')$('wol_mac').value=c.wol_target_mac;
  $('wifi_stat').textContent=c.wifi_status?('Wi-Fi: '+c.wifi_status):'';
  toggleWol();
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
$('subnet').onchange=async()=>{
  toggleCustomIp();
  await save();
  // The new address only takes effect on the next replug; warn if it changed.
  if(+$('subnet').value!==savedSubnet){savedSubnet=+$('subnet').value;$('subnetwarn').hidden=false}
};
async function saveCustomIp(){
  await save();
  $('subnetwarn').hidden=false;
}
$('custom_ip_save').onclick=saveCustomIp;
$('custom_ip').addEventListener('keydown',e=>{if(e.key==='Enter')saveCustomIp()});

// ---- Wake-on-LAN ----
$('wol_en').onchange=async()=>{toggleWol();await save()};

// Wi-Fi SSID picker: only runs when the user clicks Scan (no background polling).
// After a click we poll /api/wifi-scan until the hardware scan completes.
let wifiScanTimer=null;
function fillSsidList(nets){
  const sel=$('wifi_ssid_sel'),cur=sel.value;
  sel.innerHTML='<option value="">&mdash; pick a network &mdash;</option>';
  (nets||[]).sort((a,b)=>b.rssi-a.rssi).forEach(n=>{
    const o=document.createElement('option');o.value=n.ssid;o.textContent=`${n.ssid} (${n.rssi} dBm)`;
    sel.appendChild(o);
  });
  sel.value=cur;
}
async function wifiScanPoll(){
  try{const d=await (await fetch('/api/wifi-scan')).json();fillSsidList(d.networks);
    if(!d.scanning){
      clearInterval(wifiScanTimer);wifiScanTimer=null;
      $('wifi_scan').disabled=false;
      const n=(d.networks||[]).length;
      $('wifi_stat').textContent=n?`found ${n} network${n>1?'s':''}`:'no 2.4 GHz networks found';
    }
  }catch(e){}
}
function startWifiScan(){
  $('wifi_scan').disabled=true;$('wifi_stat').textContent='scanning…';
  wifiScanPoll();
  if(!wifiScanTimer)wifiScanTimer=setInterval(wifiScanPoll,1500);
}
$('wifi_scan').onclick=startWifiScan;
// Picking from the dropdown copies the SSID INTO the text field (the field
// save() reads first). This makes the selection robust against the option list
// being rebuilt by later scan polls, which would otherwise drop a <select>
// selection and leave the SSID empty.
$('wifi_ssid_sel').onchange=()=>{if($('wifi_ssid_sel').value)$('wifi_ssid').value=$('wifi_ssid_sel').value};

async function saveWifi(){
  $('wifi_stat').textContent='saving…';
  await save();
  $('wifi_pass').value='';                // never keep the typed pass around
  setTimeout(refreshWifiStatus,1000);
}
$('wifi_save').onclick=saveWifi;
$('wifi_pass').addEventListener('keydown',e=>{if(e.key==='Enter')saveWifi()});

async function refreshWifiStatus(){
  try{const c=await (await fetch('/api/config')).json();
    $('wifi_stat').textContent=c.wifi_status?('Wi-Fi: '+c.wifi_status):'';
  }catch(e){}
}

// Resolve MAC from the target IP via ARP on the dongle's Wi-Fi netif.
$('wol_resolve').onclick=async()=>{
  const ip=$('wol_ip').value.trim();
  if(!ip){return}
  $('wol_stat').textContent='resolving…';
  try{
    const r=await fetch('/api/wol-resolve',{method:'POST',
      headers:{'Content-Type':'application/x-www-form-urlencoded'},body:`ip=${encodeURIComponent(ip)}`});
    const c=await r.json();
    if(c.wol_target_mac&&c.wol_target_mac!=='00:00:00:00:00:00'){
      $('wol_mac').value=c.wol_target_mac;$('wol_stat').textContent='resolved ✓';
    }else{
      $('wol_stat').textContent='no reply — is the PC on? try again';
    }
  }catch(e){$('wol_stat').textContent='resolve failed'}
};
async function saveWol(){await save();$('wol_stat').textContent='Saved ✓'}
$('wol_save').onclick=saveWol;

// Show/hide the Wi-Fi password.
$('wifi_pass_eye').onclick=()=>{
  const p=$('wifi_pass');p.type=p.type==='password'?'text':'password';
};

loadCfg();poll();setInterval(poll,2000);setInterval(()=>{if($('wol_en').checked)refreshWifiStatus()},5000);
</script>
<footer>
  <div id="kofi"></div>
</footer>
<script>
// Ko-fi support button (loads from ko-fi CDN; needs internet on the browsing
// device -- the dongle itself serves no external assets).
(function(){
  var s=document.createElement('script');
  s.src='https://storage.ko-fi.com/cdn/widget/Widget_2.js';
  s.onload=function(){try{
    kofiwidget2.init('Support me on Ko-fi','#72a4f2','N4A11ZXR0I');
    document.getElementById('kofi').innerHTML=kofiwidget2.getHTML();
  }catch(e){}};
  document.body.appendChild(s);
})();
</script></body></html>
)rawhtml";

#endif // WAKE_DONGLE_WEB_PAGE_H
