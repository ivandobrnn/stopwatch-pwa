const STORAGE_KEY = "calctim_sequence_v1";
const DEFAULT_SEQUENCE = [18, 26, 15];

const timeDisplay = document.getElementById("timeDisplay");
const appRoot = document.getElementById("appRoot");
const mainButton = document.getElementById("mainButton");
const leftButton = document.getElementById("leftButton");
const lapsList = document.getElementById("lapsList");
const worldClockButton = document.getElementById("worldClockButton");
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
let laps = [];

currentSequence.textContent = sequence.join(",");
renderTime();
renderControls();
renderLaps();
registerServiceWorker();
lockPortraitIfPossible();

mainButton.addEventListener("click", () => {
  if (!running) {
    start();
  } else {
    stop();
  }
});

leftButton.addEventListener("click", () => {
  if (running) {
    addLap();
    return;
  }

  if (elapsedMs > 0) {
    reset();
  }
});

worldClockButton.addEventListener("click", () => {
  settingsError.textContent = "";
  sequenceInput.value = sequence.join(",");
  settingsDialog.showModal();
  sequenceInput.focus();
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
  laps = [];
  renderTime();
  renderControls();
  renderLaps();
}

function tick() {
  elapsedMs = performance.now() - startedAt;
  renderTime();
}

function addLap() {
  if (!running) {
    return;
  }

  const total = formatTime(elapsedMs, null);
  laps.unshift({
    title: `Круг ${laps.length + 1}`,
    value: total
  });
  renderLaps();
}

function renderLaps() {
  lapsList.innerHTML = "";
  appRoot.classList.toggle("has-laps", laps.length > 0);

  laps.forEach((lap) => {
    const li = document.createElement("li");
    const name = document.createElement("span");
    const value = document.createElement("span");
    name.textContent = lap.title;
    value.textContent = lap.value;
    li.append(name, value);
    lapsList.appendChild(li);
  });
}

function renderTime() {
  const shown = running || frozenHundredths === null ? null : frozenHundredths;
  timeDisplay.textContent = formatTime(elapsedMs, shown);
}

function formatTime(ms, forcedHundredths) {
  const minutes = Math.floor(ms / 60000);
  const seconds = Math.floor((ms % 60000) / 1000);
  const hundredths = Math.floor((ms % 1000) / 10);
  const shownHundredths = forcedHundredths === null ? hundredths : forcedHundredths;

  return `${pad2(minutes)}:${pad2(seconds)},${pad2(shownHundredths)}`;
}

function renderControls() {
  if (running) {
    mainButton.textContent = "Стоп";
    mainButton.classList.remove("round-green");
    mainButton.classList.add("round-red");
    leftButton.textContent = "Круг";
  } else if (elapsedMs > 0) {
    mainButton.textContent = "Старт";
    mainButton.classList.remove("round-red");
    mainButton.classList.add("round-green");
    leftButton.textContent = "Сброс";
  } else {
    mainButton.textContent = "Старт";
    mainButton.classList.remove("round-red");
    mainButton.classList.add("round-green");
    leftButton.textContent = "Круг";
  }
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

function triggerStopHaptics() {
  if (!("vibrate" in navigator)) {
    return;
  }

  navigator.vibrate(25);
}

function pad2(value) {
  return String(value).padStart(2, "0");
}

function registerServiceWorker() {
  if ("serviceWorker" in navigator) {
    window.addEventListener("load", () => {
      navigator.serviceWorker.register("./sw.js").catch(() => {
        // Keep app usable even when SW registration fails.
      });
    });
  }
}

function lockPortraitIfPossible() {
  if (!screen.orientation || typeof screen.orientation.lock !== "function") {
    return;
  }

  screen.orientation.lock("portrait").catch(() => {
    // Some browsers only allow lock in installed/fullscreen contexts.
  });
}
