// The configuration page (R9, R12, R13): one self-contained HTML document, no external
// resources (the hotspot has no internet). Plain words, big touch targets; nothing that
// requires understanding video signalling. Status and "On the projector" are always
// shown; the rest is split into tabs (Setup, Slide, Diagnostics) so each screen stays
// short.
//
// The slide editor renders the slide here, in the browser, at the projector's resolution
// and uploads it as a PNG; the box only stores and shows pictures. A slide is a template
// (a list of parts, each drawn by a function in PARTS) filled from a small spec object;
// adding a part type (e.g. an uploaded picture) means one new PARTS entry.
#pragma once

namespace ht {

inline constexpr const char* kConfigPage = R"HTML(<!doctype html>
<html lang="en"><head>
<meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1">
<title>hdmi-transcode</title>
<style>
:root{--bg:#f6f6f4;--fg:#1b1b1b;--muted:#666;--card:#fff;--line:#ddd;--accent:#1f6feb;--on:#e8f0fe}
@media (prefers-color-scheme:dark){:root{--bg:#141414;--fg:#eee;--muted:#9a9a9a;--card:#1e1e1e;--line:#333;--accent:#5b9dff;--on:#1d2a40}}
*{box-sizing:border-box}body{margin:0;font:16px/1.4 system-ui,sans-serif;background:var(--bg);color:var(--fg)}
main{max-width:520px;margin:0 auto;padding:16px}
h1{font-size:20px;margin:4px 0 12px}h2{font-size:15px;margin:0 0 8px;color:var(--muted);font-weight:600}
section{background:var(--card);border:1px solid var(--line);border-radius:12px;padding:14px;margin-bottom:12px}
.status{display:grid;grid-template-columns:auto 1fr;gap:4px 12px;font-size:15px}.status span:nth-child(odd){color:var(--muted)}
.choice{display:block;width:100%;text-align:left;padding:12px;margin:6px 0;border:1px solid var(--line);border-radius:10px;background:transparent;color:var(--fg);font:inherit;cursor:pointer}
.choice small{display:block;color:var(--muted)}.choice[aria-pressed=true]{border-color:var(--accent);background:var(--on)}
.row3{display:grid;grid-template-columns:repeat(3,1fr);gap:6px}.row3 .choice{margin:0;text-align:center;padding:10px 6px;font-size:14px}
select,input[type=text],textarea{width:100%;padding:10px;font:inherit;border-radius:10px;border:1px solid var(--line);background:var(--card);color:var(--fg)}
textarea{min-height:5.5em;resize:vertical}
label.f{display:block;margin:10px 0 4px;font-size:14px;color:var(--muted)}
.colours{display:flex;gap:16px;margin-top:10px}.colours label{display:flex;align-items:center;gap:8px;font-size:14px}
input[type=color]{width:44px;height:34px;border:1px solid var(--line);border-radius:8px;padding:2px;background:var(--card)}
canvas{display:block;width:100%;height:auto;margin-top:12px;border:1px solid var(--line);border-radius:6px}
.buttons{display:flex;flex-wrap:wrap;gap:8px;margin-top:12px}
.btn{flex:1 1 auto;padding:12px;border-radius:10px;border:1px solid var(--line);background:transparent;color:var(--fg);font:inherit;cursor:pointer}
.btn.primary{background:var(--accent);border-color:var(--accent);color:#fff}
.hint{font-size:13px;color:var(--muted);margin:6px 0 0}
#msg{min-height:1.4em;color:var(--muted);font-size:14px}
nav{display:flex;gap:6px;margin:0 0 12px;position:sticky;top:0;background:var(--bg);padding:6px 0;z-index:1}
nav button{flex:1;padding:10px 6px;border:1px solid var(--line);border-radius:10px;background:transparent;color:var(--fg);font:inherit;font-weight:600;cursor:pointer}
nav button[aria-selected=true]{border-color:var(--accent);background:var(--accent);color:#fff}
[data-tab][hidden]{display:none}
.verdict{font-size:16px;margin:0 0 4px}
ol.events{list-style:none;margin:0;padding:0;font-size:14px}ol.events li{padding:6px 0;border-top:1px solid var(--line)}
ol.events li:first-child{border-top:0}ol.events time{display:block;color:var(--muted);font-size:12px}
textarea.report{font:12px/1.35 ui-monospace,Menlo,monospace;min-height:12em}
</style></head><body><main>
<h1>hdmi-transcode</h1>
<section><h2>Now</h2><div class="status">
<span>Input</span><span id="in">…</span><span>Projector</span><span id="out">…</span></div></section>
<section><h2>On the projector</h2>
<div class="row3">
<button class="choice" data-show="input">Live picture</button>
<button class="choice" data-show="slide">Slide</button>
<button class="choice" data-show="pattern">Test pattern</button>
</div>
<p class="hint">With no input the slide is shown, or the test pattern if no slide is saved.</p>
</section>
<nav role="tablist">
<button role="tab" data-go="setup">Setup</button>
<button role="tab" data-go="slide">Slide</button>
<button role="tab" data-go="diag">Diagnostics</button>
</nav>
<div data-tab="setup">
<section><h2>Picture shape</h2>
<button class="choice" data-aspect="compress">Fill the screen (stretch)<small>Nothing cut off; the picture is squeezed to fit.</small></button>
<button class="choice" data-aspect="letterbox">Show the whole picture<small>Correct shape, black bars on the edges.</small></button>
<button class="choice" data-aspect="crop">Fill the screen (trim edges)<small>Correct shape; the sides are cut off.</small></button>
</section>
<section><h2>Projector resolution</h2><select id="mode"></select></section>
</div>
<div data-tab="slide" hidden>
<section><h2>Slide</h2>
<div class="row3" id="templates"></div>
<label class="f" for="s-title">Title</label><input type="text" id="s-title" maxlength="200">
<label class="f" for="s-body" id="s-body-label">Text</label><textarea id="s-body" maxlength="2000"></textarea>
<label class="f" for="s-footer">Footer (optional)</label><input type="text" id="s-footer" maxlength="200">
<div class="colours"><label><input type="color" id="s-bg"> Background</label><label><input type="color" id="s-fg"> Text</label></div>
<canvas id="preview" aria-label="Slide preview"></canvas>
<p class="hint" id="s-note"></p>
<div class="buttons">
<button class="btn primary" id="s-show">Save and show</button>
<button class="btn" id="s-save">Save</button>
<button class="btn" id="s-clear">Remove</button>
</div>
</section>
</div>
<div data-tab="diag" hidden>
<section><h2>Signal</h2><p class="verdict" id="d-verdict">…</p><p class="hint" id="d-advice"></p></section>
<section><h2>Details</h2><div class="status" id="d-table"></div></section>
<section><h2>What happened (newest first)</h2><ol class="events" id="d-events"></ol></section>
<section><h2>Report</h2>
<p class="hint">Everything on this tab as text, to send to whoever looks after the box.</p>
<textarea class="report" id="d-report" readonly></textarea>
<div class="buttons"><button class="btn" id="d-copy">Copy report</button></div>
</section>
</div>
<div id="msg" role="status"></div>
</main><script>
const $=s=>document.querySelector(s);let busy=false,status={};
const H={'Content-Type':'application/x-www-form-urlencoded','X-HT-Request':'1'};

// --- slide templates: a template is a list of parts; boxes are fractions of the screen ---
const FONT='system-ui,-apple-system,"Segoe UI",Roboto,"Helvetica Neue",Arial,sans-serif';
const FOOTER={type:'text',field:'footer',box:[.06,.86,.88,.09],align:'center',size:.045,alpha:.85};
const TEMPLATES={
 title:{label:'Title',body:'Subtitle',parts:[
  {type:'text',field:'title',box:[.08,.16,.84,.46],align:'center',valign:'bottom',size:.13,weight:700},
  {type:'text',field:'body',box:[.08,.65,.84,.18],align:'center',valign:'top',size:.065}]},
 heading:{label:'Heading + text',body:'Text',parts:[
  {type:'text',field:'title',box:[.08,.07,.84,.2],align:'center',size:.1,weight:700},
  {type:'text',field:'body',box:[.1,.31,.8,.52],align:'center',valign:'top',size:.06}]},
 left:{label:'Left aligned',body:'Text',parts:[
  {type:'text',field:'title',box:[.08,.07,.84,.2],align:'left',size:.1,weight:700},
  {type:'text',field:'body',box:[.08,.31,.84,.52],align:'left',valign:'top',size:.06}]}};
const DEFAULT={v:1,template:'title',title:'Coming up next',body:'',footer:'',bg:'#10213f',fg:'#ffffff'};

function wrap(c,text,maxW){const out=[];
 for(const para of text.split('\n')){let line='';
  for(const w of para.split(/\s+/).filter(Boolean)){const t=line?line+' '+w:w;
   if(line&&c.measureText(t).width>maxW){out.push(line);line=w}else line=t}
  out.push(line)}
 return out}
const PARTS={
 // Text in a box: wrapped, shrunk until it fits, aligned within the box.
 text(c,p,spec,W,Hh){const txt=(spec[p.field]||'').trim();if(!txt)return;
  const bx=p.box[0]*W,by=p.box[1]*Hh,bw=p.box[2]*W,bh=p.box[3]*Hh;let size=p.size*Hh,lines;
  for(;;){c.font=(p.weight||400)+' '+Math.round(size)+'px '+FONT;lines=wrap(c,txt,bw);
   const wide=Math.max(...lines.map(l=>c.measureText(l).width));
   if((lines.length*size*1.2<=bh&&wide<=bw)||size<Hh*.015)break;size*=.92}
  const lh=size*1.2,total=lines.length*lh;
  const y=p.valign==='top'?by:p.valign==='bottom'?by+bh-total:by+(bh-total)/2;
  const x=p.align==='left'?bx:bx+bw/2;
  c.save();c.globalAlpha=p.alpha||1;c.fillStyle=spec.fg;c.textAlign=p.align==='left'?'left':'center';c.textBaseline='middle';
  lines.forEach((l,i)=>c.fillText(l,x,y+lh*(i+.5)));c.restore()}};
function renderSlide(spec,cv,W,Hh){cv.width=W;cv.height=Hh;const c=cv.getContext('2d');
 c.fillStyle=spec.bg;c.fillRect(0,0,W,Hh);
 const t=TEMPLATES[spec.template]||TEMPLATES.title;
 for(const p of [...t.parts,FOOTER])(PARTS[p.type]||(()=>{}))(c,p,spec,W,Hh)}

let slide={...DEFAULT};
function outSize(){return[status.out_w||1920,status.out_h||1080]}
function readEditor(){slide={v:1,template:slide.template,title:$('#s-title').value,body:$('#s-body').value,
 footer:$('#s-footer').value,bg:$('#s-bg').value,fg:$('#s-fg').value}}
function fillEditor(){$('#s-title').value=slide.title||'';$('#s-body').value=slide.body||'';$('#s-footer').value=slide.footer||'';
 $('#s-bg').value=slide.bg||DEFAULT.bg;$('#s-fg').value=slide.fg||DEFAULT.fg;drawTemplates()}
function drawTemplates(){const t=TEMPLATES[slide.template]||TEMPLATES.title;$('#s-body-label').textContent=t.body;
 document.querySelectorAll('[data-template]').forEach(b=>b.setAttribute('aria-pressed',b.dataset.template===slide.template))}
let drawn='';
function preview(){const[W,Hh]=outSize();drawn=W+'x'+Hh;renderSlide(slide,$('#preview'),W,Hh);note()}
function note(){const[W,Hh]=outSize();const n=status.has_slide&&(status.slide_w!==W||status.slide_h!==Hh);
 $('#s-note').textContent=n?'The projector resolution has changed since the slide was saved; save it again to fit.':''}
for(const[k,t]of Object.entries(TEMPLATES)){const b=document.createElement('button');b.className='choice';b.dataset.template=k;b.textContent=t.label;
 b.onclick=()=>{readEditor();slide.template=k;drawTemplates();preview()};$('#templates').appendChild(b)}
['#s-title','#s-body','#s-footer','#s-bg','#s-fg'].forEach(id=>$(id).addEventListener('input',()=>{readEditor();preview()}));

async function saveSlide(andShow){readEditor();const[W,Hh]=outSize();const cv=document.createElement('canvas');renderSlide(slide,cv,W,Hh);
 const ok=await post('/api/slide',{spec:JSON.stringify(slide),png:cv.toDataURL('image/png')},'Slide saved.');
 if(ok&&andShow)await post('/api/settings',{show:'slide'},'Slide on the projector.')}
async function clearSlide(){if(!confirm('Remove the saved slide?'))return;
 busy=true;try{const r=await fetch('/api/slide',{method:'DELETE',headers:H});const s=await r.json();if(!r.ok)throw new Error(s.error||r.status);
 $('#msg').textContent='Slide removed.';render(s)}catch(e){$('#msg').textContent='Not removed: '+e.message}finally{busy=false}}
$('#s-save').onclick=()=>saveSlide(false);$('#s-show').onclick=()=>saveSlide(true);$('#s-clear').onclick=clearSlide;

// --- settings + status ---
function render(s){status=s;
 $('#in').textContent=s.input||'No input';
 $('#out').textContent=s.output||'No projector connected';
 document.querySelectorAll('[data-aspect]').forEach(b=>b.setAttribute('aria-pressed',b.dataset.aspect===s.aspect));
 document.querySelectorAll('[data-show]').forEach(b=>b.setAttribute('aria-pressed',b.dataset.show===s.show));
 const sel=$('#mode');if(!busy&&document.activeElement!==sel){sel.innerHTML='';for(const m of s.modes){const o=document.createElement('option');o.value=m.value;o.textContent=m.label;sel.appendChild(o)}sel.value=s.mode}
 $('#s-clear').disabled=!s.has_slide;
 if(s.warning)$('#msg').textContent=s.warning;
 const[W,Hh]=outSize();if(drawn!==W+'x'+Hh)preview();else note()}
async function load(){try{const r=await fetch('/api/status');render(await r.json())}catch(e){$('#msg').textContent='Lost contact with the box.'}}
async function post(url,fields,done){busy=true;$('#msg').textContent='Saving…';
 try{const r=await fetch(url,{method:'POST',headers:H,body:new URLSearchParams(fields)});
  const s=await r.json();if(!r.ok)throw new Error(s.error||r.status);$('#msg').textContent=s.warning||done||'Saved.';render(s);return true}
 catch(e){$('#msg').textContent='Not saved: '+e.message;return false}finally{busy=false}}
document.querySelectorAll('[data-aspect]').forEach(b=>b.onclick=()=>post('/api/settings',{aspect:b.dataset.aspect}));
document.querySelectorAll('[data-show]').forEach(b=>b.onclick=()=>post('/api/settings',{show:b.dataset.show}));
$('#mode').onchange=e=>post('/api/settings',{mode:e.target.value});

// --- diagnostics (R15): read on a phone at the venue, so plain words first ---
function ago(sec){sec=Math.max(0,Math.round(sec));if(sec<60)return sec+' s ago';if(sec<3600)return Math.floor(sec/60)+' min ago';
 return Math.floor(sec/3600)+' h '+Math.floor(sec%3600/60)+' min ago'}
function dur(sec){sec=Math.round(sec);const d=Math.floor(sec/86400),h=Math.floor(sec%86400/3600),m=Math.floor(sec%3600/60);
 return (d?d+' d ':'')+(d||h?h+' h ':'')+m+' min'}
function verdict(d){
 if(!d.display)return['No projector connected.','Check the HDMI/DVI cable from the box to the projector.'];
 if(d.backing_off)return['The input keeps dropping out.',
  'The box was restarted by the input '+d.recent_starts+' times in the last 30 s, so it shows the slide and tries the live picture every 5 s. This is the cable or splitter in front of the box, not a setting: try another cable, splitter output, or connect the laptop directly.'];
 if(!d.input)return['No input.','Nothing is arriving at the box. Check the cable into the box, that the laptop is awake and is sending to this screen (mirror or extend), and any splitter on the way.'];
 if(d.fps_in>0&&d.fps_in<d.input.hz*0.9)return['The input is live but missing frames.','Frames arrive at '+d.fps_in+' per second instead of '+d.input.hz+'. Usually a marginal cable or splitter.'];
 return['The input is steady.',d.resyncs?'The signal blinked '+d.resyncs+' times since the box started without the picture dropping; a cable or splitter is a little marginal.':'']}
function diagRows(d){const r=[['Projector shows',d.state]];
 r.push(['Input',d.input?d.input.width+'×'+d.input.height+' at '+d.input.hz+' Hz ('+d.input.pixel_clock_mhz+' MHz)':'none']);
 r.push(['Frames per second','in '+d.fps_in+', shown '+d.fps_shown]);
 r.push(['Scaler time',d.scaler_ms_avg+' ms average, '+d.scaler_ms_max+' ms worst']);
 r.push(['Input started',d.starts+' times ('+d.recent_starts+' in the last 30 s)']);
 r.push(['Input lost',d.losses+' times']);
 r.push(['Blips ridden through',String(d.resyncs)]);
 r.push(['Projector',d.display?((d.display.name||'unnamed')+(d.display.mode?', '+d.display.mode:'')):'none']);
 if(d.temp_c!==undefined)r.push(['Box temperature',d.temp_c+' °C']);
 if(d.uptime_s!==undefined)r.push(['Box on for',dur(d.uptime_s)]);
 return r}
function renderDiag(d){const[v,a]=verdict(d);$('#d-verdict').textContent=v;$('#d-advice').textContent=a;
 const rows=diagRows(d),tbl=$('#d-table');tbl.textContent='';
 for(const[k,val]of rows){const x=document.createElement('span'),y=document.createElement('span');x.textContent=k;y.textContent=val;tbl.append(x,y)}
 const ev=[...d.events].reverse(),ol=$('#d-events');ol.textContent='';
 for(const e of ev){const li=document.createElement('li'),t=document.createElement('time');t.textContent=ago(d.now-e.t);li.append(t,e.text);ol.appendChild(li)}
 if(!ev.length)ol.textContent='Nothing yet.';
 const rep=$('#d-report');if(document.activeElement!==rep)rep.value=['hdmi-transcode diagnostics',v,a,'',...rows.map(([k,x])=>k+': '+x),'',
  'Events (seconds since the box software started, now '+d.now+'):',...d.events.map(e=>e.t+'  '+e.text)].join('\n')}
let diagTimer=0;
async function loadDiag(){try{const r=await fetch('/api/diag');renderDiag(await r.json())}catch(e){$('#d-verdict').textContent='Lost contact with the box.'}}
function diagPolling(on){clearInterval(diagTimer);diagTimer=0;if(on){loadDiag();diagTimer=setInterval(loadDiag,2000)}}
// The clipboard API needs https, which the box doesn't have: fall back to selecting the
// text and the old copy command (iOS only selects an editable field, hence readOnly).
$('#d-copy').onclick=async()=>{const t=$('#d-report');
 try{await navigator.clipboard.writeText(t.value);$('#msg').textContent='Report copied.';return}catch(e){}
 t.readOnly=false;t.focus();t.select();t.setSelectionRange(0,t.value.length);
 let ok=false;try{ok=document.execCommand('copy')}catch(e){}
 t.readOnly=true;$('#msg').textContent=ok?'Report copied.':'Report selected: tap Copy.'};

// --- tabs: the choice lives in the URL fragment, so reload and back keep it ---
function tab(){const want=location.hash.slice(1);const name=['setup','slide','diag'].includes(want)?want:'setup';
 document.querySelectorAll('[data-tab]').forEach(d=>d.hidden=d.dataset.tab!==name);
 document.querySelectorAll('[data-go]').forEach(b=>b.setAttribute('aria-selected',b.dataset.go===name));
 diagPolling(name==='diag')}
document.querySelectorAll('[data-go]').forEach(b=>b.onclick=()=>{location.hash=b.dataset.go});
window.addEventListener('hashchange',tab);tab();

(async()=>{try{const r=await fetch('/api/slide');const s=await r.json();if(s&&typeof s==='object')slide={...DEFAULT,...s}}catch(e){}
 fillEditor();await load();setInterval(load,3000)})();
</script></body></html>
)HTML";

}  // namespace ht
