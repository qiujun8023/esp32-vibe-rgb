// 效果元数据（与 effects.c EFFECT_INFO 对齐）
const EFFECTS = [
  {id:0,  name:"Spectrum",    c1:"色彩(0=频段/1=高度)", c2:"峰值点",  c3:"镜像"},
  {id:1,  name:"Waterfall",   c1:"饱和度",   c2:"消退速率",c3:""},
  {id:2,  name:"Gravimeter",  c1:"保持时间", c2:"峰值亮度",c3:""},
  {id:3,  name:"Funky Plank", c1:"下落速度", c2:"间距",    c3:""},
  {id:4,  name:"Scroll",      c1:"滚动方向", c2:"",        c3:""},
  {id:5,  name:"CenterBars",  c1:"色彩模式", c2:"",        c3:""},
  {id:6,  name:"GravFreq",    c1:"重力强度", c2:"灵敏度",  c3:""},
  {id:7,  name:"Super Freq",  c1:"线条数量", c2:"",        c3:""},
  {id:8,  name:"Ripple",      c1:"扩散速度", c2:"最大涟漪",c3:""},
  {id:9,  name:"Juggles",     c1:"球数量",   c2:"消退速率",c3:""},
  {id:10, name:"Blurz",       c1:"色块数量", c2:"模糊度",  c3:""},
  {id:11, name:"DJ Light",    c1:"扫描速度", c2:"闪光时长",c3:""},
  {id:12, name:"Ripplepeak",  c1:"扩散速度", c2:"触发阈值",c3:""},
  {id:13, name:"Freqwave",    c1:"波动幅度", c2:"",        c3:""},
  {id:14, name:"Freqmap",     c1:"饱和度",   c2:"",        c3:""},
  {id:15, name:"Noisemove",   c1:"噪声缩放", c2:"亮度调制",c3:""},
  {id:16, name:"Rocktaves",   c1:"八度数量", c2:"",        c3:""},
  {id:17, name:"Energy",      c1:"响应速度", c2:"最低亮度",c3:"径向模式"},
  {id:18, name:"Plasma",      c1:"复杂度",   c2:"音频调制",c3:""},
  {id:19, name:"Swirl",       c1:"旋转速度", c2:"音频调制",c3:""},
  {id:20, name:"Waverly",     c1:"波动数量", c2:"振幅",    c3:""},
  {id:21, name:"Fire",        c1:"冷却速率", c2:"点燃率",  c3:"音频调制"},
];
const PALETTES = ["Rainbow","Sunset","Ocean","Lava","Forest","Party","Heat","Mono"];

// 全局状态
var cfg = {};
var ws = null;
var wsReconnectTimer = null;
var matW = 8, matH = 8;

// 初始化
(function init() {
  buildEffectGrid();
  buildPaletteGrid();
  bindEvents();
  connectWebSocket();
})();

function buildEffectGrid() {
  var grid = document.getElementById('fx-grid');
  EFFECTS.forEach(function(fx) {
    var btn = document.createElement('button');
    btn.className = 'fx-btn';
    btn.id = 'fx-' + fx.id;
    btn.textContent = fx.name;
    btn.dataset.effect = fx.id;
    grid.appendChild(btn);
  });
}

function buildPaletteGrid() {
  var grid = document.getElementById('palette-grid');
  PALETTES.forEach(function(name, i) {
    var btn = document.createElement('button');
    btn.className = 'fx-btn';
    btn.id = 'pal-' + i;
    btn.textContent = name;
    btn.dataset.palette = i;
    grid.appendChild(btn);
  });
}

function bindEvents() {
  // 标签页切换
  document.querySelectorAll('.tab').forEach(function(tab) {
    tab.addEventListener('click', function() {
      switchTab(this.dataset.tab);
    });
  });

  // 效果按钮
  document.getElementById('fx-grid').addEventListener('click', function(e) {
    if (e.target.classList.contains('fx-btn')) {
      setEffect(parseInt(e.target.dataset.effect));
    }
  });

  // 调色板按钮
  document.getElementById('palette-grid').addEventListener('click', function(e) {
    if (e.target.classList.contains('fx-btn')) {
      setPalette(parseInt(e.target.dataset.palette));
    }
  });

  // AGC按钮组
  document.querySelectorAll('#agc0, #agc1, #agc2').forEach(function(btn) {
    btn.addEventListener('click', function() {
      setAGC(parseInt(this.dataset.value));
    });
  });

  // 频率方向按钮组
  document.querySelectorAll('#dir0, #dir1').forEach(function(btn) {
    btn.addEventListener('click', function() {
      setFreqDir(parseInt(this.dataset.value));
    });
  });

  // 滑块
  document.querySelectorAll('input[type="range"]').forEach(function(input) {
    input.addEventListener('input', function() {
      onSliderChange(this.id, parseInt(this.value));
    });
  });

  // 数值显示（点击编辑）
  document.querySelectorAll('.val-display').forEach(function(span) {
    span.addEventListener('click', function() {
      editValue(this);
    });
  });

  // LED配置变化时更新预览
  ['led_w', 'led_h', 'led_serpentine', 'led_start', 'led_rotation'].forEach(function(id) {
    var el = document.getElementById(id);
    if (el) el.addEventListener('change', updateLedPreview);
  });

  // IP模式切换
  document.getElementById('ip_mode').addEventListener('change', toggleStaticFields);
}

// WebSocket连接
function connectWebSocket() {
  if (ws) { try { ws.close(); } catch(e) {} }

  ws = new WebSocket('ws://' + location.host + '/ws');

  ws.onopen = function() {
    setConnectionStatus('connected');
    if (wsReconnectTimer) {
      clearTimeout(wsReconnectTimer);
      wsReconnectTimer = null;
    }
  };

  ws.onclose = function() {
    setConnectionStatus('disconnected');
    if (!wsReconnectTimer) {
      wsReconnectTimer = setTimeout(function() {
        wsReconnectTimer = null;
        connectWebSocket();
      }, 2000);
    }
  };

  ws.onerror = function() {
    setConnectionStatus('disconnected');
    try { ws.close(); } catch(e) {}
  };

  ws.onmessage = function(e) {
    var data = JSON.parse(e.data);

    // 设置包（包含ssid字段）
    if (data.ssid !== undefined) {
      loadConfig(data);
      return;
    }

    // 状态推送
    if (data.fps !== undefined) updateStatus('fps', data.fps + ' fps');
    if (data.volume !== undefined) updateStatus('vol', '音量 ' + Math.round(data.volume * 100) + '%');
    if (data.pixels !== undefined) renderMatrix(data.pixels);
    if (cfg.effect !== undefined) {
      updateStatus('effect', EFFECTS[cfg.effect] ? EFFECTS[cfg.effect].name : '-');
    }
  };
}

function setConnectionStatus(status) {
  var el = document.getElementById('st-conn');
  el.className = 'status-dot ' + status;
  el.textContent = status === 'connected' ? '已连接' :
                   status === 'disconnected' ? '已断开' : '连接中';
}

function updateStatus(id, text) {
  var el = document.getElementById('st-' + id);
  if (el) el.textContent = text;
}

// 矩阵渲染
function renderMatrix(hex) {
  var canvas = document.getElementById('matrix-canvas');
  var ctx = canvas.getContext('2d');
  var n = hex.length / 6;
  var cols = matW, rows = matH;

  // 动态调整canvas尺寸
  var cellSize = Math.floor(160 / Math.max(cols, rows));
  canvas.width = cols * cellSize;
  canvas.height = rows * cellSize;

  ctx.clearRect(0, 0, canvas.width, canvas.height);

  for (var i = 0; i < n && i < cols * rows; i++) {
    var r = parseInt(hex.substr(i * 6, 2), 16);
    var g = parseInt(hex.substr(i * 6 + 2, 2), 16);
    var b = parseInt(hex.substr(i * 6 + 4, 2), 16);
    var x = (i % cols) * cellSize;
    var y = (rows - 1 - Math.floor(i / cols)) * cellSize;
    ctx.fillStyle = 'rgb(' + r + ',' + g + ',' + b + ')';
    ctx.fillRect(x, y, cellSize - 1, cellSize - 1);
  }
}

// 加载配置到UI
function loadConfig(data) {
  cfg = data;
  matW = data.led_w || 8;
  matH = data.led_h || 8;

  // 滑块
  setUIValue('speed', data.speed);
  setUIValue('intensity', data.intensity);
  setUIValue('custom1', data.custom1);
  setUIValue('custom2', data.custom2);
  setUIValue('custom3', data.custom3);
  setUIValue('brightness', data.brightness);
  setUIValue('gain', Math.round(data.gain || 40));
  setUIValue('squelch', data.squelch);
  setUIValue('fft_smooth', data.fft_smooth);

  // 字段
  setFieldValue('led_gpio', data.led_gpio);
  setFieldValue('led_w', data.led_w);
  setFieldValue('led_h', data.led_h);
  setFieldValue('led_serpentine', data.led_serpentine);
  setFieldValue('led_start', data.led_start);
  setFieldValue('led_rotation', data.led_rotation || 0);
  setFieldValue('mic_sck', data.mic_sck);
  setFieldValue('mic_ws', data.mic_ws);
  setFieldValue('mic_din', data.mic_din);
  setFieldValue('cfg_ssid', data.ssid || '');
  setFieldValue('ip_mode', data.ip_mode);
  setFieldValue('s_ip', data.s_ip || '');
  setFieldValue('s_mask', data.s_mask || '');
  setFieldValue('s_gw', data.s_gw || '');
  setFieldValue('s_dns1', data.s_dns1 || '');
  setFieldValue('s_dns2', data.s_dns2 || '');

  // AGC
  updateButtonGroupById(['agc0', 'agc1', 'agc2'], data.agc_mode);

  // 效果
  updateEffectButtons(data.effect);
  updateEffectLabels(data.effect);

  // 调色板
  updatePaletteButtons(data.palette);

  // 频率方向
  updateButtonGroupById(['dir0', 'dir1'], data.freq_dir);

  toggleStaticFields();
  updateLedPreview();
}

function setUIValue(id, value) {
  var input = document.getElementById(id);
  var display = document.getElementById(id + '_v');
  if (input) input.value = value;
  if (display) display.textContent = value;
}

function setFieldValue(id, value) {
  var el = document.getElementById(id);
  if (el) el.value = value;
}

function updateButtonGroupById(ids, activeValue) {
  ids.forEach(function(id) {
    var btn = document.getElementById(id);
    if (btn) {
      var v = parseInt(btn.dataset.value);
      btn.classList.toggle('active', v === activeValue);
    }
  });
}

function updateEffectButtons(effectId) {
  document.querySelectorAll('#fx-grid .fx-btn').forEach(function(btn) {
    var id = parseInt(btn.dataset.effect);
    btn.classList.toggle('active', id === effectId);
  });
}

function updatePaletteButtons(paletteId) {
  document.querySelectorAll('#palette-grid .fx-btn').forEach(function(btn) {
    var id = parseInt(btn.dataset.palette);
    btn.classList.toggle('active', id === paletteId);
  });
}

function updateEffectLabels(effectId) {
  var fx = EFFECTS[effectId] || {};
  ['c1', 'c2', 'c3'].forEach(function(c, i) {
    var row = document.getElementById(c + '-row');
    var lbl = document.getElementById(c + '-lbl');
    if (row) row.style.display = fx[c] ? '' : 'none';
    if (lbl) lbl.textContent = fx[c] || '';
  });
}

// 控件回调
function onSliderChange(key, value) {
  var display = document.getElementById(key + '_v');
  if (display && !display.querySelector('input')) {
    display.textContent = value;
  }
  cfg[key] = value;
  sendWS(key, value);
}

function editValue(span) {
  if (span.querySelector('input')) return;

  var key = span.dataset.key;
  var minVal = parseInt(span.dataset.min) || 0;
  var maxVal = parseInt(span.dataset.max) || 255;
  var oldVal = parseInt(span.textContent) || 0;

  span.innerHTML = '<input type="number" value="' + oldVal + '" min="' + minVal + '" max="' + maxVal + '">';
  var input = span.querySelector('input');
  input.focus();
  input.select();

  function finish() {
    var v = parseInt(input.value) || oldVal;
    v = Math.max(minVal, Math.min(maxVal, v));
    span.textContent = v;
    cfg[key] = v;
    document.getElementById(key).value = v;
    sendWS(key, v);
  }

  input.onkeydown = function(e) {
    if (e.key === 'Enter') { input.blur(); return false; }
    if (e.key === 'Escape') { span.textContent = oldVal; return false; }
  };
  input.onblur = finish;
}

function setEffect(id) {
  cfg.effect = id;
  updateEffectButtons(id);
  updateEffectLabels(id);
  sendWS('effect', id);
}

function setPalette(id) {
  cfg.palette = id;
  updatePaletteButtons(id);
  sendWS('palette', id);
}

function setAGC(value) {
  cfg.agc_mode = value;
  updateButtonGroupById(['agc0', 'agc1', 'agc2'], value);
  sendWS('agc_mode', value);
}

function setFreqDir(value) {
  cfg.freq_dir = value;
  updateButtonGroupById(['dir0', 'dir1'], value);
  sendWS('freq_dir', value);
}

function toggleStaticFields() {
  var mode = document.getElementById('ip_mode').value;
  var fields = document.getElementById('static-ip-fields');
  fields.classList.toggle('show', mode === '1');
}

// WebSocket发送
function sendWS(key, value) {
  if (ws && ws.readyState === 1) {
    var obj = {};
    obj[key] = value;
    ws.send(JSON.stringify(obj));
  }
}

// 标签页切换
function switchTab(tabId) {
  document.querySelectorAll('.tab').forEach(function(t) {
    t.classList.toggle('active', t.dataset.tab === tabId);
  });
  document.querySelectorAll('.tab-pane').forEach(function(p) {
    p.classList.toggle('active', p.id === 'tab-' + tabId);
  });
}

// 保存所有设置到Flash
function saveAllSettings() {
  setSysMsg('保存中...', '#8b949e');

  var payload = {
    // WiFi配置
    ssid: document.getElementById('cfg_ssid').value,
    pass_new: document.getElementById('pass_new').value,
    ip_mode: parseInt(document.getElementById('ip_mode').value),
    s_ip: document.getElementById('s_ip').value,
    s_mask: document.getElementById('s_mask').value,
    s_gw: document.getElementById('s_gw').value,
    s_dns1: document.getElementById('s_dns1').value,
    s_dns2: document.getElementById('s_dns2').value,
    // LED配置
    led_gpio: parseInt(document.getElementById('led_gpio').value) || 16,
    led_w: parseInt(document.getElementById('led_w').value) || 8,
    led_h: parseInt(document.getElementById('led_h').value) || 8,
    led_serpentine: parseInt(document.getElementById('led_serpentine').value),
    led_start: parseInt(document.getElementById('led_start').value),
    led_rotation: parseInt(document.getElementById('led_rotation').value),
    brightness: parseInt(document.getElementById('brightness').value) || 160,
    // 麦克风配置
    mic_sck: parseInt(document.getElementById('mic_sck').value) || 5,
    mic_ws: parseInt(document.getElementById('mic_ws').value) || 4,
    mic_din: parseInt(document.getElementById('mic_din').value) || 6,
    // 音频参数
    agc_mode: cfg.agc_mode || 1,
    gain: parseFloat(document.getElementById('gain').value) || 15,
    squelch: parseInt(document.getElementById('squelch').value) || 10,
    fft_smooth: parseInt(document.getElementById('fft_smooth').value) || 100,
    // 效果参数
    effect: cfg.effect || 0,
    palette: cfg.palette || 0,
    speed: parseInt(document.getElementById('speed').value) || 128,
    intensity: parseInt(document.getElementById('intensity').value) || 128,
    custom1: parseInt(document.getElementById('custom1').value) || 128,
    custom2: parseInt(document.getElementById('custom2').value) || 128,
    custom3: parseInt(document.getElementById('custom3').value) || 128,
    freq_dir: cfg.freq_dir || 0,
  };

  fetch('/api/settings', {
    method: 'POST',
    headers: { 'Content-Type': 'application/json' },
    body: JSON.stringify(payload)
  })
  .then(function(r) { return r.json(); })
  .then(function(d) {
    if (d.ok) {
      if (d.restart_required) {
        setSysMsg('已保存，设备正在重启...', '#3fb950');
      } else {
        setSysMsg('已保存', '#3fb950');
      }
    } else {
      setSysMsg('保存失败', '#f85149');
    }
  })
  .catch(function() {
    setSysMsg('已保存，设备正在重启...', '#3fb950');
  });
}

function doReboot() {
  if (!confirm('确定要重启设备吗？')) return;
  fetch('/api/reboot', { method: 'POST' }).catch(function() {});
  setSysMsg('正在重启...', '#8b949e');
}

function resetWifi() {
  if (!confirm('确定重置WiFi配置？设备将重启进入配网模式。')) return;
  fetch('/api/reset_wifi', { method: 'POST' }).catch(function() {});
  setSysMsg('正在重启...', '#8b949e');
}

function factoryReset() {
  if (!confirm('确定恢复出厂设置？所有配置将被清除。')) return;
  fetch('/api/factory', { method: 'POST' }).catch(function() {});
  setSysMsg('正在恢复出厂...', '#8b949e');
}

function setSysMsg(text, color) {
  var el = document.getElementById('sys-msg');
  if (el) {
    el.textContent = text;
    el.style.color = color;
  }
}

// LED编号预览
function updateLedPreview() {
  var canvas = document.getElementById('led-preview');
  if (!canvas) return;
  var ctx = canvas.getContext('2d');

  var w = parseInt(document.getElementById('led_w').value) || 8;
  var h = parseInt(document.getElementById('led_h').value) || 8;
  var serpentine = parseInt(document.getElementById('led_serpentine').value);
  var start = parseInt(document.getElementById('led_start').value);

  // 自适应cell尺寸，确保canvas不超过容器宽度
  var maxCanvasWidth = 280;
  var cellSize = Math.floor((maxCanvasWidth - 16) / Math.max(w, 1));
  cellSize = Math.min(cellSize, 36);
  cellSize = Math.max(cellSize, 24);

  var cols = w;
  var rows = h;
  var padding = 8;

  canvas.width = cols * cellSize + padding * 2;
  canvas.height = rows * cellSize + padding * 2 + 14;

  // 背景
  ctx.fillStyle = '#0d1117';
  ctx.fillRect(0, 0, canvas.width, canvas.height);

  // 计算LED索引
  function ledIndex(x, y) {
    var px = (start & 1) ? (cols - 1 - x) : x;
    var py = (start & 2) ? (rows - 1 - y) : y;
    if (serpentine && (py & 1)) px = (cols - 1) - px;
    return py * cols + px;
  }

  // 构建位置映射
  var posMap = {};
  for (var y = 0; y < rows; y++) {
    for (var x = 0; x < cols; x++) {
      var idx = ledIndex(x, y);
      posMap[idx] = { x: x, y: y };
    }
  }

  // 绘制数据流连线（红线）
  ctx.strokeStyle = '#f85149';
  ctx.lineWidth = 2;
  ctx.setLineDash([3, 3]);

  for (var i = 0; i < cols * rows - 1; i++) {
    var p1 = posMap[i], p2 = posMap[i + 1];
    if (!p1 || !p2) continue;

    var x1 = p1.x * cellSize + cellSize / 2 + padding;
    var y1 = (rows - 1 - p1.y) * cellSize + cellSize / 2 + padding;
    var x2 = p2.x * cellSize + cellSize / 2 + padding;
    var y2 = (rows - 1 - p2.y) * cellSize + cellSize / 2 + padding;

    ctx.beginPath();
    ctx.moveTo(x1, y1);
    ctx.lineTo(x2, y2);
    ctx.stroke();
  }
  ctx.setLineDash([]);

  // 绘制LED圆圈和数字
  var radius = Math.max(cellSize / 2 - 4, 10);
  ctx.font = 'bold ' + Math.max(cellSize / 3.5, 9) + 'px sans-serif';
  ctx.textAlign = 'center';
  ctx.textBaseline = 'middle';

  for (var y = 0; y < rows; y++) {
    for (var x = 0; x < cols; x++) {
      var idx = ledIndex(x, y);
      var px = x * cellSize + cellSize / 2 + padding;
      var py = (rows - 1 - y) * cellSize + cellSize / 2 + padding;

      ctx.fillStyle = '#21262d';
      ctx.beginPath();
      ctx.arc(px, py, radius, 0, Math.PI * 2);
      ctx.fill();

      ctx.fillStyle = '#58a6ff';
      ctx.fillText(idx, px, py);
    }
  }

  // 起始角标注
  ctx.fillStyle = '#3fb950';
  ctx.font = '10px sans-serif';
  var cornerText = ['左下', '右下', '左上', '右上'][start] || '左下';
  ctx.fillText('LED 0 在 ' + cornerText, canvas.width / 2, canvas.height - 4);
}

function testLedOrder() {
  if (!confirm('LED将从索引0开始依次点亮红色，用于观察实际走线顺序。\n\n确定开始测试？')) return;
  if (ws && ws.readyState === 1) {
    ws.send(JSON.stringify({ test_led: true }));
  }
}