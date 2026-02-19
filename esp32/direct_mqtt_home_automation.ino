#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <PubSubClient.h>

const char* WIFI_SSID = "YOUR_WIFI_SSID";
const char* WIFI_PASSWORD = "YOUR_WIFI_PASSWORD";

const char* MQTT_BROKER_HOST = "YOUR_CLUSTER_ID.s1.eu.hivemq.cloud";
const uint16_t MQTT_BROKER_PORT = 8883;
const char* MQTT_CLIENT_ID = "esp32-relay-direct-01";
const char* MQTT_USERNAME = "YOUR_MQTT_USERNAME";
const char* MQTT_PASSWORD = "YOUR_MQTT_PASSWORD";

const char* MQTT_TOPIC_CMD = "home/workshop/relay/cmd";
const char* MQTT_TOPIC_STATE = "home/workshop/relay/state";

// For workshop demo only. For production, use CA certificate pinning.
const bool MQTT_ALLOW_INSECURE_TLS = true;

const int RELAY1_PIN = 16;
const int RELAY2_PIN = 17;

// Most relay boards are active LOW.
const int RELAY_ON_LEVEL = LOW;
const int RELAY_OFF_LEVEL = HIGH;

WiFiClientSecure secureClient;
PubSubClient mqttClient(secureClient);

int relay1State = 0;
int relay2State = 0;

void setRelayPin(int pin, int state) {
  digitalWrite(pin, state == 1 ? RELAY_ON_LEVEL : RELAY_OFF_LEVEL);
}

void applyRelayState(int nextRelay1, int nextRelay2) {
  relay1State = nextRelay1;
  relay2State = nextRelay2;

  setRelayPin(RELAY1_PIN, relay1State);
  setRelayPin(RELAY2_PIN, relay2State);

  Serial.print("Applied state: ");
  Serial.print(relay1State);
  Serial.print(",");
  Serial.println(relay2State);
}

bool parseRelayPayload(const String& payload, int& outRelay1, int& outRelay2) {
  int commaIndex = payload.indexOf(',');
  if (commaIndex < 0) return false;

  String relay1Text = payload.substring(0, commaIndex);
  String relay2Text = payload.substring(commaIndex + 1);

  relay1Text.trim();
  relay2Text.trim();

  if (!((relay1Text == "0" || relay1Text == "1") && (relay2Text == "0" || relay2Text == "1"))) {
    return false;
  }

  outRelay1 = relay1Text.toInt();
  outRelay2 = relay2Text.toInt();
  return true;
}

void publishRelayState() {
  char payload[8];
  snprintf(payload, sizeof(payload), "%d,%d", relay1State, relay2State);

  if (mqttClient.publish(MQTT_TOPIC_STATE, payload, true)) {
    Serial.print("Published state: ");
    Serial.println(payload);
  } else {
    Serial.println("Failed to publish state");
  }
}

void mqttCallback(char* topic, byte* payload, unsigned int length) {
  String message;
  message.reserve(length);

  for (unsigned int i = 0; i < length; i++) {
    message += static_cast<char>(payload[i]);
  }

  Serial.print("MQTT message on ");
  Serial.print(topic);
  Serial.print(": ");
  Serial.println(message);

  int nextRelay1 = 0;
  int nextRelay2 = 0;

  if (!parseRelayPayload(message, nextRelay1, nextRelay2)) {
    Serial.println("Invalid payload. Expected format: 0,1");
    return;
  }

  applyRelayState(nextRelay1, nextRelay2);
  publishRelayState();
}

void connectWifiBlocking() {
  if (WiFi.status() == WL_CONNECTED) return;

  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  Serial.print("Connecting to WiFi");

  while (WiFi.status() != WL_CONNECTED) {
    delay(400);
    Serial.print('.');
  }

  Serial.println("\nWiFi connected");
  Serial.print("IP: ");
  Serial.println(WiFi.localIP());
}

void ensureWifi() {
  if (WiFi.status() == WL_CONNECTED) return;
  Serial.println("WiFi disconnected. Reconnecting...");
  connectWifiBlocking();
}

void connectMqttBlocking() {
  if (mqttClient.connected()) return;

  Serial.print("Connecting MQTT...");

  while (!mqttClient.connected() && WiFi.status() == WL_CONNECTED) {
    bool connected = false;

    if (strlen(MQTT_USERNAME) > 0) {
      connected = mqttClient.connect(MQTT_CLIENT_ID, MQTT_USERNAME, MQTT_PASSWORD);
    } else {
      connected = mqttClient.connect(MQTT_CLIENT_ID);
    }

    if (connected) {
      Serial.println("connected");

      mqttClient.subscribe(MQTT_TOPIC_CMD);
      Serial.print("Subscribed: ");
      Serial.println(MQTT_TOPIC_CMD);

      publishRelayState();
    } else {
      Serial.print("failed, rc=");
      Serial.print(mqttClient.state());
      Serial.println(" retry in 2 sec");
      delay(2000);
    }
  }
}

void setup() {
  Serial.begin(115200);

  pinMode(RELAY1_PIN, OUTPUT);
  pinMode(RELAY2_PIN, OUTPUT);
  applyRelayState(0, 0);

  connectWifiBlocking();

  if (MQTT_ALLOW_INSECURE_TLS) {
    secureClient.setInsecure();
  }

  mqttClient.setServer(MQTT_BROKER_HOST, MQTT_BROKER_PORT);
  mqttClient.setCallback(mqttCallback);
  connectMqttBlocking();
}

void loop() {
  ensureWifi();

  if (!mqttClient.connected()) {
    connectMqttBlocking();
  }

  mqttClient.loop();
}
