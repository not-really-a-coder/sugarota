#include "ble.h"
#include <ArduinoJson.h>

// Forward reference
extern bool debugMode;
#define BLE_DBG_PRINT(...) do { if(debugMode && Serial) Serial.print(__VA_ARGS__); } while(0)
#define BLE_DBG_PRINTLN(...) do { if(debugMode && Serial) Serial.println(__VA_ARGS__); } while(0)
#define BLE_DBG_PRINTF(...) do { if(debugMode && Serial) Serial.printf(__VA_ARGS__); } while(0)

class SugarotaServerCallbacks : public NimBLEServerCallbacks {
public:
    SugarotaServerCallbacks(SugarotaBLE* ble) : m_ble(ble) {}

    void onConnect(NimBLEServer* pServer, NimBLEConnInfo& connInfo) override {
        m_ble->m_connectedCount = pServer->getConnectedCount();
        BLE_DBG_PRINTF("[BLE] Central connected: %s (total clients: %d)\n", connInfo.getAddress().toString().c_str(), m_ble->m_connectedCount);
        
        // Adjust advertising: switch to low-duty cycle if 1 slot remains, or stop if full
        m_ble->updateAdvertising();

        // Negotiate reliable BLE connection parameters:
        // minInterval = 80 (100ms), maxInterval = 120 (150ms), latency = 2 intervals, timeout = 600 (6.0s)
        pServer->updateConnParams(connInfo.getConnHandle(), 80, 120, 2, 600);
    }

    void onDisconnect(NimBLEServer* pServer, NimBLEConnInfo& connInfo, int reason) override {
        m_ble->m_connectedCount = pServer->getConnectedCount();
        if (m_ble->m_connectedCount == 0) {
            m_ble->m_glucoseBridged = false;
        }
        if (m_ble->m_pairingActive && m_ble->m_pairingConnHandle == connInfo.getConnHandle()) {
            m_ble->m_pairingActive = false;
            if (m_ble->m_pairingCallback) {
                m_ble->m_pairingCallback(0, false);
            }
        }
        BLE_DBG_PRINTF("[BLE] Central disconnected (reason %d). Remaining clients: %d\n", reason, m_ble->m_connectedCount);
        
        // Resume fast or low-duty advertising based on remaining slots
        m_ble->updateAdvertising();
    }

    void onConnParamsUpdate(NimBLEConnInfo& connInfo) override {
        BLE_DBG_PRINTF("[BLE] Conn params updated: interval=%.2fms, latency=%d, timeout=%dms\n",
            connInfo.getConnInterval() * 1.25f,
            connInfo.getConnLatency(),
            connInfo.getConnTimeout() * 10);
    }

    void onConfirmPassKey(NimBLEConnInfo& connInfo, uint32_t pin) override {
        BLE_DBG_PRINTF("[BLE Security] Numeric Comparison Request: PIN=%06u from %s (conn %u)\n", 
            pin, connInfo.getAddress().toString().c_str(), connInfo.getConnHandle());

        // Reject pairing from unknown devices if pairing mode is NOT active (requires Boot or Config mode)
        if (!m_ble->m_pairingModeEnabled) {
            BLE_DBG_PRINTLN("[BLE Security] Denied pairing request: Pairing mode not active (requires Boot or Config mode)");
            NimBLEDevice::injectConfirmPasskey(connInfo, false);
            return;
        }

        m_ble->m_pairingActive = true;
        m_ble->m_pairingPin = pin;
        m_ble->m_pairingConnHandle = connInfo.getConnHandle();
        m_ble->m_pairingStartTime = millis();

        if (m_ble->m_pairingCallback) {
            m_ble->m_pairingCallback(pin, true);
        }
    }

    void onAuthenticationComplete(NimBLEConnInfo& connInfo) override {
        BLE_DBG_PRINTF("[BLE Security] Auth complete for %s (bonded=%d, encrypted=%d, authenticated=%d)\n",
            connInfo.getAddress().toString().c_str(),
            connInfo.isBonded(),
            connInfo.isEncrypted(),
            connInfo.isAuthenticated());
        
        m_ble->m_pairingActive = false;
        if (m_ble->m_pairingCallback) {
            m_ble->m_pairingCallback(0, false);
        }
    }
private:
    SugarotaBLE* m_ble;
};

class SugarotaGlucoseCallbacks : public NimBLECharacteristicCallbacks {
public:
    SugarotaGlucoseCallbacks(SugarotaBLE* ble) : m_ble(ble) {}

    void onWrite(NimBLECharacteristic* pCharacteristic, NimBLEConnInfo& connInfo) override {
        // Enforce that link must be encrypted
        if (!connInfo.isEncrypted() && !connInfo.isBonded()) {
            BLE_DBG_PRINTLN("[BLE Security] Rejected unencrypted glucose write");
            return;
        }

        std::string rxValue = pCharacteristic->getValue();
        if (rxValue.empty()) return;

        JsonDocument doc;
        DeserializationError err = deserializeJson(doc, rxValue.c_str());
        if (!err) {
            m_ble->m_glucoseBridged = true;
            m_ble->m_lastPacketTime = millis();
            if (m_ble->m_glucoseCallback) {
                m_ble->m_glucoseCallback(doc);
            }
        } else {
            BLE_DBG_PRINTF("[BLE] Glucose JSON error: %s\n", err.c_str());
        }
    }
private:
    SugarotaBLE* m_ble;
};

class SugarotaConfigCallbacks : public NimBLECharacteristicCallbacks {
public:
    SugarotaConfigCallbacks(SugarotaBLE* ble) : m_ble(ble) {}

    void onRead(NimBLECharacteristic* pCharacteristic, NimBLEConnInfo& connInfo) override {
        // Enforce that config can only be read over an encrypted link
        if (!connInfo.isEncrypted() && !connInfo.isBonded()) {
            BLE_DBG_PRINTLN("[BLE Security] Denied unencrypted /config.json read");
            pCharacteristic->setValue("{}");
            return;
        }

        // Read active /config.json directly from LittleFS
        if (LittleFS.exists("/config.json")) {
            File f = LittleFS.open("/config.json", "r");
            if (f) {
                String content = f.readString();
                f.close();
                pCharacteristic->setValue((const uint8_t*)content.c_str(), content.length());
                BLE_DBG_PRINTLN("[BLE] Sent /config.json to central");
                return;
            }
        }
        pCharacteristic->setValue("{}");
    }

    void onWrite(NimBLECharacteristic* pCharacteristic, NimBLEConnInfo& connInfo) override {
        // Enforce that config can only be written over an encrypted link
        if (!connInfo.isEncrypted() && !connInfo.isBonded()) {
            BLE_DBG_PRINTLN("[BLE Security] Denied unencrypted /config.json write");
            return;
        }

        std::string val = pCharacteristic->getValue();
        if (val.empty()) return;

        // Support chunking protocol:
        // "[START]" resets buffer
        // "[END]" commits to LittleFS
        // Or if it starts with '{' and ends with '}', commit immediately
        String chunk = String(val.c_str());
        chunk.trim();

        if (chunk == "[START]") {
            m_ble->m_configWriteBuffer = "";
            BLE_DBG_PRINTLN("[BLE] Config write started");
            return;
        } else if (chunk == "[END]") {
            commitConfig(m_ble->m_configWriteBuffer);
            m_ble->m_configWriteBuffer = "";
            return;
        }

        if (chunk.startsWith("{") && chunk.endsWith("}")) {
            // Direct single-packet payload
            commitConfig(chunk);
        } else {
            // Accumulate chunk
            m_ble->m_configWriteBuffer += chunk;
        }
    }

private:
    SugarotaBLE* m_ble;

    void commitConfig(const String& payload) {
        JsonDocument doc;
        DeserializationError err = deserializeJson(doc, payload.c_str());
        if (!err) {
            File f = LittleFS.open("/config.json", "w");
            if (f) {
                f.print(payload);
                f.close();
                BLE_DBG_PRINTLN("[BLE] Successfully updated /config.json");
                if (m_ble->m_configCallback) {
                    m_ble->m_configCallback();
                }
            } else {
                BLE_DBG_PRINTLN("[BLE ERROR] Failed to open /config.json for writing");
            }
        } else {
            BLE_DBG_PRINTF("[BLE ERROR] Invalid config JSON received: %s\n", err.c_str());
        }
    }
};

class SugarotaOTACallbacks : public NimBLECharacteristicCallbacks {
public:
    SugarotaOTACallbacks(SugarotaBLE* ble) : m_ble(ble) {}

    void onWrite(NimBLECharacteristic* pCharacteristic, NimBLEConnInfo& connInfo) override {
        std::string val = pCharacteristic->getValue();
        size_t len = val.length();
        if (len == 0) return;

        const uint8_t* data = (const uint8_t*)val.data();

        // Check command frames (short text payloads)
        if (len < 16) {
            String cmd = String((const char*)data);
            cmd.trim();
            if (cmd == "OTA_BEGIN") {
                m_ble->m_updatePartition = esp_ota_get_next_update_partition(NULL);
                if (!m_ble->m_updatePartition) {
                    BLE_DBG_PRINTLN("[BLE OTA] No OTA partition found!");
                    pCharacteristic->setValue("ERR_NO_PART");
                    pCharacteristic->notify();
                    return;
                }
                esp_err_t err = esp_ota_begin(m_ble->m_updatePartition, OTA_WITH_SEQUENTIAL_WRITES, &m_ble->m_otaHandle);
                if (err != ESP_OK) {
                    BLE_DBG_PRINTF("[BLE OTA] esp_ota_begin failed: 0x%x\n", err);
                    pCharacteristic->setValue("ERR_BEGIN");
                    pCharacteristic->notify();
                    return;
                }
                m_ble->m_otaInProgress = true;
                m_ble->m_otaBytesWritten = 0;
                BLE_DBG_PRINTLN("[BLE OTA] Ready for firmware stream");
                pCharacteristic->setValue("OTA_READY");
                pCharacteristic->notify();
                return;
            } else if (cmd == "OTA_END") {
                if (!m_ble->m_otaInProgress) return;
                esp_err_t err = esp_ota_end(m_ble->m_otaHandle);
                if (err != ESP_OK) {
                    BLE_DBG_PRINTF("[BLE OTA] esp_ota_end failed: 0x%x\n", err);
                    pCharacteristic->setValue("ERR_END");
                    pCharacteristic->notify();
                    m_ble->m_otaInProgress = false;
                    return;
                }
                err = esp_ota_set_boot_partition(m_ble->m_updatePartition);
                if (err != ESP_OK) {
                    BLE_DBG_PRINTF("[BLE OTA] set_boot_partition failed: 0x%x\n", err);
                    pCharacteristic->setValue("ERR_BOOT");
                    pCharacteristic->notify();
                    m_ble->m_otaInProgress = false;
                    return;
                }
                BLE_DBG_PRINTLN("[BLE OTA] Firmware update verified! Rebooting...");
                pCharacteristic->setValue("OTA_OK");
                pCharacteristic->notify();
                delay(1000);
                ESP.restart();
                return;
            } else if (cmd == "OTA_ABORT") {
                if (m_ble->m_otaInProgress) {
                    esp_ota_abort(m_ble->m_otaHandle);
                    m_ble->m_otaInProgress = false;
                    BLE_DBG_PRINTLN("[BLE OTA] Update aborted");
                }
                return;
            }
        }

        // Binary chunk received
        if (m_ble->m_otaInProgress) {
            esp_err_t err = esp_ota_write(m_ble->m_otaHandle, data, len);
            if (err != ESP_OK) {
                BLE_DBG_PRINTF("[BLE OTA] Write error: 0x%x\n", err);
                pCharacteristic->setValue("ERR_WRITE");
                pCharacteristic->notify();
                esp_ota_abort(m_ble->m_otaHandle);
                m_ble->m_otaInProgress = false;
            } else {
                m_ble->m_otaBytesWritten += len;
            }
        }
    }

private:
    SugarotaBLE* m_ble;
};

SugarotaBLE::SugarotaBLE() 
    : m_connectedCount(0),
      m_glucoseBridged(false),
      m_lastPacketTime(0),
      m_pServer(nullptr),
      m_pService(nullptr),
      m_pGlucoseChar(nullptr),
      m_pConfigChar(nullptr),
      m_pStatusChar(nullptr),
      m_pOTAChar(nullptr),
      m_glucoseCallback(nullptr),
      m_configCallback(nullptr),
      m_pairingCallback(nullptr),
      m_pairingModeEnabled(false),
      m_pairingActive(false),
      m_pairingPin(0),
      m_pairingConnHandle(BLE_HS_CONN_HANDLE_NONE),
      m_pairingStartTime(0),
      m_otaHandle(0),
      m_updatePartition(nullptr),
      m_otaInProgress(false),
      m_otaBytesWritten(0) {
}

SugarotaBLE::~SugarotaBLE() {}

void SugarotaBLE::begin(const char* deviceNamePrefix) {
    uint8_t mac[6];
    esp_read_mac(mac, ESP_MAC_BT);
    char fullDeviceName[32];
    snprintf(fullDeviceName, sizeof(fullDeviceName), "%s-%02X%02X", deviceNamePrefix, mac[4], mac[5]);

    NimBLEDevice::init(fullDeviceName);
    NimBLEDevice::setMTU(517); // Maximum BLE MTU for high throughput
    NimBLEDevice::setPower(ESP_PWR_LVL_P9); // Max TX power for reliable range

    // Configure BLE Security: Bonding enabled, MITM protection enabled, Secure Connections (LE SC) enabled
    NimBLEDevice::setSecurityAuth(true, true, true);
    // Display Yes/No allows Numeric Comparison on screen
    NimBLEDevice::setSecurityIOCap(BLE_HS_IO_DISPLAY_YESNO);

    m_pServer = NimBLEDevice::createServer();
    m_pServer->setCallbacks(new SugarotaServerCallbacks(this));

    m_pService = m_pServer->createService(SUGAROTA_SERVICE_UUID);

    // 1. Glucose Data Stream (Protected: Write Encrypted / Notify)
    m_pGlucoseChar = m_pService->createCharacteristic(
        CHAR_GLUCOSE_DATA_UUID,
        NIMBLE_PROPERTY::WRITE | NIMBLE_PROPERTY::WRITE_NR | NIMBLE_PROPERTY::WRITE_ENC | NIMBLE_PROPERTY::NOTIFY
    );
    m_pGlucoseChar->setCallbacks(new SugarotaGlucoseCallbacks(this));

    // 2. Config Read/Write (Protected: Read Encrypted / Write Encrypted / Notify)
    m_pConfigChar = m_pService->createCharacteristic(
        CHAR_CONFIG_UUID,
        NIMBLE_PROPERTY::READ | NIMBLE_PROPERTY::READ_ENC | NIMBLE_PROPERTY::WRITE | NIMBLE_PROPERTY::WRITE_ENC | NIMBLE_PROPERTY::NOTIFY
    );
    m_pConfigChar->setCallbacks(new SugarotaConfigCallbacks(this));

    // 3. Status Characteristic (Battery, Version, etc. - unencrypted read for status check)
    m_pStatusChar = m_pService->createCharacteristic(
        CHAR_STATUS_UUID,
        NIMBLE_PROPERTY::READ | NIMBLE_PROPERTY::NOTIFY
    );

    // 4. OTA Control & Stream (Protected: Write Encrypted / Notify)
    m_pOTAChar = m_pService->createCharacteristic(
        CHAR_OTA_UUID,
        NIMBLE_PROPERTY::WRITE | NIMBLE_PROPERTY::WRITE_NR | NIMBLE_PROPERTY::WRITE_ENC | NIMBLE_PROPERTY::NOTIFY
    );
    m_pOTAChar->setCallbacks(new SugarotaOTACallbacks(this));

    m_pService->start();

    // Advertising (100ms min, 150ms max - standard recommended interval for low power discovery)
    NimBLEAdvertising* pAdvertising = NimBLEDevice::getAdvertising();
    pAdvertising->setName(fullDeviceName);
    pAdvertising->addServiceUUID(SUGAROTA_SERVICE_UUID);
    pAdvertising->enableScanResponse(true);
    pAdvertising->setMinInterval(0x00A0); // 160 * 0.625ms = 100ms
    pAdvertising->setMaxInterval(0x00F0); // 240 * 0.625ms = 150ms
    pAdvertising->start();

    BLE_DBG_PRINTF("[BLE] Started advertising as '%s' (LE Secure Connections enabled)\n", fullDeviceName);
}

void SugarotaBLE::update() {
    // Check pairing timeout (30 seconds)
    if (m_pairingActive && (millis() - m_pairingStartTime > 30000)) {
        BLE_DBG_PRINTLN("[BLE Security] Pairing timed out");
        confirmPairing(false);
    }
}

void SugarotaBLE::confirmPairing(bool accept) {
    if (!m_pairingActive || !m_pServer) return;

    BLE_DBG_PRINTF("[BLE Security] Pairing %s by user for handle %u\n", accept ? "ACCEPTED" : "REJECTED", m_pairingConnHandle);
    NimBLEConnInfo peerInfo = m_pServer->getPeerInfoByHandle(m_pairingConnHandle);
    NimBLEDevice::injectConfirmPasskey(peerInfo, accept);
    m_pairingActive = false;
    m_pairingPin = 0;
    m_pairingConnHandle = BLE_HS_CONN_HANDLE_NONE;

    if (m_pairingCallback) {
        m_pairingCallback(0, false);
    }
}

void SugarotaBLE::notifyStatus(int batteryPct, bool isCharging, const char* version, int brightness, int darkTheme, bool requestRefresh, int debug) {
    if (!m_pStatusChar) return;

    JsonDocument doc;
    doc["battery"] = batteryPct;
    doc["charging"] = isCharging;
    doc["version"] = version;
    if (brightness >= 0) {
        doc["brightness"] = brightness;
    }
    if (darkTheme >= 0) {
        doc["dark_theme"] = (darkTheme == 1);
    }
    if (requestRefresh) {
        doc["request_refresh"] = true;
    }
    if (debug >= 0) {
        doc["debug"] = (debug == 1);
    } else {
        doc["debug"] = debugMode;
    }

    String payload;
    serializeJson(doc, payload);
    m_pStatusChar->setValue((const uint8_t*)payload.c_str(), payload.length());
    if (isConnected()) {
        m_pStatusChar->notify();
    }
}

void SugarotaBLE::enablePairingMode(bool enable) {
    if (m_pairingModeEnabled == enable) return;
    m_pairingModeEnabled = enable;
    BLE_DBG_PRINTF("[BLE Security] Pairing mode %s\n", enable ? "ENABLED" : "DISABLED");
    updateAdvertising();
}

void SugarotaBLE::updateAdvertising() {
    NimBLEAdvertising* pAdvertising = NimBLEDevice::getAdvertising();
    if (!pAdvertising) return;

    if (m_connectedCount >= MAX_BLE_CLIENTS) {
        if (pAdvertising->isAdvertising()) {
            NimBLEDevice::stopAdvertising();
            BLE_DBG_PRINTLN("[BLE] Max clients reached. Advertising stopped.");
        }
        return;
    }

    // When 0 clients connected: fast advertising (100-150ms) for snappy first connection.
    // When 1 client connected: slow low-duty cycle advertising (1000-1280ms).
    // This allows a second bonded device to connect while avoiding spamming
    // the already-connected phone's OS Nearby scanner with rapid beacons.
    uint16_t minInt = (m_connectedCount == 0 || m_pairingModeEnabled) ? 0x00A0 : 0x0640; // 100ms vs 1000ms
    uint16_t maxInt = (m_connectedCount == 0 || m_pairingModeEnabled) ? 0x00F0 : 0x0800; // 150ms vs 1280ms

    // If currently advertising, stop briefly to apply updated interval
    if (pAdvertising->isAdvertising()) {
        NimBLEDevice::stopAdvertising();
    }

    pAdvertising->setMinInterval(minInt);
    pAdvertising->setMaxInterval(maxInt);
    pAdvertising->start();
    BLE_DBG_PRINTF("[BLE] Advertising active (clients: %d/%d, interval: %s)\n",
        m_connectedCount, MAX_BLE_CLIENTS, (m_connectedCount == 0 || m_pairingModeEnabled) ? "fast" : "low-duty");
}

void SugarotaBLE::disconnect() {
    if (m_pServer && isConnected()) {
        std::vector<uint16_t> connIds = m_pServer->getPeerDevices();
        for (uint16_t connId : connIds) {
            m_pServer->disconnect(connId);
        }
    }
}


