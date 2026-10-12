// The configuration page (R9, R12, R13): one self-contained HTML document, no external
// resources (the hotspot has no internet). Plain words, big touch targets; nothing that
// requires understanding video signalling.
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
<section><h2>Picture shape</h2>
<button class="choice" data-aspect="compress">Fill the screen (stretch)<small>Nothing cut off; the picture is squeezed to fit.</small></button>
<button class="choice" data-aspect="letterbox">Show the whole picture<small>Correct shape, black bars on the edges.</small></button>
<button class="choice" data-aspect="crop">Fill the screen (trim edges)<small>Correct shape; the sides are cut off.</small></button>
</section>
<section><h2>Projector resolution</h2><select id="mode"></select></section>
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

(async()=>{try{const r=await fetch('/api/slide');const s=await r.json();if(s&&typeof s==='object')slide={...DEFAULT,...s}}catch(e){}
 fillEditor();await load();setInterval(load,3000)})();
</script></body></html>
)HTML";

}  // namespace ht
