#include <Arduino.h>
#include <Wire.h>
#include <WiFi.h>
#include <esp_now.h>
#include <esp_wifi.h>

// =====================================================
// NODE 1 - ESP32
// MPU6050 + Vibration Sensor + Potentiometer
// ESP-NOW -> NODE 2
// =====================================================

// -------------------- PINS --------------------

#define SDA_PIN       21
#define SCL_PIN       22

#define VIBRATION_PIN 27
#define POT_PIN       34


// -------------------- MPU6050 --------------------

#define MPU_ADDR          0x68
#define PWR_MGMT_1        0x6B
#define SMPLRT_DIV        0x19
#define CONFIG_REG        0x1A
#define GYRO_CONFIG       0x1B
#define ACCEL_CONFIG      0x1C
#define ACCEL_XOUT_H      0x3B
#define WHO_AM_I          0x75


// =====================================================
// IMPORTANT
// NODE 2 IS CURRENTLY CONNECTED TO WI-FI CHANNEL 1
// =====================================================

#define ESPNOW_CHANNEL 1


// =====================================================
// NODE 2 MAC
//
// REPLACE THESE 6 BYTES WITH THE MAC PRINTED BY NODE 2
//
// Example:
// NODE 2 MAC: 68:09:47:74:A3:94
//
// Then use:
// =====================================================

uint8_t node2MAC[] = {
  0x68,
  0x09,
  0x47,
  0x74,
  0xA3,
  0x94
};


// =====================================================
// STATUS
// =====================================================

#define NORMAL    0
#define MEDIUM    1
#define HIGH      2
#define CRITICAL  3


// =====================================================
// DATA PACKET
// MUST BE IDENTICAL TO NODE 2
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


SensorData node1Data;


// =====================================================
// TILT BASELINE
// =====================================================

float baselineTiltX = 0.0;
float baselineTiltY = 0.0;


// =====================================================
// MPU WRITE
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

  uint8_t result =
    Wire.endTransmission();

  if (result != 0) {

    Serial.print(
      "MPU write error: "
    );

    Serial.println(result);
  }
}


// =====================================================
// MPU READ
// =====================================================

bool readMPU(
  uint8_t reg,
  uint8_t *buffer,
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

    buffer[i] =
      Wire.read();
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


  writeMPU(
    GYRO_CONFIG,
    0x08
  );


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


  // ±8g

  ax =
    rawAx / 4096.0;

  ay =
    rawAy / 4096.0;

  az =
    rawAz / 4096.0;


  // ±500 deg/s

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
// ESP-NOW SEND CALLBACK
// =====================================================

void onDataSent(
  const wifi_tx_info_t *info,
  esp_now_send_status_t status
) {

  if (
    status ==
    ESP_NOW_SEND_SUCCESS
  ) {

    Serial.println(
      "ESP-NOW: DATA SENT SUCCESSFULLY"
    );

  } else {

    Serial.println(
      "ESP-NOW: SEND FAILED"
    );
  }
}


// =====================================================
// PRINT MAC
// =====================================================

void printMAC() {

  uint8_t mac[6];


  esp_err_t result =
    esp_wifi_get_mac(
      WIFI_IF_STA,
      mac
    );


  if (
    result != ESP_OK
  ) {

    Serial.println(
      "Could not read MAC."
    );

    return;
  }


  Serial.printf(
    "NODE 1 MAC: %02X:%02X:%02X:%02X:%02X:%02X\n",
    mac[0],
    mac[1],
    mac[2],
    mac[3],
    mac[4],
    mac[5]
  );
}


// =====================================================
// PRINT NODE 2 MAC
// =====================================================

void printNode2MAC() {

  Serial.printf(
    "NODE 2 TARGET MAC: %02X:%02X:%02X:%02X:%02X:%02X\n",
    node2MAC[0],
    node2MAC[1],
    node2MAC[2],
    node2MAC[3],
    node2MAC[4],
    node2MAC[5]
  );
}


// =====================================================
// SET CHANNEL
// =====================================================

bool setESPNowChannel() {

  esp_err_t result =
    esp_wifi_set_channel(
      ESPNOW_CHANNEL,
      WIFI_SECOND_CHAN_NONE
    );


  if (
    result != ESP_OK
  ) {

    Serial.print(
      "Channel setup failed: "
    );

    Serial.println(
      result
    );

    return false;
  }


  Serial.print(
    "ESP-NOW Channel: "
  );

  Serial.println(
    ESPNOW_CHANNEL
  );


  return true;
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
    "TILT CALIBRATION"
  );

  Serial.println(
    "Keep NODE 1 completely still"
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


  if (
    validSamples > 0
  ) {

    baselineTiltX =
      sumX /
      validSamples;


    baselineTiltY =
      sumY /
      validSamples;
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

  Serial.println();
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
    "        NODE 1 - ESP32"
  );

  Serial.println(
    "========================================"
  );


  // --------------------
  // PINS
  // --------------------

  pinMode(
    VIBRATION_PIN,
    INPUT
  );

  pinMode(
    POT_PIN,
    INPUT
  );


  // --------------------
  // I2C
  // --------------------

  Wire.begin(
    SDA_PIN,
    SCL_PIN
  );

  Wire.setClock(400000);


  // --------------------
  // MPU
  // --------------------

  if (
    !initMPU()
  ) {

    while (true) {
      delay(1000);
    }
  }


  // --------------------
  // WIFI STA
  // --------------------

  WiFi.mode(
    WIFI_STA
  );

  delay(100);


  printMAC();


  // --------------------
  // TARGET
  // --------------------

  printNode2MAC();


  // --------------------
  // CHANNEL
  // --------------------

  if (
    !setESPNowChannel()
  ) {

    while (true) {
      delay(1000);
    }
  }


  // --------------------
  // CALIBRATION
  // --------------------

  calibrateTilt();


  // --------------------
  // ESP-NOW
  // --------------------

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


  Serial.println(
    "ESP-NOW initialized."
  );


  esp_now_register_send_cb(
    onDataSent
  );


  // --------------------
  // PEER
  // --------------------

  esp_now_peer_info_t peerInfo = {};


  memcpy(
    peerInfo.peer_addr,
    node2MAC,
    6
  );


  peerInfo.channel =
    ESPNOW_CHANNEL;


  peerInfo.encrypt =
    false;


  if (
    esp_now_add_peer(
      &peerInfo
    )
    != ESP_OK
  ) {

    Serial.println(
      "ERROR: Failed to add Node 2."
    );

    while (true) {
      delay(1000);
    }
  }


  Serial.println();
  Serial.println(
    "========================================"
  );

  Serial.println(
    "NODE 1 READY"
  );

  Serial.println(
    "========================================"
  );

  Serial.print(
    "ESP-NOW Channel: "
  );

  Serial.println(
    ESPNOW_CHANNEL
  );

  Serial.println();
}


// =====================================================
// LOOP
// =====================================================

void loop() {

  float ax, ay, az;
  float gx, gy, gz;


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

    Serial.println(
      "ERROR: MPU6050 read failed."
    );

    delay(1000);

    return;
  }


  // ===================================================
  // TILT
  // ===================================================

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


  // ===================================================
  // VIBRATION
  // ===================================================

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


  // ===================================================
  // DISPLACEMENT
  // ===================================================

  int potValue =
    analogRead(
      POT_PIN
    );


  float displacement =
    (
      (float)potValue /
      4095.0
    )
    * 20.0;


  // ===================================================
  // STATUS
  // ===================================================

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


  // ===================================================
  // PACKET
  // ===================================================

  node1Data.nodeID =
    1;


  node1Data.tiltX =
    tiltX;


  node1Data.tiltY =
    tiltY;


  node1Data.vibration =
    vibration;


  node1Data.displacement =
    displacement;


  node1Data.vibrationDetected =
    vibrationDetected;


  node1Data.tiltStatus =
    tiltStatus;


  node1Data.vibrationStatus =
    vibrationStatus;


  node1Data.displacementStatus =
    displacementStatus;


  node1Data.overallStatus =
    overallStatus;


  // ===================================================
  // DISPLAY
  // ===================================================

  Serial.println();
  Serial.println(
    "========== NODE 1 =========="
  );


  Serial.print(
    "Tilt X: "
  );

  Serial.print(
    tiltX,
    2
  );

  Serial.println(
    " deg"
  );


  Serial.print(
    "Tilt Y: "
  );

  Serial.print(
    tiltY,
    2
  );

  Serial.println(
    " deg"
  );


  Serial.print(
    "Vibration: "
  );

  Serial.print(
    vibration,
    3
  );

  Serial.println(
    " g"
  );


  Serial.print(
    "Displacement: "
  );

  Serial.print(
    displacement,
    2
  );

  Serial.println(
    " mm"
  );


  Serial.print(
    "Tilt Status: "
  );

  Serial.println(
    statusText(
      tiltStatus
    )
  );


  Serial.print(
    "Vibration Status: "
  );

  Serial.println(
    statusText(
      vibrationStatus
    )
  );


  Serial.print(
    "Displacement Status: "
  );

  Serial.println(
    statusText(
      displacementStatus
    )
  );


  Serial.print(
    "Overall Status: "
  );

  Serial.println(
    statusText(
      overallStatus
    )
  );


  // ===================================================
  // ESP-NOW SEND
  // ===================================================

  Serial.println(
    "Sending to NODE 2..."
  );


  esp_err_t result =
    esp_now_send(
      node2MAC,
      (uint8_t *)&node1Data,
      sizeof(node1Data)
    );


  if (
    result != ESP_OK
  ) {

    Serial.print(
      "ESP-NOW send error: "
    );

    Serial.println(
      result
    );
  }


  delay(1000);
}