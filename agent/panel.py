# -*- coding: utf-8 -*-
"""Панель программы-компаньона (открывается в окне программы или в окне Edge)."""

PAGE = r"""<!doctype html>
<html lang="ru"><head><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1">
<title>PC Monitor</title><style>
:root{--bg:#0d0f13;--side:#111419;--card:#171a20;--card2:#1e222a;--line:#2a2f39;--text:#f2f4f7;--mut:#99a2b0;--acc:#ff9f1c;--acc2:#4cc9f0;--ok:#46e06a;--bad:#ff5a4f}
*{box-sizing:border-box}html,body{margin:0;height:100%}
body{background:var(--bg);color:var(--text);font:14px/1.45 "Segoe UI Variable Text","Segoe UI",system-ui,sans-serif;display:flex;overflow:hidden}
nav{width:200px;flex:none;background:var(--side);border-right:1px solid var(--line);display:flex;flex-direction:column;padding:18px 12px;gap:4px}
.logo{display:flex;align-items:center;gap:10px;font-weight:700;font-size:16px;margin:0 6px 18px}
.logo svg{flex:none}
nav button{all:unset;display:flex;align-items:center;gap:10px;padding:9px 12px;border-radius:9px;color:var(--mut);cursor:pointer;font-weight:600}
nav button:hover{background:var(--card);color:var(--text)}
nav button[aria-current=page]{background:var(--card2);color:var(--text);box-shadow:inset 3px 0 0 var(--acc)}
nav button:focus-visible{outline:2px solid var(--acc)}
nav .grow{flex:1}
.link{display:flex;align-items:center;gap:8px;color:var(--mut);font-size:12px;padding:10px 8px;border-top:1px solid var(--line)}
.dot{width:9px;height:9px;border-radius:50%;background:var(--bad);flex:none}.dot.on{background:var(--ok)}
main{flex:1;overflow:auto;padding:26px 30px}
section{display:none;max-width:880px}section.on{display:block}
h1{font-size:22px;margin:0 0 4px;font-weight:700}
.sub{color:var(--mut);margin:0 0 20px}
h2{font-size:12px;margin:22px 0 10px;font-weight:700;color:var(--mut);text-transform:uppercase;letter-spacing:.08em}
.grid{display:grid;grid-template-columns:repeat(auto-fill,minmax(160px,1fr));gap:12px}
.card{background:var(--card);border:1px solid var(--line);border-radius:14px;padding:16px}
.k{color:var(--mut);font-size:11px;text-transform:uppercase;letter-spacing:.08em}
.v{font-size:28px;font-weight:700;margin:4px 0 2px;font-variant-numeric:tabular-nums}
.s{color:var(--mut);font-size:12px;min-height:17px}
.bar{height:5px;background:var(--card2);border-radius:99px;overflow:hidden;margin-top:10px}.bar>i{display:block;height:100%;width:0;background:var(--acc);border-radius:99px;transition:width .4s}
.skins{display:grid;grid-template-columns:repeat(auto-fill,minmax(176px,1fr));gap:14px}
.skin{all:unset;cursor:pointer;border-radius:12px;overflow:hidden;border:2px solid var(--line);background:var(--card)}
.skin img{display:block;width:100%;aspect-ratio:4/3;object-fit:cover}
.skin span{display:block;padding:8px 10px;font-weight:600}
.skin[aria-pressed=true]{border-color:var(--acc);box-shadow:0 0 0 3px #ff9f1c33}
.skin:focus-visible{outline:2px solid var(--acc)}
.row{display:flex;align-items:center;gap:10px;margin-top:12px;flex-wrap:wrap}
input[type=text],select{background:var(--card2);border:1px solid var(--line);border-radius:9px;color:var(--text);font:inherit;padding:8px 10px;min-width:0}
input[type=range]{flex:1;min-width:140px;accent-color:var(--acc)}
input[type=checkbox]{width:18px;height:18px;accent-color:var(--acc)}
.b{padding:8px 12px;border-radius:9px;border:1px solid var(--line);background:var(--card2);color:var(--text);font:inherit;font-weight:600;cursor:pointer;white-space:nowrap}
.b:hover{border-color:#3a414e}.b.p{background:var(--acc);border-color:var(--acc);color:#1a1205}
.b:disabled{opacity:.5;cursor:default}
.b:focus-visible,input:focus-visible,select:focus-visible{outline:2px solid var(--acc);outline-offset:2px}
.seg{display:inline-flex;border:1px solid var(--line);border-radius:10px;overflow:hidden}
.seg button{all:unset;padding:7px 16px;cursor:pointer;font-weight:600;color:var(--mut)}
.seg button[aria-pressed=true]{background:var(--acc);color:#1a1205}
.slots{display:flex;flex-direction:column;gap:10px}
.slot{display:grid;grid-template-columns:56px 140px 120px minmax(160px,1fr) auto;gap:10px;align-items:center;background:var(--card);border:1px solid var(--line);border-radius:12px;padding:10px}
.slot img{width:48px;height:48px;border-radius:10px;background:var(--card2);object-fit:contain}
.slot.off{opacity:.45}
.note{color:var(--mut);font-size:12px;margin-top:10px}
.shows{display:flex;gap:8px;flex-wrap:wrap}
dialog{background:var(--card);color:var(--text);border:1px solid var(--line);border-radius:14px;padding:18px;width:min(560px,90vw)}
dialog::backdrop{background:#0009}
.list{max-height:360px;overflow:auto;margin-top:10px;display:flex;flex-direction:column;gap:2px}
.list button{all:unset;padding:8px 10px;border-radius:8px;cursor:pointer}
.list button:hover,.list button:focus-visible{background:var(--card2)}
.toast{position:fixed;right:24px;bottom:24px;background:var(--card2);border:1px solid var(--line);border-radius:10px;padding:10px 16px;opacity:0;transition:opacity .25s;pointer-events:none}
.toast.on{opacity:1}
</style></head><body>
<nav aria-label="Разделы">
<div class="logo"><svg width="28" height="28" viewBox="0 0 64 64" aria-hidden="true"><rect x="2" y="2" width="60" height="60" rx="14" fill="#0e1014" stroke="#2a2f39"/><path d="M18 44 A18 18 0 1 1 46 44" fill="none" stroke="#3a3f47" stroke-width="6" stroke-linecap="round"/><path d="M18 44 A18 18 0 0 1 44 18" fill="none" stroke="#ff9f1c" stroke-width="6" stroke-linecap="round"/><circle cx="32" cy="32" r="5" fill="#ff9f1c"/></svg>PC Monitor</div>
<button data-p="ov" aria-current="page">Обзор</button>
<button data-p="scr">Экран</button>
<button data-p="apps">Быстрый запуск</button>
<button data-p="wx">Погода</button>
<button data-p="pc">Компьютер</button>
<button data-p="set">Настройки</button>
<div class="grow"></div>
<div class="link"><span class="dot" id="dot"></span><span id="link">ищу экран…</span></div>
</nav>
<main>
<section id="ov" class="on"><h1>Обзор</h1><p class="sub" id="host">—</p>
<div class="grid">
<div class="card"><div class="k">Процессор</div><div class="v" id="cpu">—</div><div class="s" id="cpus"></div><div class="bar"><i id="cpub"></i></div></div>
<div class="card"><div class="k">Видеокарта</div><div class="v" id="gpu">—</div><div class="s" id="gpus"></div><div class="bar"><i id="gpub"></i></div></div>
<div class="card"><div class="k">Память</div><div class="v" id="ram">—</div><div class="s" id="rams"></div><div class="bar"><i id="ramb"></i></div></div>
<div class="card"><div class="k">Диск</div><div class="v" id="disk">—</div><div class="s" id="disks"></div><div class="bar"><i id="diskb"></i></div></div>
<div class="card"><div class="k">Сеть</div><div class="v" id="net">—</div><div class="s" id="nets"></div></div>
<div class="card" style="grid-column:span 2;display:flex;gap:14px;align-items:center"><img id="cov" alt="" style="width:72px;height:72px;border-radius:10px;object-fit:cover;background:var(--card2);flex:none"><div style="min-width:0"><div class="k" id="mapp">Сейчас играет</div><div class="v" id="med" style="font-size:18px;white-space:nowrap;overflow:hidden;text-overflow:ellipsis">—</div><div class="s" id="meds"></div></div></div>
</div>
<h2>Экран</h2><div class="card"><div id="scrinfo" class="s">—</div></div>
</section>

<section id="scr"><h1>Экран</h1><p class="sub">Скин главного экрана. Свайп влево-вправо по экрану тоже меняет скин.</p>
<div class="skins" id="skins"></div>
<h2>Показать на экране</h2>
<div class="shows">
<button class="b" data-show="fc">Прогноз</button><button class="b" data-show="media">Медиапанель</button>
<button class="b" data-show="launch">Быстрый запуск</button><button class="b" data-show="night">Последние 12 часов</button>
<button class="b" data-show="idle">Заставку</button><button class="b" data-show="home">Главный экран</button></div>
<div class="row"><input type="checkbox" id="flip"><label for="flip">Перевернуть экран на 180°</label></div>
<p class="note">На экране: свайп вверх — медиапанель, вниз — быстрый запуск, касание погоды — прогноз, касание часов — последние 12 часов.</p>
</section>

<section id="apps"><h1>Быстрый запуск</h1><p class="sub">Плитки, которые открываются свайпом вниз по экрану. Касание плитки запускает программу на этом ПК.</p>
<div class="row" style="margin-top:0"><span>Плиток на экране:</span><div class="seg" id="cnt"><button data-n="4">4</button><button data-n="5">5</button><button data-n="6">6</button></div></div>
<h2>Плитки</h2><div class="slots" id="slots"></div>
<div class="row"><button class="b p" id="save">Сохранить и отправить на экран</button><span class="s" id="savenote"></span></div>
<p class="note">Выключение и перезагрузка на экране срабатывают только после второго касания. Иконки берутся из программ автоматически; свою картинку (.png, .ico) можно указать кнопкой «Иконка…».</p>
</section>

<section id="wx"><h1>Погода</h1><p class="sub">Прогноз берётся с Gismeteo, запасной источник — Погода Mail.ru.</p>
<div class="card"><div class="row" style="margin-top:0"><label for="city">Город</label><input type="text" id="city" list="citylist" placeholder="Город России" autocomplete="off" style="width:240px"><datalist id="citylist"></datalist><button class="b p" id="cityb">Выбрать</button></div>
<div class="note" id="wxnote">—</div></div>
</section>

<section id="pc"><h1>Компьютер</h1><p class="sub">Звук и проверка интернета.</p>
<div class="card"><div class="row" style="margin-top:0"><label for="vol">Громкость</label><input type="range" id="vol" min="0" max="100"><span id="volv" style="width:44px;text-align:right">—</span><button class="b" id="mute">Звук</button></div>
<div class="row"><button class="b" id="prev">⏮</button><button class="b" id="play">⏯</button><button class="b" id="next">⏭</button><span class="s">медиаклавиши</span></div></div>
<h2>Скорость интернета</h2>
<div class="card"><div class="row" style="margin-top:0"><button class="b p" id="test">Проверить скорость</button><span id="speed" class="s"></span></div><div class="bar"><i id="spb"></i></div></div>
</section>

<section id="set"><h1>Настройки</h1><p class="sub" id="ver">—</p>
<div class="card"><div class="row" style="margin-top:0"><input type="checkbox" id="auto"><label for="auto">Запускать вместе с Windows</label></div>
<div class="note" id="appnote"></div></div>
<h2>Сенсор экрана</h2><div class="card"><div class="row" style="margin-top:0"><button class="b p" data-show="cal">Откалибровать сенсор</button><button class="b" data-show="calreset">Сбросить калибровку</button></div>
<div class="note">На экране по очереди появятся 4 крестика — коснитесь центра каждого (лучше стилусом или ногтем). Поправки сохранятся в плате.</div></div>
<h2>Температура процессора</h2><div class="card"><div class="s" id="hw">—</div>
<div class="note">Её даёт HWiNFO (значок температуры в трее или «Shared Memory Support» в настройках) либо LibreHardwareMonitor.</div></div>
</section>
</main>

<dialog id="dlg"><div class="row" style="margin-top:0"><input type="text" id="q" placeholder="Поиск по меню «Пуск»" style="flex:1"><button class="b" id="dlgx">Закрыть</button></div><div class="list" id="dlgl"></div></dialog>
<div class="toast" id="toast" role="status"></div>

<script>
const $=id=>document.getElementById(id);
async function j(u,o){const r=await fetch(u,o);return r.json()}
function post(u,b){return j(u,{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify(b)})}
function toast(t){const e=$('toast');e.textContent=t;e.classList.add('on');clearTimeout(e._t);e._t=setTimeout(()=>e.classList.remove('on'),2200)}
document.querySelectorAll('nav button[data-p]').forEach(b=>b.onclick=()=>{
 document.querySelectorAll('nav button[data-p]').forEach(x=>x.removeAttribute('aria-current'));b.setAttribute('aria-current','page');
 document.querySelectorAll('section').forEach(s=>s.classList.toggle('on',s.id===b.dataset.p));if(b.dataset.p==='apps')loadApps()});

// ---- обзор
function pct(id,v,on){$(id).textContent=on?v.toFixed(0)+'%':'—';const b=$(id+'b');if(b)b.style.width=(on?v:0)+'%'}
let volDrag=false,lastCover=-1;
async function refresh(){try{const d=await j('/api/stats');
$('host').textContent=`${d.host} · ${d.os}`;
pct('cpu',d.cpu.load,true);$('cpus').textContent=(d.cpu.temp>0?d.cpu.temp.toFixed(0)+'°C · ':'')+d.cpu.freq.toFixed(1)+' ГГц';
pct('gpu',d.gpu.load,d.gpu.present);$('gpus').textContent=d.gpu.present?`${d.gpu.temp>0?d.gpu.temp.toFixed(0)+'°C · ':''}VRAM ${d.gpu.vram.toFixed(0)}%`:'нет видеокарты';
pct('ram',d.ram.percent,true);$('rams').textContent=`${d.ram.used.toFixed(1)} из ${d.ram.total.toFixed(0)} ГБ`;
pct('disk',d.disk.percent,true);$('disks').textContent=`${d.disk.used.toFixed(0)} из ${d.disk.total.toFixed(0)} ГБ`;
$('net').textContent=`↓ ${d.net.down.toFixed(1)}`;$('nets').textContent=`↑ ${d.net.up.toFixed(1)} Мбит/с · пинг ${d.ping||'—'} мс`;
const m=d.media||{};$('mapp').textContent=m.app?`${m.app} · ${m.playing?'играет':'пауза'}`:'Сейчас играет';$('med').textContent=m.title||(m.app?'':'тишина');$('meds').textContent=m.artist||'';
if((m.cover||0)!==lastCover){lastCover=m.cover||0;if(lastCover)j('/api/media/cover').then(c=>{$('cov').src=c.img||''}).catch(()=>{});else $('cov').removeAttribute('src')}
if(!volDrag){$('vol').value=d.volumeLevel;$('volv').textContent=d.volumeAvailable?d.volumeLevel+'%':'—'}
$('mute').textContent=d.muted?'Включить звук':'Выключить звук';
const s=d.speed;$('spb').style.width=(s.testing?s.progress*100:0)+'%';
$('speed').textContent=s.testing?s.stage:(s.error||(s.down?`↓ ${s.down.toFixed(1)} · ↑ ${s.up.toFixed(1)} Мбит/с · ${s.ping.toFixed(0)} мс`:''));
}catch(e){$('host').textContent='агент не отвечает'}}

// ---- экран
const NAMES=["Приборы","Неон","Минимализм","Ретро-радио","Часы","Терминал","Плитки","Лампы","Импульс","Шина","HUD","Сканлайн","Звук"];
j('/api/previews').then(r=>{NAMES.forEach((n,i)=>{const b=document.createElement('button');b.className='skin';b.type='button';b.setAttribute('aria-pressed','false');
const img=document.createElement('img');img.alt='';img.src=(r.previews||[])[i]||'';const sp=document.createElement('span');sp.textContent=n;b.append(img,sp);
b.onclick=async()=>{mark(i);show(await post('/api/screen/set',{skin:i}))};$('skins').appendChild(b)});refreshScreen()}).catch(()=>{});
function mark(i){[...$('skins').children].forEach((b,k)=>b.setAttribute('aria-pressed',k===i?'true':'false'))}
function show(s){if(!s||!s.ok)return;const on=s.link!=='none';$('dot').className='dot'+(on?' on':'');
$('link').textContent=s.link==='usb'?`по USB · ${s.fw||'?'}`:s.link==='wifi'?`по Wi-Fi · ${s.ip}`:'экран не найден';
$('scrinfo').textContent=s.link==='usb'?`Экран подключён по USB (${s.port}), прошивка ${s.fw||'?'}${s.ip?', в Wi-Fi: '+s.ip:''}`:s.link==='wifi'?`Экран на связи по Wi-Fi (${s.ip})`:'Экран не найден — проверьте кабель USB.';
if(s.skin!=null)mark(s.skin);if(s.flip!=null)$('flip').checked=!!s.flip;if(s.city!=null&&!cityEdit)$('city').value=s.city}
async function refreshScreen(){try{show(await j('/api/screen/state'))}catch(e){}}
$('flip').onchange=async e=>show(await post('/api/screen/set',{flip:e.target.checked?1:0}));
document.querySelectorAll('[data-show]').forEach(b=>b.onclick=async()=>{const r=await post('/api/screen/set',{show:b.dataset.show});toast(r&&r.ok?'Отправлено на экран':'Экран не на связи')});

// ---- быстрый запуск
let A=null;const KIND={app:'Программа',folder:'Папка',url:'Сайт',action:'Действие'};
async function loadApps(){try{A=await j('/api/apps');drawApps()}catch(e){}}
function drawApps(){if(!A)return;[...$('cnt').children].forEach(b=>b.setAttribute('aria-pressed',+b.dataset.n===A.count?'true':'false'));
const box=$('slots');box.innerHTML='';A.items.forEach((it,i)=>{const r=document.createElement('div');r.className='slot'+(i>=A.count?' off':'');
const img=document.createElement('img');img.alt='';img.src=A.icons[i]||'';
const nm=document.createElement('input');nm.type='text';nm.value=it.name;nm.maxLength=18;nm.setAttribute('aria-label','Название плитки '+(i+1));nm.oninput=()=>it.name=nm.value;
const kd=document.createElement('select');kd.setAttribute('aria-label','Тип');Object.entries(KIND).forEach(([k,v])=>{const o=document.createElement('option');o.value=k;o.textContent=v;kd.appendChild(o)});kd.value=it.kind;
const tgWrap=document.createElement('div');tgWrap.className='row';tgWrap.style.margin='0';tgWrap.style.flexWrap='nowrap';
const btns=document.createElement('div');btns.className='row';btns.style.margin='0';btns.style.flexWrap='nowrap';btns.style.gap='6px';
function drawTarget(){tgWrap.innerHTML='';btns.innerHTML='';
 if(it.kind==='action'){const s=document.createElement('select');s.setAttribute('aria-label','Действие');Object.entries(A.actions).forEach(([k,v])=>{const o=document.createElement('option');o.value=k;o.textContent=v;s.appendChild(o)});
  if(!A.actions[it.target])it.target='lock';s.value=it.target;s.onchange=()=>{it.target=s.value;if(!it.name||Object.values(A.actions).includes(it.name))it.name=nm.value=A.actions[s.value];preview(i,img)};tgWrap.appendChild(s);return}
 const t=document.createElement('input');t.type='text';t.value=it.target;t.style.flex='1';t.placeholder=it.kind==='url'?'https://…':it.kind==='folder'?'C:\\Users\\…':'путь к программе или ярлыку';t.setAttribute('aria-label','Что запускать');
 t.onchange=()=>{it.target=t.value.trim();preview(i,img)};tgWrap.appendChild(t);
 if(it.kind==='app'){const s=document.createElement('button');s.className='b';s.textContent='Пуск…';s.title='Выбрать из меню «Пуск»';s.onclick=()=>pickStart(i,img,t,nm);btns.appendChild(s)}
 if(it.kind!=='url'){const o=document.createElement('button');o.className='b';o.textContent='Обзор…';o.onclick=async()=>{const r=await post('/api/apps/pick',{});if(r.path){it.target=t.value=r.path;if(!nm.value)nm.value=it.name=r.path.split(/[\\\/]/).pop().replace(/\.(exe|lnk)$/i,'');preview(i,img)}};btns.appendChild(o)}
 const ic=document.createElement('button');ic.className='b';ic.textContent='Иконка…';ic.onclick=async()=>{const r=await post('/api/apps/pick',{});if(r.path){it.icon=r.path;preview(i,img)}};btns.appendChild(ic)}
kd.onchange=()=>{it.kind=kd.value;it.target='';it.icon='';drawTarget();preview(i,img)};drawTarget();
r.append(img,nm,kd,tgWrap,btns);box.appendChild(r)})}
async function preview(i,img){try{const r=await post('/api/apps/preview',A.items[i]);if(r.icon)img.src=r.icon}catch(e){}}
$('cnt').onclick=e=>{const n=+e.target.dataset.n;if(n&&A){A.count=n;drawApps()}};
$('save').onclick=async()=>{const r=await post('/api/apps',{count:A.count,items:A.items});if(r.ok){A={...A,...r};$('savenote').textContent='Сохранено — экран получит плитки за пару секунд';loadApps()}else $('savenote').textContent=r.error||'ошибка'};
let START=null,pickCb=null;
async function pickStart(i,img,t,nm){$('dlg').showModal();$('q').value='';$('q').focus();if(!START){$('dlgl').textContent='загрузка…';START=(await j('/api/apps/start')).apps||[]}
pickCb=a=>{A.items[i].target=t.value=a.target;A.items[i].name=nm.value=a.name.slice(0,18);A.items[i].icon='';preview(i,img);$('dlg').close()};drawList()}
function drawList(){const q=$('q').value.toLowerCase();const l=$('dlgl');l.innerHTML='';(START||[]).filter(a=>a.name.toLowerCase().includes(q)).slice(0,200).forEach(a=>{const b=document.createElement('button');b.textContent=a.name;b.onclick=()=>pickCb(a);l.appendChild(b)})}
$('q').oninput=drawList;$('dlgx').onclick=()=>$('dlg').close();

// ---- погода
let cityEdit=false;const cityMap=new Map();
function fillCities(list){const dl=$('citylist');dl.innerHTML='';list.forEach(c=>{cityMap.set(c[0].toLowerCase(),{city:c[0],gm:c[2],mail:c[3]});const o=document.createElement('option');o.value=c[0];o.label=c[1];dl.appendChild(o)})}
let cityTimer=null;
$('city').oninput=e=>{cityEdit=true;const q=e.target.value.trim();clearTimeout(cityTimer);if(q.length<2)return;
cityTimer=setTimeout(async()=>{try{const r=await j('/api/weather/cities?q='+encodeURIComponent(q));if(r&&r.cities&&r.cities.length)fillCities(r.cities.map(c=>[c.city,c.region,c.gm,c.mail]))}catch(err){}},250)};
$('cityb').onclick=async()=>{const name=$('city').value.trim();if(!name)return;cityEdit=false;const hit=cityMap.get(name.toLowerCase());
const res=await post('/api/screen/set',hit||{city:name});show(res);toast('Город погоды: '+(res.city||name))};

// ---- компьютер
$('vol').addEventListener('pointerdown',()=>volDrag=true);
$('vol').addEventListener('input',e=>$('volv').textContent=e.target.value+'%');
$('vol').addEventListener('change',async e=>{volDrag=false;await post('/api/volume',{level:+e.target.value})});
$('mute').onclick=async()=>{await post('/api/volume',{toggle:true});refresh()};
['prev','play','next'].forEach(k=>$(k).onclick=()=>post('/api/media',{key:k}));
$('test').onclick=async()=>{await post('/api/speedtest',{});refresh()};

// ---- настройки
$('auto').onchange=async e=>app(await post('/api/app',{autostart:e.target.checked}));
function app(a){if(!a)return;$('auto').checked=!!a.autostart;$('auto').disabled=!a.available;
$('appnote').textContent=a.available?'Программа живёт в трее: закрытие окна её не останавливает.':'Автозапуск доступен, когда запущена сама программа PC Monitor.'}
async function health(){try{const h=await j('/api/health');$('ver').textContent=`Агент ${h.version}`+(h.usb&&h.usb.hello&&h.usb.hello.fw?` · прошивка экрана ${h.usb.hello.fw}`:'');
$('hw').textContent=h.cpuTemp>0?`Сейчас ${h.cpuTemp.toFixed(0)}°C — температура читается.`:`Температуры нет. ${h.hwinfo||''}`;
$('wxnote').textContent=h.forecastHours?`Прогноз на ${h.forecastHours} ч · ${h.forecastSource}`:(h.forecastError||'прогноз загружается…')}catch(e){}}
refresh();j('/api/app').then(app).catch(()=>{});health();
setInterval(refresh,1000);setInterval(refreshScreen,2000);setInterval(health,10000);
</script></body></html>"""
