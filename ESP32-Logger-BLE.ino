/*
   MIT License
   Copyright (c) 2021 Felix Biego
   Permission is hereby granted, free of charge, to any person obtaining a copy
   of this software and associated documentation files (the "Software"), to deal
   in the Software without restriction, including without limitation the rights
   to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
   copies of the Software, and to permit persons to whom the Software is
   furnished to do so, subject to the following conditions:
   The above copyright notice and this permission notice shall be included in all
   copies or substantial portions of the Software.
   THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
   IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
   FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
   AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
   LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
   OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
   SOFTWARE.
*/

#include "FS.h"
#include "FFat.h"
#include "SPIFFS.h"
#include <BLEDevice.h>
#include <BLEUtils.h>
#include <BLEServer.h>
#include <BLE2902.h>
#include <ESP32Time.h>

#define BUILTINLED 2
#define FORMAT_SPIFFS_IF_FAILED true
#define FORMAT_FFAT_IF_FAILED true

#define USE_SPIFFS  //comment to use FFat

#ifdef USE_SPIFFS
#define FLASH SPIFFS
#else
#define FLASH FFat
#endif

#define SERVICE_UUID              "fb1e4001-54ae-4a28-9f74-dfccb248601d"
#define CHARACTERISTIC_UUID_RX    "fb1e4002-54ae-4a28-9f74-dfccb248601d"
#define CHARACTERISTIC_UUID_TX    "fb1e4003-54ae-4a28-9f74-dfccb248601d"

#define LOG 5

ESP32Time rtc;

static BLECharacteristic* pCharacteristicTX;
static BLECharacteristic* pCharacteristicRX;

static bool deviceConnected = false;
static int interval = 5;
int mins = 0;
uint8_t logger[LOG];

class MyServerCallbacks: public BLEServerCallbacks {
    void onConnect(BLEServer* pServer) {
      deviceConnected = true;

    }
    void onDisconnect(BLEServer* pServer) {
      deviceConnected = false;
    }
};

class MyCallbacks: public BLECharacteristicCallbacks {

    void onStatus(BLECharacteristic* pCharacteristic, Status s, uint32_t code) {
      Serial.print("Status ");
      Serial.print(s);
      Serial.print(" on characteristic ");
      Serial.print(pCharacteristic->getUUID().toString().c_str());
      Serial.print(" with code ");
      Serial.println(code);
    }

    void onNotify(BLECharacteristic *pCharacteristic) {
      uint8_t* pData;
      std::string value = pCharacteristic->getValue();
      int len = value.length();
      pData = pCharacteristic->getData();
      if (pData != NULL) {
        Serial.print("TX  ");
        for (int i = 0; i < len; i++) {
          Serial.printf("%02X ", pData[i]);
        }
        Serial.println();
      }
    }

    void onWrite(BLECharacteristic *pCharacteristic) {
      uint8_t* pData;
      std::string value = pCharacteristic->getValue();
      int len = value.length();
      pData = pCharacteristic->getData();
      if (pData != NULL) {
        Serial.print("RX  ");
        for (int i = 0; i < len; i++) {
          Serial.printf("%02X ", pData[i]);
        }
        Serial.println();
      }

    }
};

void writeBinary(fs::FS &fs, const char * path, uint8_t *dat, int len) {

  File file = fs.open(path, FILE_APPEND);

  if (!file) {
    Serial.println("- failed to open file for writing");
    return;
  }
  file.write(dat, len);
  file.close();
}

void initBLE() {
  BLEDevice::init("ESP32 Logger");
  BLEServer *pServer = BLEDevice::createServer();
  pServer->setCallbacks(new MyServerCallbacks());

  BLEService *pService = pServer->createService(SERVICE_UUID);
  pCharacteristicTX = pService->createCharacteristic(CHARACTERISTIC_UUID_TX, BLECharacteristic::PROPERTY_NOTIFY );
  pCharacteristicRX = pService->createCharacteristic(CHARACTERISTIC_UUID_RX, BLECharacteristic::PROPERTY_WRITE | BLECharacteristic::PROPERTY_WRITE_NR);
  pCharacteristicRX->setCallbacks(new MyCallbacks());
  pCharacteristicTX->setCallbacks(new MyCallbacks());
  pCharacteristicTX->addDescriptor(new BLE2902());
  pCharacteristicTX->setNotifyProperty(true);
  pService->start();


  // BLEAdvertising *pAdvertising = pServer->getAdvertising();  // this still is working for backward compatibility
  BLEAdvertising *pAdvertising = BLEDevice::getAdvertising();
  pAdvertising->addServiceUUID(SERVICE_UUID);
  pAdvertising->setScanResponse(true);
  pAdvertising->setMinPreferred(0x06);  // functions that help with iPhone connections issue
  pAdvertising->setMinPreferred(0x12);
  BLEDevice::startAdvertising();
  Serial.println("Characteristic defined! Now you can read it in your phone!");
}

void setup() {
  Serial.begin(115200);

#ifdef USE_SPIFFS
  if (!SPIFFS.begin(FORMAT_SPIFFS_IF_FAILED)) {
    Serial.println("SPIFFS Mount Failed");
    return;
  }
#else
  if (!FFat.begin()) {
    Serial.println("FFat Mount Failed");
    if (FORMAT_FFAT_IF_FAILED) FFat.format();
    return;
  }
#endif


  initBLE();
}

void loop() {

  if (rtc.getMinute() / interval != mins) {
    mins = rtc.getMinute() / interval;
    logger[0] = 0xBA;
    logger[1] = analogRead(34) / 100;
    logger[2] = analogRead(34) % 100;
    logger[3] = analogRead(35) / 100;
    logger[4] = analogRead(35) % 100;
    writeBinary(FLASH, "/logs.bin", logger, LOG);
  }

}
