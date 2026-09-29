#include <Arduino.h>
#include <Wire.h>
#include <WiFi.h>
#include <HTTPClient.h>
#include <esp_now.h>
#include <esp_wifi.h>

// =====================================================
// NODE 2 - ESP32 GATEWAY
//
// FUNCTIONS
// 1. Reads Node 2 sensors
// 2. Receives Node 1 through ESP-NOW
// 3. Combines Node 1 + Node 2 data
// 4. Sends combined data to Linux/Flask server
//
// IMPORTANT
// Node 1 is configured for ESP-NOW channel 1.
// Node 2 Wi-Fi router MUST therefore use 2.4 GHz
// channel 1.
//
// Node 2 initial physical position automatically becomes:
// Tilt X = 0.00 deg
// Tilt Y = 0.00 deg
// Displacement = 0.00 mm
//
// Serial output is deliberately limited to every 2 sec.
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
  "10.43.88.185";

const uint16_t SERVER_PORT =
  5000;

const char* SERVER_ENDPOINT =
  "/api/data";


// =====================================================
// ESP-NOW / WIFI CHANNEL
//
// MUST MATCH NODE 1.
//
// NODE 1 = CHANNEL 1
// ROUTER MUST = CHANNEL 1
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
// TIMING
// =====================================================

// Serial display every 2 seconds.
const unsigned long DISPLAY_INTERVAL = 2000;

// Send server data every 2 seconds.
const unsigned long SERVER_INTERVAL = 2000;

// Node 1 considered disconnected after 5 seconds.
const unsigned long NODE1_TIMEOUT = 5000;

// Wi-Fi reconnect attempt every 5 seconds.
const unsigned long WIFI_RECONNECT_INTERVAL = 5000;


// =====================================================
// TILT ZERO DEAD BAND
// =====================================================

const float TILT_ZERO_DEADBAND = 0.25;


// =====================================================
// DATA PACKET
//
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

SensorData node1Data = {};
SensorData node2Data = {};


// =====================================================
// NODE 1 CONNECTION
// =====================================================

volatile bool node1DataReceived = false;

volatile unsigned long lastNode1Packet = 0;


// =====================================================
// BASELINES
// =====================================================

// Node 2 initial physical position.
float baselineTiltX = 0.0;
float baselineTiltY = 0.0;

// Node 2 initial potentiometer position.
int baselinePot = 0;


// =====================================================
// TIMERS
// =====================================================

unsigned long lastDisplay = 0;
unsigned long lastServerSend = 0;
unsigned long lastWiFiReconnect = 0;


// =====================================================
// MPU WRITE
// =====================================================

void writeMPU(
  uint8_t reg,
  uint8_t value
) {

  Wire.beginTransmission(MPU_ADDR);

  Wire.write(reg);
  Wire.write(value);

  Wire.endTransmission();
}


// =====================================================
// MPU READ
// =====================================================

bool readMPU(
  uint8_t reg,
  uint8_t* buffer,
  uint8_t length
) {

  Wire.beginTransmission(MPU_ADDR);

  Wire.write(reg);

  if (
    Wire.endTransmission(false) != 0
  ) {

    return false;
  }

  uint8_t received =
    Wire.requestFrom(
      MPU_ADDR,
      length,
      true
    );

  if (
    received != length
  ) {

    return false;
  }

  for (
    uint8_t i = 0;
    i < length;
    i++
  ) {

    buffer[i] = Wire.read();
  }

  return true;
}


// =====================================================
// MPU INITIALIZATION
// =====================================================

bool initMPU() {

  uint8_t whoAmI = 0;

  if (
    !readMPU(
      WHO_AM_I,
      &whoAmI,
      1
    )
  ) {

    Serial.println(
      "ERROR: MPU6050 not responding."
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


  // Gyroscope +/-500 deg/s

  writeMPU(
    GYRO_CONFIG,
    0x08
  );


  // Accelerometer +/-8g

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
// READ MPU SENSOR VALUES
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


  // +/-8g

  ax =
    rawAx / 4096.0;

  ay =
    rawAy / 4096.0;

  az =
    rawAz / 4096.0;


  // +/-500 deg/s

  gx =
    rawGx / 65.5;

  gy =
    rawGy / 65.5;

  gz =
    rawGz / 65.5;


  return true;
}


// =====================================================
// CALCULATE TILT
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
    )
    * 180.0 / PI;


  tiltY =
    atan2(
      -ax,
      sqrt(
        (ay * ay) +
        (az * az)
      )
    )
    * 180.0 / PI;
}


// =====================================================
// ZERO DEAD BAND
// =====================================================

float applyTiltDeadband(
  float value
) {

  if (
    abs(value)
    < TILT_ZERO_DEADBAND
  ) {

    return 0.0;
  }

  return value;
}


// =====================================================
// STATUS FUNCTIONS
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


// =====================================================
// VIBRATION STATUS
// =====================================================

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


// =====================================================
// DISPLACEMENT STATUS
// =====================================================

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


// =====================================================
// OVERALL STATUS
// =====================================================

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


// =====================================================
// STATUS TEXT
// =====================================================

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
// NODE 1 ESP-NOW RECEIVE CALLBACK
//
// IMPORTANT:
// Keep callback short.
// Do not print from callback.
// =====================================================

void onDataRecv(
  const esp_now_recv_info_t *info,
  const uint8_t *incomingData,
  int len
) {

  if (
    len != sizeof(SensorData)
  ) {

    return;
  }


  SensorData received;


  memcpy(
    &received,
    incomingData,
    sizeof(SensorData)
  );


  if (
    received.nodeID != 1
  ) {

    return;
  }


  memcpy(
    &node1Data,
    &received,
    sizeof(SensorData)
  );


  node1DataReceived = true;

  lastNode1Packet = millis();
}


// =====================================================
// PRINT NODE 2 MAC
// =====================================================

void printMAC() {

  uint8_t mac[6];


  if (
    esp_wifi_get_mac(
      WIFI_IF_STA,
      mac
    ) != ESP_OK
  ) {

    Serial.println(
      "ERROR: Could not read Node 2 MAC."
    );

    return;
  }


  Serial.printf(
    "NODE 2 MAC: %02X:%02X:%02X:%02X:%02X:%02X\n",
    mac[0],
    mac[1],
    mac[2],
    mac[3],
    mac[4],
    mac[5]
  );
}


// =====================================================
// CONNECT WIFI
// =====================================================

bool connectWiFi() {

  Serial.println();
  Serial.println(
    "Connecting to Wi-Fi..."
  );


  WiFi.disconnect(true, true);

  delay(200);


  WiFi.mode(WIFI_STA);

  WiFi.setSleep(false);


  WiFi.begin(
    WIFI_SSID,
    WIFI_PASSWORD
  );


  const int maxAttempts = 30;


  for (
    int i = 0;
    i < maxAttempts;
    i++
  ) {

    if (
      WiFi.status()
      == WL_CONNECTED
    ) {

      Serial.println();
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
        "Router Wi-Fi channel: "
      );

      Serial.println(
        WiFi.channel()
      );


      return true;
    }


    Serial.print(
      "."
    );


    delay(300);
  }


  Serial.println();


  Serial.println(
    "ERROR: Wi-Fi connection failed."
  );


  return false;
}


// =====================================================
// CHECK CHANNEL
// =====================================================

bool checkChannel() {

  int channel =
    WiFi.channel();


  Serial.print(
    "Router Wi-Fi channel: "
  );

  Serial.println(
    channel
  );


  if (
    channel != ESPNOW_CHANNEL
  ) {

    Serial.println();
    Serial.println(
      "!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!"
    );

    Serial.println(
      "CHANNEL MISMATCH"
    );

    Serial.print(
      "Node 1 ESP-NOW channel: "
    );

    Serial.println(
      ESPNOW_CHANNEL
    );

    Serial.print(
      "Node 2 router channel: "
    );

    Serial.println(
      channel
    );

    Serial.println(
      "Set the 2.4 GHz router channel to 1."
    );

    Serial.println(
      "Do NOT force Node 2 to another channel."
    );

    Serial.println(
      "!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!"
    );

    return false;
  }


  Serial.println(
    "ESP-NOW / Wi-Fi channel: OK"
  );


  return true;
}


// =====================================================
// AUTOMATIC NODE 2 ZERO CALIBRATION
//
// Initial physical position becomes zero.
// Initial potentiometer position becomes zero.
// =====================================================

bool calibrateInitialPosition() {

  Serial.println();
  Serial.println(
    "========================================"
  );

  Serial.println(
    "       NODE 2 ZERO CALIBRATION"
  );

  Serial.println(
    "========================================"
  );

  Serial.println(
    "Keep NODE 2 completely still."
  );

  Serial.println(
    "Current physical position = ZERO."
  );

  Serial.println(
    "Calibration starting in 3 seconds..."
  );


  delay(1000);

  Serial.println("3...");
  delay(1000);

  Serial.println("2...");
  delay(1000);

  Serial.println("1...");
  delay(1000);


  Serial.println(
    "Sampling initial position..."
  );


  const int samples = 200;


  float sumTiltX = 0.0;
  float sumTiltY = 0.0;

  long sumPot = 0;


  int validSamples = 0;


  for (
    int i = 0;
    i < samples;
    i++
  ) {

    float ax;
    float ay;
    float az;

    float gx;
    float gy;
    float gz;


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

      float rawTiltX;
      float rawTiltY;


      calculateTilt(
        ax,
        ay,
        az,
        rawTiltX,
        rawTiltY
      );


      sumTiltX += rawTiltX;
      sumTiltY += rawTiltY;


      sumPot +=
        analogRead(
          POT_PIN
        );


      validSamples++;
    }


    delay(10);
  }


  if (
    validSamples == 0
  ) {

    Serial.println(
      "ERROR: Node 2 calibration failed."
    );

    return false;
  }


  baselineTiltX =
    sumTiltX /
    validSamples;


  baselineTiltY =
    sumTiltY /
    validSamples;


  baselinePot =
    sumPot /
    validSamples;


  Serial.println();
  Serial.println(
    "Initial position stored as ZERO."
  );


  Serial.print(
    "Baseline Tilt X: "
  );

  Serial.print(
    baselineTiltX,
    2
  );

  Serial.println(
    " deg"
  );


  Serial.print(
    "Baseline Tilt Y: "
  );

  Serial.print(
    baselineTiltY,
    2
  );

  Serial.println(
    " deg"
  );


  Serial.print(
    "Baseline Pot: "
  );

  Serial.println(
    baselinePot
  );


  Serial.println();
  Serial.println(
    "NODE 2 ZERO REFERENCE READY."
  );


  return true;
}


// =====================================================
// INITIALIZE ESP-NOW
// =====================================================

bool initESPNow() {

  Serial.println(
    "Initializing ESP-NOW..."
  );


  esp_err_t result =
    esp_now_init();


  Serial.print(
    "ESP-NOW init result: "
  );

  Serial.println(
    result
  );


  if (
    result != ESP_OK
  ) {

    Serial.println(
      "ERROR: ESP-NOW initialization failed."
    );

    return false;
  }


  esp_now_register_recv_cb(
    onDataRecv
  );


  Serial.println(
    "ESP-NOW receiver ready."
  );


  return true;
}


// =====================================================
// NODE 2 SENSOR UPDATE
// =====================================================

void updateNode2Sensors() {

  float ax;
  float ay;
  float az;

  float gx;
  float gy;
  float gz;


  if (
    !readSensors(
      ax,
      ay,
      az,
      gx,
      gy,
      gz
    )
  ) {

    return;
  }


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


  // Initial physical position = zero.

  float tiltX =
    rawTiltX -
    baselineTiltX;


  float tiltY =
    rawTiltY -
    baselineTiltY;


  tiltX =
    applyTiltDeadband(
      tiltX
    );


  tiltY =
    applyTiltDeadband(
      tiltY
    );


  // ---------------- VIBRATION ----------------

  float accelerationMagnitude =
    sqrt(
      (ax * ax) +
      (ay * ay) +
      (az * az)
    );


  float vibration =
    abs(
      accelerationMagnitude -
      1.0
    );


  uint8_t vibrationDetected =
    digitalRead(
      VIBRATION_PIN
    );


  // ---------------- DISPLACEMENT ----------------

  int potValue =
    analogRead(
      POT_PIN
    );


  int relativePot =
    potValue -
    baselinePot;


  float displacement =
    (
      (float)relativePot /
      4095.0
    ) * 20.0;


  if (
    abs(displacement)
    < 0.05
  ) {

    displacement = 0.0;
  }


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
      abs(displacement)
    );


  uint8_t overallStatus =
    getOverallStatus(
      tiltStatus,
      vibrationStatus,
      displacementStatus
    );


  // ---------------- STORE ----------------

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


// =====================================================
// NODE 1 CONNECTION STATUS
// =====================================================

bool isNode1Connected() {

  if (
    !node1DataReceived
  ) {

    return false;
  }


  unsigned long age =
    millis() -
    lastNode1Packet;


  return (
    age <
    NODE1_TIMEOUT
  );
}


// =====================================================
// CREATE JSON
// =====================================================

String createJSON() {

  bool node1Connected =
    isNode1Connected();


  uint8_t systemStatus =
    node2Data.overallStatus;


  if (
    node1Connected
  ) {

    systemStatus =
      max(
        node1Data.overallStatus,
        node2Data.overallStatus
      );
  }


  String json = "{";


  // ===================================================
  // NODE 1
  // ===================================================

  json += "\"node1\":{";


  json += "\"connected\":";
  json +=
    node1Connected
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


  // ===================================================
  // NODE 2
  // ===================================================

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


  // ===================================================
  // SYSTEM
  // ===================================================

  json += "\"systemStatus\":";
  json += String(
    systemStatus
  );


  json += ",";


  json += "\"node1Connected\":";
  json +=
    node1Connected
    ? "true"
    : "false";


  json += "}";


  return json;
}


// =====================================================
// SEND DATA TO SERVER
// =====================================================

bool sendToServer() {

  if (
    WiFi.status()
    != WL_CONNECTED
  ) {

    return false;
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


  // Short timeout so a server/network problem cannot
  // freeze Node 2 for a long time.

  http.setConnectTimeout(1000);
  http.setTimeout(1000);


  if (
    !http.begin(url)
  ) {

    return false;
  }


  http.addHeader(
    "Content-Type",
    "application/json"
  );


  int httpCode =
    http.POST(json);


  http.end();


  return (
    httpCode >= 200 &&
    httpCode < 300
  );
}


// =====================================================
// PRINT STATUS
// =====================================================

void printNodeStatus() {

  bool node1Connected =
    isNode1Connected();


  Serial.println();
  Serial.println(
    "========================================"
  );

  Serial.println(
    "              NODE 2 DATA"
  );

  Serial.println(
    "========================================"
  );


  // ===================================================
  // NODE 1
  // ===================================================

  if (
    node1Connected
  ) {

    Serial.println(
      "NODE 1: CONNECTED"
    );


    Serial.print(
      "Tilt X:          "
    );

    Serial.print(
      node1Data.tiltX,
      2
    );

    Serial.println(
      " deg"
    );


    Serial.print(
      "Tilt Y:          "
    );

    Serial.print(
      node1Data.tiltY,
      2
    );

    Serial.println(
      " deg"
    );


    Serial.print(
      "Vibration:       "
    );

    Serial.print(
      node1Data.vibration,
      3
    );

    Serial.println(
      " g"
    );


    Serial.print(
      "Displacement:    "
    );

    Serial.print(
      node1Data.displacement,
      2
    );

    Serial.println(
      " mm"
    );


    Serial.print(
      "Overall Status:  "
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


  // ===================================================
  // NODE 2
  // ===================================================

  Serial.println();


  Serial.println(
    "NODE 2: ACTIVE"
  );


  Serial.print(
    "Tilt X:          "
  );

  Serial.print(
    node2Data.tiltX,
    2
  );

  Serial.println(
    " deg"
  );


  Serial.print(
    "Tilt Y:          "
  );

  Serial.print(
    node2Data.tiltY,
    2
  );

  Serial.println(
    " deg"
  );


  Serial.print(
    "Vibration:       "
  );

  Serial.print(
    node2Data.vibration,
    3
  );

  Serial.println(
    " g"
  );


  Serial.print(
    "Displacement:    "
  );

  Serial.print(
    node2Data.displacement,
    2
  );

  Serial.println(
    " mm"
  );


  Serial.print(
    "Overall Status:  "
  );

  Serial.println(
    statusText(
      node2Data.overallStatus
    )
  );


  // ===================================================
  // CONNECTIONS
  // ===================================================

  Serial.println();


  Serial.print(
    "ESP-NOW:         "
  );

  if (
    node1Connected
  ) {

    Serial.println(
      "NODE 1 CONNECTED"
    );

  } else {

    Serial.println(
      "WAITING FOR NODE 1"
    );
  }


  Serial.print(
    "Wi-Fi:           "
  );

  if (
    WiFi.status()
    == WL_CONNECTED
  ) {

    Serial.println(
      "CONNECTED"
    );

  } else {

    Serial.println(
      "DISCONNECTED"
    );
  }


  Serial.println(
    "========================================"
  );
}


// =====================================================
// SETUP
// =====================================================

void setup() {

  Serial.begin(115200);

  delay(1500);


  Serial.println();
  Serial.println();
  Serial.println(
    "========================================"
  );

  Serial.println(
    "             NODE 2 START"
  );

  Serial.println(
    "========================================"
  );


  // ===================================================
  // PINS
  // ===================================================

  pinMode(
    VIBRATION_PIN,
    INPUT
  );

  pinMode(
    POT_PIN,
    INPUT
  );


  Serial.println(
    "[1/8] Pins initialized."
  );


  // ===================================================
  // I2C
  // ===================================================

  Wire.begin(
    SDA_PIN,
    SCL_PIN
  );

  Wire.setClock(
    400000
  );


  Serial.println(
    "[2/8] I2C initialized."
  );


  // ===================================================
  // MPU
  // ===================================================

  Serial.println(
    "[3/8] Initializing MPU6050..."
  );


  if (
    !initMPU()
  ) {

    Serial.println(
      "SYSTEM STOPPED: MPU6050 error."
    );


    while (true) {
      delay(1000);
    }
  }


  // ===================================================
  // WIFI RADIO
  // ===================================================

  Serial.println(
    "[4/8] Starting Wi-Fi..."
  );


  WiFi.mode(
    WIFI_STA
  );


  WiFi.setSleep(
    false
  );


  printMAC();


  if (
    !connectWiFi()
  ) {

    Serial.println(
      "WARNING: Wi-Fi unavailable."
    );

    Serial.println(
      "ESP-NOW will still be initialized."
    );
  }


  // ===================================================
  // CHANNEL
  // ===================================================

  Serial.println(
    "[5/8] Checking wireless channel..."
  );


  if (
    WiFi.status()
    == WL_CONNECTED
  ) {

    if (
      !checkChannel()
    ) {

      Serial.println();
      Serial.println(
        "NODE 2 CANNOT CONTINUE WITH"
      );

      Serial.println(
        "ESP-NOW + Wi-Fi until the router"
      );

      Serial.println(
        "is configured for channel 1."
      );

      while (true) {
        delay(2000);
      }
    }

  } else {

    Serial.println(
      "Wi-Fi not connected."
    );

    Serial.println(
      "Channel cannot be verified yet."
    );
  }


  // ===================================================
  // ESP-NOW
  // ===================================================

  Serial.println(
    "[6/8] Initializing ESP-NOW..."
  );


  if (
    !initESPNow()
  ) {

    Serial.println(
      "SYSTEM STOPPED: ESP-NOW error."
    );


    while (true) {
      delay(1000);
    }
  }


  // ===================================================
  // ZERO CALIBRATION
  // ===================================================

  Serial.println(
    "[7/8] Setting Node 2 initial position to ZERO..."
  );


  if (
    !calibrateInitialPosition()
  ) {

    Serial.println(
      "SYSTEM STOPPED: Calibration error."
    );


    while (true) {
      delay(1000);
    }
  }


  // ===================================================
  // READY
  // ===================================================

  Serial.println(
    "[8/8] Startup complete."
  );


  Serial.println();
  Serial.println(
    "========================================"
  );

  Serial.println(
    "             NODE 2 READY"
  );

  Serial.println(
    "========================================"
  );

  Serial.println(
    "Node 2 initial position: ZERO"
  );

  Serial.println(
    "ESP-NOW receiver: ACTIVE"
  );

  Serial.print(
    "Wireless channel: "
  );

  Serial.println(
    ESPNOW_CHANNEL
  );

  Serial.println(
    "Serial update: every 2 seconds"
  );

  Serial.println(
    "Server update: every 2 seconds"
  );

  Serial.println(
    "========================================"
  );

  Serial.println();
}


// =====================================================
// LOOP
// =====================================================

void loop() {

  // ===================================================
  // WIFI RECOVERY
  // ===================================================

  if (
    WiFi.status()
    != WL_CONNECTED
  ) {

    if (
      millis() -
      lastWiFiReconnect
      >=
      WIFI_RECONNECT_INTERVAL
    ) {

      lastWiFiReconnect =
        millis();


      Serial.println(
        "Wi-Fi disconnected. Reconnecting..."
      );


      connectWiFi();


      // If Wi-Fi reconnects to the wrong channel,
      // ESP-NOW cannot coexist correctly.

      if (
        WiFi.status()
        == WL_CONNECTED
      ) {

        checkChannel();
      }
    }
  }


  // ===================================================
  // UPDATE NODE 2 SENSORS
  // ===================================================

  updateNode2Sensors();


  // ===================================================
  // SERIAL DISPLAY
  // ===================================================

  if (
    millis() -
    lastDisplay
    >=
    DISPLAY_INTERVAL
  ) {

    lastDisplay =
      millis();


    printNodeStatus();
  }


  // ===================================================
  // SEND TO SERVER
  // ===================================================

  if (
    millis() -
    lastServerSend
    >=
    SERVER_INTERVAL
  ) {

    lastServerSend =
      millis();


    if (
      WiFi.status()
      == WL_CONNECTED
    ) {

      bool sent =
        sendToServer();


      if (!sent) {

        Serial.println(
          "Server update: FAILED"
        );

      } else {

        Serial.println(
          "Server update: OK"
        );
      }
    }
  }


  delay(10);
}
