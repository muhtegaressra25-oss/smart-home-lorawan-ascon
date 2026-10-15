#include <Arduino.h>

#include <U8g2lib.h>

#include <Wire.h>

#include <Adafruit_PN532.h>

#include <DHT.h>

#include <PZEM004Tv30.h>

#include <RadioLib.h>

#include <Preferences.h>

#include <WiFi.h>

#include <time.h>

#include <sys/time.h>



// =====================================================

// ASCON-AEAD128

// =====================================================

extern "C" {

#include "api.h"

#include "crypto_aead.h"

}



// =====================================================

// SMART HOME + LORAWAN + ASCON-AEAD128

// ESP32 HELTEC V3 WIFI LORA

// Region : AS923-2

// Radio  : SX1262

//

// UPLINK:

// 14 byte sensor

// -> ASCON-AEAD128

// -> nonce 16 B + ciphertext 14 B + tag 16 B

// -> total 46 byte

//

// DOWNLINK:

// 0x00 = LIGHT OFF

// 0x01 = LIGHT ON

// 0x02 = DOOR OPEN

// 0x03 = DOOR CLOSE

// =====================================================





// =====================================================

// PIN CONFIGURATION

// =====================================================



// OLED

#define OLED_RST    21

#define OLED_SCL    18

#define OLED_SDA    17

#define VEXT_PIN    36



// Smart Home

#define PIR_PIN      6

#define SERVO_PIN    2

#define RELAY_LIGHT  7

#define RELAY_FAN    3

#define DHT_PIN      1



// PN532 - Software SPI

#define PN532_SCK   34

#define PN532_MISO  33

#define PN532_MOSI  26

#define PN532_SS    35

#define PN532_IRQ   41

#define PN532_RST   42



// PZEM-004T V3

#define PZEM_RX_PIN 4

#define PZEM_TX_PIN 5



// SX1262

#define LORA_NSS    8

#define LORA_RST    12

#define LORA_BUSY   13

#define LORA_DIO1   14





// =====================================================

// RELAY

// =====================================================



#define RELAY_ON  LOW

#define RELAY_OFF HIGH





// =====================================================

// SERVO

// =====================================================



const int pwmFreq = 50;

const int pwmResolution = 12;



const int SERVO_0_DEGREES  = 102;

const int SERVO_90_DEGREES = 307;





// =====================================================

// OBJECTS

// =====================================================



U8G2_SSD1306_128X64_NONAME_F_HW_I2C u8g2(

  U8G2_R0,

  OLED_RST,

  OLED_SCL,

  OLED_SDA

);



Adafruit_PN532 nfc(

  PN532_SCK,

  PN532_MISO,

  PN532_MOSI,

  PN532_SS

);



DHT dht(DHT_PIN, DHT22);



PZEM004Tv30 pzem(

  Serial1,

  PZEM_RX_PIN,

  PZEM_TX_PIN

);





// =====================================================

// LORAWAN

// =====================================================



SX1262 radio = new Module(

  LORA_NSS,

  LORA_DIO1,

  LORA_RST,

  LORA_BUSY

);



const LoRaWANBand_t Region = AS923_2;

const uint8_t subBand = 0;



LoRaWANNode node(

  &radio,

  &Region,

  subBand

);





// =====================================================

// NVS STORAGE LORAWAN

// =====================================================



Preferences loraStore;





// =====================================================

// OTAA

// =====================================================



uint64_t joinEUI =

  0x0000000000000001;



uint64_t devEUI =

  0xB0BD3C04A7AC0001;





// LoRaWAN AppKey

uint8_t appKey[] = {

  0x00, 0x11, 0x22, 0x33,

  0x44, 0x55, 0x66, 0x77,

  0x88, 0x99, 0xAA, 0xBB,

  0xCC, 0xDD, 0xEE, 0xFF

};





// LoRaWAN 1.0.3

// NwkKey sama dengan AppKey

uint8_t nwkKey[] = {

  0x00, 0x11, 0x22, 0x33,

  0x44, 0x55, 0x66, 0x77,

  0x88, 0x99, 0xAA, 0xBB,

  0xCC, 0xDD, 0xEE, 0xFF

};





// =====================================================

// ASCON-AEAD128

// =====================================================



// Key aplikasi ASCON.

// BERBEDA dengan AppKey LoRaWAN.

const uint8_t ASCON_KEY[16] = {

  0x00, 0x01, 0x02, 0x03,

  0x04, 0x05, 0x06, 0x07,

  0x08, 0x09, 0x0A, 0x0B,

  0x0C, 0x0D, 0x0E, 0x0F

};





// Penyimpanan counter nonce ASCON

Preferences asconPreferences;



uint64_t asconCounter = 0;





// Prefix nonce berdasarkan DevEUI

const uint8_t ASCON_NONCE_PREFIX[8] = {

  0xB0, 0xBD, 0x3C, 0x04,

  0xA7, 0xAC, 0x00, 0x01

};





// =====================================================

// WIFI + NTP TIME FOR T0 TIMESTAMP

// =====================================================



// GANTI dengan WiFi yang dipakai saat pengujian.

// WiFi hanya dipakai untuk sinkronisasi jam ESP32.

const char* WIFI_SSID = "TP-Link_3758";

const char* WIFI_PASSWORD = "96028643";



// WIB = UTC+7

const long GMT_OFFSET_SEC = 7 * 3600;

const int DAYLIGHT_OFFSET_SEC = 0;



bool nodeTimeSynchronized = false;



// Menyimpan timestamp dalam format:

// YYYY-MM-DD HH:MM:SS.uuuuuu

void formatTimestamp(const struct timeval &tv, char *buffer, size_t bufferSize) {

  time_t seconds = tv.tv_sec;

  struct tm timeInfo;

  localtime_r(&seconds, &timeInfo);



  char dateTime[32];

  strftime(

    dateTime,

    sizeof(dateTime),

    "%Y-%m-%d %H:%M:%S",

    &timeInfo

  );



  snprintf(

    buffer,

    bufferSize,

    "%s.%06ld",

    dateTime,

    (long)tv.tv_usec

  );

}





bool synchronizeNodeTime() {

  Serial.println();

  Serial.println("[TIME] Connecting WiFi for NTP synchronization...");



  WiFi.mode(WIFI_STA);

  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);



  unsigned long wifiStart = millis();



  while (

    WiFi.status() != WL_CONNECTED &&

    millis() - wifiStart < 20000UL

  ) {

    delay(250);

    Serial.print(".");

  }



  Serial.println();



  if (WiFi.status() != WL_CONNECTED) {

    Serial.println("[TIME] WiFi connection FAILED.");

    Serial.println("[TIME] T0 absolute timestamp unavailable.");

    WiFi.disconnect(true);

    WiFi.mode(WIFI_OFF);

    return false;

  }



  Serial.println("[TIME] WiFi connected.");



  configTime(
    GMT_OFFSET_SEC,
    DAYLIGHT_OFFSET_SEC,
    "192.168.0.107"
  );



  struct tm timeInfo;

  unsigned long ntpStart = millis();



  while (

    !getLocalTime(&timeInfo, 1000) &&

    millis() - ntpStart < 20000UL

  ) {

    Serial.println("[TIME] Waiting for NTP...");

  }



  bool ok = getLocalTime(&timeInfo, 1000);



  if (ok) {

    struct timeval now;

    gettimeofday(&now, nullptr);



    char timestamp[40];

    formatTimestamp(now, timestamp, sizeof(timestamp));



    Serial.print("[TIME] ESP32 synchronized : ");

    Serial.println(timestamp);

  } else {

    Serial.println("[TIME] NTP synchronization FAILED.");

  }



  WiFi.disconnect(true);

  WiFi.mode(WIFI_OFF);



  return ok;

}





// =====================================================

// TIMER

// =====================================================



unsigned long lastMotionTime = 0;

unsigned long displayUpdateTime = 0;

unsigned long nfcScanTime = 0;

unsigned long loraTxTime = 0;



const unsigned long LORA_TX_INTERVAL =

  15000UL;





// =====================================================

// SYSTEM VARIABLES

// =====================================================



bool systemActive = false;

bool motionDetected = false;

bool accessGrantedFlag = false;

bool lorawanJoined = false;





// =====================================================

// SENSOR VALUES

// =====================================================



float voltage = 0.0;

float current = 0.0;

float power = 0.0;

float energy = 0.0;



float temperature = 25.0;

float humidity = 50.0;





// =====================================================

// PRINT HEX

// =====================================================



void printHexArray(

  const uint8_t *data,

  size_t length

) {

  for (size_t i = 0; i < length; i++) {



    if (data[i] < 0x10) {

      Serial.print("0");

    }



    Serial.print(data[i], HEX);



    if (i < length - 1) {

      Serial.print(" ");

    }

  }



  Serial.println();

}





// =====================================================

// CREATE UNIQUE ASCON NONCE

// =====================================================



bool getNextAsconNonce(

  uint8_t nonce[16]

) {



  uint64_t nextCounter =

    asconCounter + 1;



  // Counter disimpan sebelum nonce digunakan.

  // Dengan begitu nonce tidak kembali ke nilai lama

  // setelah ESP32 restart.

  size_t written =

    asconPreferences.putULong64(

      "counter",

      nextCounter

    );



  if (written != sizeof(uint64_t)) {



    Serial.println(

      "[ASCON] ERROR saving nonce counter!"

    );



    return false;

  }



  asconCounter =

    nextCounter;





  // Byte 0 - 7 = prefix device

  for (int i = 0; i < 8; i++) {

    nonce[i] =

      ASCON_NONCE_PREFIX[i];

  }





  // Byte 8 - 15 = counter 64-bit big endian

  nonce[8] =

    (asconCounter >> 56) & 0xFF;



  nonce[9] =

    (asconCounter >> 48) & 0xFF;



  nonce[10] =

    (asconCounter >> 40) & 0xFF;



  nonce[11] =

    (asconCounter >> 32) & 0xFF;



  nonce[12] =

    (asconCounter >> 24) & 0xFF;



  nonce[13] =

    (asconCounter >> 16) & 0xFF;



  nonce[14] =

    (asconCounter >> 8) & 0xFF;



  nonce[15] =

    asconCounter & 0xFF;



  return true;

}





// =====================================================

// SERVO

// =====================================================



void moveServo(

  int dutyValue

) {

  ledcWrite(

    SERVO_PIN,

    dutyValue

  );

}





// =====================================================

// OLED MESSAGE

// =====================================================



void showMessage(

  const char* line1,

  const char* line2 = ""

) {



  u8g2.clearBuffer();



  u8g2.setFont(

    u8g2_font_6x10_tf

  );



  u8g2.drawStr(

    0,

    25,

    line1

  );



  if (strlen(line2) > 0) {



    u8g2.drawStr(

      0,

      45,

      line2

    );

  }



  u8g2.sendBuffer();

}





// =====================================================

// READ SENSOR

// =====================================================



void readSensors() {



  // -------------------------

  // PZEM

  // -------------------------



  float tempVoltage =

    pzem.voltage();



  if (

    !isnan(tempVoltage) &&

    tempVoltage > 0

  ) {



    voltage =

      tempVoltage;



    float tempCurrent =

      pzem.current();



    float tempPower =

      pzem.power();



    float tempEnergy =

      pzem.energy();



    if (!isnan(tempCurrent)) {

      current =

        tempCurrent;

    }



    if (!isnan(tempPower)) {

      power =

        tempPower;

    }



    if (!isnan(tempEnergy)) {

      energy =

        tempEnergy;

    }

  }





  // -------------------------

  // DHT22

  // -------------------------



  float tempT =

    dht.readTemperature();



  float tempH =

    dht.readHumidity();



  if (

    !isnan(tempT) &&

    tempT > -10.0 &&

    tempT < 60.0

  ) {

    temperature =

      tempT;

  }



  if (

    !isnan(tempH) &&

    tempH >= 0 &&

    tempH <= 100

  ) {

    humidity =

      tempH;

  }

}





// =====================================================

// UPDATE OLED

// =====================================================



void updateDisplay() {



  u8g2.clearBuffer();



  u8g2.setFont(

    u8g2_font_6x10_tf

  );



  u8g2.drawStr(

    0,

    10,

    "=== SMART HOME ==="

  );



  u8g2.setCursor(0, 22);

  u8g2.print("Volt : ");

  u8g2.print(voltage, 1);

  u8g2.print(" V");



  u8g2.setCursor(0, 32);

  u8g2.print("Power: ");

  u8g2.print(power, 1);

  u8g2.print(" W");



  u8g2.setCursor(0, 42);

  u8g2.print("Temp : ");

  u8g2.print(temperature, 1);

  u8g2.print(" C");



  u8g2.setCursor(0, 52);

  u8g2.print("PIR  : ");



  if (motionDetected) {

    u8g2.print("ACTIVE");

  } else {

    u8g2.print("STILL");

  }



  u8g2.setCursor(0, 62);



  if (systemActive) {

    u8g2.print("STATUS: OCCUPIED");

  } else {

    u8g2.print("STATUS: SECURED");

  }



  u8g2.sendBuffer();

}





// =====================================================

// NFC SCANNER

// =====================================================



void scanNFC() {



  if (systemActive) {

    return;

  }



  if (

    millis() - nfcScanTime <

    250

  ) {

    return;

  }



  nfcScanTime =

    millis();



  uint8_t uid[] = {

    0, 0, 0, 0, 0, 0, 0

  };



  uint8_t uidLength = 0;



  uint8_t success =

    nfc.readPassiveTargetID(

      PN532_MIFARE_ISO14443A,

      uid,

      &uidLength,

      80

    );



  if (success) {



    Serial.println();



    Serial.println(

      "=============================="

    );



    Serial.println(

      "NFC CARD DETECTED"

    );



    Serial.print(

      "UID: "

    );



    for (

      uint8_t i = 0;

      i < uidLength;

      i++

    ) {



      if (uid[i] < 0x10) {

        Serial.print("0");

      }



      Serial.print(

        uid[i],

        HEX

      );



      Serial.print(" ");

    }



    Serial.println();



    Serial.println(

      "=============================="

    );



    systemActive =

      true;



    accessGrantedFlag =

      true;



    lastMotionTime =

      millis();



    showMessage(

      "ACCESS ACCEPTED",

      "WELCOME"

    );



    delay(1000);



    moveServo(

      SERVO_90_DEGREES

    );



    showMessage(

      "DOOR OPENING",

      ""

    );



    delay(1000);



    digitalWrite(

      RELAY_LIGHT,

      RELAY_ON

    );



    Serial.println(

      "Light ON"

    );



    Serial.println(

      "Door OPEN"

    );

  }

}





// =====================================================

// FAN AUTOMATIC

// =====================================================



void climateControl() {



  // Fan hanya aktif jika sistem sudah aktif

  // melalui RFID atau command DOOR OPEN.



  if (!systemActive) {



    digitalWrite(

      RELAY_FAN,

      RELAY_OFF

    );



    return;

  }





  // FAN ON >= 25 C

  if (temperature >= 25.0) {



    if (

      digitalRead(RELAY_FAN) !=

      RELAY_ON

    ) {



      digitalWrite(

        RELAY_FAN,

        RELAY_ON

      );



      Serial.println(

        "Temperature >= 25C -> FAN ON"

      );

    }



  } else {



    if (

      digitalRead(RELAY_FAN) !=

      RELAY_OFF

    ) {



      digitalWrite(

        RELAY_FAN,

        RELAY_OFF

      );



      Serial.println(

        "Temperature < 25C -> FAN OFF"

      );

    }

  }

}





// =====================================================

// DOWNLINK ACTUATOR CONTROL

//

// 00 = LIGHT OFF

// 01 = LIGHT ON

// 02 = DOOR OPEN

// 03 = DOOR CLOSE

// =====================================================



void processDownlink(

  uint8_t *data,

  size_t len

) {



  if (len == 0) {

    return;

  }



  Serial.println();



  Serial.println(

    "======================================"

  );



  Serial.println(

    "DOWNLINK ACTUATOR COMMAND"

  );



  Serial.print(

    "Downlink HEX : "

  );



  for (

    size_t i = 0;

    i < len;

    i++

  ) {



    if (data[i] < 0x10) {

      Serial.print("0");

    }



    Serial.print(

      data[i],

      HEX

    );



    Serial.print(" ");

  }



  Serial.println();





  // -------------------------

  // LIGHT OFF

  // -------------------------



  if (data[0] == 0x00) {



    digitalWrite(

      RELAY_LIGHT,

      RELAY_OFF

    );



    Serial.println(

      "DASHBOARD -> LIGHT OFF"

    );



    showMessage(

      "REMOTE CONTROL",

      "LIGHT OFF"

    );

  }





  // -------------------------

  // LIGHT ON

  // -------------------------



  else if (data[0] == 0x01) {



    digitalWrite(

      RELAY_LIGHT,

      RELAY_ON

    );



    Serial.println(

      "DASHBOARD -> LIGHT ON"

    );



    showMessage(

      "REMOTE CONTROL",

      "LIGHT ON"

    );

  }





  // -------------------------

  // DOOR OPEN

  // -------------------------



  else if (data[0] == 0x02) {



    moveServo(

      SERVO_90_DEGREES

    );



    systemActive =

      true;



    accessGrantedFlag =

      true;



    lastMotionTime =

      millis();



    Serial.println(

      "DASHBOARD -> DOOR OPEN"

    );



    showMessage(

      "REMOTE CONTROL",

      "DOOR OPEN"

    );

  }





  // -------------------------

  // DOOR CLOSE

  // -------------------------



  else if (data[0] == 0x03) {



    moveServo(

      SERVO_0_DEGREES

    );



    accessGrantedFlag =

      false;



    // systemActive TIDAK dibuat false.

    // Ini mempertahankan perilaku firmware

    // aktuator yang sebelumnya sudah berhasil.



    Serial.println(

      "DASHBOARD -> DOOR CLOSED"

    );



    showMessage(

      "REMOTE CONTROL",

      "DOOR CLOSED"

    );

  }





  // -------------------------

  // UNKNOWN

  // -------------------------



  else {



    Serial.print(

      "Unknown command: 0x"

    );



    if (data[0] < 0x10) {

      Serial.print("0");

    }



    Serial.println(

      data[0],

      HEX

    );

  }



  Serial.println(

    "======================================"

  );

}





// =====================================================

// SAVE LORAWAN NONCES

// =====================================================



void saveLoRaWANNonces() {



  uint8_t *nonceBuffer =

    node.getBufferNonces();



  if (nonceBuffer == nullptr) {



    Serial.println(

      "[LoRaWAN] Nonce buffer unavailable."

    );



    return;

  }



  loraStore.begin(

    "lorawan",

    false

  );



  loraStore.putBytes(

    "nonces",

    nonceBuffer,

    RADIOLIB_LORAWAN_NONCES_BUF_SIZE

  );



  loraStore.end();



  Serial.println(

    "[LoRaWAN] Nonces saved to NVS."

  );

}





// =====================================================

// SAVE LORAWAN SESSION

// =====================================================



void saveLoRaWANSession() {



  uint8_t *sessionBuffer =

    node.getBufferSession();



  if (sessionBuffer == nullptr) {



    Serial.println(

      "[LoRaWAN] Session buffer unavailable."

    );



    return;

  }



  loraStore.begin(

    "lorawan",

    false

  );



  loraStore.putBytes(

    "session",

    sessionBuffer,

    RADIOLIB_LORAWAN_SESSION_BUF_SIZE

  );



  loraStore.end();

}





// =====================================================

// RESTORE LORAWAN STATE

// =====================================================



bool restoreLoRaWANState() {



  bool nonceOK =

    false;



  bool sessionOK =

    false;



  loraStore.begin(

    "lorawan",

    true

  );





  // -------------------------

  // NONCES

  // -------------------------



  size_t nonceLength =

    loraStore.getBytesLength(

      "nonces"

    );



  if (

    nonceLength ==

    RADIOLIB_LORAWAN_NONCES_BUF_SIZE

  ) {



    uint8_t nonceBuffer[

      RADIOLIB_LORAWAN_NONCES_BUF_SIZE

    ];



    loraStore.getBytes(

      "nonces",

      nonceBuffer,

      sizeof(nonceBuffer)

    );



    int16_t nonceState =

      node.setBufferNonces(

        nonceBuffer

      );



    if (

      nonceState ==

      RADIOLIB_ERR_NONE

    ) {



      nonceOK =

        true;



      Serial.println(

        "[LoRaWAN] Nonces restored from NVS."

      );



    } else {



      Serial.print(

        "[LoRaWAN] Nonce restore error = "

      );



      Serial.println(

        nonceState

      );

    }



  } else {



    Serial.println(

      "[LoRaWAN] No saved nonces."

    );

  }





  // -------------------------

  // SESSION

  // -------------------------



  if (nonceOK) {



    size_t sessionLength =

      loraStore.getBytesLength(

        "session"

      );



    if (

      sessionLength ==

      RADIOLIB_LORAWAN_SESSION_BUF_SIZE

    ) {



      uint8_t sessionBuffer[

        RADIOLIB_LORAWAN_SESSION_BUF_SIZE

      ];



      loraStore.getBytes(

        "session",

        sessionBuffer,

        sizeof(sessionBuffer)

      );



      int16_t sessionState =

        node.setBufferSession(

          sessionBuffer

        );



      if (

        sessionState ==

        RADIOLIB_ERR_NONE

      ) {



        sessionOK =

          true;



        Serial.println(

          "[LoRaWAN] Session restored from NVS."

        );



      } else {



        Serial.print(

          "[LoRaWAN] Session restore error = "

        );



        Serial.println(

          sessionState

        );

      }

    }

  }



  loraStore.end();



  return sessionOK;

}





// =====================================================

// BUILD + ASCON ENCRYPT + SEND LORAWAN

// =====================================================



void sendLoRaWANData() {



  if (!lorawanJoined) {

    return;

  }



  Serial.println();



  Serial.println(

    "======================================"

  );



  Serial.println(

    "Preparing ASCON encrypted payload..."

  );





  // =================================================

  // CONVERT SENSOR VALUES

  // =================================================



  int16_t txTemp =

    (int16_t)(

      temperature * 10.0

    );



  uint16_t txHum =

    (uint16_t)(

      humidity * 10.0

    );



  uint16_t txVolt =

    (uint16_t)(

      voltage * 10.0

    );



  uint16_t txCurrent =

    (uint16_t)(

      current * 100.0

    );



  uint16_t txPower =

    (uint16_t)(

      power * 10.0

    );



  uint16_t txEnergy =

    (uint16_t)(

      energy * 100.0

    );



  uint8_t txMotion =

    motionDetected ? 1 : 0;



  uint8_t txAccess =

    accessGrantedFlag ? 1 : 0;





  // =================================================

  // PLAINTEXT SENSOR = 14 BYTE

  // =================================================



  uint8_t plaintext[14];



  // Temperature

  plaintext[0] =

    (txTemp >> 8) & 0xFF;



  plaintext[1] =

    txTemp & 0xFF;



  // Humidity

  plaintext[2] =

    (txHum >> 8) & 0xFF;



  plaintext[3] =

    txHum & 0xFF;



  // Motion

  plaintext[4] =

    txMotion;



  // Voltage

  plaintext[5] =

    (txVolt >> 8) & 0xFF;



  plaintext[6] =

    txVolt & 0xFF;



  // Current

  plaintext[7] =

    (txCurrent >> 8) & 0xFF;



  plaintext[8] =

    txCurrent & 0xFF;



  // Power

  plaintext[9] =

    (txPower >> 8) & 0xFF;



  plaintext[10] =

    txPower & 0xFF;



  // Energy

  plaintext[11] =

    (txEnergy >> 8) & 0xFF;



  plaintext[12] =

    txEnergy & 0xFF;



  // Access

  plaintext[13] =

    txAccess;





  // =================================================

  // PRINT SENSOR DATA

  // =================================================



  Serial.print("Temperature : ");

  Serial.print(temperature, 1);

  Serial.println(" C");



  Serial.print("Humidity    : ");

  Serial.print(humidity, 1);

  Serial.println(" %");



  Serial.print("PIR         : ");

  Serial.println(txMotion);



  Serial.print("Voltage     : ");

  Serial.print(voltage, 1);

  Serial.println(" V");



  Serial.print("Current     : ");

  Serial.print(current, 2);

  Serial.println(" A");



  Serial.print("Power       : ");

  Serial.print(power, 1);

  Serial.println(" W");



  Serial.print("Energy      : ");

  Serial.print(energy, 2);

  Serial.println(" kWh");



  Serial.print("Access      : ");

  Serial.println(txAccess);



  Serial.println();



  Serial.print(

    "Plaintext 14 B : "

  );



  printHexArray(

    plaintext,

    sizeof(plaintext)

  );





  // =================================================

  // GENERATE UNIQUE NONCE 16 BYTE

  // =================================================



  uint8_t nonce[16];



  if (

    !getNextAsconNonce(nonce)

  ) {



    Serial.println(

      "ASCON encryption CANCELLED!"

    );



    Serial.println(

      "======================================"

    );



    return;

  }



  Serial.print(

    "ASCON counter  : "

  );



  Serial.println(

    (unsigned long long)

    asconCounter

  );



  Serial.print(

    "Nonce 16 B     : "

  );



  printHexArray(

    nonce,

    sizeof(nonce)

  );





  // =================================================

  // ASCON-AEAD128 ENCRYPT

  //

  // plaintext  = 14 byte

  // ciphertext = 14 byte

  // tag        = 16 byte

  //

  // crypto_aead_encrypt() output = 30 byte

  // =================================================



  uint8_t ciphertext[

    14 + CRYPTO_ABYTES

  ];



  unsigned long long

    ciphertextLength = 0;





  // Waktu mulai enkripsi

  unsigned long encryptionStartUs =

    micros();
  int encryptResult =

    crypto_aead_encrypt(
      ciphertext,
      &ciphertextLength,
      plaintext,

      sizeof(plaintext),
      NULL,
      0,
      NULL,
      nonce,
      ASCON_KEY

    );
  // Waktu selesai enkripsi

  unsigned long encryptionEndUs =

    micros();

  unsigned long encryptionTimeUs =

    encryptionEndUs -

    encryptionStartUs;

  Serial.print(

    "ASCON encryption time : "

  );



  Serial.print(

    encryptionTimeUs

  );



  Serial.println(

    " us"

  );





  if (encryptResult != 0) {



    Serial.print(

      "ASCON ENCRYPT ERROR = "

    );



    Serial.println(

      encryptResult

    );



    return;

  }





  if (ciphertextLength != 30) {



    Serial.print(

      "Unexpected ciphertext length = "

    );



    Serial.println(

      (unsigned long)

      ciphertextLength

    );



    return;

  }





  Serial.println(

    "ASCON encryption SUCCESS!"

  );



  Serial.print(

    "Ciphertext + TAG 30 B : "

  );



  printHexArray(

    ciphertext,

    ciphertextLength

  );





  // =================================================

  // FINAL APPLICATION PAYLOAD = 46 BYTE

  //

  // byte  0 - 15 = nonce

  // byte 16 - 29 = ciphertext

  // byte 30 - 45 = authentication tag

  // =================================================



  uint8_t encryptedPayload[46];



  memcpy(

    encryptedPayload,

    nonce,

    16

  );



  memcpy(

    encryptedPayload + 16,

    ciphertext,

    ciphertextLength

  );





  Serial.print(

    "Encrypted LoRaWAN payload 46 B : "

  );



  printHexArray(

    encryptedPayload,

    sizeof(encryptedPayload)

  );





  // =================================================

  // DOWNLINK BUFFER

  // =================================================



  uint8_t downlinkPayload[16] = {0};



  size_t downlinkLength =

    sizeof(downlinkPayload);





  // =================================================

  // SEND 46 B UPLINK + WAIT CLASS A DOWNLINK

  // =================================================



  Serial.println();



  Serial.println(

    "Sending encrypted LoRaWAN uplink..."

  );





  struct timeval t0Node;

  gettimeofday(&t0Node, nullptr);



  char t0Timestamp[40];



  if (nodeTimeSynchronized) {

    formatTimestamp(

      t0Node,

      t0Timestamp,

      sizeof(t0Timestamp)

    );

  } else {

    snprintf(

      t0Timestamp,

      sizeof(t0Timestamp),

      "TIME_NOT_SYNCED"

    );

  }





  // PENGIRIMAN DIMULAI LANGSUNG SETELAH T0 DICATAT

  int16_t state =

    node.sendReceive(

      encryptedPayload,

      sizeof(encryptedPayload),

      1,

      downlinkPayload,

      &downlinkLength

    );





  // Ditampilkan setelah sendReceive agar pencetakan Serial

  // tidak menambah delay T0 -> proses transmisi.

  Serial.print(

    "T0 NODE           : "

  );



  Serial.println(

    t0Timestamp

  );



  Serial.print(

    "Encrypted Payload : "

  );



  Serial.print(

    sizeof(encryptedPayload)

  );



  Serial.println(

    " B"

  );





  // =================================================

  // RESULT

  // =================================================



  if (

    state <

    RADIOLIB_ERR_NONE

  ) {



    Serial.print(

      "LoRaWAN uplink ERROR = "

    );



    Serial.println(

      state

    );



  } else {



    Serial.println(

      "Encrypted LoRaWAN uplink SUCCESS!"

    );





    // Simpan session terbaru termasuk FCnt

    saveLoRaWANSession();





    // =================================================

    // DOWNLINK ACTUATOR

    // =================================================



    if (state > 0) {



      Serial.println(

        "Downlink received."

      );



      Serial.print(

        "Downlink length : "

      );



      Serial.println(

        downlinkLength

      );



      if (

        downlinkLength > 0 &&

        downlinkLength <=

        sizeof(downlinkPayload)

      ) {



        processDownlink(

          downlinkPayload,

          downlinkLength

        );



      } else {



        Serial.println(

          "MAC downlink only."

        );

      }



    } else {



      Serial.println(

        "No downlink."

      );

    }

  }



  Serial.println(

    "======================================"

  );

}





// =====================================================

// SETUP LORAWAN

// =====================================================



void setupLoRaWAN() {



  Serial.println();



  Serial.println(

    "[LoRaWAN] Initializing SX1262..."

  );





  // Heltec SX1262 TCXO

  radio.tcxoVoltage =

    1.6;





  ConfigLoRa_t config;



  config.frequency =

    923.0;





  int16_t state =

    radio.begin(

      config

    );





  if (

    state !=

    RADIOLIB_ERR_NONE

  ) {



    Serial.print(

      "SX1262 initialization FAILED: "

    );



    Serial.println(

      state

    );



    showMessage(

      "SX1262 ERROR",

      "CHECK RADIO"

    );



    return;

  }





  Serial.println(

    "SX1262 initialization OK."

  );





  // =================================================

  // OTAA CONFIGURATION

  // =================================================



  Serial.println(

    "[LoRaWAN] Configuring OTAA..."

  );





  state =

    node.beginOTAA(

      joinEUI,

      devEUI,

      nwkKey,

      appKey

    );





  if (

    state !=

    RADIOLIB_ERR_NONE

  ) {



    Serial.print(

      "beginOTAA ERROR = "

    );



    Serial.println(

      state

    );



    showMessage(

      "OTAA CONFIG",

      "ERROR"

    );



    return;

  }





  Serial.println(

    "OTAA configuration OK."

  );





  // =================================================

  // RESTORE NVS

  // =================================================



  Serial.println(

    "[LoRaWAN] Checking saved NVS state..."

  );





  bool sessionAvailable =

    restoreLoRaWANState();





  // =================================================

  // TRY RESTORED SESSION

  // =================================================



  if (sessionAvailable) {



    Serial.println(

      "[LoRaWAN] Trying saved session..."

    );





    state =

      node.activateOTAA();





    if (

      state ==

      RADIOLIB_LORAWAN_SESSION_RESTORED

    ) {



      lorawanJoined =

        true;



      Serial.println();



      Serial.println(

        "======================================"

      );



      Serial.println(

        " LORAWAN SESSION RESTORED!"

      );



      Serial.println(

        " NO NEW OTAA JOIN REQUIRED"

      );



      Serial.println(

        "======================================"

      );



      showMessage(

        "LORAWAN",

        "SESSION RESTORED"

      );



      delay(1000);



      return;

    }





    Serial.print(

      "[LoRaWAN] Restore result = "

    );



    Serial.println(

      state

    );

  }





  // =================================================

  // NEW OTAA JOIN

  // =================================================



  showMessage(

    "LORAWAN JOIN",

    "PLEASE WAIT..."

  );





  Serial.println();



  Serial.println(

    "======================================"

  );



  Serial.println(

    "Attempting LoRaWAN OTAA JOIN..."

  );



  Serial.println(

    "Region: AS923-2"

  );



  Serial.println(

    "======================================"

  );





  state =

    node.activateOTAA();





  // Simpan nonce LoRaWAN setelah percobaan join

  saveLoRaWANNonces();





  if (

    state ==

    RADIOLIB_LORAWAN_NEW_SESSION

  ) {



    lorawanJoined =

      true;





    // Simpan session hasil join

    saveLoRaWANSession();





    Serial.println();



    Serial.println(

      "======================================"

    );



    Serial.println(

      " LORAWAN JOIN SUCCESS!"

    );



    Serial.println(

      " SESSION SAVED TO NVS"

    );



    Serial.println(

      "======================================"

    );





    showMessage(

      "LORAWAN",

      "JOIN SUCCESS"

    );





    delay(1500);



  } else {



    lorawanJoined =

      false;





    Serial.println();



    Serial.println(

      "======================================"

    );



    Serial.println(

      " LORAWAN JOIN FAILED"

    );





    Serial.print(

      " Error code = "

    );



    Serial.println(

      state

    );





    Serial.println(

      "======================================"

    );





    showMessage(

      "LORAWAN JOIN",

      "FAILED"

    );

  }

}





// =====================================================

// SETUP

// =====================================================



void setup() {



  Serial.begin(

    115200

  );



  delay(2000);





  Serial.println();



  Serial.println(

    "======================================"

  );



  Serial.println(

    " HELTEC V3 SMART HOME"

  );



  Serial.println(

    " LORAWAN + ASCON-AEAD128"

  );



  Serial.println(

    " ACTUATOR DOWNLINK CONTROL"

  );



  Serial.println(

    " AS923-2 / CHIRPSTACK"

  );



  Serial.println(

    "======================================"

  );





  // =================================================

  // SYNCHRONIZE ESP32 CLOCK FOR T0

  // =================================================



  nodeTimeSynchronized =

    synchronizeNodeTime();



  if (!nodeTimeSynchronized) {

    Serial.println(

      "[TIME] WARNING: T0 absolute time is not synchronized."

    );

  }





  // =================================================

  // ASCON PERSISTENT NONCE COUNTER

  // =================================================



  Serial.println(

    "[ASCON] Opening persistent nonce storage..."

  );





  if (

    !asconPreferences.begin(

      "ascon",

      false

    )

  ) {



    Serial.println(

      "[ASCON] NVS ERROR!"

    );



    while (true) {

      delay(1000);

    }

  }





  asconCounter =

    asconPreferences.getULong64(

      "counter",

      0

    );





  Serial.print(

    "[ASCON] Last nonce counter = "

  );



  Serial.println(

    (unsigned long long)

    asconCounter

  );





  // =================================================

  // VEXT

  // =================================================



  Serial.println(

    "[1] Powering VEXT..."

  );



  pinMode(

    VEXT_PIN,

    OUTPUT

  );



  digitalWrite(

    VEXT_PIN,

    LOW

  );



  delay(500);





  // =================================================

  // OLED

  // =================================================



  Serial.println(

    "[2] Starting OLED..."

  );



  u8g2.begin();



  showMessage(

    "SMART HOME",

    "BOOTING..."

  );





  // =================================================

  // DHT22

  // =================================================



  Serial.println(

    "[3] Starting DHT22..."

  );



  dht.begin();





  // =================================================

  // PZEM

  // =================================================



  Serial.println(

    "[4] Starting PZEM..."

  );



  Serial1.begin(

    9600,

    SERIAL_8N1,

    PZEM_RX_PIN,

    PZEM_TX_PIN

  );





  // =================================================

  // PIR + RELAY

  // =================================================



  Serial.println(

    "[5] Configuring PIR & Relay..."

  );



  pinMode(

    PIR_PIN,

    INPUT

  );



  pinMode(

    RELAY_LIGHT,

    OUTPUT

  );



  pinMode(

    RELAY_FAN,

    OUTPUT

  );



  digitalWrite(

    RELAY_LIGHT,

    RELAY_OFF

  );



  digitalWrite(

    RELAY_FAN,

    RELAY_OFF

  );





  // =================================================

  // SERVO

  // =================================================



  Serial.println(

    "[6] Starting Servo PWM..."

  );



  ledcAttach(

    SERVO_PIN,

    pwmFreq,

    pwmResolution

  );



  moveServo(

    SERVO_0_DEGREES

  );





  // =================================================

  // PN532 RESET

  // =================================================



  Serial.println(

    "[7] Resetting PN532..."

  );



  pinMode(

    PN532_RST,

    OUTPUT

  );



  digitalWrite(

    PN532_RST,

    HIGH

  );



  delay(10);



  digitalWrite(

    PN532_RST,

    LOW

  );



  delay(10);



  digitalWrite(

    PN532_RST,

    HIGH

  );



  delay(100);





  // =================================================

  // PN532 START

  // =================================================



  Serial.println(

    "[8] Starting PN532..."

  );



  nfc.begin();



  nfc.SAMConfig();



  nfc.setPassiveActivationRetries(

    0x01

  );





  // =================================================

  // LORAWAN

  // =================================================



  Serial.println(

    "[9] Starting LoRaWAN..."

  );



  setupLoRaWAN();





  // =================================================

  // START TIMER

  // =================================================



  displayUpdateTime =

    millis();



  nfcScanTime =

    millis();



  loraTxTime =

    millis();



  lastMotionTime =

    millis();





  Serial.println();



  Serial.println(

    "======================================"

  );



  Serial.println(

    " SMART HOME + ASCON READY"

  );



  Serial.println(

    " UPLINK = 46 BYTE ENCRYPTED"

  );



  Serial.println(

    " DOWNLINK COMMAND:"

  );



  Serial.println(

    " 00 = LIGHT OFF"

  );



  Serial.println(

    " 01 = LIGHT ON"

  );



  Serial.println(

    " 02 = DOOR OPEN"

  );



  Serial.println(

    " 03 = DOOR CLOSE"

  );



  Serial.println(

    "======================================"

  );

}





// =====================================================

// MAIN LOOP

// =====================================================



void loop() {





  // =================================================

  // PIR

  // =================================================



  motionDetected =

    digitalRead(PIR_PIN) ==

    HIGH;





  if (

    systemActive &&

    motionDetected

  ) {



    lastMotionTime =

      millis();

  }





  // =================================================

  // SENSOR + OLED

  // EVERY 1 SECOND

  // =================================================



  if (

    millis() -

    displayUpdateTime >=

    1000

  ) {



    displayUpdateTime =

      millis();



    readSensors();



    updateDisplay();

  }





  // =================================================

  // NFC

  // =================================================



  scanNFC();





  // =================================================

  // AUTOMATIC FAN

  // =================================================



  climateControl();





  // =================================================

  // TIMEOUT 5 MENIT DINONAKTIFKAN

  //

  // Sengaja tidak ada checkSystemTimeout()

  // supaya perilakunya sama dengan firmware aktuator

  // yang sudah berhasil.

  // =================================================





  // =================================================

  // LORAWAN UPLINK

  // EVERY 15 SECONDS

  // =================================================



  if (

    lorawanJoined &&

    millis() -

    loraTxTime >=

    LORA_TX_INTERVAL

  ) {



    loraTxTime =

      millis();



    sendLoRaWANData();

  }





  delay(5);

}