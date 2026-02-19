const STORAGE_KEYS = {
  brokerWsUrl: "dmq_broker_url",
  mqttUsername: "dmq_user",
  topicCmd: "dmq_topic_cmd",
  topicState: "dmq_topic_state",
};

const brokerUrlEl = document.getElementById("broker-url");
const mqttUsernameEl = document.getElementById("mqtt-username");
const mqttPasswordEl = document.getElementById("mqtt-password");
const topicCmdEl = document.getElementById("topic-cmd");
const topicStateEl = document.getElementById("topic-state");

const saveSettingsBtnEl = document.getElementById("save-settings");
const connectBtnEl = document.getElementById("connect-mqtt");
const disconnectBtnEl = document.getElementById("disconnect-mqtt");

const statusEl = document.getElementById("status");
const relay1CardEl = document.getElementById("relay1-card");
const relay2CardEl = document.getElementById("relay2-card");
const relay1StateEl = document.getElementById("relay1-state");
const relay2StateEl = document.getElementById("relay2-state");
const servoStateEl = document.getElementById("servo-state");
const servoSliderEl = document.getElementById("servo-slider");
const servoSendBtnEl = document.getElementById("servo-send");

let mqttClient = null;
let mqttConnected = false;
let servoPublishTimer = null;

const state = {
  relay1: 0,
  relay2: 0,
  servo: 90,
};

function setStatus(text) {
  statusEl.textContent = text;
}

function renderRelay(cardEl, stateEl, relayValue) {
  const isOn = relayValue === 1;
  stateEl.textContent = `State: ${isOn ? "ON (1)" : "OFF (0)"}`;
  cardEl.classList.toggle("active", isOn);
}

function clampServoAngle(value) {
  return Math.max(0, Math.min(180, value));
}

function renderServo() {
  const angle = clampServoAngle(Number(state.servo));
  state.servo = angle;

  servoStateEl.textContent = `Position: ${angle} deg`;
  if (Number(servoSliderEl.value) !== angle) {
    servoSliderEl.value = String(angle);
  }
}

function renderState() {
  renderRelay(relay1CardEl, relay1StateEl, state.relay1);
  renderRelay(relay2CardEl, relay2StateEl, state.relay2);
  renderServo();
}

function parseStatePayload(payloadText) {
  const parts = payloadText.trim().split(",");
  if (!(parts.length === 2 || parts.length === 3)) return null;

  const relay1 = Number(parts[0]);
  const relay2 = Number(parts[1]);

  if (!Number.isInteger(relay1) || !Number.isInteger(relay2)) return null;
  if (!([0, 1].includes(relay1) && [0, 1].includes(relay2))) return null;

  let servo = null;
  if (parts.length === 3) {
    const nextServo = Number(parts[2]);
    if (!Number.isInteger(nextServo)) return null;
    if (nextServo < 0 || nextServo > 180) return null;
    servo = nextServo;
  }

  return { relay1, relay2, servo };
}

function normalizeBrokerUrl(rawUrl) {
  let value = rawUrl.trim();
  if (!value) {
    throw new Error("Broker WebSocket URL is required.");
  }

  if (!/^wss?:\/\//i.test(value)) {
    value = `wss://${value}`;
  }

  const parsed = new URL(value);

  if (!(parsed.protocol === "ws:" || parsed.protocol === "wss:")) {
    throw new Error("Broker URL must use ws:// or wss://");
  }

  if (parsed.hostname.endsWith(".hivemq.cloud")) {
    if (!parsed.port) {
      parsed.port = parsed.protocol === "wss:" ? "8884" : "8000";
    }

    if (!parsed.pathname || parsed.pathname === "/") {
      parsed.pathname = "/mqtt";
    }
  }

  return parsed.toString();
}

function saveSettings() {
  localStorage.setItem(STORAGE_KEYS.brokerWsUrl, brokerUrlEl.value.trim());
  localStorage.setItem(STORAGE_KEYS.mqttUsername, mqttUsernameEl.value.trim());
  localStorage.setItem(STORAGE_KEYS.topicCmd, topicCmdEl.value.trim());
  localStorage.setItem(STORAGE_KEYS.topicState, topicStateEl.value.trim());
  setStatus("Settings saved in browser.");
}

function disconnectMqtt(showMessage = true) {
  if (servoPublishTimer) {
    window.clearTimeout(servoPublishTimer);
    servoPublishTimer = null;
  }

  if (mqttClient) {
    mqttClient.end(true);
    mqttClient = null;
  }

  mqttConnected = false;
  if (showMessage) {
    setStatus("MQTT disconnected.");
  }
}

function connectMqtt() {
  const topicCmd = topicCmdEl.value.trim();
  const topicState = topicStateEl.value.trim();

  if (!topicCmd || !topicState) {
    setStatus("Command and state topic are required.");
    return;
  }

  let brokerUrl;
  try {
    brokerUrl = normalizeBrokerUrl(brokerUrlEl.value);
  } catch (error) {
    setStatus(error.message);
    return;
  }

  brokerUrlEl.value = brokerUrl;
  saveSettings();
  disconnectMqtt(false);

  const options = {
    clean: true,
    reconnectPeriod: 2000,
    connectTimeout: 10000,
    clientId: `web-control-${Math.random().toString(16).slice(2, 10)}`,
  };

  const username = mqttUsernameEl.value.trim();
  const password = mqttPasswordEl.value;
  if (username) {
    options.username = username;
    options.password = password;
  }

  setStatus("Connecting MQTT...");
  mqttClient = window.mqtt.connect(brokerUrl, options);

  mqttClient.on("connect", () => {
    mqttConnected = true;

    mqttClient.subscribe(topicState, (error) => {
      if (error) {
        setStatus(`Subscribe failed: ${error.message || String(error)}`);
        return;
      }

      setStatus(`MQTT connected. Listening on ${topicState}`);
    });
  });

  mqttClient.on("message", (topic, payloadBuffer) => {
    if (topic !== topicState) return;

    const payload = payloadBuffer.toString("utf8");
    const parsed = parseStatePayload(payload);

    if (!parsed) {
      setStatus(`Invalid state payload: ${payload}`);
      return;
    }

    state.relay1 = parsed.relay1;
    state.relay2 = parsed.relay2;
    if (parsed.servo !== null) {
      state.servo = parsed.servo;
    }

    renderState();
    setStatus(`State received: ${payload}`);
  });

  mqttClient.on("reconnect", () => {
    setStatus("MQTT reconnecting...");
  });

  mqttClient.on("close", () => {
    mqttConnected = false;
    setStatus("MQTT connection closed.");
  });

  mqttClient.on("error", (error) => {
    mqttConnected = false;
    setStatus(`MQTT error: ${error.message || String(error)}`);
  });
}

function publishCommand() {
  if (!mqttClient || !mqttConnected) {
    throw new Error("MQTT not connected.");
  }

  const topicCmd = topicCmdEl.value.trim();
  const payload = `${state.relay1},${state.relay2},${state.servo}`;

  mqttClient.publish(topicCmd, payload, { retain: false });
  setStatus(`Command published: ${payload}`);
}

function queueServoPublish() {
  if (servoPublishTimer) {
    window.clearTimeout(servoPublishTimer);
  }

  servoPublishTimer = window.setTimeout(() => {
    servoPublishTimer = null;

    try {
      publishCommand();
    } catch (error) {
      setStatus(`Action failed: ${error.message}`);
    }
  }, 140);
}

function setRelay(relayIndex, value) {
  try {
    if (relayIndex === 1) {
      state.relay1 = value;
    } else {
      state.relay2 = value;
    }

    renderState();
    publishCommand();
  } catch (error) {
    setStatus(`Action failed: ${error.message}`);
  }
}

function handleServoInput() {
  state.servo = clampServoAngle(Number(servoSliderEl.value));
  renderServo();

  if (mqttConnected) {
    queueServoPublish();
  }
}

function sendServoNow() {
  try {
    state.servo = clampServoAngle(Number(servoSliderEl.value));
    renderServo();
    publishCommand();
  } catch (error) {
    setStatus(`Action failed: ${error.message}`);
  }
}

function loadSettings() {
  const defaults = window.APP_CONFIG || {};

  brokerUrlEl.value = localStorage.getItem(STORAGE_KEYS.brokerWsUrl) || defaults.brokerWsUrl || "";
  mqttUsernameEl.value = localStorage.getItem(STORAGE_KEYS.mqttUsername) || defaults.mqttUsername || "";
  mqttPasswordEl.value = defaults.mqttPassword || "";
  topicCmdEl.value = localStorage.getItem(STORAGE_KEYS.topicCmd) || defaults.topicCmd || "home/workshop/relay/cmd";
  topicStateEl.value = localStorage.getItem(STORAGE_KEYS.topicState) || defaults.topicState || "home/workshop/relay/state";
}

function bindEvents() {
  saveSettingsBtnEl.addEventListener("click", saveSettings);
  connectBtnEl.addEventListener("click", connectMqtt);
  disconnectBtnEl.addEventListener("click", () => disconnectMqtt(true));

  servoSliderEl.addEventListener("input", handleServoInput);
  servoSendBtnEl.addEventListener("click", sendServoNow);
}

function init() {
  loadSettings();
  bindEvents();
  renderState();
}

window.setRelay = setRelay;
init();
