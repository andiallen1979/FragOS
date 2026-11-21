const TOKEN = window.STUDIO_TOKEN;
const history = [];
let tabs = [], activeTab = null, chatCtrl = null;
const term = document.getElementById("term");
const chat = document.getElementById("chat");
const promptEl = document.getElementById("prompt");

const cm = CodeMirror.fromTextArea(document.getElementById("editor"), {
  theme: "material-darker", lineNumbers: true, matchBrackets: true, autoCloseBrackets: true,
  indentUnit: 4, tabSize: 4, indentWithTabs: false, mode: "python"
});

cm.on("change", () => {
  if (!activeTab) return;
  activeTab.content = cm.getValue();
  activeTab.dirty = true;
  renderTabs();
});
cm.on("cursorActivity", updateStatus);

document.querySelectorAll(".menu-btn").forEach(b => b.addEventListener("click", e => {
  e.stopPropagation();
  const p = b.parentElement, was = p.classList.contains("open");
  document.querySelectorAll(".menu").forEach(m => m.classList.remove("open"));
  if (!was) p.classList.add("open");
}));
document.addEventListener("click", () => document.querySelectorAll(".menu").forEach(m => m.classList.remove("open")));

document.addEventListener("keydown", e => {
  if (e.ctrlKey && e.key.toLowerCase() === "s") { e.preventDefault(); runCode("save"); }
  if (e.key === "F5") { e.preventDefault(); runCode(e.shiftKey ? "konsole" : "inline"); }
});
promptEl.addEventListener("keydown", e => { if (e.key === "Enter" && !e.shiftKey) { e.preventDefault(); sendChat(); } });

function modeFor(p) {
  if (p.endsWith(".sh")) return "shell";
  if (p.endsWith(".c") || p.endsWith(".h") || p.endsWith(".cpp")) return "text/x-c++src";
  if (p.endsWith(".js") || p.endsWith(".json")) return "javascript";
  if (p.endsWith(".html")) return "htmlmixed";
  if (p.endsWith(".css")) return "css";
  return "python";
}
function langFor(p) { return p.endsWith(".sh") ? "bash" : p.endsWith(".c") ? "c" : "python"; }
function iconFor(n) { return n.endsWith(".py") ? "&#128013;" : n.endsWith(".sh") ? "&#9002;" : n.endsWith(".html") ? "&#127760;" : "&#128196;"; }

function updateStatus() {
  const cur = cm.getCursor(), p = activeTab ? activeTab.path : "/home/andi/.code/script.py";
  document.getElementById("sb-file").textContent = p;
  document.getElementById("sb-pos").textContent = `Ln ${cur.line + 1}, Col ${cur.ch + 1}  |  UTF-8  |  ${langFor(p)}`;
}

function togglePane(id) { document.getElementById(id).classList.toggle("hidden"); cm.refresh(); }
function clearTerm() { term.textContent = ""; }
function clearChat() { history.length = 0; chat.innerHTML = ""; }

function openTab(rel, path, content) {
  let t = tabs.find(x => x.path === path);
  if (!t) { t = { rel, path, name: path.split("/").pop(), content, dirty: false }; tabs.push(t); }
  activeTab = t;
  cm.setOption("mode", modeFor(path));
  cm.setValue(t.content);
  t.dirty = false;
  renderTabs(); updateStatus();
}

function renderTabs() {
  const el = document.getElementById("tabs"); el.innerHTML = "";
  tabs.forEach(t => {
    const d = document.createElement("div");
    d.className = "tab" + (t === activeTab ? " active" : "");
    d.innerHTML = `<span>${iconFor(t.name)} ${t.name}${t.dirty ? " •" : ""}</span><button class="tab-x">&times;</button>`;
    d.onclick = () => openTab(t.rel, t.path, t.content);
    d.querySelector(".tab-x").onclick = e => {
      e.stopPropagation();
      tabs = tabs.filter(x => x !== t);
      if (activeTab === t) { activeTab = tabs[tabs.length - 1] || null; cm.setValue(activeTab ? activeTab.content : ""); }
      renderTabs(); updateStatus();
    };
    el.appendChild(d);
  });
}

async function refreshTree() {
  const d = await (await fetch("/api/tree", { headers: { "X-Token": TOKEN } })).json();
  const root = document.getElementById("tree"); root.innerHTML = "";
  function draw(items, depth) {
    items.forEach(it => {
      const row = document.createElement("div");
      row.className = "t-item" + (activeTab && activeTab.rel === it.rel ? " active" : "");
      row.style.paddingLeft = (10 + depth * 14) + "px";
      row.innerHTML = `<span class="t-name">${it.type === "dir" ? "&#128193;" : iconFor(it.name)} ${it.name}</span><button class="t-del" title="Delete">&times;</button>`;
      row.onclick = async () => {
        if (it.type === "file") {
          const f = await (await fetch("/api/file?rel=" + encodeURIComponent(it.rel), { headers: { "X-Token": TOKEN } })).json();
          if (f.path) openTab(f.rel, f.path, f.content || "");
          refreshTree();
        }
      };
      row.querySelector(".t-del").onclick = async e => {
        e.stopPropagation();
        if (!confirm("Delete " + it.rel + "?")) return;
        await fetch("/api/delete", { method: "POST", headers: { "Content-Type": "application/json", "X-Token": TOKEN }, body: JSON.stringify({ rel: it.rel }) });
        refreshTree();
      };
      root.appendChild(row);
      if (it.type === "dir" && it.children) draw(it.children, depth + 1);
    });
  }
  draw(d.tree || [], 0);
}

function newFile() {
  const name = prompt("New file name (relative to ~/.code):", "script.py");
  if (!name) return;
  openTab(name, "/home/andi/.code/" + name.replace(/^\/+/, ""), "");
}
async function newFolder() {
  const name = prompt("New folder name (relative to ~/.code):");
  if (!name) return;
  await fetch("/api/mkdir", { method: "POST", headers: { "Content-Type": "application/json", "X-Token": TOKEN }, body: JSON.stringify({ rel: name }) });
  refreshTree();
}
function saveAs() {
  const name = prompt("Save as path:", activeTab ? activeTab.path : "/home/andi/.code/script.py");
  if (!name) return;
  if (activeTab) { activeTab.path = name; activeTab.name = name.split("/").pop(); }
  runCode("save");
}

async function runCode(mode) {
  if (!activeTab) newFile();
  if (!activeTab) return;
  const p = activeTab.path;
  term.textContent = (mode === "save" ? "Saving " : "Saving & running ") + p + "...";
  try {
    const r = await fetch("/run", {
      method: "POST", headers: { "Content-Type": "application/json", "X-Token": TOKEN },
      body: JSON.stringify({ lang: langFor(p), code: cm.getValue(), mode, path: p })
    });
    const d = await r.json();
    term.textContent = (d.stdout || "") + (d.stderr ? "\n[STDERR]\n" + d.stderr : "") + "\n[exit " + d.returncode + "]";
    activeTab.dirty = false; renderTabs(); refreshTree();
    if (mode !== "save" && document.getElementById("auto-ai")?.checked) sendOutput();
  } catch (e) { term.textContent = "Runner error: " + e; }
}

function addMsg(r, t) { const d = document.createElement("div"); d.className = "msg " + r; d.textContent = t; chat.appendChild(d); chat.scrollTop = chat.scrollHeight; return d; }
function stopChat() { if (chatCtrl) { chatCtrl.abort(); chatCtrl = null; } }
function sendOutput() {
  const out = term.textContent.trim(); if (!out) return;
  promptEl.value = "Latest output:\n" + out + "\n\nFix any errors or continue.";
  sendChat();
}

async function sendChat() {
  const text = promptEl.value.trim(), model = document.getElementById("model").value;
  if (!text || !model || chatCtrl) return;
  promptEl.value = ""; addMsg("user", text);
  const wait = addMsg("ai", "Thinking...");
  const sb = document.getElementById("sendBtn"), st = document.getElementById("stopBtn");
  sb.style.display = "none"; st.style.display = "inline-flex";
  chatCtrl = new AbortController();
  const p = activeTab ? activeTab.path : "/home/andi/.code/script.py", code = cm.getValue().trim();
  const sys = "Concise coding assistant. Output ONE complete fenced code block. No Steps to Run.";
  const u = code ? `Active file (${p}):\n\`\`\`\n${code}\n\`\`\`\n\nRequest: ${text}` : text;
  try {
    const r = await fetch("/api/chat", { method: "POST", signal: chatCtrl.signal, headers: { "Content-Type": "application/json", "X-Token": TOKEN }, body: JSON.stringify({ model, messages: [{ role: "system", content: sys }, ...history, { role: "user", content: u }] }) });
    const d = await r.json();
    const reply = (d.message && d.message.content) || d.error || "(empty)";
    history.push({ role: "user", content: text }, { role: "assistant", content: reply });
    const m = reply.match(/```([a-zA-Z0-9_+-]*)\n([\s\S]*?)```/);
    if (m) {
      if (!activeTab) openTab("script.py", "/home/andi/.code/script.py", "");
      cm.setValue(m[2].trim() + "\n");
      wait.textContent = reply.replace(/```[a-zA-Z0-9_+-]*\n[\s\S]*?```/g, "").trim() || "Updated active file.";
      term.textContent = "Updated from " + model + ". Press F5 or click Run / Konsole.";
    } else { wait.textContent = reply; }
  } catch (e) { wait.textContent = (e.name === "AbortError") ? "[Stopped]" : "Chat error: " + e; }
  finally { chatCtrl = null; st.style.display = "none"; sb.style.display = "inline-flex"; }
}

async function init() {
  const m = await (await fetch("/api/models")).json();
  const sel = document.getElementById("model"); sel.innerHTML = "";
  (m.models || []).forEach(x => { const o = document.createElement("option"); o.value = x.name; o.textContent = x.name; sel.appendChild(o); });
  await refreshTree();
  openTab("script.py", "/home/andi/.code/script.py", "# Ready. Open a file on the left or ask AI on the right.\n");
}
init();

// --- Resizable panes ---
function bindResize(handleId, onMove) {
  const h = document.getElementById(handleId);
  if (!h) return;
  h.addEventListener("mousedown", e => {
    e.preventDefault();
    h.classList.add("drag");
    const move = ev => { onMove(ev); cm.refresh(); };
    const up = () => { h.classList.remove("drag"); window.removeEventListener("mousemove", move); window.removeEventListener("mouseup", up); };
    window.addEventListener("mousemove", move);
    window.addEventListener("mouseup", up);
  });
}

bindResize("res-exp", e => {
  const w = Math.max(150, Math.min(520, e.clientX - 42));
  document.getElementById("explorer").style.width = w + "px";
});

bindResize("res-term", e => {
  const c = document.getElementById("center").getBoundingClientRect();
  const h = Math.max(80, Math.min(c.height - 120, c.bottom - e.clientY));
  document.getElementById("term-pane").style.height = h + "px";
});

bindResize("res-ai", e => {
  const ai = document.getElementById("ai-pane");
  if (ai.classList.contains("floating")) return;
  const w = Math.max(240, Math.min(650, window.innerWidth - e.clientX));
  ai.style.width = w + "px";
});

// --- AI Collapse / Pull-out Tab & Floating Window ---
function toggleAI() {
  const ai = document.getElementById("ai-pane");
  const res = document.getElementById("res-ai");
  const hidden = ai.classList.toggle("hidden");
  res.classList.toggle("hidden", hidden || ai.classList.contains("floating"));
  cm.refresh();
}

function setAIOpacity(val) {
  document.getElementById("ai-pane").style.opacity = val;
}

function toggleFloatAI() {
  const ai = document.getElementById("ai-pane");
  const res = document.getElementById("res-ai");
  const alpha = document.getElementById("ai-alpha");
  const floating = ai.classList.toggle("floating");
  res.classList.toggle("hidden", floating || ai.classList.contains("hidden"));
  alpha.style.display = floating ? "inline-block" : "none";
  if (!floating) {
    ai.style.opacity = "1";
    ai.style.left = ""; ai.style.top = ""; ai.style.right = ""; ai.style.height = "";
    alpha.value = "1";
  } else {
    ai.style.opacity = "0.92";
    alpha.value = "0.92";
  }
  cm.refresh();
}

// Drag floating AI window by its header
(function () {
  const hdr = document.getElementById("ai-hdr");
  const ai = document.getElementById("ai-pane");
  if (!hdr || !ai) return;
  hdr.addEventListener("mousedown", e => {
    if (!ai.classList.contains("floating") || e.target.closest("button,input")) return;
    e.preventDefault();
    const rect = ai.getBoundingClientRect();
    const dx = e.clientX - rect.left, dy = e.clientY - rect.top;
    ai.style.right = "auto";
    const move = ev => {
      ai.style.left = Math.max(0, Math.min(window.innerWidth - 120, ev.clientX - dx)) + "px";
      ai.style.top = Math.max(30, Math.min(window.innerHeight - 80, ev.clientY - dy)) + "px";
    };
    const up = () => { window.removeEventListener("mousemove", move); window.removeEventListener("mouseup", up); };
    window.addEventListener("mousemove", move);
    window.addEventListener("mouseup", up);
  });
})();

function bindResize(id, fn){const h=document.getElementById(id);if(!h)return;h.onmousedown=e=>{e.preventDefault();h.classList.add("drag");const mv=ev=>{fn(ev);cm.refresh();};const up=()=>{h.classList.remove("drag");removeEventListener("mousemove",mv);removeEventListener("mouseup",up);};addEventListener("mousemove",mv);addEventListener("mouseup",up);};}
bindResize("res-exp",e=>{document.getElementById("explorer").style.width=Math.max(150,Math.min(520,e.clientX-42))+"px";});
bindResize("res-term",e=>{const c=document.getElementById("center").getBoundingClientRect();document.getElementById("term-pane").style.height=Math.max(80,Math.min(c.height-120,c.bottom-e.clientY))+"px";});
bindResize("res-ai",e=>{const ai=document.getElementById("ai-pane");if(!ai.classList.contains("floating"))ai.style.width=Math.max(240,Math.min(650,innerWidth-e.clientX))+"px";});
function toggleAI(){const ai=document.getElementById("ai-pane"),r=document.getElementById("res-ai"),h=ai.classList.toggle("hidden");r.classList.toggle("hidden",h||ai.classList.contains("floating"));cm.refresh();}
function setAIOpacity(v){document.getElementById("ai-pane").style.opacity=v;}
function toggleFloatAI(){const ai=document.getElementById("ai-pane"),r=document.getElementById("res-ai"),a=document.getElementById("ai-alpha"),f=ai.classList.toggle("floating");r.classList.toggle("hidden",f||ai.classList.contains("hidden"));a.style.display=f?"inline-block":"none";if(!f){ai.style.opacity="1";ai.style.left=ai.style.top=ai.style.right=ai.style.height="";a.value="1";}else{ai.style.opacity="0.92";a.value="0.92";}cm.refresh();}
(()=>{const hdr=document.getElementById("ai-hdr"),ai=document.getElementById("ai-pane");if(!hdr||!ai)return;hdr.onmousedown=e=>{if(!ai.classList.contains("floating")||e.target.closest("button,input"))return;e.preventDefault();const r=ai.getBoundingClientRect(),dx=e.clientX-r.left,dy=e.clientY-r.top;ai.style.right="auto";const mv=ev=>{ai.style.left=Math.max(0,Math.min(innerWidth-120,ev.clientX-dx))+"px";ai.style.top=Math.max(30,Math.min(innerHeight-80,ev.clientY-dy))+"px";};const up=()=>{removeEventListener("mousemove",mv);removeEventListener("mouseup",up);};addEventListener("mousemove",mv);addEventListener("mouseup",up);};})();

const THEMES = {
  "default":  { name:"Dark+ (Default)",      bg:"#1e1e1e", side:"#252526", act:"#333333", bar:"#3c3c3c", bdr:"#2d2d2d", fg:"#cccccc", acc:"#007acc", grn:"#238636", red:"#c93c37" },
  "gameboy":  { name:"Game Boy DMG-01",      bg:"#0f380f", side:"#1f4f1f", act:"#0a280a", bar:"#306230", bdr:"#8bac0f", fg:"#9bbc0f", acc:"#8bac0f", grn:"#306230", red:"#6b2c2c" },
  "nes":      { name:"NES Classic",          bg:"#181818", side:"#242424", act:"#101010", bar:"#303030", bdr:"#7c7c7c", fg:"#fcfcfc", acc:"#e40058", grn:"#00a800", red:"#d82800" },
  "c64":      { name:"Commodore 64",         bg:"#352879", side:"#281d60", act:"#1f164d", bar:"#6c5eb5", bdr:"#70a4b2", fg:"#b8c76f", acc:"#6c5eb5", grn:"#588d43", red:"#883932" },
  "pico8":    { name:"PICO-8 Fantasy",       bg:"#1d2b53", side:"#141e3c", act:"#000000", bar:"#29366f", bdr:"#5f574f", fg:"#fff1e8", acc:"#ff004d", grn:"#00e436", red:"#ff004d" },
  "spectrum": { name:"ZX Spectrum",          bg:"#000000", side:"#0d0d1a", act:"#000022", bar:"#1a1a33", bdr:"#00d7d7", fg:"#d7d7d7", acc:"#d700d7", grn:"#00d700", red:"#d70000" },
  "amiga":    { name:"Amiga Workbench 1.3",  bg:"#0055aa", side:"#004488", act:"#003366", bar:"#ffffff", bdr:"#ff8800", fg:"#ffffff", acc:"#ff8800", grn:"#22aa44", red:"#cc3322" },
  "cga":      { name:"CGA Mode 4 Neon",      bg:"#000000", side:"#120018", act:"#0a0010", bar:"#260033", bdr:"#55ffff", fg:"#ffffff", acc:"#ff55ff", grn:"#55ffff", red:"#ff5555" },
  "famicom":  { name:"Famicom Gold & Red",   bg:"#201614", side:"#2e1d1a", act:"#180f0e", bar:"#7a1c1c", bdr:"#d4af37", fg:"#f5e6c8", acc:"#d4af37", grn:"#3b8c3b", red:"#b82525" }
};

const TM_KEYS = [["bg","Editor BG"],["side","Sidebar"],["bar","Menu Bar"],["fg","Text"],["acc","Accent"],["grn","Run Btn"],["bdr","Borders"],["act","Activity"]];
let curTheme = JSON.parse(localStorage.getItem("cs_theme") || "null") || { ...THEMES.default };
function applyTheme(t) {
  curTheme = { ...curTheme, ...t };
  Object.entries(curTheme).forEach(([k,v]) => { if (k !== "name") document.documentElement.style.setProperty("--" + k, v); });
  localStorage.setItem("cs_theme", JSON.stringify(curTheme));
  cm.refresh();
}
function applyPreset(key) { if (THEMES[key]) { applyTheme(THEMES[key]); renderPickers(); } }
function renderPickers() {
  const g = document.getElementById("tm-pickers"); if (!g) return; g.innerHTML = "";
  TM_KEYS.forEach(([k,lbl]) => {
    const d = document.createElement("div"); d.className = "tm-row";
    d.innerHTML = `<span>${lbl}</span><input type="color" value="${curTheme[k]||'#1e1e1e'}">`;
    d.querySelector("input").oninput = e => applyTheme({ [k]: e.target.value });
    g.appendChild(d);
  });
}
function openThemeModal() {
  const s = document.getElementById("tm-preset"); s.innerHTML = "";
  Object.entries(THEMES).forEach(([k,v]) => { const o = document.createElement("option"); o.value = k; o.textContent = v.name; s.appendChild(o); });
  renderPickers(); document.getElementById("theme-modal").classList.remove("hidden");
}
function closeThemeModal() { document.getElementById("theme-modal").classList.add("hidden"); }
applyTheme(curTheme);
