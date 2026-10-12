// The configuration page (R9, R12): one self-contained HTML document, no external
// resources (the hotspot has no internet). Plain words, big touch targets; nothing that
// requires understanding video signalling.
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
select{width:100%;padding:10px;font:inherit;border-radius:10px;border:1px solid var(--line);background:var(--card);color:var(--fg)}
label.row{display:flex;align-items:center;gap:10px;padding:6px 0}input[type=checkbox]{width:22px;height:22px}
#msg{min-height:1.4em;color:var(--muted);font-size:14px}
</style></head><body><main>
<h1>hdmi-transcode</h1>
<section><h2>Now</h2><div class="status">
<span>Input</span><span id="in">…</span><span>Projector</span><span id="out">…</span></div></section>
<section><h2>Picture shape</h2>
<button class="choice" data-aspect="compress">Fill the screen (stretch)<small>Nothing cut off; the picture is squeezed to fit.</small></button>
<button class="choice" data-aspect="letterbox">Show the whole picture<small>Correct shape, black bars on the edges.</small></button>
<button class="choice" data-aspect="crop">Fill the screen (trim edges)<small>Correct shape; the sides are cut off.</small></button>
</section>
<section><h2>Projector resolution</h2><select id="mode"></select></section>
<section><h2>Test pattern</h2><label class="row"><input type="checkbox" id="tp"> Show the test pattern instead of the input</label></section>
<div id="msg" role="status"></div>
</main><script>
const $=s=>document.querySelector(s);let busy=false;
function render(s){
 $('#in').textContent=s.input||'No input';
 $('#out').textContent=s.output||'No projector connected';
 document.querySelectorAll('[data-aspect]').forEach(b=>b.setAttribute('aria-pressed',b.dataset.aspect===s.aspect));
 const sel=$('#mode');if(!busy&&document.activeElement!==sel){sel.innerHTML='';for(const m of s.modes){const o=document.createElement('option');o.value=m.value;o.textContent=m.label;sel.appendChild(o)}sel.value=s.mode}
 if(document.activeElement!==$('#tp'))$('#tp').checked=s.test_pattern;
 if(s.warning)$('#msg').textContent=s.warning;
}
async function load(){try{const r=await fetch('/api/status');render(await r.json())}catch(e){$('#msg').textContent='Lost contact with the box.'}}
async function save(fields){busy=true;$('#msg').textContent='Saving…';
 try{const r=await fetch('/api/settings',{method:'POST',headers:{'Content-Type':'application/x-www-form-urlencoded','X-HT-Request':'1'},body:new URLSearchParams(fields)});
  const s=await r.json();if(!r.ok)throw new Error(s.error||r.status);$('#msg').textContent=s.warning||'Saved.';render(s)}
 catch(e){$('#msg').textContent='Not saved: '+e.message}finally{busy=false}}
document.querySelectorAll('[data-aspect]').forEach(b=>b.onclick=()=>save({aspect:b.dataset.aspect}));
$('#mode').onchange=e=>save({mode:e.target.value});
$('#tp').onchange=e=>save({test_pattern:e.target.checked?'on':'off'});
load();setInterval(load,3000);
</script></body></html>
)HTML";

}  // namespace ht
