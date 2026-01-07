import createModule from "./sv_engine.js";

const el = (id) => document.getElementById(id);

const ui = {
  archSelect: el("arch_select"),
  examples: el("examples"),
  run: el("run"),
  newCode: el("new_code"),
  status: el("status"),

  code: el("code"),

  out: el("out"),
  k_sp: el("k_sp"),
  v_sp: el("v_sp"),
  k_fp: el("k_fp"),
  v_fp: el("v_fp"),
  v_instr: el("v_instr"),

  instrTable: el("instrTable"),
  stackTable: el("stackTable"),
  regTable: el("regTable"),

  autoscrollInstr: el("autoscroll_instr"),
  relativeSp: el("relative_sp"),

  supportedText: el("supported_text"),

  prevBtn: el("prev_btn"),
  nextBtn: el("next_btn"),
  resetBtn: el("reset_btn"),
  step: el("step"),
  stepMax: el("stepMax"),
};

let simulate = null;
let lastResult = null;
let stepIndex = 0;
let visibleInstructions = []; // {line, addr, text}
let lastExecutedRow = -1;

let arch = "arm64"; // "arm64" | "x86_64"

const SUPPORTED = {
  arm64: `ARM64 (subset)
- stp x29, x30, [sp, #-16]!
- mov x29, sp
- sub sp, sp, #imm
- mov wN, #imm        (N = any w register name you type, like w0/w1/w2)
- str wN, [x29, #off] (off can be negative, like #-4)
- ret

Notes:
- stack frame base uses FP (x29)
- stores are shown as slots relative to FP (and optionally SP in UI)
`,
  x86_64: `x86_64 (subset)
- push rbp
- mov rbp, rsp
- sub rsp, imm
- mov dword ptr [rbp-off], imm
- leave
- ret

Notes:
- locals are modeled as 4-byte slots at [rbp - off]
- only immediate-to-stack stores are supported in this teaching subset
`,
};


// ---------------------
// helpers
// ---------------------
function setStatus(text) {
  ui.status.textContent = text;
}

function clearTbody(tbody) {
  while (tbody.firstChild) tbody.removeChild(tbody.firstChild);
}

function clamp(n, lo, hi) {
  return Math.max(lo, Math.min(hi, n));
}

function hexOrDash(x) {
  if (x == null) return "-";
  return String(x);
}

function normalizeLine(s) {
  return s.replace(/\/\/.*$/g, "").replace(/;.*$/g, "").trim();
}

function parseHexStr(s) {
  if (!s) return null;
  const t = String(s).trim();
  if (!t.startsWith("0x")) return null;
  const v = Number.parseInt(t, 16);
  return Number.isFinite(v) ? v : null;
}

function fmtRel(off) {
  if (off === 0) return "sp";
  const sign = off < 0 ? "-" : "+";
  const mag = Math.abs(off);
  return `sp ${sign} 0x${mag.toString(16)}`;
}

function setArch(newArch) {
  arch = newArch;

  if (arch === "arm64") {
    ui.k_fp.textContent = "FP";
    ui.k_sp.textContent = "SP";
    if (ui.supportedText) ui.supportedText.textContent = SUPPORTED.arm64;
  } else {
    ui.k_fp.textContent = "RBP";
    ui.k_sp.textContent = "RSP";
    if (ui.supportedText) ui.supportedText.textContent = SUPPORTED.x86_64;
  }
}


// map engine regs to arch naming for display
function getArchRegs(step) {
  const r = step?.regs ?? {};
  if (arch === "arm64") return { PC: r.rip, SP: r.rsp, FP: r.rbp };
  return { RIP: r.rip, RSP: r.rsp, RBP: r.rbp };
}

// visible instruction table from assembly
function buildVisibleInstructionsFromAsm(code) {
  const lines = code.split("\n").map(normalizeLine).filter(Boolean);

  const base = arch === "arm64" ? 0x108760 : 0x401000;
  const step = arch === "arm64" ? 4 : 1;

  let addr = base;
  return lines.map((text, idx) => {
    const row = { line: idx + 1, addr: `0x${addr.toString(16)}`, text };
    addr += step;
    return row;
  });
}

// ---------------------
// Rendering
// ---------------------
function renderInstructionsPanel(activeIndex) {
  clearTbody(ui.instrTable);

  visibleInstructions.forEach((row, idx) => {
    const tr = document.createElement("tr");
    if (idx === activeIndex) tr.classList.add("active");
    if (idx === lastExecutedRow) tr.classList.add("exec");

    const tdLine = document.createElement("td");
    tdLine.textContent = row.line;

    const tdAddr = document.createElement("td");
    tdAddr.textContent = row.addr;

    const tdText = document.createElement("td");
    tdText.textContent = row.text;

    tr.appendChild(tdLine);
    tr.appendChild(tdAddr);
    tr.appendChild(tdText);
    ui.instrTable.appendChild(tr);

    if (idx === activeIndex && ui.autoscrollInstr.checked) {
      setTimeout(() => tr.scrollIntoView({ block: "center" }), 0);
    }
  });
}

function renderStackPanel(step) {
  clearTbody(ui.stackTable);

  const slots = step?.slots ?? [];
  const writes = step?.writes ?? [];
  const writeAddrs = new Set(writes.map((w) => String(w.addr)));

  const regs = step?.regs ?? {};
  const spStr = regs.rsp ? String(regs.rsp) : null;
  const spNum = parseHexStr(spStr);

  const relative = !!ui.relativeSp?.checked;

  slots.forEach((sl) => {
    const tr = document.createElement("tr");

    // ✅ column 1: FP/SP arrow marker (uses engine flags if present)
    const tdMark = document.createElement("td");
    const mark = document.createElement("span");
    mark.className = "marker";

    if (sl.is_fp) {
      mark.classList.add("fp");
      mark.textContent = "▶";
    } else if (sl.is_sp) {
      mark.classList.add("sp");
      mark.textContent = "▶";
    } else {
      mark.textContent = "";
    }
    tdMark.appendChild(mark);

    // column 2: current/wrote dot
    const tdDot = document.createElement("td");
    const dot = document.createElement("span");
    dot.className = "dot";
    if (writeAddrs.has(String(sl.addr))) dot.classList.add("exec");
    tdDot.appendChild(dot);

    // address column (absolute or relative to sp)
    const tdAddr = document.createElement("td");
    if (relative && spNum != null) {
      const aNum = parseHexStr(sl.addr);
      if (aNum != null) tdAddr.textContent = fmtRel(aNum - spNum);
      else tdAddr.textContent = sl.addr ?? "";
    } else {
      tdAddr.textContent = sl.addr ?? "";
    }

    const tdOff = document.createElement("td");
    if (sl.off != null) {
      tdOff.textContent = String(sl.off);
    } else {
      // fallback compute from addr - fp
      const fpStr = regs.rbp ? String(regs.rbp) : null;
      const fpNum = parseHexStr(fpStr);
      const aNum = parseHexStr(sl.addr);
      if (fpNum != null && aNum != null) tdOff.textContent = String(aNum - fpNum);
      else tdOff.textContent = "";
    }

    const tdVal = document.createElement("td");
    tdVal.textContent = sl.value ?? "";

    const tdLab = document.createElement("td");
    tdLab.textContent = sl.label ?? "";

    tr.appendChild(tdMark);
    tr.appendChild(tdDot);
    tr.appendChild(tdAddr);
    tr.appendChild(tdOff);
    tr.appendChild(tdVal);
    tr.appendChild(tdLab);

    ui.stackTable.appendChild(tr);
  });
}

function renderRegsPanel(step, prevStep) {
  clearTbody(ui.regTable);

  const rNow = getArchRegs(step);
  const rPrev = getArchRegs(prevStep);

  for (const [k, v] of Object.entries(rNow)) {
    const tr = document.createElement("tr");
    const tdK = document.createElement("td");
    tdK.textContent = k;

    const tdV = document.createElement("td");
    tdV.textContent = hexOrDash(v);

    if (prevStep) {
      const prevVal = rPrev[k];
      if (String(prevVal ?? "") !== String(v ?? "")) tdV.classList.add("reg-changed");
    }

    tr.appendChild(tdK);
    tr.appendChild(tdV);
    ui.regTable.appendChild(tr);
  }
}

function renderSide(step) {
  const s = step ?? null;
  ui.v_instr.textContent = s?.instr ?? "-";

  const r = getArchRegs(s);

  if (arch === "arm64") {
    ui.v_fp.textContent = hexOrDash(r.FP);
    ui.v_sp.textContent = hexOrDash(r.SP);
  } else {
    ui.v_fp.textContent = hexOrDash(r.RBP);
    ui.v_sp.textContent = hexOrDash(r.RSP);
  }
}

function updateBottomControls() {
  const hasSteps = !!(lastResult?.steps?.length);
  ui.prevBtn.disabled = !hasSteps || stepIndex === 0;
  ui.nextBtn.disabled = !hasSteps || stepIndex >= (lastResult.steps.length - 1);
  ui.resetBtn.disabled = !hasSteps || stepIndex === 0;
}

function renderStep(i) {
  if (!lastResult || !Array.isArray(lastResult.steps) || lastResult.steps.length === 0) {
    ui.stepMax.textContent = "0";
    ui.step.value = "0";
    renderInstructionsPanel(-1);
    clearTbody(ui.stackTable);
    clearTbody(ui.regTable);
    renderSide(null);
    updateBottomControls();
    return;
  }

  stepIndex = clamp(i, 0, lastResult.steps.length - 1);
  ui.step.value = String(stepIndex);
  ui.stepMax.textContent = String(lastResult.steps.length - 1);

  const step = lastResult.steps[stepIndex];
  const prevStep = stepIndex > 0 ? lastResult.steps[stepIndex - 1] : null;

  // align visible instruction text to step index (entry is step 0)
  const vis = visibleInstructions[stepIndex - 1];
  if (stepIndex === 0) step.instr = "entry";
  else step.instr = vis?.text ?? step.instr;

  renderSide(step);
  renderInstructionsPanel(stepIndex - 1);
  renderStackPanel(step);
  renderRegsPanel(step, prevStep);

  updateBottomControls();
}

// ---------------------
// Run
// ---------------------
async function run() {
  if (!simulate) return;

  const code = ui.code.value ?? "";
  setStatus("running...");

  try {
    visibleInstructions = buildVisibleInstructionsFromAsm(code);

    const jsonStr = simulate(code, 0);
    ui.out.textContent = jsonStr;

    lastResult = JSON.parse(jsonStr);

    if (!lastResult.ok) setStatus(`error: ${lastResult.message || "unknown"}`);
    else setStatus("ok");

    lastExecutedRow = -1;
    renderStep(0);
  } catch (e) {
    console.error(e);
    setStatus("error (see console)");
  }
}

// ---------------------
// Examples
// ---------------------
const EXAMPLES = {
  // -----------------
  // ARM64 (5)
  // -----------------
  arm64_overwrite: `stp x29, x30, [sp, #-16]!
mov x29, sp
sub sp, sp, #0x20
mov w0, #1
mov w1, #2
str w0, [x29, #-4]
str w1, [x29, #-4]
ret
`,

  arm64_three_locals: `stp x29, x30, [sp, #-16]!
mov x29, sp
sub sp, sp, #0x20
mov w0, #10
mov w1, #20
mov w2, #30
str w0, [x29, #-4]
str w1, [x29, #-8]
str w2, [x29, #-12]
ret
`,

  arm64_saved_lr_fp_and_locals: `stp x29, x30, [sp, #-16]!
mov x29, sp
sub sp, sp, #0x10
mov w0, #0x7
mov w1, #0x9
str w0, [x29, #-4]
str w1, [x29, #-8]
ret
`,

  arm64_big_stack_frame: `stp x29, x30, [sp, #-16]!
mov x29, sp
sub sp, sp, #0x30
mov w0, #0x11
mov w1, #0x22
mov w2, #0x33
str w0, [x29, #-4]
str w1, [x29, #-16]
str w2, [x29, #-28]
ret
`,

  arm64_sparse_offsets: `stp x29, x30, [sp, #-16]!
mov x29, sp
sub sp, sp, #0x40
mov w0, #5
mov w1, #6
mov w2, #7
str w0, [x29, #-4]
str w1, [x29, #-16]
str w2, [x29, #-32]
ret
`,

  // -----------------
  // x86_64 (5)
  // -----------------
  x64_three_locals: `push rbp
mov rbp, rsp
sub rsp, 0x30
mov dword ptr [rbp-4], 10
mov dword ptr [rbp-8], 20
mov dword ptr [rbp-12], 30
leave
ret
`,

  x64_overwrite: `push rbp
mov rbp, rsp
sub rsp, 0x20
mov dword ptr [rbp-4], 0x11111111
mov dword ptr [rbp-4], 0x22222222
leave
ret
`,

  x64_big_stack_frame: `push rbp
mov rbp, rsp
sub rsp, 0x40
mov dword ptr [rbp-4], 1
mov dword ptr [rbp-8], 2
mov dword ptr [rbp-12], 3
mov dword ptr [rbp-16], 4
leave
ret
`,

  x64_sparse_offsets: `push rbp
mov rbp, rsp
sub rsp, 0x60
mov dword ptr [rbp-4], 0xaa
mov dword ptr [rbp-0x20], 0xbb
mov dword ptr [rbp-0x3c], 0xcc
leave
ret
`,

  x64_many_locals: `push rbp
mov rbp, rsp
sub rsp, 0x50
mov dword ptr [rbp-4], 11
mov dword ptr [rbp-8], 22
mov dword ptr [rbp-12], 33
mov dword ptr [rbp-16], 44
mov dword ptr [rbp-20], 55
leave
ret
`,
};


function applyExample(key) {
  if (!key || !EXAMPLES[key]) return;
  ui.code.value = EXAMPLES[key];

  if (key.startsWith("arm64")) {
    ui.archSelect.value = "arm64";
    setArch("arm64");
  } else {
    ui.archSelect.value = "x86_64";
    setArch("x86_64");
  }
}


// ---------------------
// boot
// ---------------------
(async () => {
  setStatus("loading wasm...");
  setArch("arm64");

  const mod = await createModule();
  simulate = mod.cwrap("simulate", "string", ["string", "number"]);

  applyExample("arm64_overwrite");

  if (window.location.hash === "#demo") {
    await run();
  } else {
    setStatus("ready");
  }

  ui.archSelect.onchange = () => {
    setArch(ui.archSelect.value);
    // rebuild instruction addresses with new arch base/step next time you run
  };

  ui.examples.onchange = () => applyExample(ui.examples.value);

  ui.newCode.onclick = () => {
    ui.code.value = "";
    lastResult = null;
    visibleInstructions = [];
    lastExecutedRow = -1;
    renderStep(0);
    setStatus("ready");
  };

  ui.run.onclick = () => run();

  // ✅ prev/next/reset
  ui.prevBtn.onclick = () => {
    lastExecutedRow = stepIndex - 1;
    renderStep(stepIndex - 1);
  };

  ui.nextBtn.onclick = () => {
    lastExecutedRow = stepIndex - 1;
    renderStep(stepIndex + 1);
  };

  ui.resetBtn.onclick = () => {
    lastExecutedRow = -1;
    renderStep(0);
  };

  ui.step.onchange = () => {
    const v = Number(ui.step.value);
    if (Number.isFinite(v)) renderStep(v);
  };

  ui.relativeSp.onchange = () => {
    renderStep(stepIndex);
  };

  ui.autoscrollInstr.onchange = () => {
    renderStep(stepIndex);
  };
})();
