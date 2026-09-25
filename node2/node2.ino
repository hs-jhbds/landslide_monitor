#include <Arduino.h>
#include <Wire.h>
#include <WiFi.h>
#include <HTTPClient.h>
#include <esp_now.h>
#include <esp_wifi.h>

// =====================================================
// NODE 2
// ESP32
//
// Sensors:
// MPU6050 + Vibration + Potentiometer
//
// Receives NODE 1 through ESP-NOW
// Sends combined data to Linux PC through HTTP
// =====================================================


// =====================================================
// WIFI
// =====================================================

const char* WIFI_SSID =
  "Anan";

const char* WIFI_PASSWORD =
  "12345678";


// =====================================================
// LINUX SERVER
// =====================================================

const char* SERVER_IP =
  "10.233.241.185";

const uint16_t SERVER_PORT =
  5000;


// Flask endpoint
const char* SERVER_ENDPOINT =
  "/api/data";


// =====================================================
// ESP-NOW CHANNEL
// MUST MATCH NODE 1
// =====================================================

#define ESPNOW_CHANNEL 6


// =====================================================
// PINS
// =====================================================

#define SDA_PIN       21
#define SCL_PIN       22

#define VIBRATION_PIN 27
#define POT_PIN       34


// =====================================================
// MPU6050
// =====================================================

#define MPU_ADDR          0x68

#define PWR_MGMT_1        0x6B
#define SMPLRT_DIV        0x19
#define CONFIG_REG        0x1A
#define GYRO_CONFIG       0x1B
#define ACCEL_CONFIG      0x1C
#define ACCEL_XOUT_H      0x3B
#define WHO_AM_I          0x75


// =====================================================
// STATUS
// =====================================================

#define NORMAL    0
#define MEDIUM    1
#define HIGH      2
#define CRITICAL  3


// =====================================================
// DATA PACKET
// MUST BE IDENTICAL TO NODE 1
// =====================================================

typedef struct SensorData {

  uint8_t nodeID;

  float tiltX;
  float tiltY;

  float vibration;
  float displacement;

  uint8_t vibrationDetected;

  uint8_t tiltStatus;
  uint8_t vibrationStatus;
  uint8_t displacementStatus;
  uint8_t overallStatus;

} SensorData;


// =====================================================
// NODE DATA
// =====================================================

SensorData node1Data;
SensorData node2Data;


// =====================================================
// NODE 1 CONNECTION
// =====================================================

volatile bool node1DataReceived =
  false;

unsigned long lastNode1Packet =
  0;

const unsigned long NODE1_TIMEOUT =
  5000;


// =====================================================
// TILT BASELINE
// =====================================================

float baselineTiltX = 0.0;
float baselineTiltY = 0.0;


// =====================================================
// MPU FUNCTIONS
// =====================================================

void writeMPU(
  uint8_t reg,
  uint8_t value
) {

  Wire.beginTransmission(
    MPU_ADDR
  );

  Wire.write(reg);
  Wire.write(value);

  Wire.endTransmission();
}


bool readMPU(
  uint8_t reg,
  uint8_t* buffer,
  uint8_t length
) {

  Wire.beginTransmission(
    MPU_ADDR
  );

  Wire.write(reg);

  if (
    Wire.endTransmission(false)
    != 0
  ) {

    return false;
  }

  uint8_t received =
    Wire.requestFrom(
      MPU_ADDR,
      length,
      true
    );

  if (received != length) {
    return false;
  }

  for (
    uint8_t i = 0;
    i < length;
    i++
  ) {

    buffer[i] =
      Wire.read();
  }

  return true;
}


bool initMPU() {

  uint8_t whoAmI;

  if (
    !readMPU(
      WHO_AM_I,
      &whoAmI,
      1
    )
  ) {

    Serial.println(
      "MPU6050 not responding!"
    );

    return false;
  }

  Serial.print(
    "MPU6050 WHO_AM_I = 0x"
  );

  Serial.println(
    whoAmI,
    HEX
  );

  writeMPU(
    PWR_MGMT_1,
    0x00
  );

  delay(100);

  writeMPU(
    SMPLRT_DIV,
    0x07
  );

  writeMPU(
    CONFIG_REG,
    0x03
  );

  // ±500 deg/s
  writeMPU(
    GYRO_CONFIG,
    0x08
  );

  // ±8g
  writeMPU(
    ACCEL_CONFIG,
    0x10
  );

  delay(100);

  Serial.println(
    "MPU6050 initialized."
  );

  return true;
}


// =====================================================
// READ MPU
// =====================================================

bool readSensors(
  float &ax,
  float &ay,
  float &az,
  float &gx,
  float &gy,
  float &gz
) {

  uint8_t buffer[14];

  if (
    !readMPU(
      ACCEL_XOUT_H,
      buffer,
      14
    )
  ) {

    return false;
  }

  int16_t rawAx =
    ((int16_t)buffer[0] << 8)
    | buffer[1];

  int16_t rawAy =
    ((int16_t)buffer[2] << 8)
    | buffer[3];

  int16_t rawAz =
    ((int16_t)buffer[4] << 8)
    | buffer[5];

  int16_t rawGx =
    ((int16_t)buffer[8] << 8)
    | buffer[9];

  int16_t rawGy =
    ((int16_t)buffer[10] << 8)
    | buffer[11];

  int16_t rawGz =
    ((int16_t)buffer[12] << 8)
    | buffer[13];

  ax = rawAx / 4096.0;
  ay = rawAy / 4096.0;
  az = rawAz / 4096.0;

  gx = rawGx / 65.5;
  gy = rawGy / 65.5;
  gz = rawGz / 65.5;

  return true;
}


// =====================================================
// TILT
// =====================================================

void calculateTilt(
  float ax,
  float ay,
  float az,
  float &tiltX,
  float &tiltY
) {

  tiltX =
    atan2(
      ay,
      sqrt(
        (ax * ax) +
        (az * az)
      )
    ) * 180.0 / PI;

  tiltY =
    atan2(
      -ax,
      sqrt(
        (ay * ay) +
        (az * az)
      )
    ) * 180.0 / PI;
}


// =====================================================
// STATUS
// =====================================================

uint8_t getTiltStatus(
  float tiltX,
  float tiltY
) {

  float tilt =
    max(
      abs(tiltX),
      abs(tiltY)
    );

  if (tilt < 3.0)
    return NORMAL;

  if (tilt < 7.0)
    return MEDIUM;

  if (tilt < 12.0)
    return HIGH;

  return CRITICAL;
}


uint8_t getVibrationStatus(
  float vibration
) {

  if (vibration < 0.10)
    return NORMAL;

  if (vibration < 0.25)
    return MEDIUM;

  if (vibration < 0.50)
    return HIGH;

  return CRITICAL;
}


uint8_t getDisplacementStatus(
  float displacement
) {

  if (displacement < 2.0)
    return NORMAL;

  if (displacement < 5.0)
    return MEDIUM;

  if (displacement < 10.0)
    return HIGH;

  return CRITICAL;
}


uint8_t getOverallStatus(
  uint8_t tiltStatus,
  uint8_t vibrationStatus,
  uint8_t displacementStatus
) {

  return max(
    tiltStatus,
    max(
      vibrationStatus,
      displacementStatus
    )
  );
}


const char* statusText(
  uint8_t status
) {

  switch (status) {

    case NORMAL:
      return "NORMAL";

    case MEDIUM:
      return "MEDIUM";

    case HIGH:
      return "HIGH";

    case CRITICAL:
      return "CRITICAL";

    default:
      return "UNKNOWN";
  }
}


// =====================================================
// ESP-NOW RECEIVE CALLBACK
// =====================================================

void onDataRecv(
  const esp_now_recv_info_t *info,
  const uint8_t *incomingData,
  int len
) {

  if (
    len == sizeof(SensorData)
  ) {

    SensorData received;

    memcpy(
      &received,
      incomingData,
      sizeof(SensorData)
    );

    if (received.nodeID == 1) {

      memcpy(
        &node1Data,
        &received,
        sizeof(SensorData)
      );

      node1DataReceived =
        true;

      lastNode1Packet =
        millis();
    }
  }
}


// =====================================================
// SET CHANNEL
// =====================================================

void setESPNowChannel() {

  esp_err_t result =
    esp_wifi_set_channel(
      ESPNOW_CHANNEL,
      WIFI_SECOND_CHAN_NONE
    );

  if (result == ESP_OK) {

    Serial.print(
      "ESP-NOW Channel: "
    );

    Serial.println(
      ESPNOW_CHANNEL
    );

  } else {

    Serial.print(
      "Channel setup failed: "
    );

    Serial.println(result);
  }
}


// =====================================================
// WIFI CONNECTION
// =====================================================

void connectWiFi() {

  Serial.println();
  Serial.println(
    "Connecting to Wi-Fi..."
  );

  WiFi.begin(
    WIFI_SSID,
    WIFI_PASSWORD
  );

  int attempts = 0;

  while (
    WiFi.status() != WL_CONNECTED
    &&
    attempts < 30
  ) {

    delay(500);

    Serial.print(".");

    attempts++;
  }

  Serial.println();

  if (
    WiFi.status()
    == WL_CONNECTED
  ) {

    Serial.println(
      "Wi-Fi connected."
    );

    Serial.print(
      "Node 2 IP: "
    );

    Serial.println(
      WiFi.localIP()
    );

    Serial.print(
      "Wi-Fi channel: "
    );

    Serial.println(
      WiFi.channel()
    );

  } else {

    Serial.println(
      "ERROR: Wi-Fi connection failed."
    );

    Serial.println(
      "Check SSID/password."
    );
  }
}


// =====================================================
// CALIBRATION
// =====================================================

void calibrateTilt() {

  Serial.println();
  Serial.println(
    "================================"
  );

  Serial.println(
    "NODE 2 TILT CALIBRATION"
  );

  Serial.println(
    "Keep NODE 2 completely still"
  );

  Serial.println(
    "================================"
  );

  delay(2000);

  float sumX = 0;
  float sumY = 0;

  const int samples = 100;

  int validSamples = 0;

  for (
    int i = 0;
    i < samples;
    i++
  ) {

    float ax, ay, az;
    float gx, gy, gz;

    if (
      readSensors(
        ax,
        ay,
        az,
        gx,
        gy,
        gz
      )
    ) {

      float tx;
      float ty;

      calculateTilt(
        ax,
        ay,
        az,
        tx,
        ty
      );

      sumX += tx;
      sumY += ty;

      validSamples++;
    }

    delay(10);
  }

  if (validSamples > 0) {

    baselineTiltX =
      sumX / validSamples;

    baselineTiltY =
      sumY / validSamples;
  }

  Serial.print(
    "Baseline Tilt X: "
  );

  Serial.println(
    baselineTiltX,
    2
  );

  Serial.print(
    "Baseline Tilt Y: "
  );

  Serial.println(
    baselineTiltY,
    2
  );

  Serial.println(
    "Calibration complete."
  );
}


// =====================================================
// JSON CREATION
// =====================================================

String createJSON() {

  bool node1Connected =
    node1DataReceived &&
    (
      millis() - lastNode1Packet
      < NODE1_TIMEOUT
    );

  String json = "{";

  // ---------------- NODE 1 ----------------

  json += "\"node1\":{";

  json += "\"connected\":";
  json += node1Connected
    ? "true"
    : "false";

  json += ",";

  json += "\"tiltX\":";
  json += String(
    node1Data.tiltX,
    2
  );

  json += ",";

  json += "\"tiltY\":";
  json += String(
    node1Data.tiltY,
    2
  );

  json += ",";

  json += "\"vibration\":";
  json += String(
    node1Data.vibration,
    3
  );

  json += ",";

  json += "\"displacement\":";
  json += String(
    node1Data.displacement,
    2
  );

  json += ",";

  json += "\"vibrationDetected\":";
  json += String(
    node1Data.vibrationDetected
  );

  json += ",";

  json += "\"tiltStatus\":";
  json += String(
    node1Data.tiltStatus
  );

  json += ",";

  json += "\"vibrationStatus\":";
  json += String(
    node1Data.vibrationStatus
  );

  json += ",";

  json += "\"displacementStatus\":";
  json += String(
    node1Data.displacementStatus
  );

  json += ",";

  json += "\"overallStatus\":";
  json += String(
    node1Data.overallStatus
  );

  json += "},";


  // ---------------- NODE 2 ----------------

  json += "\"node2\":{";

  json += "\"connected\":true,";

  json += "\"tiltX\":";
  json += String(
    node2Data.tiltX,
    2
  );

  json += ",";

  json += "\"tiltY\":";
  json += String(
    node2Data.tiltY,
    2
  );

  json += ",";

  json += "\"vibration\":";
  json += String(
    node2Data.vibration,
    3
  );

  json += ",";

  json += "\"displacement\":";
  json += String(
    node2Data.displacement,
    2
  );

  json += ",";

  json += "\"vibrationDetected\":";
  json += String(
    node2Data.vibrationDetected
  );

  json += ",";

  json += "\"tiltStatus\":";
  json += String(
    node2Data.tiltStatus
  );

  json += ",";

  json += "\"vibrationStatus\":";
  json += String(
    node2Data.vibrationStatus
  );

  json += ",";

  json += "\"displacementStatus\":";
  json += String(
    node2Data.displacementStatus
  );

  json += ",";

  json += "\"overallStatus\":";
  json += String(
    node2Data.overallStatus
  );

  json += "},";


  // ---------------- SYSTEM ----------------

  uint8_t systemStatus =
    node2Data.overallStatus;

  if (node1Connected) {

    systemStatus =
      max(
        node1Data.overallStatus,
        node2Data.overallStatus
      );
  }

  json += "\"systemStatus\":";
  json += String(systemStatus);

  json += ",";

  json += "\"node1Connected\":";
  json += node1Connected
    ? "true"
    : "false";

  json += "}";

  return json;
}


// =====================================================
// SEND HTTP TO LINUX
// =====================================================

void sendToServer() {

  if (
    WiFi.status()
    != WL_CONNECTED
  ) {

    Serial.println(
      "Wi-Fi disconnected."
    );

    return;
  }

  String url =
    "http://" +
    String(SERVER_IP) +
    ":" +
    String(SERVER_PORT) +
    String(SERVER_ENDPOINT);

  String json =
    createJSON();

  HTTPClient http;

  http.begin(url);

  http.addHeader(
    "Content-Type",
    "application/json"
  );

  int httpCode =
    http.POST(json);

  Serial.println();
  Serial.println(
    "========== HTTP =========="
  );

  Serial.print(
    "URL: "
  );

  Serial.println(url);

  Serial.print(
    "HTTP Code: "
  );

  Serial.println(httpCode);

  if (httpCode > 0) {

    String response =
      http.getString();

    Serial.print(
      "Server: "
    );

    Serial.println(
      response
    );
  }

  http.end();
}


// =====================================================
// SETUP
// =====================================================

void setup() {

  Serial.begin(115200);

  delay(1000);

  Serial.println();
  Serial.println(
    "========================================"
  );

  Serial.println(
    "        NODE 2 - ESP32"
  );

  Serial.println(
    "========================================"
  );

  // ---------------- PINS ----------------

  pinMode(
    VIBRATION_PIN,
    INPUT
  );

  pinMode(
    POT_PIN,
    INPUT
  );

  // ---------------- I2C ----------------

  Wire.begin(
    SDA_PIN,
    SCL_PIN
  );

  Wire.setClock(400000);

  // ---------------- MPU ----------------

  if (!initMPU()) {

    Serial.println(
      "ERROR: MPU6050 initialization failed."
    );

    while (true) {
      delay(1000);
    }
  }

  // ---------------- MAC ----------------

  WiFi.mode(WIFI_STA);

  Serial.print(
    "NODE 2 MAC: "
  );

  Serial.println(
    WiFi.macAddress()
  );

  // ---------------- WIFI ----------------

  connectWiFi();

  // ---------------- CHANNEL ----------------

  setESPNowChannel();

  // ---------------- CALIBRATION ----------------

  calibrateTilt();

  // ---------------- ESP-NOW ----------------

  if (
    esp_now_init()
    != ESP_OK
  ) {

    Serial.println(
      "ERROR: ESP-NOW initialization failed."
    );

    while (true) {
      delay(1000);
    }
  }

  esp_now_register_recv_cb(
    onDataRecv
  );

  Serial.println();
  Serial.println(
    "NODE 2 READY."
  );

  Serial.println(
    "Waiting for NODE 1..."
  );

  Serial.println();
}


// =====================================================
// LOOP
// =====================================================

void loop() {

  // ===================================================
  // READ NODE 2 SENSORS
  // ===================================================

  float ax, ay, az;
  float gx, gy, gz;

  if (
    readSensors(
      ax,
      ay,
      az,
      gx,
      gy,
      gz
    )
  ) {

    // ---------------- TILT ----------------

    float rawTiltX;
    float rawTiltY;

    calculateTilt(
      ax,
      ay,
      az,
      rawTiltX,
      rawTiltY
    );

    float tiltX =
      rawTiltX -
      baselineTiltX;

    float tiltY =
      rawTiltY -
      baselineTiltY;

    // ---------------- VIBRATION ----------------

    float accelerationMagnitude =
      sqrt(
        (ax * ax) +
        (ay * ay) +
        (az * az)
      );

    float vibration =
      abs(
        accelerationMagnitude - 1.0
      );

    uint8_t vibrationDetected =
      digitalRead(
        VIBRATION_PIN
      );

    // ---------------- DISPLACEMENT ----------------

    int potValue =
      analogRead(POT_PIN);

    float displacement =
      ((float)potValue / 4095.0)
      * 20.0;

    // ---------------- STATUS ----------------

    uint8_t tiltStatus =
      getTiltStatus(
        tiltX,
        tiltY
      );

    uint8_t vibrationStatus =
      getVibrationStatus(
        vibration
      );

    uint8_t displacementStatus =
      getDisplacementStatus(
        displacement
      );

    uint8_t overallStatus =
      getOverallStatus(
        tiltStatus,
        vibrationStatus,
        displacementStatus
      );

    // ---------------- STORE NODE 2 ----------------

    node2Data.nodeID = 2;

    node2Data.tiltX =
      tiltX;

    node2Data.tiltY =
      tiltY;

    node2Data.vibration =
      vibration;

    node2Data.displacement =
      displacement;

    node2Data.vibrationDetected =
      vibrationDetected;

    node2Data.tiltStatus =
      tiltStatus;

    node2Data.vibrationStatus =
      vibrationStatus;

    node2Data.displacementStatus =
      displacementStatus;

    node2Data.overallStatus =
      overallStatus;
  }


  // ===================================================
  // SERIAL MONITOR
  // ===================================================

  Serial.println();
  Serial.println(
    "========================================"
  );

  Serial.println(
    "             NODE 2 DATA"
  );

  Serial.println(
    "========================================"
  );

  // ---------------- NODE 1 ----------------

  bool node1Connected =
    node1DataReceived &&
    (
      millis() -
      lastNode1Packet
      < NODE1_TIMEOUT
    );

  if (node1Connected) {

    Serial.println(
      "NODE 1: CONNECTED"
    );

    Serial.print(
      "Tilt X: "
    );

    Serial.println(
      node1Data.tiltX,
      2
    );

    Serial.print(
      "Tilt Y: "
    );

    Serial.println(
      node1Data.tiltY,
      2
    );

    Serial.print(
      "Vibration: "
    );

    Serial.println(
      node1Data.vibration,
      3
    );

    Serial.print(
      "Displacement: "
    );

    Serial.println(
      node1Data.displacement,
      2
    );

    Serial.print(
      "Overall: "
    );

    Serial.println(
      statusText(
        node1Data.overallStatus
      )
    );

  } else {

    Serial.println(
      "NODE 1: WAITING"
    );
  }


  // ---------------- NODE 2 ----------------

  Serial.println();
  Serial.println(
    "NODE 2: ACTIVE"
  );

  Serial.print(
    "Tilt X: "
  );

  Serial.println(
    node2Data.tiltX,
    2
  );

  Serial.print(
    "Tilt Y: "
  );

  Serial.println(
    node2Data.tiltY,
    2
  );

  Serial.print(
    "Vibration: "
  );

  Serial.println(
    node2Data.vibration,
    3
  );

  Serial.print(
    "Displacement: "
  );

  Serial.println(
    node2Data.displacement,
    2
  );

  Serial.print(
    "Overall: "
  );

  Serial.println(
    statusText(
      node2Data.overallStatus
    )
  );


  // ===================================================
  // SEND COMBINED DATA TO LINUX
  // ===================================================

  sendToServer();


  delay(1000);
}