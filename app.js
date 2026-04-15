const STORAGE_KEY = "stopwatch_sequence_v1";
const DEFAULT_SEQUENCE = [18, 26, 15];

const timeDisplay = document.getElementById("timeDisplay");
const mainButton = document.getElementById("mainButton");
const resetButton = document.getElementById("resetButton");
const menuButton = document.getElementById("menuButton");
const popupMenu = document.getElementById("popupMenu");
const openSettings = document.getElementById("openSettings");
const settingsDialog = document.getElementById("settingsDialog");
const settingsForm = document.getElementById("settingsForm");
const sequenceInput = document.getElementById("sequenceInput");
const currentSequence = document.getElementById("currentSequence");
const settingsError = document.getElementById("settingsError");
const cancelSettings = document.getElementById("cancelSettings");

let running = false;
let elapsedMs = 0;
let startedAt = 0;
let timerId = null;
let stopped = false;
let frozenHundredths = null;
let stopIndex = 0;
let sequence = loadSequence();

currentSequence.textContent = sequence.join(",");
renderTime();
renderControls();
registerServiceWorker();

mainButton.addEventListener("click", () => {
  if (!running) {
    start();
  } else {
    stop();
  }
});

resetButton.addEventListener("click", reset);

menuButton.addEventListener("click", () => {
  popupMenu.hidden = !popupMenu.hidden;
});

openSettings.addEventListener("click", () => {
  popupMenu.hidden = true;
  settingsError.textContent = "";
  sequenceInput.value = sequence.join(",");
  settingsDialog.showModal();
  sequenceInput.focus();
});

document.addEventListener("click", (event) => {
  const clickedMenu = event.target.closest(".menu-wrap");
  if (!clickedMenu) {
    popupMenu.hidden = true;
  }
});

cancelSettings.addEventListener("click", () => settingsDialog.close());

settingsForm.addEventListener("submit", (event) => {
  event.preventDefault();
  const parsed = parseSequence(sequenceInput.value);

  if (!parsed.ok) {
    settingsError.textContent = parsed.error;
    return;
  }

  sequence = parsed.value;
  stopIndex = 0;
  saveSequence(sequence);
  currentSequence.textContent = sequence.join(",");
  settingsDialog.close();
  settingsError.textContent = "";

  if (stopped) {
    frozenHundredths = sequence[0];
    renderTime();
  }
});

function start() {
  startedAt = performance.now() - elapsedMs;
  running = true;
  stopped = false;
  frozenHundredths = null;
  tick();
  timerId = setInterval(tick, 20);
  renderControls();
}

function stop() {
  if (!running) {
    return;
  }

  elapsedMs = performance.now() - startedAt;
  clearInterval(timerId);
  timerId = null;
  running = false;
  stopped = true;
  frozenHundredths = sequence[stopIndex % sequence.length];
  stopIndex += 1;
  triggerStopHaptics();
  renderTime();
  renderControls();
}

function reset() {
  clearInterval(timerId);
  timerId = null;
  running = false;
  elapsedMs = 0;
  startedAt = 0;
  stopped = false;
  frozenHundredths = null;
  stopIndex = 0;
  renderTime();
  renderControls();
}

function tick() {
  elapsedMs = performance.now() - startedAt;
  renderTime();
}

function renderTime() {
  const minutes = Math.floor(elapsedMs / 60000);
  const seconds = Math.floor((elapsedMs % 60000) / 1000);
  const hundredths = Math.floor((elapsedMs % 1000) / 10);
  const shownHundredths = running || frozenHundredths === null ? hundredths : frozenHundredths;

  timeDisplay.textContent = `${pad2(minutes)}:${pad2(seconds)},${pad2(shownHundredths)}`;
}

function renderControls() {
  if (running) {
    mainButton.textContent = "Стоп";
    mainButton.classList.add("danger");
  } else if (elapsedMs > 0) {
    mainButton.textContent = "Продолж.";
    mainButton.classList.remove("danger");
  } else {
    mainButton.textContent = "Начать";
    mainButton.classList.remove("danger");
  }

  resetButton.disabled = running || elapsedMs === 0;
}

function parseSequence(rawValue) {
  const values = rawValue
    .split(/[\s,;]+/)
    .map((x) => x.trim())
    .filter(Boolean)
    .map(Number);

  if (!values.length) {
    return { ok: false, error: "Нужно указать хотя бы одно число." };
  }

  if (values.some((n) => !Number.isInteger(n) || n < 0 || n > 99)) {
    return { ok: false, error: "Каждое число должно быть целым от 0 до 99." };
  }

  return { ok: true, value: values };
}

function loadSequence() {
  try {
    const raw = localStorage.getItem(STORAGE_KEY);
    if (!raw) {
      return DEFAULT_SEQUENCE;
    }

    const parsed = JSON.parse(raw);
    if (Array.isArray(parsed) && parsed.every((n) => Number.isInteger(n) && n >= 0 && n <= 99) && parsed.length) {
      return parsed;
    }
  } catch {
    // Ignore damaged storage and use defaults.
  }

  return DEFAULT_SEQUENCE;
}

function saveSequence(value) {
  localStorage.setItem(STORAGE_KEY, JSON.stringify(value));
}

function pad2(value) {
  return String(value).padStart(2, "0");
}

function registerServiceWorker() {
  if ("serviceWorker" in navigator) {
    window.addEventListener("load", () => {
      navigator.serviceWorker.register("./sw.js").catch(() => {
        // Silently ignore registration errors in unsupported environments.
      });
    });
  }
}

function triggerStopHaptics() {
  if (!("vibrate" in navigator)) {
    return;
  }

  // A short pulse that feels like a native stopwatch stop feedback.
  navigator.vibrate(25);
}
