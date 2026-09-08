#pragma once

#include <Arduino.h>
#include <NimBLEDevice.h>
#include <ArduinoJson.h>
#include <LittleFS.h>
#include "esp_ota_ops.h"
#include "esp_mac.h"

// Sugarota BLE GATT Service UUIDs
#define SUGAROTA_SERVICE_UUID           "6E400001-B5A3-F393-E0A9-E50E24DCCA9E"
#define CHAR_GLUCOSE_DATA_UUID          "6E400002-B5A3-F393-E0A9-E50E24DCCA9E"
#define CHAR_CONFIG_UUID                "6E400003-B5A3-F393-E0A9-E50E24DCCA9E"
#define CHAR_STATUS_UUID                "6E400004-B5A3-F393-E0A9-E50E24DCCA9E"
#define CHAR_OTA_UUID                   "6E400005-B5A3-F393-E0A9-E50E24DCCA9E"

// Callback type for when a new glucose payload is pushed from the phone
typedef void (*GlucoseDataCallback)(const JsonDocument& doc);
// Callback type for when config is updated or reboot requested
typedef void (*ConfigUpdatedCallback)();
// Callback type for when pairing comparison code should be shown or dismissed
typedef void (*PairingDisplayCallback)(uint32_t pin, bool active);

class SugarotaBLE {
public:
    static SugarotaBLE& getInstance() {
        static SugarotaBLE instance;
        return instance;
    }

    void begin(const char* deviceNamePrefix = "Sugarota");
    void update();
    int getConnectedCount() const { return m_connectedCount; }
    bool isConnected() const { return m_connectedCount > 0; }
    bool isBridged() const { return (m_connectedCount > 0) && m_glucoseBridged; }
    unsigned long getLastPacketTime() const { return m_lastPacketTime; }
    void recordPacketActivity() { m_lastPacketTime = millis(); }

    void setGlucoseCallback(GlucoseDataCallback cb) { m_glucoseCallback = cb; }
    void setConfigCallback(ConfigUpdatedCallback cb) { m_configCallback = cb; }
    void setPairingCallback(PairingDisplayCallback cb) { m_pairingCallback = cb; }

    // Pairing dialog handling
    bool isPairingActive() const { return m_pairingActive; }
    uint32_t getPairingPin() const { return m_pairingPin; }
    void confirmPairing(bool accept);

    void notifyStatus(int batteryPct, bool isCharging, const char* version);

private:
    SugarotaBLE();
    ~SugarotaBLE();

    int m_connectedCount;
    bool m_glucoseBridged;
    unsigned long m_lastPacketTime;
    NimBLEServer* m_pServer;
    NimBLEService* m_pService;

    NimBLECharacteristic* m_pGlucoseChar;
    NimBLECharacteristic* m_pConfigChar;
    NimBLECharacteristic* m_pStatusChar;
    NimBLECharacteristic* m_pOTAChar;

    GlucoseDataCallback m_glucoseCallback;
    ConfigUpdatedCallback m_configCallback;
    PairingDisplayCallback m_pairingCallback;

    // Pairing / Security state
    bool m_pairingActive;
    uint32_t m_pairingPin;
    uint16_t m_pairingConnHandle;
    unsigned long m_pairingStartTime;

    // OTA internal state
    esp_ota_handle_t m_otaHandle;
    const esp_partition_t* m_updatePartition;
    bool m_otaInProgress;
    size_t m_otaBytesWritten;

    // Config buffer for chunked writes if needed
    String m_configWriteBuffer;

    friend class SugarotaServerCallbacks;
    friend class SugarotaGlucoseCallbacks;
    friend class SugarotaConfigCallbacks;
    friend class SugarotaOTACallbacks;
};
