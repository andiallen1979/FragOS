const TOKEN = window.STUDIO_TOKEN;
const PRESETS = [
  ["qwen2.5-coder:7b", "4.7GB • 8GB VRAM • ESP-IDF / AVR"],
  ["qwen3.5:9b", "6.6GB • 8GB+ VRAM • General + Code"],
  ["qwen2.5-coder:14b", "9.0GB • 12-16GB VRAM • C/C++ / Android"],
  ["deepseek-coder-v2:16b", "8.9GB • 12-16GB VRAM • Fast MoE"],
  ["devstral:24b", "14GB • 24GB VRAM • Multi-file Debug"],
  ["qwen3-coder:30b", "19GB • 24GB+ VRAM • 256K Context"]
];

// Apply saved Code Studio 8-bit theme
const saved = JSON.parse(localStorage.getItem("cs_theme") || "null");
if (saved) Object.entries(saved).forEach(([k, v]) => { if (k !== "name") document.documentElement.style.setProperty("--" + k, v); });

function fmtGB(b) { return (b / (1024 ** 3)).toFixed(2) + " GB"; }

async function refreshMM() {
  const r = await fetch("/api/mm/status", { headers: { "X-Token": TOKEN } });
  const d = await r.json();
  if (d.error) { document.getElementById("hw-bar").textContent = "Ollama error: " + d.error; return; }
  const h = d.hw;
  document.getElementById("hw-bar").textContent = `${h.gpu} (${h.vram_gb}GB VRAM) | RAM: ${h.ram_gb}GB | Free Disk: ${h.disk_free_gb}GB`;
  const running = new Set((d.running || []).map(x => x.name));
  const tb = document.getElementById("mm-list"); tb.innerHTML = "";
  (d.models || []).forEach(m => {
    const live = running.has(m.name);
    const det = m.details || {};
    const tr = document.createElement("tr");
    tr.innerHTML = `<td><strong>${m.name}</strong></td><td>${fmtGB(m.size || 0)}</td>`
      + `<td>${det.parameter_size || "?"} / ${det.quantization_level || "?"}</td>`
      + `<td><span class="badge ${live ? "live" : ""}">${live ? "Loaded (VRAM)" : "Idle"}</span></td>`
      + `<td style="display:flex;gap:6px">${live ? `<button class="tb-btn" onclick="unloadModel('${m.name}')">Unload</button>` : ""}`
      + `<button class="tb-btn" style="background:var(--red);color:#fff" onclick="delModel('${m.name}')">Delete</button></td>`;
    tb.appendChild(tr);
  });
  document.getElementById("mm-count").textContent = `${(d.models || []).length} installed | ${running.size} loaded`;
  updatePullUI(d.pull);
}

function renderPresets() {
  const box = document.getElementById("presets"); box.innerHTML = "";
  PRESETS.forEach(([name, desc]) => {
    const div = document.createElement("div"); div.className = "preset";
    div.innerHTML = `<div><strong>${name}</strong><div style="opacity:.75;font-size:11px">${desc}</div></div>`
      + `<button class="tb-btn run" onclick="pullModel('${name}')">Pull</button>`;
    box.appendChild(div);
  });
}

async function pullModel(name) {
  const m = (name || document.getElementById("pull-in").value).trim();
  if (!m) return;
  const r = await fetch("/api/mm/pull", { method: "POST", headers: { "Content-Type": "application/json", "X-Token": TOKEN }, body: JSON.stringify({ model: m }) });
  if (!r.ok) { alert("A download is already in progress."); return; }
  pollPull();
}

function updatePullUI(st) {
  if (!st) return;
  document.getElementById("pull-msg").textContent = st.error ? `Error: ${st.error}` : (st.model ? `${st.model}: ${st.status} (${st.percent}%)` : "Idle");
  document.getElementById("prog-bar").style.width = (st.percent || 0) + "%";
}

async function pollPull() {
  const st = await (await fetch("/api/mm/pull_status", { headers: { "X-Token": TOKEN } })).json();
  updatePullUI(st);
  if (!st.done) setTimeout(pollPull, 800);
  else refreshMM();
}

async function unloadModel(m) {
  await fetch("/api/mm/unload", { method: "POST", headers: { "Content-Type": "application/json", "X-Token": TOKEN }, body: JSON.stringify({ model: m }) });
  refreshMM();
}

async function delModel(m) {
  if (!confirm("Delete model " + m + " from disk?")) return;
  await fetch("/api/mm/delete", { method: "POST", headers: { "Content-Type": "application/json", "X-Token": TOKEN }, body: JSON.stringify({ model: m }) });
  refreshMM();
}

renderPresets();
refreshMM();
