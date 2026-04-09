/**
 * ESP32 Vibe RGB - 控制界面
 */

// ============================================================
// 常量
// ============================================================

const EFFECTS = [
  {id:0,  name:"频谱柱",   c1:"色彩模式", c2:"显示峰值", c3:"镜像模式"},
  {id:1,  name:"频谱均衡", c1:"消退速率", c2:"显示峰值", c3:""},
  {id:2,  name:"中心柱",   c1:"水平居中", c2:"垂直居中", c3:"颜色方向"},
  {id:3,  name:"频谱映射", c1:"增益调节", c2:"",         c3:""},
  {id:4,  name:"瀑布流",   c1:"颜色偏移", c2:"消退速率", c3:""},
  {id:5,  name:"重力计",   c1:"下落速度", c2:"峰值保持", c3:""},
  {id:6,  name:"重力中心", c1:"下落速度", c2:"峰值保持", c3:""},
  {id:7,  name:"重力偏心", c1:"下落速度", c2:"峰值保持", c3:""},
  {id:8,  name:"重力频率", c1:"下落速度", c2:"峰值保持", c3:""},
  {id:9,  name:"下落木板", c1:"下落速度", c2:"频段数量", c3:""},
  {id:10, name:"矩阵像素", c1:"滚动速度", c2:"亮度增益", c3:""},
  {id:11, name:"频率波",   c1:"波动速度", c2:"扩散强度", c3:""},
  {id:12, name:"像素波",   c1:"波动速度", c2:"亮度增益", c3:""},
  {id:13, name:"涟漪峰值", c1:"涟漪数量", c2:"触发阈值", c3:""},
  {id:14, name:"弹跳球",   c1:"球体数量", c2:"轨迹消退", c3:""},
  {id:15, name:"水塘峰值", c1:"消退速率", c2:"触发阈值", c3:""},
  {id:16, name:"水塘",     c1:"消退速率", c2:"闪烁大小", c3:""},
  {id:17, name:"频率像素", c1:"消退速率", c2:"像素数量", c3:""},
  {id:18, name:"频率映射", c1:"消退速率", c2:"",         c3:""},
  {id:19, name:"随机像素", c1:"像素数量", c2:"颜色偏移", c3:""},
  {id:20, name:"噪声火焰", c1:"火焰速度", c2:"亮度阈值", c3:""},
  {id:21, name:"等离子",   c1:"相位速度", c2:"亮度阈值", c3:""},
  {id:22, name:"极光",     c1:"流动速度", c2:"色彩跨度", c3:""},
  {id:23, name:"中间噪声", c1:"消退速率", c2:"灵敏度",   c3:""},
  {id:24, name:"噪声计",   c1:"消退速率", c2:"灵敏度",   c3:""},
  {id:25, name:"噪声移动", c1:"移动速度", c2:"频段数量", c3:""},
  {id:26, name:"模糊色块", c1:"消退速率", c2:"模糊强度", c3:""},
  {id:27, name:"DJ灯光",   c1:"扫描速度", c2:"闪烁时长", c3:""}
];

const PALETTES = ["彩虹", "派对", "日落", "岩浆", "热力", "梦幻", "海洋", "极光", "森林", "赛博", "单色", "随机"];

// ============================================================
// 应用状态
// ============================================================

const App = {
  socket: null,
  reconnectTimer: null,
  
  matrixCanvas: null,
  matrixCtx: null,
  previewCanvas: null,
  previewCtx: null,
  
  state: {
    // LED 配置
    led_w: 8,
    led_h: 8,
    led_gpio: 16,
    led_serpentine: 1,
    led_start: 0,
    led_rotation: 0,
    brightness: 160,
    
    // 效果
    effect: 0,
    palette: 0,
    speed: 128,
    intensity: 128,
    custom1: 128,
    custom2: 128,
    custom3: 128,
    freq_dir: 0,
    
    // 音频
    agc_mode: 1,
    gain: 40,
    squelch: 5,
    fft_smooth: 100,
    mic_ws: 4,
    mic_sck: 5,
    mic_din: 6,
    
    // 网络
    ssid: '',
    ip_mode: 0,
    
    // 实时状态（只读，不发送）
    fps: null,
    volume: null,
    heap: null,
    rssi: null,
    uptime: null,
    pixels: null,
    bands: null
  }
};

// ============================================================
// 初始化
// ============================================================

document.addEventListener('DOMContentLoaded', init);

function init() {
  initCanvas();
  initEffectGrid();
  initPaletteGrid();
  initTabs();
  initControls();
  connectWebSocket();
}

function initCanvas() {
  App.matrixCanvas = document.getElementById('matrix-canvas');
  if (App.matrixCanvas) {
    App.matrixCtx = App.matrixCanvas.getContext('2d');
  }
  
  App.previewCanvas = document.getElementById('led-preview');
  if (App.previewCanvas) {
    App.previewCtx = App.previewCanvas.getContext('2d');
  }
}

function initEffectGrid() {
  const grid = document.getElementById('fx-grid');
  if (!grid) return;
  
  grid.innerHTML = EFFECTS.map(e => 
    `<div class="fx-btn" data-effect="${e.id}">${e.name}</div>`
  ).join('');
  
  grid.addEventListener('click', e => {
    if (e.target.classList.contains('fx-btn')) {
      App.state.effect = parseInt(e.target.dataset.effect);
      syncUI();
      sendUpdate();
    }
  });
}

function initPaletteGrid() {
  const grid = document.getElementById('palette-grid');
  if (!grid) return;
  
  grid.innerHTML = PALETTES.map((name, i) => 
    `<div class="fx-btn" data-palette="${i}">${name}</div>`
  ).join('');
  
  grid.addEventListener('click', e => {
    if (e.target.classList.contains('fx-btn')) {
      App.state.palette = parseInt(e.target.dataset.palette);
      syncUI();
      sendUpdate();
    }
  });
}

function initTabs() {
  document.querySelectorAll('.tab-btn').forEach(btn => {
    btn.addEventListener('click', () => {
      const tab = btn.dataset.tab;
      
      document.querySelectorAll('.tab-btn').forEach(b => {
        b.classList.toggle('active', b.dataset.tab === tab);
      });
      
      document.querySelectorAll('.tab-pane').forEach(p => {
        p.classList.toggle('active', p.id === `tab-${tab}`);
      });
    });
  });
}

function initControls() {
  // 滑块 + 数值输入联动
  document.querySelectorAll('.param-row').forEach(row => {
    const slider = row.querySelector('.param-slider');
    const input = row.querySelector('.param-value');
    
    if (!slider || !input) return;
    
    // 滑块拖动时更新输入框
    slider.addEventListener('input', () => {
      input.value = slider.value;
    });
    
    // 滑块改变时发送更新
    slider.addEventListener('change', () => {
      App.state[slider.id] = parseInt(slider.value);
      sendUpdate();
    });
    
    // 数值输入框可编辑
    input.addEventListener('focus', () => {
      input.classList.add('editing');
    });
    
    input.addEventListener('blur', () => {
      input.classList.remove('editing');
      // 校验范围
      const val = Math.max(slider.min, Math.min(slider.max, parseInt(input.value) || slider.min));
      input.value = val;
      slider.value = val;
      App.state[slider.id] = val;
      sendUpdate();
    });
    
    input.addEventListener('keydown', e => {
      if (e.key === 'Enter') {
        input.blur();
      }
    });
  });
  
  // 选项按钮
  document.querySelectorAll('.choice-btn').forEach(btn => {
    btn.addEventListener('click', () => {
      const val = parseInt(btn.dataset.value);
      
      if (btn.id.startsWith('dir')) {
        App.state.freq_dir = val;
      } else if (btn.id.startsWith('agc')) {
        App.state.agc_mode = val;
      }
      
      syncUI();
      sendUpdate();
    });
  });
  
  // 输入字段
  document.querySelectorAll('.field-input, .field-select').forEach(input => {
    input.addEventListener('change', () => {
      if (input.type === 'number') {
        App.state[input.id] = parseInt(input.value) || 0;
      } else {
        App.state[input.id] = input.value;
      }
      sendUpdate();
      
      if (input.id === 'ip_mode') {
        document.getElementById('static-ip-fields')
          ?.classList.toggle('show', input.value === '1');
      }
    });
  });
}

// ============================================================
// WebSocket
// ============================================================

function connectWebSocket() {
  const protocol = location.protocol === 'https:' ? 'wss:' : 'ws:';
  const url = `${protocol}//${location.host}/ws`;
  
  try {
    App.socket = new WebSocket(url);
  } catch (e) {
    console.error('WebSocket 创建失败:', e);
    scheduleReconnect();
    return;
  }
  
  App.socket.onopen = () => {
    updateConnectionStatus('connected', '已连接');
    App.socket.send(JSON.stringify({ get_cfg: true }));
  };
  
  App.socket.onmessage = e => {
    try {
      const data = JSON.parse(e.data);
      
      // 合并所有状态（包括 pixels 消息中的 fps/volume 等）
      Object.assign(App.state, data);
      
      // 如果有像素数据，渲染预览
      if (data.pixels) {
        renderMatrix(data.pixels);
      }
      
      // 同步 UI（排除只读字段）
      syncUI();
    } catch (err) {
      console.error('消息解析失败:', err);
    }
  };
  
  App.socket.onclose = () => {
    updateConnectionStatus('disconnected', '已断开');
    scheduleReconnect();
  };
  
  App.socket.onerror = e => {
    console.error('WebSocket 错误:', e);
  };
}

function scheduleReconnect() {
  if (App.reconnectTimer) clearTimeout(App.reconnectTimer);
  App.reconnectTimer = setTimeout(connectWebSocket, 2000);
}

function sendUpdate() {
  if (!App.socket || App.socket.readyState !== WebSocket.OPEN) return;
  
  // 只发送可配置字段，排除只读状态
  const { fps, volume, heap, rssi, uptime, pixels, bands, beat, ...payload } = App.state;
  App.socket.send(JSON.stringify(payload));
}

function updateConnectionStatus(status, text) {
  const indicator = document.getElementById('st-conn-indicator');
  const label = document.getElementById('st-conn');
  
  if (indicator) {
    indicator.className = `status-indicator ${status}`;
  }
  if (label) {
    label.textContent = text;
  }
}

// ============================================================
// UI 同步
// ============================================================

function syncUI() {
  const s = App.state;
  
  // 效果按钮
  document.querySelectorAll('[data-effect]').forEach(btn => {
    btn.classList.toggle('active', parseInt(btn.dataset.effect) === s.effect);
  });
  
  // 调色板按钮
  document.querySelectorAll('[data-palette]').forEach(btn => {
    btn.classList.toggle('active', parseInt(btn.dataset.palette) === s.palette);
  });
  
  // 方向按钮
  document.querySelectorAll('[id^="dir"]').forEach(btn => {
    btn.classList.toggle('active', parseInt(btn.dataset.value) === s.freq_dir);
  });
  
  // AGC 按钮
  document.querySelectorAll('[id^="agc"]').forEach(btn => {
    btn.classList.toggle('active', parseInt(btn.dataset.value) === s.agc_mode);
  });
  
  // 滑块和数值
  syncSlider('speed', s.speed);
  syncSlider('intensity', s.intensity);
  syncSlider('custom1', s.custom1);
  syncSlider('custom2', s.custom2);
  syncSlider('custom3', s.custom3);
  syncSlider('brightness', s.brightness);
  syncSlider('gain', s.gain);
  syncSlider('squelch', s.squelch);
  syncSlider('fft_smooth', s.fft_smooth);
  
  // 输入字段
  syncField('led_gpio', s.led_gpio);
  syncField('led_w', s.led_w);
  syncField('led_h', s.led_h);
  syncField('mic_ws', s.mic_ws);
  syncField('mic_sck', s.mic_sck);
  syncField('mic_din', s.mic_din);
  
  // 下拉框
  syncSelect('led_serpentine', s.led_serpentine);
  syncSelect('led_start', s.led_start);
  syncSelect('led_rotation', s.led_rotation);
  
  // WiFi
  if (s.ssid) syncField('cfg_ssid', s.ssid);
  
  // IP 模式
  if (s.ip_mode !== undefined) {
    syncSelect('ip_mode', s.ip_mode);
    document.getElementById('static-ip-fields')?.classList.toggle('show', s.ip_mode === 1);
  }
  
  // 效果参数标签
  updateEffectParams();
  
  // 实时状态显示
  updateStatusDisplay();
  
  // LED 预览
  renderLedPreview();
}

function syncSlider(id, value) {
  if (value === undefined) return;
  
  const slider = document.getElementById(id);
  const input = document.getElementById(`${id}_v`);
  
  // 避免覆盖正在编辑的输入框
  if (slider && document.activeElement !== slider) {
    slider.value = value;
  }
  if (input && !input.classList.contains('editing')) {
    input.value = value;
  }
}

function syncField(id, value) {
  if (value === undefined) return;
  const el = document.getElementById(id);
  if (el && document.activeElement !== el) {
    el.value = value;
  }
}

function syncSelect(id, value) {
  if (value === undefined) return;
  const el = document.getElementById(id);
  if (el && document.activeElement !== el) {
    el.value = value;
  }
}

function updateEffectParams() {
  const s = App.state;
  const eff = EFFECTS.find(e => e.id === s.effect);
  
  if (eff) {
    setText('st-effect', eff.name);
    
    toggleParamRow('c1', eff.c1);
    toggleParamRow('c2', eff.c2);
    toggleParamRow('c3', eff.c3);
  }
}

function toggleParamRow(prefix, label) {
  const row = document.getElementById(`${prefix}-row`);
  const lbl = document.getElementById(`${prefix}-lbl`);
  
  if (row) {
    row.style.display = label && label.trim() ? '' : 'none';
  }
  if (lbl && label) {
    lbl.textContent = label;
  }
}

function updateStatusDisplay() {
  const s = App.state;
  
  // 帧率
  if (s.fps !== undefined && s.fps !== null) {
    setText('st-fps', `${s.fps} fps`);
  }
  
  // 音量
  if (s.volume !== undefined && s.volume !== null) {
    const vol = Math.round(s.volume * 100);
    setText('st-vol', `${vol}%`);
  }
  
  // 运行时间
  if (s.uptime !== undefined && s.uptime !== null) {
    const h = Math.floor(s.uptime / 3600);
    const m = Math.floor((s.uptime % 3600) / 60);
    const sec = s.uptime % 60;
    setText('st-uptime', `${h.toString().padStart(2,'0')}:${m.toString().padStart(2,'0')}:${sec.toString().padStart(2,'0')}`);
  }
  
  // 内存
  if (s.heap !== undefined && s.heap !== null) {
    setText('st-heap', `${Math.round(s.heap / 1024)} KB`);
  }
  
  // 信号
  if (s.rssi !== undefined && s.rssi !== null) {
    setText('st-rssi', `${s.rssi} dBm`);
  }
}

// ============================================================
// Canvas 渲染
// ============================================================

function renderMatrix(hex) {
  if (!App.matrixCtx || !App.matrixCanvas || !hex) return;
  
  const w = App.state.led_w || 8;
  const h = App.state.led_h || 8;
  
  // 自适应单元格大小，预留发光空间
  const maxDim = Math.max(w, h);
  const maxSize = Math.min(260, window.innerWidth - 56);
  const cellSize = Math.max(8, Math.floor(maxSize / maxDim));
  const radius = Math.max(2, (cellSize - 4) / 2);
  
  const canvasW = w * cellSize;
  const canvasH = h * cellSize;
  
  if (App.matrixCanvas.width !== canvasW || App.matrixCanvas.height !== canvasH) {
    App.matrixCanvas.width = canvasW;
    App.matrixCanvas.height = canvasH;
  }
  
  const ctx = App.matrixCtx;
  
  // 清空背景
  ctx.fillStyle = '#000';
  ctx.fillRect(0, 0, canvasW, canvasH);
  
  // 绘制圆形像素
  const count = Math.min(hex.length / 6, w * h);
  
  for (let i = 0; i < count; i++) {
    const r = parseInt(hex.substr(i * 6, 2), 16);
    const g = parseInt(hex.substr(i * 6 + 2, 2), 16);
    const b = parseInt(hex.substr(i * 6 + 4, 2), 16);
    
    const x = i % w;
    const y = Math.floor(i / w);
    
    if (y >= h) continue;
    
    const cx = x * cellSize + cellSize / 2;
    const cy = (h - 1 - y) * cellSize + cellSize / 2;
    
    // 发光效果（亮度足够时）
    const brightness = (r + g + b) / 3;
    if (brightness > 20) {
      ctx.shadowColor = `rgb(${r},${g},${b})`;
      ctx.shadowBlur = Math.max(4, cellSize / 3);
    } else {
      ctx.shadowBlur = 0;
    }
    
    // 绘制圆形 LED
    ctx.fillStyle = `rgb(${r},${g},${b})`;
    ctx.beginPath();
    ctx.arc(cx, cy, radius, 0, Math.PI * 2);
    ctx.fill();
  }
  
  ctx.shadowBlur = 0;
}

function renderLedPreview() {
  if (!App.previewCtx || !App.previewCanvas) return;
  
  const w = App.state.led_w || 8;
  const h = App.state.led_h || 8;
  
  // 较大的单元格，带连线
  const maxDim = Math.max(w, h);
  const maxSize = Math.min(260, window.innerWidth - 48);
  const cellSize = Math.max(20, Math.floor(maxSize / maxDim));
  
  const canvasW = w * cellSize;
  const canvasH = h * cellSize;
  
  if (App.previewCanvas.width !== canvasW || App.previewCanvas.height !== canvasH) {
    App.previewCanvas.width = canvasW;
    App.previewCanvas.height = canvasH;
  }
  
  const ctx = App.previewCtx;
  
  // 背景
  ctx.fillStyle = '#000';
  ctx.fillRect(0, 0, canvasW, canvasH);
  
  const total = w * h;
  const points = [];
  
  // 计算所有 LED 坐标
  for (let i = 0; i < total; i++) {
    const coord = computeLedCoord(i, w, h);
    points.push({
      x: coord.x * cellSize + cellSize / 2,
      y: (h - 1 - coord.y) * cellSize + cellSize / 2
    });
  }
  
  // 绘制连线（RGB 渐变）
  for (let i = 1; i < points.length; i++) {
    const t = i / total;
    const gradient = ctx.createLinearGradient(
      points[i-1].x, points[i-1].y, points[i].x, points[i].y
    );
    
    // RGB 色相循环
    const hue = (t * 360) % 360;
    const color = `hsl(${hue}, 100%, 60%)`;
    
    ctx.strokeStyle = color;
    ctx.lineWidth = 2;
    ctx.beginPath();
    ctx.moveTo(points[i-1].x, points[i-1].y);
    ctx.lineTo(points[i].x, points[i].y);
    ctx.stroke();
  }
  
  // 绘制 LED 节点和序号
  for (let i = 0; i < points.length; i++) {
    const p = points[i];
    const t = i / total;
    const hue = (t * 360) % 360;
    
    // 发光效果
    ctx.shadowColor = `hsl(${hue}, 100%, 50%)`;
    ctx.shadowBlur = 8;
    
    // 圆形节点
    ctx.fillStyle = `hsla(${hue}, 100%, 60%, 0.3)`;
    ctx.beginPath();
    ctx.arc(p.x, p.y, cellSize / 3, 0, Math.PI * 2);
    ctx.fill();
    
    ctx.strokeStyle = `hsl(${hue}, 100%, 60%)`;
    ctx.lineWidth = 1;
    ctx.stroke();
    
    ctx.shadowBlur = 0;
    
    // 序号
    ctx.fillStyle = '#fff';
    ctx.font = `bold ${Math.max(10, cellSize / 3)}px JetBrains Mono, monospace`;
    ctx.textAlign = 'center';
    ctx.textBaseline = 'middle';
    ctx.fillText(String(i), p.x, p.y);
  }
}

function computeLedCoord(index, w, h) {
  const serpentine = App.state.led_serpentine;
  const start = App.state.led_start;
  
  let x = index % w;
  let y = Math.floor(index / w);
  
  // 蛇形布线：偶数行正向，奇数行反向
  if (serpentine && (y % 2 === 1)) {
    x = w - 1 - x;
  }
  
  // 起始角调整
  switch (start) {
    case 1: x = w - 1 - x; break;            // 右下
    case 2: y = h - 1 - y; break;            // 左上
    case 3: x = w - 1 - x; y = h - 1 - y; break; // 右上
  }
  
  return { x, y };
}

// ============================================================
// 工具函数
// ============================================================

function setText(id, text) {
  const el = document.getElementById(id);
  if (el) el.textContent = text;
}

function showToast(msg) {
  const toast = document.getElementById('sys-msg');
  if (toast) {
    toast.textContent = msg;
    toast.classList.add('show');
    setTimeout(() => toast.classList.remove('show'), 3000);
  }
}

// ============================================================
// 全局操作
// ============================================================

window.saveAllSettings = function() {
  if (App.socket && App.socket.readyState === WebSocket.OPEN) {
    App.socket.send(JSON.stringify({ save: true }));
    showToast('配置已保存');
  }
};

window.doReboot = function() {
  if (!confirm('确定要重启设备吗？')) return;
  if (App.socket && App.socket.readyState === WebSocket.OPEN) {
    App.socket.send(JSON.stringify({ reboot: true }));
  }
};

window.factoryReset = function() {
  if (!confirm('确定要恢复出厂设置吗？所有配置将被清除！')) return;
  if (App.socket && App.socket.readyState === WebSocket.OPEN) {
    App.socket.send(JSON.stringify({ factory: true }));
  }
};

window.testLedOrder = function() {
  if (App.socket && App.socket.readyState === WebSocket.OPEN) {
    App.socket.send(JSON.stringify({ test_led: true }));
    showToast('LED 测试已启动');
  }
};