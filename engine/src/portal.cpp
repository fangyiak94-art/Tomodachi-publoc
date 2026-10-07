#include "deskpet/portal.h"

#include <ArduinoJson.h>

#include "deskpet/pack.h"

namespace dp {

std::string roomListJson(PortalHost& host) {
  JsonDocument doc;
  JsonArray rooms = doc["rooms"].to<JsonArray>();
  for (const std::string& id : host.roomIds()) rooms.add(id);
  doc["active"] = host.activeRoomId();
  std::string out;
  serializeJson(doc, out);
  return out;
}

bool portalUploadPath(const std::string& kind, const std::string& id, const std::string& file,
                      std::string& path, std::string& err) {
  if (kind != "pack" && kind != "room") {
    err = "kind must be pack or room";
    return false;
  }
  if (!isSafeFileName(id) || id == "custom") {
    err = "bad id (a-z 0-9 - _ .)";
    return false;
  }
  if (!isSafeFileName(file)) {
    err = "bad file name";
    return false;
  }
  const bool image = file.size() > 4 && file.compare(file.size() - 4, 4, ".dps") == 0;
  const bool okName = image || file == (kind == "pack" ? "manifest.json" : "room.json");
  if (!okName) {
    err = kind == "pack" ? "packs take manifest.json and .dps files" : "rooms take room.json and .dps files";
    return false;
  }
  path = (kind == "pack" ? "/packs/" : "/rooms/") + id + "/" + file;
  return true;
}

const char kPortalPage[] = R"HTML(<!doctype html><html><head>
<meta name="viewport" content="width=device-width,initial-scale=1"><title>Desk Pet</title>
<style>
*{box-sizing:border-box}
body{font-family:system-ui,sans-serif;max-width:30em;margin:1em auto;padding:0 1em;color:#222}
h2{margin:1.2em 0 .4em;font-size:1.2em}
fieldset{border:1px solid #ccc;border-radius:8px;margin:.6em 0;min-width:0}
label{display:flex;align-items:center;gap:.5em;margin:.35em 0}
label>input:not([type=range]),label>select{margin-left:auto;min-width:0}
input[type=range]{flex:1;min-width:0}label span{width:2.2em;text-align:right}
input,select,button{font-size:1em}#name,#upid{width:60%}select{max-width:60%}
input[type=color]{width:3em;height:2em;padding:0;border:none}
.colors{display:grid;grid-template-columns:1fr 1fr;gap:0 1em}
#status{position:sticky;top:0;background:#fff;padding:.3em 0;color:#2a7}
button{width:100%;padding:.5em;margin:.3em 0}
</style></head><body>
<div id="status">Connecting...</div>
<h2>Room theme</h2>
<select id="theme" onchange="selectTheme()"></select>
<h2>Edit the room (updates live)</h2>
<label>Name <input id="name" maxlength="24"></label>
<fieldset><legend>Layout</legend><div id="layout"></div></fieldset>
<fieldset><legend>Colours</legend><div id="colors" class="colors"></div></fieldset>
<h2>Upload files</h2>
<label>Kind <select id="kind"><option value="pack">Pet pack</option><option value="room">Room theme</option></select></label>
<label>Id <input id="upid" placeholder="e.g. castle"></label>
<input id="files" type="file" multiple><button onclick="upload()">Upload</button>
<p>Press BOOT on the pet when you are done.</p>
<script>
const LAYOUT=[["groundY","Floor line",150,200],["windowX","Window x",30,150],["windowY","Window y",20,90],
["bed","Bed",50,120],["bowl","Bowl",90,190],["door","Door",120,196],["yardDoor","Yard door",120,190],["tree","Tree",40,120]];
let room=null,timer=null;
const $=id=>document.getElementById(id);
function status(t,bad){$('status').textContent=t;$('status').style.color=bad?'#c33':'#2a7';}
async function load(){
  const list=await (await fetch('/rooms')).json();
  $('theme').innerHTML=list.rooms.map(r=>`<option ${r==list.active?'selected':''}>${r}</option>`).join('');
  room=await (await fetch('/room')).json();
  $('name').value=room.name;$('name').oninput=()=>{room.name=$('name').value;queue();};
  $('layout').innerHTML='';
  for(const [k,label,lo,hi] of LAYOUT){
    const v=k.startsWith('window')?room.layout.window[k=='windowX'?0:1]:room.layout[k];
    const l=document.createElement('label');
    l.innerHTML=`<b style="width:5.5em;font-weight:normal">${label}</b><input type=range min=${lo} max=${hi} value=${v}><span>${v}</span>`;
    const r=l.querySelector('input');
    r.oninput=()=>{l.querySelector('span').textContent=r.value;
      if(k=='windowX')room.layout.window[0]=+r.value;else if(k=='windowY')room.layout.window[1]=+r.value;
      else room.layout[k]=+r.value;queue();};
    $('layout').appendChild(l);
  }
  $('colors').innerHTML='';
  for(const k in room.colors){
    const l=document.createElement('label');
    l.innerHTML=`${k}<input type=color value="${room.colors[k]}">`;
    l.querySelector('input').oninput=e=>{room.colors[k]=e.target.value;queue();};
    $('colors').appendChild(l);
  }
  status('Editing "'+room.name+'"');
}
function queue(){clearTimeout(timer);timer=setTimeout(save,250);}
async function save(){
  const r=await fetch('/room',{method:'POST',headers:{'Content-Type':'text/plain'},body:JSON.stringify(room)});
  const t=await r.text();status(r.ok?'Saved: the pet shows it now':t,!r.ok);
  if(r.ok&&$('theme').value!='custom'){const l=await (await fetch('/rooms')).json();
    $('theme').innerHTML=l.rooms.map(x=>`<option ${x==l.active?'selected':''}>${x}</option>`).join('');}
}
async function selectTheme(){
  const r=await fetch('/room/select?id='+encodeURIComponent($('theme').value),{method:'POST'});
  status(r.ok?'Switched theme':await r.text(),!r.ok);if(r.ok)load();
}
async function upload(){
  const kind=$('kind').value,id=$('upid').value.trim();
  for(const f of $('files').files){const fd=new FormData();fd.append('file',f,f.name);
    const r=await fetch(`/upload?kind=${kind}&id=${encodeURIComponent(id)}`,{method:'POST',body:fd});
    status(f.name+': '+await r.text(),!r.ok);if(!r.ok)return;}
  status('Uploaded. Pick it in Settings after pressing BOOT.');
}
load().catch(e=>status('Cannot reach the pet: '+e,true));
</script></body></html>)HTML";

}  // namespace dp
