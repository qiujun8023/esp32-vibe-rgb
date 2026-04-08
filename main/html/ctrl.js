// ── 效果元数据（与 effects.c EFFECT_INFO 对齐）────────
const EFFECTS = [
  {id:0,  name:"Spectrum",    c1:"色彩模式", c2:"峰值点",  c3:"镜像"   },
  {id:1,  name:"Waterfall",   c1:"饱和度",   c2:"消退速率",c3:""       },
  {id:2,  name:"Gravimeter",  c1:"保持时间", c2:"峰值亮度",c3:""       },
  {id:3,  name:"Funky Plank", c1:"下落速度", c2:"间距",    c3:""       },
  {id:4,  name:"Scroll",      c1:"滚动方向", c2:"",        c3:""       },
  {id:5,  name:"CenterBars",  c1:"色彩模式", c2:"",        c3:""       },
  {id:6,  name:"GravFreq",    c1:"重力强度", c2:"灵敏度",  c3:""       },
  {id:7,  name:"Super Freq",  c1:"线条数量", c2:"",        c3:""       },
  {id:8,  name:"Ripple",      c1:"扩散速度", c2:"最大涟漪",c3:""       },
  {id:9,  name:"Juggles",     c1:"球数量",   c2:"消退速率",c3:""       },
  {id:10, name:"Blurz",       c1:"色块数量", c2:"模糊度",  c3:""       },
  {id:11, name:"DJ Light",    c1:"扫描速度", c2:"闪光时长",c3:""       },
  {id:12, name:"Ripplepeak",  c1:"扩散速度", c2:"触发阈值",c3:""       },
  {id:13, name:"Freqwave",    c1:"波动幅度", c2:"",        c3:""       },
  {id:14, name:"Freqmap",     c1:"饱和度",   c2:"",        c3:""       },
  {id:15, name:"Noisemove",   c1:"噪声缩放", c2:"亮度调制",c3:""       },
  {id:16, name:"Rocktaves",   c1:"八度数量", c2:"",        c3:""       },
  {id:17, name:"Energy",      c1:"响应速度", c2:"最低亮度",c3:"径向模式"},
  {id:18, name:"Plasma",      c1:"复杂度",   c2:"音频调制",c3:""       },
  {id:19, name:"Swirl",       c1:"旋转速度", c2:"音频调制",c3:""       },
  {id:20, name:"Waverly",     c1:"波动数量", c2:"振幅",    c3:""       },
  {id:21, name:"Fire",        c1:"冷却速率", c2:"点燃率",  c3:"音频调制"},
];
const PALETTES = ["Rainbow","Sunset","Ocean","Lava","Forest","Party","Heat","Mono"];

// ── 状态 ─────────────────────────────────────────────
var cfg = {};           // 当前设置（从服务端加载）
var ws;
var matW = 8, matH = 8; // 矩阵尺寸（从设置读取）

// ── 初始化 UI ─────────────────────────────────────────
(function init(){
  // 效果网格
  var grid = document.getElementById('fx-grid');
  EFFECTS.forEach(function(fx){
    var b = document.createElement('button');
    b.className = 'fx-btn'; b.id = 'fx-'+fx.id; b.textContent = fx.name;
    b.onclick = function(){ setEffect(fx.id); };
    grid.appendChild(b);
  });

  // 调色板网格
  var pg = document.getElementById('palette-grid');
  PALETTES.forEach(function(name, i){
    var b = document.createElement('button');
    b.className = 'fx-btn'; b.id = 'pal-'+i; b.textContent = name;
    b.onclick = function(){ setPalette(i); };
    pg.appendChild(b);
  });

  connect();
})();

// ── WebSocket ─────────────────────────────────────────
var wsReconnectTimer = null;
function connect(){
  if(ws){ try{ ws.close(); }catch(e){} }
  ws = new WebSocket('ws://'+location.host+'/ws');
  ws.onopen = function(){
    setSt('conn','⬤ 已连接','#3fb950');
    if(wsReconnectTimer){ clearTimeout(wsReconnectTimer); wsReconnectTimer = null; }
  };
  ws.onclose = function(){
    setSt('conn','⬤ 已断开','#f85149');
    if(!wsReconnectTimer) wsReconnectTimer = setTimeout(function(){
      wsReconnectTimer = null;
      connect();
    }, 2000);
  };
  ws.onerror = function(){
    setSt('conn','⬤ 连接错误','#f85149');
    try{ ws.close(); }catch(e){}
  };
  ws.onmessage = function(e){
    var d = JSON.parse(e.data);
    // 如果是设置包（包含 ssid 字段）→ 加载设置
    if(d.ssid !== undefined){ loadConfig(d); return; }
    // 否则是状态推送包
    if(d.fps !== undefined) setSt('fps', d.fps+' fps');
    if(d.volume !== undefined) setSt('vol', '音量 '+(d.volume*100|0)+'%');
    if(d.pixels !== undefined) renderMatrix(d.pixels);
    if(cfg.effect !== undefined) setSt('effect', EFFECTS[cfg.effect]?EFFECTS[cfg.effect].name:'-');
  };
}

// ── 矩阵预览 ──────────────────────────────────────────
var canvas = document.getElementById('matrix-canvas');
var ctx = canvas.getContext('2d');

function renderMatrix(hex){
  var n = hex.length / 6; // 像素数
  var cols = matW, rows = matH;
  var cw = canvas.width / cols, ch = canvas.height / rows;
  for(var i = 0; i < n && i < cols*rows; i++){
    var r = parseInt(hex.substr(i*6,   2), 16);
    var g = parseInt(hex.substr(i*6+2, 2), 16);
    var b = parseInt(hex.substr(i*6+4, 2), 16);
    var x = (i % cols) * cw;
    var y = Math.floor(i / cols) * ch;
    ctx.fillStyle = 'rgb('+r+','+g+','+b+')';
    ctx.fillRect(x, y, cw-1, ch-1);
  }
}

// ── 加载设置到 UI ─────────────────────────────────────
function loadConfig(d){
  cfg = d;
  matW = d.led_w || 8; matH = d.led_h || 8;

  // 调整 canvas
  var size = Math.min(160, matW * 20);
  canvas.width = size; canvas.height = size * matH / matW;

  // 填充所有控件
  function setVal(id, v){
    var el = document.getElementById(id);
    if(!el) return;
    if(el.type==='range'||el.tagName==='SELECT') el.value = v;
    else el.value = v;
    var sv = document.getElementById(id+'_v');
    if(sv) sv.textContent = v;
  }

  setVal('speed', d.speed); setVal('intensity', d.intensity);
  setVal('custom1', d.custom1); setVal('custom2', d.custom2); setVal('custom3', d.custom3);
  setVal('brightness', d.brightness);
  setVal('gain', d.gain|0); setVal('squelch', d.squelch); setVal('fft_smooth', d.fft_smooth);
  setVal('led_gpio', d.led_gpio); setVal('led_w', d.led_w); setVal('led_h', d.led_h);
  setVal('led_serpentine', d.led_serpentine); setVal('led_start', d.led_start);
  setVal('mic_sck', d.mic_sck); setVal('mic_ws', d.mic_ws); setVal('mic_din', d.mic_din);
  setVal('cfg_ssid', d.ssid); setVal('ip_mode', d.ip_mode);
  setVal('s_ip', d.s_ip); setVal('s_mask', d.s_mask); setVal('s_gw', d.s_gw);
  setVal('s_dns1', d.s_dns1); setVal('s_dns2', d.s_dns2);

  // AGC 按钮
  [0,1,2].forEach(function(i){
    var b = document.getElementById('agc'+i);
    if(b) b.className = 'btn'+(d.agc_mode===i?' active':'');
  });

  // 效果按钮
  EFFECTS.forEach(function(fx){
    var b = document.getElementById('fx-'+fx.id);
    if(b) b.className = 'fx-btn'+(d.effect===fx.id?' active':'');
  });
  updateEffectLabels(d.effect);

  // 调色板按钮
  PALETTES.forEach(function(_,i){
    var b = document.getElementById('pal-'+i);
    if(b) b.className = 'fx-btn'+(d.palette===i?' active':'');
  });

  // 频率方向
  document.getElementById('dir0').className = 'btn'+(d.freq_dir===0?' active':'');
  document.getElementById('dir1').className = 'btn'+(d.freq_dir===1?' active':'');

  toggleStaticFields();
  updateLedPreview();
}

function updateEffectLabels(effectId){
  var fx = EFFECTS[effectId] || {};
  function setLbl(rowId, lblId, txt){
    var row = document.getElementById(rowId);
    if(!row) return;
    row.style.display = txt?'':'none';
    var lbl = document.getElementById(lblId);
    if(lbl) lbl.textContent = txt||'';
  }
  setLbl('c1-row','c1-lbl', fx.c1); setLbl('c2-row','c2-lbl', fx.c2); setLbl('c3-row','c3-lbl', fx.c3);
}

// ── 控件回调 ─────────────────────────────────────────
function onSlider(key, el){
  var v = parseInt(el.value);
  var sv = document.getElementById(key+'_v');
  if(sv) sv.textContent = v;
  cfg[key] = v;
  sendWS(buildPartial(key, v));
}

function editVal(key, minVal, maxVal){
  var sv = document.getElementById(key+'_v');
  if(!sv || sv.querySelector('input')) return;
  var oldVal = parseInt(sv.textContent) || 0;
  minVal = minVal !== undefined ? minVal : 0;
  maxVal = maxVal !== undefined ? maxVal : 255;
  sv.innerHTML = '<input type="number" value="'+oldVal+'" min="'+minVal+'" max="'+maxVal+'">';
  var inp = sv.querySelector('input');
  inp.focus();
  inp.select();
  inp.onkeydown = function(e){
    if(e.key === 'Enter'){ inp.blur(); return false; }
    if(e.key === 'Escape'){ sv.textContent = oldVal; return false; }
  };
  inp.onblur = function(){
    var v = parseInt(inp.value) || oldVal;
    v = Math.max(minVal, Math.min(maxVal, v));
    sv.textContent = v;
    cfg[key] = v;
    document.getElementById(key).value = v;
    sendWS(buildPartial(key, v));
  };
}

function setEffect(id){
  cfg.effect = id;
  EFFECTS.forEach(function(fx){
    var b=document.getElementById('fx-'+fx.id);
    if(b) b.className='fx-btn'+(fx.id===id?' active':'');
  });
  updateEffectLabels(id);
  sendWS({effect:id});
}

function setPalette(i){
  cfg.palette = i;
  PALETTES.forEach(function(_,j){
    var b=document.getElementById('pal-'+j);
    if(b) b.className='fx-btn'+(j===i?' active':'');
  });
  sendWS({palette:i});
}

function setFreqDir(v){
  cfg.freq_dir = v;
  document.getElementById('dir0').className='btn'+(v===0?' active':'');
  document.getElementById('dir1').className='btn'+(v===1?' active':'');
  sendWS({freq_dir:v});
}

function setAGC(v){
  cfg.agc_mode = v;
  [0,1,2].forEach(function(i){
    document.getElementById('agc'+i).className='btn'+(i===v?' active':'');
  });
  sendWS({agc_mode:v});
}

function toggleStaticFields(){
  var v = document.getElementById('ip_mode').value;
  document.getElementById('static-ip-fields').className = v==='1'?'show':'';
}

// ── WebSocket 发送 ────────────────────────────────────
function buildPartial(key, val){
  var obj={};obj[key]=val;return obj;
}

function sendWS(obj){
  if(ws && ws.readyState===1) ws.send(JSON.stringify(obj));
}

// ── 保存设置（POST /api/settings）────────────────────
function saveSettings(){
  setSysMsg('保存中...', '#8b949e');
  var payload = {
    ssid: document.getElementById('cfg_ssid').value,
    pass_new: document.getElementById('pass_new').value,
    ip_mode: parseInt(document.getElementById('ip_mode').value),
    s_ip:   document.getElementById('s_ip').value,
    s_mask: document.getElementById('s_mask').value,
    s_gw:   document.getElementById('s_gw').value,
    s_dns1: document.getElementById('s_dns1').value,
    s_dns2: document.getElementById('s_dns2').value,
    led_gpio: parseInt(document.getElementById('led_gpio').value)||8,
    led_w: parseInt(document.getElementById('led_w').value)||8,
    led_h: parseInt(document.getElementById('led_h').value)||8,
    led_serpentine: parseInt(document.getElementById('led_serpentine').value),
    led_start: parseInt(document.getElementById('led_start').value),
    brightness: parseInt(document.getElementById('brightness').value),
    agc_mode: cfg.agc_mode||0,
    gain: parseFloat(document.getElementById('gain').value)||40,
    squelch: parseInt(document.getElementById('squelch').value),
    fft_smooth: parseInt(document.getElementById('fft_smooth').value),
    mic_sck: parseInt(document.getElementById('mic_sck').value)||4,
    mic_ws: parseInt(document.getElementById('mic_ws').value)||5,
    mic_din: parseInt(document.getElementById('mic_din').value)||6,
    effect: cfg.effect||0,
    palette: cfg.palette||0,
    speed: parseInt(document.getElementById('speed').value),
    intensity: parseInt(document.getElementById('intensity').value),
    custom1: parseInt(document.getElementById('custom1').value),
    custom2: parseInt(document.getElementById('custom2').value),
    custom3: parseInt(document.getElementById('custom3').value),
    freq_dir: cfg.freq_dir||0,
  };
  fetch('/api/settings',{method:'POST',
    headers:{'Content-Type':'application/json'},
    body:JSON.stringify(payload)})
  .then(r=>r.json()).then(d=>{
    if(d.ok){
      if(d.restart_required) setSysMsg('✓ 已保存，设备正在重启...','#3fb950');
      else setSysMsg('✓ 已保存','#3fb950');
    } else setSysMsg('✗ 保存失败','#f85149');
  }).catch(()=>setSysMsg('✓ 已保存，设备正在重启...','#3fb950'));
}

function resetWifi(){
  if(!confirm('确定重置 WiFi 配置？设备将重启进入配网模式。')) return;
  fetch('/api/reset_wifi',{method:'POST'}).catch(()=>{});
  setSysMsg('正在重启...','#8b949e');
}

function factoryReset(){
  if(!confirm('确定恢复出厂设置？所有配置将清除。')) return;
  fetch('/api/factory',{method:'POST'}).catch(()=>{});
  setSysMsg('正在恢复出厂...','#8b949e');
}

function doReboot(){
  if(!confirm('确定重启设备？')) return;
  fetch('/api/reboot',{method:'POST'}).catch(()=>{});
  setSysMsg('正在重启...','#8b949e');
}

// ── 工具 ─────────────────────────────────────────────
function switchTab(id, el){
  document.querySelectorAll('.tab-pane').forEach(function(p){ p.className='tab-pane'; });
  document.querySelectorAll('.tab').forEach(function(t){ t.className='tab'; });
  document.getElementById('tab-'+id).className='tab-pane active';
  el.className='tab active';
}

function setSt(id, txt, color){
  var el=document.getElementById('st-'+id);
  if(!el) return;
  el.textContent=txt;
  if(color) el.style.color=color;
}

function setSysMsg(txt, color){
  var el=document.getElementById('sys-msg');
  if(el){ el.textContent=txt; el.style.color=color; }
}

// ── LED 编号预览 ─────────────────────────────────────
function updateLedPreview(){
  var canvas = document.getElementById('led-preview');
  if(!canvas) return;
  var ctx = canvas.getContext('2d');
  var w = parseInt(document.getElementById('led_w').value) || 8;
  var h = parseInt(document.getElementById('led_h').value) || 8;
  var serpentine = parseInt(document.getElementById('led_serpentine').value);
  var start = parseInt(document.getElementById('led_start').value);

  var cols = Math.min(w, 16), rows = Math.min(h, 16);
  canvas.width = cols * 44 + 20;
  canvas.height = rows * 44 + 20;

  ctx.fillStyle = '#0d1117';
  ctx.fillRect(0, 0, canvas.width, canvas.height);
  ctx.font = 'bold 12px sans-serif';
  ctx.textAlign = 'center';
  ctx.textBaseline = 'middle';

  // 计算 LED 索引（与 led.c 的 matrix_idx 逻辑一致）
  // 坐标系：x=列，y=行，左下=(0,0)
  function ledIndex(x, y){
    // 先计算物理坐标（根据起始角翻转）
    var px = (start & 1) ? (cols - 1 - x) : x;  // bit0: 左/右
    var py = (start & 2) ? (rows - 1 - y) : y;  // bit1: 下/上
    // 蛇形走线：奇数行翻转
    if (serpentine && (py & 1)) px = (cols - 1) - px;
    return py * cols + px;
  }

  // 绘制网格和数字
  for(var y = 0; y < rows; y++){
    for(var x = 0; x < cols; x++){
      var idx = ledIndex(x, y);
      var px = x * 44 + 32;
      var py = (rows - 1 - y) * 44 + 32;  // Y轴翻转，让左下角是(0,0)
      
      // 背景圆
      ctx.fillStyle = '#21262d';
      ctx.beginPath(); ctx.arc(px, py, 18, 0, Math.PI*2); ctx.fill();
      
      // 数字
      ctx.fillStyle = '#58a6ff';
      ctx.fillText(idx, px, py);
    }
  }

  // 绘制数据流箭头（LED 0 → 1 → 2 ...）
  ctx.strokeStyle = '#f85149';
  ctx.lineWidth = 2;
  ctx.setLineDash([4, 2]);
  
  // 找到每个索引对应的屏幕坐标
  var posMap = {};
  for(var y = 0; y < rows; y++){
    for(var x = 0; x < cols; x++){
      var idx = ledIndex(x, y);
      posMap[idx] = {x: x, y: y};
    }
  }
  
  // 绘制相邻索引之间的连线
  for(var i = 0; i < cols * rows - 1; i++){
    var p1 = posMap[i], p2 = posMap[i + 1];
    if(!p1 || !p2) continue;
    var x1 = p1.x * 44 + 32, y1 = (rows - 1 - p1.y) * 44 + 32;
    var x2 = p2.x * 44 + 32, y2 = (rows - 1 - p2.y) * 44 + 32;
    
    // 计算方向并缩短线段
    var dx = x2 - x1, dy = y2 - y1;
    var dist = Math.sqrt(dx*dx + dy*dy);
    if(dist > 0){
      var nx = dx / dist, ny = dy / dist;
      ctx.beginPath();
      ctx.moveTo(x1 + nx * 18, y1 + ny * 18);
      ctx.lineTo(x2 - nx * 18, y2 - ny * 18);
      ctx.stroke();
    }
  }
  ctx.setLineDash([]);

  // 标注起始角
  ctx.fillStyle = '#3fb950';
  ctx.font = '10px sans-serif';
  var cornerText = ['左下', '右下', '左上', '右上'][start];
  ctx.fillText('LED 0 在 ' + cornerText, canvas.width / 2, canvas.height - 5);
}

function testLedOrder(){
  if(!confirm('LED 将从索引 0 开始依次点亮红色，观察实际走线顺序。\n\n确定开始测试？')) return;
  sendWS({test_led: true});
}
