// The device's web UI, served from flash. Plain HTML/JS, no external assets,
// so it works with no internet connection.
#pragma once

#include <pgmspace.h>

static const char kIndexHtml[] PROGMEM = R"HTML(<!doctype html>
<html lang="en"><head><meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>Printr</title>
<style>
:root{--bg:#f6f5f2;--fg:#1d1d1b;--mut:#6b6a66;--card:#fff;--line:#e2e0da;--acc:#1f6feb;--bad:#c62828;--ok:#2e7d32}
@media (prefers-color-scheme:dark){:root{--bg:#161615;--fg:#ecebe7;--mut:#9d9b95;--card:#20201e;--line:#34332f;--acc:#58a6ff;--bad:#ef5350;--ok:#66bb6a}}
*{box-sizing:border-box}body{margin:0;background:var(--bg);color:var(--fg);font:15px/1.45 system-ui,sans-serif}
main{max-width:640px;margin:0 auto;padding:16px}
h1{font-size:22px;margin:8px 0 16px}h2{font-size:16px;margin:0 0 12px}
section{background:var(--card);border:1px solid var(--line);border-radius:10px;padding:16px;margin-bottom:16px}
textarea,input,select{width:100%;padding:8px;border:1px solid var(--line);border-radius:6px;background:var(--bg);color:var(--fg);font:inherit}
textarea{min-height:110px;resize:vertical}
button{padding:8px 14px;border:0;border-radius:6px;background:var(--acc);color:#fff;font:inherit;cursor:pointer;margin:4px 4px 0 0}
button.sec{background:transparent;color:var(--fg);border:1px solid var(--line)}button.bad{background:var(--bad)}
dl{display:grid;grid-template-columns:max-content 1fr;gap:4px 12px;margin:0}dt{color:var(--mut)}dd{margin:0;overflow-wrap:anywhere}
label{display:block;margin:10px 0 4px;color:var(--mut);font-size:13px}.row{display:flex;gap:8px;align-items:center}
.row input[type=checkbox]{width:auto}#msg{position:fixed;left:16px;right:16px;bottom:16px;max-width:608px;margin:0 auto;padding:10px 14px;border-radius:8px;background:var(--fg);color:var(--bg);display:none}
.ok{color:var(--ok)}.bad{color:var(--bad)}small{color:var(--mut)}
</style></head><body><main>
<h1>Printr</h1>
<section><h2>Status</h2><dl id="st"><dt>Loading</dt><dd>&hellip;</dd></dl></section>
<section><h2>Print a message</h2>
<label for="from">From</label><input id="from" maxlength="24" placeholder="Your name">
<label for="text">Message</label><textarea id="text" maxlength="4096"></textarea>
<button onclick="doPrint()">Print</button></section>
<section><h2>Actions</h2>
<button class="sec" onclick="act('test')">Test page</button>
<button class="sec" onclick="act('chartest')">Character test</button>
<button class="sec" onclick="act('poll')">Check messages</button>
<button class="sec" onclick="act('heartbeat')">Heartbeat</button>
<button class="sec" onclick="act('register','Forget cloud credentials and register again?')">Re-register</button>
<button class="sec" onclick="act('markall','Mark every pending cloud message as printed without printing it?')">Clear queue</button>
<button class="sec" onclick="act('portal','Open the WiFi setup network? The local web UI stops until it closes.')">WiFi setup</button>
<button class="sec" onclick="act('reboot','Reboot the printer?')">Reboot</button>
<button class="bad" onclick="act('forget-wifi','Forget the WiFi network? You will need to set it up again.')">Forget WiFi</button>
<button class="bad" onclick="act('factory-reset','Erase ALL settings and WiFi?')">Factory reset</button>
<p><small>Firmware update: <a href="/update">/update</a> (user <b>admin</b>). Admin actions ask for the admin password printed on the setup receipt.</small></p>
</section>
<section><h2>Settings</h2><div id="set"><button class="sec" onclick="loadSettings()">Load settings</button></div></section>
<section><h2>Custom server certificate</h2>
<small>Only needed if your server's HTTPS certificate isn't from Let's Encrypt or Google Trust Services. Paste the root CA in PEM format; save empty to remove.</small>
<textarea id="ca" placeholder="-----BEGIN CERTIFICATE-----"></textarea>
<button class="sec" onclick="saveCa()">Save certificate</button></section>
</main><div id="msg"></div>
<script>
const $=id=>document.getElementById(id);
function toast(t,bad){const m=$('msg');m.textContent=t;m.style.display='block';m.style.background=bad?'var(--bad)':'';clearTimeout(m._t);m._t=setTimeout(()=>m.style.display='none',4000)}
async function call(path,opt){try{const r=await fetch(path,opt);let j={};try{j=await r.json()}catch(e){}
if(!r.ok){toast(j.error||('HTTP '+r.status),true);return null}return j}catch(e){toast('Device unreachable',true);return null}}
function esc(s){return String(s).replace(/[&<>"']/g,c=>({'&':'&amp;','<':'&lt;','>':'&gt;','"':'&quot;',"'":'&#39;'}[c]))}
function dur(s){const d=Math.floor(s/86400),h=Math.floor(s%86400/3600),m=Math.floor(s%3600/60);return (d?d+'d ':'')+(h?h+'h ':'')+m+'m'}
async function status(){const s=await call('/api/status');if(!s)return;const c=s.cloud;
const rows=[['Name',s.name+'.local'],['Firmware',s.firmware],['Uptime',dur(s.uptime_s)],['Last reset',s.reset_reason],
['WiFi',s.wifi.connected?esc(s.wifi.ssid)+' ('+s.wifi.rssi+' dBm)':'<span class="bad">disconnected</span>',1],['IP',s.wifi.ip],
['Memory',s.free_heap+' B free, '+s.max_block+' B max block'],
['Cloud',c.server?(c.state==='online'?'<span class="ok">online</span>':esc(c.state.replace(/_/g,' ')))+(c.last_error?' &mdash; '+esc(c.last_error):''):'off (local only)',1]];
if(c.server)rows.push(['Server',c.server],['Printer code',c.display_code||'-'],['Printed',c.printed]);
// r[2] marks values that are already escaped HTML
$('st').innerHTML=rows.map(r=>'<dt>'+r[0]+'</dt><dd>'+(r[2]?r[1]:esc(r[1]))+'</dd>').join('')}
async function doPrint(){const text=$('text').value;if(!text.trim())return toast('Type a message first',true);
const j=await call('/api/print',{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify({text,from:$('from').value||'Web'})});
if(j){toast('Printed');$('text').value=''}}
async function act(a,confirmText){if(confirmText&&!confirm(confirmText))return;const j=await call('/api/action/'+a,{method:'POST'});if(j)toast('Done');setTimeout(status,1500)}
const help={deviceName:'Network name (NAME.local). Reboot to apply.',adminPassword:'8+ characters. Reboot to apply.',requireAuthToPrint:'Require the admin password to print over the network.',
upsideDown:'Printer is mounted upside down.',lineWidth:'Characters per line (32 for 58 mm paper).',printerBaud:'Printer serial speed. Reboot to apply.',
heatDots:'Heating dots (more = faster, more current).',heatTime:'Heating time (more = darker, slower).',heatInterval:'Heating interval (more = clearer, slower).',
printDensity:'Print density.',printBreakTime:'Print break time.',feedLines:'Blank lines after each receipt.',bootBanner:'Print a status receipt when WiFi connects after power-on.',
checkPaper:'Hold messages while paper is out (needs the printer TX wire).',timezone:'POSIX TZ for timestamps, e.g. EST5EDT,M3.2.0,M11.1.0. Blank = no timestamps.',
serverUrl:'Cloud server, e.g. https://printr.example.com. Blank = local only.',tlsInsecure:'Skip HTTPS certificate checks (not recommended).',
pollSeconds:'How often to check the server for messages.',heartbeatSeconds:'How often to report status to the server.'};
async function loadSettings(){const s=await call('/api/settings');if(!s)return;s.adminPassword='';
$('set').innerHTML=Object.keys(help).map(k=>{const v=s[k],h='<small>'+esc(help[k])+'</small>';
if(typeof v==='boolean')return '<div class="row"><input type="checkbox" id="s_'+k+'"'+(v?' checked':'')+'><label for="s_'+k+'">'+k+'</label></div>'+h;
return '<label for="s_'+k+'">'+k+'</label><input id="s_'+k+'" '+(typeof v==='number'?'type="number"':k==='adminPassword'?'type="password" placeholder="unchanged"':'')+' value="'+esc(v)+'">'+h}).join('')+
'<p><button onclick="saveSettings()">Save settings</button></p>';$('set')._orig=s}
async function saveSettings(){const o=$('set')._orig,out={};for(const k of Object.keys(help)){const e=$('s_'+k);let v=e.type==='checkbox'?e.checked:e.type==='number'?Number(e.value):e.value;
if(k==='adminPassword'&&!v)continue;if(v!==o[k])out[k]=v}
if(!Object.keys(out).length)return toast('No changes');const j=await call('/api/settings',{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify(out)});
if(j){toast(j.reboot_required?'Saved. Reboot to apply.':'Saved');loadSettings();status()}}
async function saveCa(){const j=await call('/api/ca',{method:'POST',headers:{'Content-Type':'text/plain'},body:$('ca').value.trim()});if(j)toast('Certificate saved')}
status();setInterval(status,10000);
</script></body></html>)HTML";
