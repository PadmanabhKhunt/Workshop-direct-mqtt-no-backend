#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <PubSubClient.h>
#include <ESP32Servo.h>

const char* WIFI_SSID = "Padmanabh_EXT";
const char* WIFI_PASSWORD = "9429115078";

const char* MQTT_BROKER_HOST = "471a1ed426ca4130bb41a243d9ccfc31.s1.eu.hivemq.cloud";
const uint16_t MQTT_BROKER_PORT = 8883;
const char* MQTT_CLIENT_ID = "esp32-relay-direct-01";
const char* MQTT_USERNAME = "padmanabh_khunt";
const char* MQTT_PASSWORD = "GVuFBCF2sN9f_g6";

const char* MQTT_TOPIC_CMD = "home/workshop/relay/cmd";
const char* MQTT_TOPIC_STATE = "home/workshop/relay/state";

// For workshop demo only. For production, use CA certificate pinning.
const bool MQTT_ALLOW_INSECURE_TLS = true;

const int RELAY1_PIN = 16;
const int RELAY2_PIN = 17;
const int SERVO_PIN = 4;

// Most relay boards are active LOW.
const int RELAY_ON_LEVEL = LOW;
const int RELAY_OFF_LEVEL = HIGH;

const int SERVO_MIN_ANGLE = 0;
const int SERVO_MAX_ANGLE = 180;
const int SERVO_DEFAULT_ANGLE = 90;

WiFiClientSecure secureClient;
PubSubClient mqttClient(secureClient);
Servo controlServo;

int relay1State = 0;
int relay2State = 0;
int servoAngle = SERVO_DEFAULT_ANGLE;

void setRelayPin(int pin, int state) {
  digitalWrite(pin, state == 1 ? RELAY_ON_LEVEL : RELAY_OFF_LEVEL);
}

int clampServoAngle(int angle) {
  if (angle < SERVO_MIN_ANGLE) return SERVO_MIN_ANGLE;
  if (angle > SERVO_MAX_ANGLE) return SERVO_MAX_ANGLE;
  return angle;
}

void applyRelayAndServoState(int nextRelay1, int nextRelay2, int nextServoAngle, bool hasServoAngle) {
  relay1State = nextRelay1;
  relay2State = nextRelay2;

  if (hasServoAngle) {
    servoAngle = clampServoAngle(nextServoAngle);
  }

  setRelayPin(RELAY1_PIN, relay1State);
  setRelayPin(RELAY2_PIN, relay2State);
  controlServo.write(servoAngle);

  Serial.print("Applied state: ");
  Serial.print(relay1State);
  Serial.print(",");
  Serial.print(relay2State);
  Serial.print(",");
  Serial.println(servoAngle);
}

bool parseRelayBit(const String& value, int& outBit) {
  if (!(value == "0" || value == "1")) return false;
  outBit = value.toInt();
  return true;
}

bool parseServoValue(const String& value, int& outServoAngle) {
  if (value.length() == 0) return false;

  for (unsigned int i = 0; i < value.length(); i++) {
    if (!isDigit(static_cast<unsigned char>(value.charAt(i)))) {
      return false;
    }
  }

  int parsed = value.toInt();
  if (parsed < SERVO_MIN_ANGLE || parsed > SERVO_MAX_ANGLE) return false;

  outServoAngle = parsed;
  return true;
}

// Supports both formats:
// 1) relay1,relay2
// 2) relay1,relay2,servoAngle
bool parseControlPayload(
  const String& payload,
  int& outRelay1,
  int& outRelay2,
  int& outServoAngle,
  bool& outHasServoAngle
) {
  int firstComma = payload.indexOf(',');
  if (firstComma < 0) return false;

  int secondComma = payload.indexOf(',', firstComma + 1);

  String relay1Text;
  String relay2Text;
  String servoText;

  if (secondComma < 0) {
    relay1Text = payload.substring(0, firstComma);
    relay2Text = payload.substring(firstComma + 1);
    outHasServoAngle = false;
  } else {
    relay1Text = payload.substring(0, firstComma);
    relay2Text = payload.substring(firstComma + 1, secondComma);
    servoText = payload.substring(secondComma + 1);
    outHasServoAngle = true;
  }

  relay1Text.trim();
  relay2Text.trim();
  servoText.trim();

  if (!parseRelayBit(relay1Text, outRelay1)) return false;
  if (!parseRelayBit(relay2Text, outRelay2)) return false;

  if (outHasServoAngle) {
    if (!parseServoValue(servoText, outServoAngle)) {
      return false;
    }
  }

  return true;
}

void publishState() {
  char payload[16];
  snprintf(payload, sizeof(payload), "%d,%d,%d", relay1State, relay2State, servoAngle);

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
  int nextServoAngle = servoAngle;
  bool hasServoAngle = false;

  if (!parseControlPayload(message, nextRelay1, nextRelay2, nextServoAngle, hasServoAngle)) {
    Serial.println("Invalid payload. Expected: 0,1 or 0,1,90");
    return;
  }

  applyRelayAndServoState(nextRelay1, nextRelay2, nextServoAngle, hasServoAngle);
  publishState();
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

      publishState();
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

  controlServo.setPeriodHertz(50);
  controlServo.attach(SERVO_PIN, 500, 2400);
  applyRelayAndServoState(0, 0, SERVO_DEFAULT_ANGLE, true);

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
