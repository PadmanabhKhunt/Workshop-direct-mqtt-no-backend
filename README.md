# Direct MQTT Home Automation (No Backend)

This is a separate project that does not use Node.js backend.

Architecture:
- Web frontend connects directly to MQTT broker (WebSocket)
- ESP32 connects directly to MQTT broker (TLS MQTT)
- Broker relays messages between frontend and ESP32

Project paths:
- `direct-mqtt-no-backend/frontend`
- `direct-mqtt-no-backend/esp32/direct_mqtt_home_automation.ino`

## 1) Topics and payload

- Command topic (frontend -> ESP32): `home/workshop/relay/cmd`
- State topic (ESP32 -> frontend): `home/workshop/relay/state`
- Payload format: `relay1,relay2,servoAngle`
  - Examples: `1,0,45`, `0,1,90`, `1,1,180`
- Backward compatible format: `relay1,relay2` (servo keeps previous angle)

## 2) Frontend setup

Edit `direct-mqtt-no-backend/frontend/config.js`:

```js
window.APP_CONFIG = {
  brokerWsUrl: "wss://YOUR_CLUSTER_ID.s1.eu.hivemq.cloud:8884/mqtt",
  mqttUsername: "YOUR_MQTT_USERNAME",
  mqttPassword: "YOUR_MQTT_PASSWORD",
  topicCmd: "home/workshop/relay/cmd",
  topicState: "home/workshop/relay/state",
};
```

Run a static server:

```bash
cd direct-mqtt-no-backend/frontend
python -m http.server 5500
```

Open:
- `http://localhost:5500`

Then click `Connect MQTT`.

## 2.1) Deploy frontend on Vercel

This frontend is static and Vercel-ready.

Files used by Vercel:
- `direct-mqtt-no-backend/frontend/index.html`
- `direct-mqtt-no-backend/frontend/vercel.json`

Steps:
1. Push project to GitHub.
2. In Vercel, click `Add New -> Project`.
3. Select your repository.
4. Set `Root Directory` to `direct-mqtt-no-backend/frontend`.
5. Framework preset: `Other`.
6. Build Command: leave empty.
7. Output Directory: leave empty.
8. Deploy.

After deploy:
1. Open your Vercel URL.
2. Enter MQTT username/password in UI (or set in `config.js` before deploy).
3. Click `Connect MQTT`.

Note:
- `vercel.json` sets `no-store` cache for `config.js`, so config updates are reflected immediately after redeploy.

## 3) ESP32 setup

Open in Arduino IDE:
- `direct-mqtt-no-backend/esp32/direct_mqtt_home_automation.ino`

Install library:
- `PubSubClient` by Nick O'Leary
- `ESP32Servo` by Kevin Harrington / John K. Bennett

Set:
- `WIFI_SSID`
- `WIFI_PASSWORD`
- `MQTT_BROKER_HOST`
- `MQTT_USERNAME`
- `MQTT_PASSWORD`
- `SERVO_PIN` is set to `4` (GPIO4)

Upload code and open Serial Monitor (115200).

## 4) Relay pins

- Relay 1 -> GPIO16
- Relay 2 -> GPIO17
- Servo signal -> GPIO4

If your relay is active HIGH, swap in code:
- `RELAY_ON_LEVEL = HIGH`
- `RELAY_OFF_LEVEL = LOW`

Servo power note:
- Do not power servo from ESP32 3.3V pin.
- Use external 5V supply and keep GND common with ESP32.

## 5) Security notes

- Frontend direct MQTT exposes credentials to browser users.
- Use a dedicated MQTT user with limited topic permissions.
- Rotate any secrets if shared publicly.
