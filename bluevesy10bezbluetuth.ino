
#include <WiFi.h>
#include <WebServer.h>
#include <Preferences.h>
#include <ArduinoJson.h>
#include <esp_now.h>
#include <esp_wifi.h>
#include "HX711.h"

// ========================= SETTINGS =========================
const int BUTTON_PIN = 4;
const int LOADCELL_DOUT_PIN = 18;
const int LOADCELL_SCK_PIN = 19;
const int VIBRO_PIN = 5;
const uint8_t VIBRO_ACTIVE_LEVEL = HIGH;  // Set to HIGH for active-high motor drivers.
const uint8_t VIBRO_IDLE_LEVEL = (VIBRO_ACTIVE_LEVEL == HIGH) ? LOW : HIGH;

const char* AP_SSID = "ESP32_Vesy_Web";
const char* AP_PASS = "12345678";

float calibration_factor = 374.546;
const float filterAlpha = 0.2f;

// Receiver MAC address
uint8_t receiverAddress[] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
const uint8_t espNowChannel = 1;

// ========================= GLOBAL VARIABLES =========================
HX711 scale;
Preferences preferences;
WebServer server(80);



float smoothedWeight = 0.0f;
bool nightMode = false;

String itemsJson = "[]";
String selectedItemId = "";
int currentCount = 0;
String currentCountText = "--";
String currentEspNowText = "--";

bool touchWasPressed = false;
unsigned long touchPressStartMs = 0;
bool touchHoldHandled = false;
const unsigned long touchHoldToSwitchMs = 3000;
const unsigned long touchDebounceMs = 80;
const unsigned long touchIgnoreAfterSwitchMs = 1500;
bool touchRawState = false;
bool touchStableState = false;
unsigned long touchRawChangedMs = 0;
unsigned long touchIgnoreUntilMs = 0;
int touchIdleLevel = HIGH;

bool vibroActive = false;
unsigned long vibroUntilMs = 0;
const unsigned long vibroPulseMs = 180;
const unsigned long vibroPauseMs = 260;
bool vibroPhaseOn = false;
uint8_t vibroPulsesLeft = 0;

unsigned long lastEspNowSend = 0;

enum DeviceMode {
  MODE_NORMAL = 0,   // ESP-NOW
  MODE_WIFI_AP = 1   // web interface
};

DeviceMode currentMode = MODE_NORMAL;

// ========================= ESP-NOW MESSAGE =========================
typedef struct struct_message {
  int count;
  float weightGrams;
  char selectedId[32];
} struct_message;

struct_message outgoingData;

typedef struct __attribute__((packed)) tare_command_message {
  uint16_t magic;
  uint8_t cmd;
  uint8_t reserved;
  uint32_t nonce;
} tare_command_message;

const uint16_t TARE_CMD_MAGIC = 0xA55A;
const uint8_t TARE_CMD_ID = 1;
unsigned long lastRemoteTareMs = 0;

// ========================= HTML =========================
const char INDEX_HTML[] PROGMEM = R"rawliteral(
<!DOCTYPE html>
<html>
<head>
<meta charset="UTF-8">
<meta name="viewport" content="width=device-width, initial-scale=1, user-scalable=no">
<title>Vesy Web Pro</title>
<style>
:root { --bg: #ffffff; --text: #000000; --card: #f4f4f4; --primary: #2196F3; --sel: #d7ecff; }
body.night { --bg: #000000; --text: #ffffff; --card: #121212; --primary: #FFD700; --sel: #2b2b00; }
body { font-family: sans-serif; background: var(--bg); color: var(--text); margin: 0; padding: 15px; transition: 0.3s; }
.card { background: var(--card); border-radius: 12px; padding: 15px; margin-bottom: 10px; display: flex; justify-content: space-between; align-items: center; box-shadow: 0 2px 5px rgba(0,0,0,0.1); cursor: pointer; }
.card.selected { outline: 2px solid var(--primary); background: var(--sel); }
#display { text-align: center; padding: 20px; cursor: pointer; }
#weight { font-size: 48px; font-weight: bold; }
#count { font-size: 80px; color: var(--primary); font-weight: bold; }
.full #count { font-size: 150px; }
.full #display { position: fixed; top: 0; left: 0; width: 100%; height: 100%; background: var(--bg); z-index: 100; display: flex; flex-direction: column; justify-content: center; }
.full #list, .full #controls, .full h2, .full .btn-back { display: none; }
.btn-back { display: none; margin-top: 20px; width: 100%; }
.full .btn-back { display: block; position: fixed; bottom: 20px; left: 10%; width: 80%; z-index: 101; }
button { padding: 12px; border-radius: 8px; border: none; background: var(--primary); color: #000; font-weight: bold; cursor: pointer; }
.btn-tare { background: #607D8B; color: #fff; }
.edit-btn { background: none; color: var(--primary); font-size: 20px; padding: 6px 10px; }
#modal { display: none; position: fixed; top: 0; left: 0; width: 100%; height: 100%; background: rgba(0,0,0,0.8); z-index: 200; justify-content: center; align-items: center; }
.modal-content { background: var(--card); padding: 20px; border-radius: 15px; width: 80%; color: var(--text); }
input { width: 100%; padding: 10px; margin: 10px 0; border-radius: 5px; border: 1px solid #555; background: #222; color: #fff; box-sizing: border-box; }
.small { opacity: 0.8; font-size: 14px; }
</style>
</head>
<body id="b">
    <div id="display" onclick="toggleFull()">
        <h2 id="cur-title">Выберите товар</h2>
        <div id="weight">0.000 кг</div>
        <div id="count">--</div>
        <div id="cur-info" class="small"></div>
    </div>

    <button class="btn-back" onclick="toggleFull()">НАЗАД</button>

    <div id="controls" style="display:flex; gap:8px; margin-bottom:20px; flex-wrap:wrap;">
        <button class="btn-tare" style="flex:1" onclick="fetch('/tare')">ТАРА</button>
        <button style="flex:1" onclick="fetch('/night')">НОЧЬ/ДЕНЬ</button>
        <button style="flex:1" onclick="openModal(null, 'unit')">+ ТОВАР</button>
        <button style="flex:1" onclick="openModal(null, 'map')">+ СПИСОК</button>
        <button style="flex:1" onclick="openModal(null, 'items')">+ ПРЕДМЕТЫ</button>
    </div>

    <div id="list"></div>

    <div id="modal">
        <div class="modal-content">
            <h3 id="modal-title">Товар</h3>
            <input type="text" id="in-title" placeholder="Название">
            <div id="unit-editor">
                <input type="number" id="in-weight" placeholder="Вес (г)" step="0.01">
                <input type="number" id="in-offset" placeholder="Допуск (г)" step="0.01">
            </div>
            <div id="map-editor" style="display:none; margin-top:6px;">
                <div class="small" style="margin-bottom:6px;">Сопоставление: количество -> текст</div>
                <div id="map-rows"></div>
                <button onclick="addMapRow()" style="width:100%; background:#4CAF50; margin-top:6px">ДОБАВИТЬ СТРОКУ</button>
            </div>
            <div id="items-editor" style="display:none; margin-top:6px;">
                <div class="small" style="margin-bottom:6px;">Предметы: название + вес + допуск</div>
                <div id="items-rows"></div>
                <button onclick="addItemRow()" style="width:100%; background:#4CAF50; margin-top:6px">ДОБАВИТЬ ПРЕДМЕТ</button>
            </div>
            <button onclick="saveItem()" style="width:100%">СОХРАНИТЬ</button>
            <button onclick="closeModal()" style="width:100%; background:#f44336; margin-top:10px">ОТМЕНА</button>
        </div>
    </div>

<script>
let items = [];
let selectedId = null;
let editId = null;
let editMode = 'unit';

function toggleFull() {
    document.body.classList.toggle('full');
}

function update() {
    fetch('/data')
      .then(r => r.json())
      .then(d => {
          document.getElementById('weight').innerText = d.w.toFixed(1) + " г";
          const body = document.getElementById('b');
          body.classList.toggle('night', !!d.n);
          items = d.l || [];
          selectedId = d.s || null;
          renderList();
          renderCurrentInfo();
          document.getElementById('count').innerText = selectedId ? (d.ct || (Number.isFinite(d.c) ? (d.c + " шт") : "--")) : "--";
      })
      .catch(() => {});
}

function renderList() {
    let h = '';
    items.forEach(i => {
        const sel = selectedId === i.id ? 'selected' : '';
        const mode = i.mode || 'unit';
        const sub = mode === 'map'
            ? `${i.unitWeight} г (&plusmn;${i.offsetWeight}) • список (${(i.map || []).length})`
            : mode === 'items'
            ? `список предметов (${(i.items || []).length})`
            : `${i.unitWeight} г (&plusmn;${i.offsetWeight})`;
        h += `<div class="card ${sel}" onclick="selectItem('${i.id}')">
            <div style="text-align:left">
                <b>${i.title}</b><br>
                <small>${sub}</small>
            </div>
            <div>
                <button class="edit-btn" onclick="event.stopPropagation(); openModal('${i.id}', '${mode}')">&#9998;</button>
                <button class="edit-btn" style="color:#f44336" onclick="event.stopPropagation(); deleteItem('${i.id}')">&#10005;</button>
            </div>
        </div>`;
    });
    document.getElementById('list').innerHTML = h;
}

function renderCurrentInfo() {
    if(!selectedId) {
        document.getElementById('cur-title').innerText = 'Выберите товар';
        document.getElementById('cur-info').innerText = '';
        return;
    }

    let i = items.find(x => x.id == selectedId);
    if(i) {
        const mode = i.mode || 'unit';
        document.getElementById('cur-title').innerText = i.title;
        if (mode === 'map') {
            document.getElementById('cur-info').innerText = i.unitWeight + " г, допуск ±" + i.offsetWeight + " г, режим: список";
        } else if (mode === 'items') {
            document.getElementById('cur-info').innerText = "Список предметов: " + ((i.items || []).length) + " шт";
        } else {
            document.getElementById('cur-info').innerText = i.unitWeight + " г, допуск ±" + i.offsetWeight + " г";
        }
    } else {
        document.getElementById('cur-title').innerText = 'Выберите товар';
        document.getElementById('cur-info').innerText = '';
    }
}

function selectItem(id) {
    selectedId = id;
    fetch('/select', {
        method: 'POST',
        body: id
    }).then(() => update());
}

function clearMapRows() {
    document.getElementById('map-rows').innerHTML = '';
}

function addMapRow(count = '', text = '') {
    const row = document.createElement('div');
    row.style.display = 'flex';
    row.style.gap = '8px';
    row.style.marginBottom = '6px';
    row.innerHTML = `
        <input type="number" class="map-count" placeholder="Кол-во" step="1" style="flex:0.45">
        <input type="text" class="map-text" placeholder="Текст" style="flex:1">
        <button style="background:#f44336; min-width:42px" onclick="this.parentElement.remove()">×</button>
    `;
    document.getElementById('map-rows').appendChild(row);
    row.querySelector('.map-count').value = count;
    row.querySelector('.map-text').value = text;
}

function collectMapRows() {
    const out = [];
    document.querySelectorAll('#map-rows > div').forEach(row => {
        const c = parseInt(row.querySelector('.map-count').value, 10);
        const t = row.querySelector('.map-text').value.trim();
        if (!Number.isNaN(c) && t) out.push({ count: c, text: t });
    });
    out.sort((a, b) => a.count - b.count);
    return out;
}

function clearItemRows() {
    document.getElementById('items-rows').innerHTML = '';
}

function addItemRow(title = '', unitWeight = '', offsetWeight = '0') {
    const row = document.createElement('div');
    row.style.display = 'grid';
    row.style.gridTemplateColumns = '1fr 0.7fr 0.7fr auto';
    row.style.gap = '8px';
    row.style.marginBottom = '6px';
    row.innerHTML = `
        <input type="text" class="item-title" placeholder="Предмет">
        <input type="number" class="item-weight" placeholder="Вес" step="0.01">
        <input type="number" class="item-offset" placeholder="Допуск" step="0.01">
        <button style="background:#f44336; min-width:42px" onclick="this.parentElement.remove()">×</button>
    `;
    document.getElementById('items-rows').appendChild(row);
    row.querySelector('.item-title').value = title;
    row.querySelector('.item-weight').value = unitWeight;
    row.querySelector('.item-offset').value = offsetWeight;
}

function collectItemRows() {
    const out = [];
    document.querySelectorAll('#items-rows > div').forEach(row => {
        const title = row.querySelector('.item-title').value.trim();
        const weight = parseFloat(row.querySelector('.item-weight').value);
        const offset = parseFloat(row.querySelector('.item-offset').value);
        if (!title || isNaN(weight)) return;
        out.push({
            title,
            unitWeight: weight,
            offsetWeight: isNaN(offset) ? 0 : offset
        });
    });
    return out;
}

function openModal(id = null, mode = 'unit') {
    editId = id;
    editMode = mode || 'unit';

    const mapEditor = document.getElementById('map-editor');
    const itemsEditor = document.getElementById('items-editor');
    const unitEditor = document.getElementById('unit-editor');
    document.getElementById('modal-title').innerText = (editMode === 'map') ? 'Список' : (editMode === 'items' ? 'Список предметов' : 'Товар');
    mapEditor.style.display = (editMode === 'map') ? 'block' : 'none';
    itemsEditor.style.display = (editMode === 'items') ? 'block' : 'none';
    unitEditor.style.display = (editMode === 'items') ? 'none' : 'block';
    clearMapRows();
    clearItemRows();

    if(id) {
        let i = items.find(x => x.id == id);
        if (i) {
            editMode = i.mode || editMode || 'unit';
            document.getElementById('modal-title').innerText = (editMode === 'map') ? 'Список' : (editMode === 'items' ? 'Список предметов' : 'Товар');
            mapEditor.style.display = (editMode === 'map') ? 'block' : 'none';
            itemsEditor.style.display = (editMode === 'items') ? 'block' : 'none';
            unitEditor.style.display = (editMode === 'items') ? 'none' : 'block';
            document.getElementById('in-title').value = i.title;
            document.getElementById('in-weight').value = i.unitWeight;
            document.getElementById('in-offset').value = i.offsetWeight;
            if (editMode === 'map') {
                const arr = Array.isArray(i.map) ? i.map : [];
                if (arr.length === 0) addMapRow();
                arr.forEach(r => addMapRow(r.count, r.text));
            } else if (editMode === 'items') {
                const arr = Array.isArray(i.items) ? i.items : [];
                if (arr.length === 0) addItemRow();
                arr.forEach(r => addItemRow(r.title, r.unitWeight, r.offsetWeight));
            }
        }
    } else {
        document.getElementById('modal-title').innerText = (editMode === 'map') ? 'Список' : (editMode === 'items' ? 'Список предметов' : 'Товар');
        document.getElementById('in-title').value = '';
        document.getElementById('in-weight').value = '';
        document.getElementById('in-offset').value = '0';
        if (editMode === 'map') addMapRow();
        if (editMode === 'items') addItemRow();
    }
    document.getElementById('modal').style.display = 'flex';
}

function closeModal() {
    document.getElementById('modal').style.display = 'none';
}

function saveItem() {
    let newItem = {
        id: editId || Date.now().toString(),
        title: document.getElementById('in-title').value,
        unitWeight: parseFloat(document.getElementById('in-weight').value),
        offsetWeight: parseFloat(document.getElementById('in-offset').value),
        mode: editMode
    };

    if (!newItem.title) {
        alert('Заполните название');
        return;
    }

    if (newItem.mode === 'items') {
        newItem.items = collectItemRows();
        if (newItem.items.length === 0) {
            alert('Добавьте хотя бы один предмет (название и вес)');
            return;
        }
        newItem.unitWeight = 0;
        newItem.offsetWeight = 0;
    } else {
        if (isNaN(newItem.unitWeight)) {
            alert('Заполните вес');
            return;
        }
        if (isNaN(newItem.offsetWeight)) newItem.offsetWeight = 0;
        delete newItem.items;
    }

    if (newItem.mode === 'map') {
        newItem.map = collectMapRows();
        if (newItem.map.length === 0) {
            alert('Добавьте хотя бы одну строку сопоставления');
            return;
        }
    } else {
        delete newItem.map;
    }

    if (newItem.mode !== 'unit' && newItem.mode !== 'map' && newItem.mode !== 'items') {
        newItem.mode = 'unit';
    }

    if(editId) items = items.map(x => x.id == editId ? newItem : x);
    else items.push(newItem);

    sendList();
    closeModal();
}

function deleteItem(id) {
    if(confirm('Удалить?')) {
        items = items.filter(x => x.id != id);
        if (selectedId === id) selectedId = null;
        sendList();
    }
}

function sendList() {
    fetch('/save', {
        method: 'POST',
        body: JSON.stringify(items)
    }).then(() => {
        if (selectedId) {
            fetch('/select', { method: 'POST', body: selectedId }).then(() => update());
        } else {
            update();
        }
    });
}

setInterval(update, 1000);
update();
</script>
</body>
</html>
)rawliteral";

// ========================= HELPERS =========================
void loadSettings() {
  preferences.begin("vesy_data", true);
  itemsJson = preferences.getString("lists", "[]");
  selectedItemId = preferences.getString("selected", "");
  nightMode = preferences.getBool("night", false);
  preferences.end();
}

void saveListsToPref(const String& json) {
  preferences.begin("vesy_data", false);
  preferences.putString("lists", json);
  preferences.end();
  itemsJson = json;
}

void saveSelectedItemToPref(const String& id) {
  preferences.begin("vesy_data", false);
  preferences.putString("selected", id);
  preferences.end();
  selectedItemId = id;
}

void saveNightModeToPref(bool enabled) {
  preferences.begin("vesy_data", false);
  preferences.putBool("night", enabled);
  preferences.end();
  nightMode = enabled;
}

bool getSelectedItemData(float& unitWeight, float& offsetWeight, String& title) {
  unitWeight = 0;
  offsetWeight = 0;
  title = "";

  if (selectedItemId.isEmpty()) return false;

  DynamicJsonDocument doc(8192);
  DeserializationError err = deserializeJson(doc, itemsJson);
  if (err) return false;

  JsonArray arr = doc.as<JsonArray>();
  for (JsonObject item : arr) {
    String id = item["id"] | "";
    if (id == selectedItemId) {
      title = String((const char*)item["title"]);
      unitWeight = item["unitWeight"] | 0.0f;
      offsetWeight = item["offsetWeight"] | 0.0f;
      return (unitWeight > 0.0f);
    }
  }
  return false;
}

String getMappedTextForCount(JsonObject item, int count) {
  JsonArray mapArr = item["map"].as<JsonArray>();
  if (mapArr.isNull()) return "";

  for (JsonObject row : mapArr) {
    int rowCount = row["count"] | INT32_MIN;
    if (rowCount == count) {
      const char* text = row["text"] | "";
      return String(text);
    }
  }
  return "";
}

void calculateCount(float weightGrams) {
  currentCount = 0;
  currentCountText = "--";
  currentEspNowText = "--";

  if (selectedItemId.isEmpty()) return;

  DynamicJsonDocument doc(8192);
  if (deserializeJson(doc, itemsJson) != DeserializationError::Ok) return;

  JsonObject selected;
  for (JsonObject item : doc.as<JsonArray>()) {
    const char* idRaw = item["id"] | "";
    if (String(idRaw) == selectedItemId) {
      selected = item;
      break;
    }
  }
  if (selected.isNull()) return;

  float unitWeight = selected["unitWeight"] | 0.0f;
  float offsetWeight = selected["offsetWeight"] | 0.0f;
  const char* modeRaw = selected["mode"] | "unit";
  String mode = String(modeRaw);
  float absWeight = fabs(weightGrams);

  if (mode == "items") {
    JsonArray subjectArr = selected["items"].as<JsonArray>();
    if (subjectArr.isNull()) return;

    bool matched = false;
    float bestDiff = 1e9f;
    String bestTitle = "";
    int bestIndex = 0;

    int idx = 0;
    for (JsonObject subject : subjectArr) {
      idx++;
      float w = subject["unitWeight"] | 0.0f;
      float off = subject["offsetWeight"] | 0.0f;
      const char* t = subject["title"] | "";
      if (w <= 0.0f || strlen(t) == 0) continue;

      if (absWeight >= (w - off) && absWeight <= (w + off)) {
        float diff = fabs(absWeight - w);
        if (!matched || diff < bestDiff) {
          matched = true;
          bestDiff = diff;
          bestTitle = String(t);
          bestIndex = idx;
        }
      }
    }

    if (!matched) return;

    currentCount = bestIndex;
    currentCountText = bestTitle;
    currentEspNowText = bestTitle;
    return;
  }

  if (unitWeight <= 0.0f) return;

  // Symmetric interval logic for + and - weights:
  // n when abs(weight) is inside [n*unitWeight - offsetWeight, n*unitWeight + offsetWeight].
  int n = (int)roundf(absWeight / unitWeight);
  if (n < 0) n = 0;

  float expected = n * unitWeight;
  float lowerBound = expected - offsetWeight;
  float upperBound = expected + offsetWeight;

  if (absWeight >= lowerBound && absWeight <= upperBound) {
    currentCount = n;
  } else {
    currentCount = 0;
  }

  if (mode == "map") {
    String mapped = getMappedTextForCount(selected, currentCount);
    if (!mapped.isEmpty()) {
      currentCountText = mapped;
      currentEspNowText = mapped;
    } else {
      currentCountText = String(currentCount);
      currentEspNowText = String(currentCount);
    }
  } else {
    currentCountText = String(currentCount) + " шт";
    currentEspNowText = String(currentCount);
  }
}

// ========================= ESP-NOW MESSAGE =========================
// Updated for ESP32 core 3.3.x
void onEspNowSent(const wifi_tx_info_t* info, esp_now_send_status_t status) {
  Serial.print("ESP-NOW send status: ");
  Serial.println(status == ESP_NOW_SEND_SUCCESS ? "OK" : "FAIL");
}

void onEspNowRecv(const esp_now_recv_info_t* info, const uint8_t* data, int len) {
  (void)info;
  if (data == nullptr || len != (int)sizeof(tare_command_message)) return;

  tare_command_message cmd = {};
  memcpy(&cmd, data, sizeof(cmd));

  if (cmd.magic != TARE_CMD_MAGIC || cmd.cmd != TARE_CMD_ID) return;
  if (currentMode != MODE_NORMAL) return;

  const unsigned long now = millis();
  if (now - lastRemoteTareMs < 800) return;  // prevent repeated tare from bouncing/retry bursts
  lastRemoteTareMs = now;

  scale.tare();
  startVibrationPattern(3);
  Serial.println("Remote TARE via ESP-NOW");
}

bool initEspNow() {
  WiFi.mode(WIFI_STA);
  WiFi.setSleep(false);
  WiFi.disconnect();
  esp_wifi_set_promiscuous(true);
  esp_wifi_set_channel(espNowChannel, WIFI_SECOND_CHAN_NONE);
  esp_wifi_set_promiscuous(false);

  if (esp_now_init() != ESP_OK) {
    Serial.println("ESP-NOW init failed");
    return false;
  }

  esp_now_register_send_cb(onEspNowSent);
  esp_now_register_recv_cb(onEspNowRecv);

  esp_now_peer_info_t peerInfo = {};
  memcpy(peerInfo.peer_addr, receiverAddress, 6);
  peerInfo.channel = espNowChannel;
  peerInfo.encrypt = false;

  if (esp_now_add_peer(&peerInfo) != ESP_OK) {
    Serial.println("Failed to add ESP-NOW peer");
    return false;
  }

  Serial.println("ESP-NOW ready");
  return true;
}

void deinitEspNow() {
  esp_now_deinit();
}

void sendCountESPNow() {
  memset(&outgoingData, 0, sizeof(outgoingData));
  outgoingData.count = currentCount;
  outgoingData.weightGrams = smoothedWeight;
  currentEspNowText.toCharArray(outgoingData.selectedId, sizeof(outgoingData.selectedId));

  esp_err_t result = esp_now_send(receiverAddress, (uint8_t*)&outgoingData, sizeof(outgoingData));
  if (result != ESP_OK) {
    Serial.print("ESP-NOW send error: ");
    Serial.println(result);
  }
}

// ========================= WEB =========================
void setupServerRoutes() {
  server.on("/", []() {
    server.send(200, "text/html; charset=UTF-8", INDEX_HTML);
  });

  server.on("/data", []() {
    String safeText = currentCountText;
    safeText.replace("\\", "\\\\");
    safeText.replace("\"", "\\\"");
    String json = "{\"w\":" + String(smoothedWeight, 1) +
                  ",\"n\":" + String(nightMode ? "true" : "false") +
                  ",\"c\":" + String(currentCount) +
                  ",\"ct\":\"" + safeText + "\"" +
                  ",\"s\":\"" + selectedItemId + "\"," +
                  "\"l\":" + itemsJson + "}";
    server.send(200, "application/json", json);
  });

  server.on("/save", HTTP_POST, []() {
    String postBody = server.arg("plain");
    if (postBody.length() == 0) {
      server.send(400, "text/plain", "Empty body");
      return;
    }

    saveListsToPref(postBody);

    bool found = false;
    DynamicJsonDocument doc(8192);
    if (deserializeJson(doc, itemsJson) == DeserializationError::Ok) {
      JsonArray arr = doc.as<JsonArray>();
      for (JsonObject item : arr) {
        String id = item["id"] | "";
        if (id == selectedItemId) {
          found = true;
          break;
        }
      }
    }

    if (!found) {
      saveSelectedItemToPref("");
    }

    server.send(200, "text/plain", "OK");
  });

  server.on("/select", HTTP_POST, []() {
    String id = server.arg("plain");
    saveSelectedItemToPref(id);
    server.send(200, "text/plain", "OK");
  });

  server.on("/tare", []() {
    scale.tare();
    server.send(200, "text/plain", "OK");
  });

  server.on("/night", []() {
    saveNightModeToPref(!nightMode);
    server.send(200, "text/plain", "OK");
  });
}

void startWiFiAPMode() {
  WiFi.mode(WIFI_AP);
  delay(100);
  WiFi.softAP(AP_SSID, AP_PASS);
  delay(100);
  server.begin();

  Serial.println("WiFi AP started");
  Serial.print("AP IP: ");
  Serial.println(WiFi.softAPIP());
}

void stopWiFiAPMode() {
  server.stop();
  WiFi.softAPdisconnect(true);
  WiFi.mode(WIFI_OFF);
  Serial.println("WiFi AP stopped");
}

// ========================= MODE SWITCHING =========================
void startVibration(unsigned long durationMs) {
  vibroActive = true;
  vibroPhaseOn = true;
  vibroPulsesLeft = 1;
  vibroUntilMs = millis() + durationMs;
  digitalWrite(VIBRO_PIN, VIBRO_ACTIVE_LEVEL);
}

void startVibrationPattern(uint8_t pulses) {
  if (pulses == 0) {
    vibroActive = false;
    vibroPhaseOn = false;
    vibroPulsesLeft = 0;
    digitalWrite(VIBRO_PIN, VIBRO_IDLE_LEVEL);
    return;
  }

  vibroActive = true;
  vibroPhaseOn = true;
  vibroPulsesLeft = pulses;
  vibroUntilMs = millis() + vibroPulseMs;
  digitalWrite(VIBRO_PIN, VIBRO_ACTIVE_LEVEL);
}

void handleVibration() {
  if (!vibroActive || (long)(millis() - vibroUntilMs) < 0) {
    return;
  }

  if (vibroPhaseOn) {
    digitalWrite(VIBRO_PIN, VIBRO_IDLE_LEVEL);
    vibroPhaseOn = false;
    vibroUntilMs = millis() + vibroPauseMs;
    return;
  }

  if (vibroPulsesLeft > 0) {
    vibroPulsesLeft--;
  }

  if (vibroPulsesLeft > 0) {
    digitalWrite(VIBRO_PIN, VIBRO_ACTIVE_LEVEL);
    vibroPhaseOn = true;
    vibroUntilMs = millis() + vibroPulseMs;
  } else {
    vibroActive = false;
    digitalWrite(VIBRO_PIN, VIBRO_IDLE_LEVEL);
  }
}

void switchToNextMode() {
  if (currentMode == MODE_NORMAL) {
    setMode(MODE_WIFI_AP);
  } else {
    setMode(MODE_NORMAL);
  }
}

void setMode(DeviceMode newMode) {
  if (currentMode == newMode) return;

  if (currentMode == MODE_WIFI_AP) {
    stopWiFiAPMode();
  } else if (currentMode == MODE_NORMAL) {
    deinitEspNow();
  }

  delay(200);

  if (newMode == MODE_NORMAL) {
    initEspNow();
    Serial.println("Mode -> NORMAL (ESP-NOW)");
  }
  else if (newMode == MODE_WIFI_AP) {
    startWiFiAPMode();
    Serial.println("Mode -> WIFI AP");
  }

  currentMode = newMode;
  uint8_t vibroPulseCount = 1;
  if (newMode == MODE_WIFI_AP) {
    vibroPulseCount = 2;
  }
  startVibrationPattern(vibroPulseCount);
}

void handleButtonModeSwitch() {
  const unsigned long now = millis();

  if ((long)(now - touchIgnoreUntilMs) < 0) {
    return;
  }

  int rawLevel = digitalRead(BUTTON_PIN);
  bool touchedRaw = (rawLevel != touchIdleLevel);

  static unsigned long lastLogMs = 0;
  if (now - lastLogMs >= 500) {
    lastLogMs = now;
    Serial.print("BUTTON_PIN=");
    Serial.print(rawLevel);
    Serial.print(" touched=");
    Serial.println(touchedRaw ? 1 : 0);
  }

  if (touchedRaw != touchRawState) {
    touchRawState = touchedRaw;
    touchRawChangedMs = now;
  }

  if ((now - touchRawChangedMs) >= touchDebounceMs && touchStableState != touchRawState) {
    touchStableState = touchRawState;
  }

  if (touchStableState && !touchWasPressed) {
    touchPressStartMs = now;
    touchHoldHandled = false;
  }

  if (touchStableState && !touchHoldHandled) {
    if (now - touchPressStartMs >= touchHoldToSwitchMs) {
      touchHoldHandled = true;
      touchIgnoreUntilMs = now + touchIgnoreAfterSwitchMs;
      switchToNextMode();  // Includes automatic mode-specific vibration pattern.
    }
  }

  if (!touchStableState) {
    touchHoldHandled = false;
  }

  touchWasPressed = touchStableState;
}

// ========================= SETUP =========================
void setup() {
  Serial.begin(115200);

  pinMode(BUTTON_PIN, INPUT);
  pinMode(VIBRO_PIN, OUTPUT);
  digitalWrite(VIBRO_PIN, VIBRO_IDLE_LEVEL);

  int highCount = 0;
  int lowCount = 0;
  for (int i = 0; i < 40; i++) {
    int v = digitalRead(BUTTON_PIN);
    if (v == HIGH) highCount++;
    else lowCount++;
    delay(10);
  }
  touchIdleLevel = (highCount >= lowCount) ? HIGH : LOW;
  Serial.print("Touch idle level=");
  Serial.println(touchIdleLevel);

  loadSettings();

  scale.begin(LOADCELL_DOUT_PIN, LOADCELL_SCK_PIN);
  scale.set_scale(calibration_factor);
  scale.tare();
  setupServerRoutes();
  currentMode = MODE_WIFI_AP; // force setMode() transition
  setMode(MODE_NORMAL);
}

// ========================= LOOP =========================
void loop() {
  handleButtonModeSwitch();
  handleVibration();

  if (scale.is_ready()) {
    // Keep raw units here; calibration_factor must match desired output units.
    // If units are off (grams vs kg), adjust calibration accordingly.
   float raw = scale.get_units(1);

    smoothedWeight = (raw * filterAlpha) + (smoothedWeight * (1.0f - filterAlpha));

    if (fabs(smoothedWeight) < 0.001f) {
      smoothedWeight = 0.0f;
    }

    calculateCount(smoothedWeight);

    if (currentMode == MODE_NORMAL) {
      if (millis() - lastEspNowSend >= 500) {
        lastEspNowSend = millis();
        sendCountESPNow();
      }
    }
  }

  if (currentMode == MODE_WIFI_AP) {
    server.handleClient();
  }

  delay(50);
}










