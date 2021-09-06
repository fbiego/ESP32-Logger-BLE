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

#define LOG 8

ESP32Time rtc;

static BLECharacteristic* pCharacteristicTX;
static BLECharacteristic* pCharacteristicRX;

static bool deviceConnected = false, getLogs = false, getUsage = false, listFiles = false;
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
        if (pData[0] == 0xCA) {
          rtc.setTime(pData[2], pData[3], pData[4], pData[5], pData[6], pData[7] * 256 + pData[8]);
        } else if (pData[0] == 0xBA) {
          getLogs = true;
        } else if (pData[0] == 0xDA) {
          getUsage = true;
        }  else if (pData[0] == 0xFF) {
          listFiles = true;
        } else if (pData[0] == 0xBF) {
          if (FLASH.exists("/logs.bin")) {
            FLASH.remove("/logs.bin");
          }
        }
      }

    }
};

void writeBinary(const char * path, uint8_t *dat, int len) {

  File file = FLASH.open(path, FILE_APPEND);

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

  rtc.setTime(1609459200);
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
    logger[1] = rtc.getDay();
    logger[2] = rtc.getHour(true);
    logger[3] = rtc.getMinute();
    logger[4] = analogRead(34) / 100;
    logger[5] = analogRead(34) % 100;
    logger[6] = analogRead(35) / 100;
    logger[7] = analogRead(35) % 100;
    String filename = rtc.getTime("/log-%j-%y");
    writeBinary(filename.c_str(), logger, LOG);
  }

  if (getLogs) {
    sendLogs("/logs.bin");
    getLogs = false;
  }

  if (getUsage) {
    getUsage = false;
    int used = FLASH.usedBytes();
    int total = FLASH.totalBytes();
    uint8_t dat[7];
    dat[0] = 0xDA;
    dat[1] = (used >> 16);
    dat[2] = (used >> 8);
    dat[3] = (used & 0xFF);
    dat[4] = (total >> 16);
    dat[5] = (total >> 8);
    dat[6] = (total & 0xFF);
    Serial.print("Usage: ");
    Serial.print(used);
    Serial.print("/");
    Serial.println(total );
    pCharacteristicTX->setValue(dat, 7);
    pCharacteristicTX->notify();
    delay(50);
  }

  if(listFiles){
    listFiles = false;
    listDir(FLASH, "/", 0);
  }

}

void listDir(fs::FS &fs, const char * dirname, uint8_t levels){
    Serial.printf("Listing directory: %s\r\n", dirname);

    File root = fs.open(dirname);
    if(!root){
        Serial.println("- failed to open directory");
        return;
    }
    if(!root.isDirectory()){
        Serial.println(" - not a directory");
        return;
    }

    File file = root.openNextFile();
    while(file){
        if(file.isDirectory()){
            Serial.print("  DIR : ");
            Serial.println(file.name());
            if(levels){
                listDir(fs, file.name(), levels -1);
            }
        } else {
            Serial.print("  FILE: ");
            Serial.print(file.name());
            Serial.print("\tSIZE: ");
            Serial.println(file.size());
        }
        file = root.openNextFile();
    }
}

void sendLogs(const char * path) {
  Serial.printf("Reading file: %s\r\n", path);

  uint8_t com[LOG];
  File file = FLASH.open(path);
  if (!file || file.isDirectory()) {
    com[0] = 0xBA;
    pCharacteristicTX->setValue(com, LOG);
    pCharacteristicTX->notify();
    delay(50);
    Serial.println("- failed to open file for reading");
    return;
  }
  int x = 0;
  bool y = false;
  while (file.available()) {
    uint8_t c = file.read();
    if (c == 0xBA) {
      y = true;
    }
    if (y) {
      com[x] = c;
      x++;
    }
    if (x == LOG) {
      y = false;
      x = 0;
      pCharacteristicTX->setValue(com, LOG);
      pCharacteristicTX->notify();
      delay(50);
    }
  }
}
