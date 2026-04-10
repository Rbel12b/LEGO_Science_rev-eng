#include <Arduino.h>

#include <NimBLEDevice.h>
#include <Lpf2/config.hpp>
#include <Lpf2//Util/Values.hpp>

#define SERVICE_UUID "0000fd02-0000-1000-8000-00805f9b34fb"
#define WRITE_CHARACHTERISTIC_UUID "0000fd02-0001-1000-8000-00805f9b34fb"
#define READ_CHARACHTERISTIC_UUID "0000fd02-0002-1000-8000-00805f9b34fb"

// bool subscribed = false;
bool connecting = false;
bool connected = false;
NimBLEAddress bleServerAddress;
NimBLERemoteCharacteristic *bleWriteCharacteristic = nullptr;
NimBLECharacteristic *bleReadCharacteristic = nullptr;

class WriteCharacteristicCallbacks : public NimBLECharacteristicCallbacks
{
public:
    WriteCharacteristicCallbacks() : NimBLECharacteristicCallbacks() {}
    // void onSubscribe(NimBLECharacteristic *pCharacteristic, NimBLEConnInfo &connInfo, uint16_t subValue) override;
    void onWrite(NimBLECharacteristic *pCharacteristic, NimBLEConnInfo &connInfo) override
    {
        std::vector<uint8_t> data(pCharacteristic->getValue());
        LPF2_LOG_I("APP -> DEVICE: %s", Lpf2::Utils::bytes_to_hexString(data));
        if (bleWriteCharacteristic)
        {
            bleWriteCharacteristic->writeValue(data);
        }
    }
};

// void WriteCharacteristicCallbacks::onSubscribe(NimBLECharacteristic *pCharacteristic, NimBLEConnInfo &connInfo, uint16_t subValue)
// {
//     LPF2_LOG_D("Client subscription status: %s (%d)",
//             subValue == 0 ? "Un-Subscribed" : subValue == 1 ? "Notifications"
//                                             : subValue == 2   ? "Indications"
//                                             : subValue == 3   ? "Notifications and Indications"
//                                                             : "unknown subscription status",
//             subValue);

//     subscribed = subValue != 0;
// }

class AdvertisedDeviceCallbacks : public BLEAdvertisedDeviceCallbacks
{
public:
    AdvertisedDeviceCallbacks() : BLEAdvertisedDeviceCallbacks() {}

    void onScanEnd(const NimBLEScanResults &results, int reason) override
    {
        LPF2_LOG_D("Scan Ended reason: %d\nNumber of devices: %d", reason, results.getCount());
        for (int i = 0; i < results.getCount(); i++)
        {
            LPF2_LOG_D("device[%d]: %s", i, results.getDevice(i)->toString().c_str());
        }
    }

    void onResult(const NimBLEAdvertisedDevice *advertisedDevice) override
    {
        // Found a device, check if the service is contained and optional if address fits requested address
        LPF2_LOG_D("advertised device: %s", advertisedDevice->toString().c_str());

        if (advertisedDevice->haveServiceUUID() && advertisedDevice->getServiceUUID().equals(NimBLEUUID(SERVICE_UUID)))
        {
            advertisedDevice->getScan()->stop();

            if (advertisedDevice->haveManufacturerData())
            {
                LPF2_LOG_D("advertisement payload: %s", Lpf2::Utils::bytes_to_hexString(advertisedDevice->getPayload()).c_str());
                LPF2_LOG_D("manufacturer data: %s", Lpf2::Utils::bytes_to_hexString(advertisedDevice->getManufacturerData()).c_str());
                bleServerAddress = BLEAddress(advertisedDevice->getAddress());
                connecting = true;
            }
        }
    }
};

bool connect();
void scan();

void setup()
{
    Serial.begin(981200);
    lpf2_set_runtime_log_level(4);

    NimBLEDevice::init("🟩 2236 Double Motor");
    NimBLEDevice::setOwnAddrType(BLE_OWN_ADDR_PUBLIC);
    NimBLEDevice::setPower(ESP_PWR_LVL_N0, NimBLETxPowerType::Advertise); // 0dB, Advertisment

    LPF2_LOG_D("Creating server");
    auto bleServer = NimBLEDevice::createServer();

    LPF2_LOG_D("Create service");
    auto bleService = bleServer->createService(SERVICE_UUID);

    auto writeBleChar = bleService->createCharacteristic(
        NimBLEUUID(WRITE_CHARACHTERISTIC_UUID),
            NIMBLE_PROPERTY::WRITE_NR);

    auto writeBleCharCallbacks = new WriteCharacteristicCallbacks();
    writeBleChar->setCallbacks(writeBleCharCallbacks);

    bleReadCharacteristic = bleService->createCharacteristic(
        NimBLEUUID(READ_CHARACHTERISTIC_UUID),
            NIMBLE_PROPERTY::NOTIFY);

    LPF2_LOG_D("Service start");

    auto bleAdvertising = NimBLEDevice::getAdvertising();

    bleAdvertising->enableScanResponse(true);
    bleAdvertising->setMinInterval(32); // 0.625ms units -> 20ms
    bleAdvertising->setMaxInterval(64); // 0.625ms units -> 40ms

    auto advertisementData = NimBLEAdvertisementData();
    auto scanResponseData = NimBLEAdvertisementData();

    // Raw advertisement payload
    std::vector<uint8_t> advPayload = {
        0x02, 0x01, 0x06,
        0x02, 0x0a, 0x00,
        0x03, 0x03, 0x02, 0xfd,
        0x08, 0xff, 0x97, 0x03, 0x02, 0x01, 0x06, 0xbc
    };

    // Raw scan response payload
    std::vector<uint8_t> scanRespPayload = {
        0x17, 0x09,
        0xf0, 0x9f, 0x9f, 0xa9, // UTF-8 emoji
        0x20,                   // space
        0x32, 0x32, 0x33, 0x36, // "2236"
        0x20,
        0x44, 0x6f, 0x75, 0x62, 0x6c, 0x65,
        0x20,
        0x4d, 0x6f, 0x74, 0x6f, 0x72 // "Double Motor"
    };

    // Assign raw data
    advertisementData.addData(advPayload);
    scanResponseData.addData(scanRespPayload);

    LPF2_LOG_D("advertisment data payload(%d): %s", advertisementData.getPayload().size(), Lpf2::Utils::bytes_to_hexString(advertisementData.getPayload()).c_str());
    LPF2_LOG_D("scan response data payload(%d): %s", scanResponseData.getPayload().size(), Lpf2::Utils::bytes_to_hexString(scanResponseData.getPayload()).c_str());

    bleAdvertising->setAdvertisementData(advertisementData);
    bleAdvertising->setScanResponseData(scanResponseData);

    LPF2_LOG_D("Start advertising");
    NimBLEDevice::startAdvertising();
    LPF2_LOG_D("Characteristic defined! Now you can connect with your PoweredUp App!");
}

void loop()
{
    vTaskDelay(1);

    if (Serial.available()) {
        uint8_t c = Serial.read();
        if (c == 0x03) {
            // Ctrl+C received
            ESP.restart();
        }
    }

    if (connecting)
    {
        connect();
    }

    if (!connecting && !connected)
    {
        scan();
        vTaskDelay(500);
    }
}

void notifyCallback(
        NimBLERemoteCharacteristic *pBLERemoteCharacteristic,
        uint8_t *pData,
        size_t length,
        bool isNotify)
{
    std::vector<uint8_t> data(pData, pData + length);
    LPF2_LOG_D("HUB -> APP: %s", Lpf2::Utils::bytes_to_hexString(data).c_str());
    if (bleReadCharacteristic)
    {
        bleReadCharacteristic->setValue(data);
        bleReadCharacteristic->notify();
    }
}

bool connect()
{
    BLEAddress pAddress = bleServerAddress;
    NimBLEClient *pClient = nullptr;

    LPF2_LOG_D("number of ble clients: %d", NimBLEDevice::getCreatedClientCount());

    /** Check if we have a client we should reuse first **/
    if (NimBLEDevice::getCreatedClientCount())
    {
        /** Special case when we already know this device, we send false as the
         *  second argument in connect() to prevent refreshing the service database.
         *  This saves considerable time and power.
         */
        pClient = NimBLEDevice::getClientByPeerAddress(pAddress);
        if (pClient)
        {
            if (!pClient->connect(pAddress, false))
            {
                LPF2_LOG_E("reconnect failed");
                return false;
            }
            LPF2_LOG_D("reconnect client");
        }
        /** We don't already have a client that knows this device,
         *  we will check for a client that is disconnected that we can use.
         */
        else
        {
            pClient = NimBLEDevice::getDisconnectedClient();
        }
    }

    /** No client to reuse? Create a new one. */
    if (!pClient)
    {
        if (NimBLEDevice::getCreatedClientCount() >= MYNEWT_VAL(BLE_MAX_CONNECTIONS))
        {
            LPF2_LOG_W("max clients reached - no more connections available: %d", NimBLEDevice::getCreatedClientCount());
            return false;
        }

        pClient = NimBLEDevice::createClient();
    }

    if (!pClient->isConnected())
    {
        if (!pClient->connect(pAddress))
        {
            LPF2_LOG_E("failed to connect");
            return false;
        }
    }

    LPF2_LOG_D("connected to: %s, RSSI: %d", pClient->getPeerAddress().toString().c_str(), pClient->getRssi());
    BLERemoteService *pRemoteService = pClient->getService(NimBLEUUID(SERVICE_UUID));
    if (pRemoteService == nullptr)
    {
        LPF2_LOG_E("failed to get ble client");
        return false;
    }

    auto bleReadCharacteristic = pRemoteService->getCharacteristic(NimBLEUUID(READ_CHARACHTERISTIC_UUID));
    if (bleReadCharacteristic == nullptr)
    {
        LPF2_LOG_E("failed to get ble service");
        return false;
    }

    // register notifications (callback function) for the characteristic
    if (bleReadCharacteristic->canNotify())
    {
        bleReadCharacteristic->subscribe(true, notifyCallback, true);
    }

    bleWriteCharacteristic = pRemoteService->getCharacteristic(NimBLEUUID(WRITE_CHARACHTERISTIC_UUID));
    if (bleWriteCharacteristic == nullptr)
    {
        LPF2_LOG_E("failed to get ble service");
        return false;
    }

    connected = true;
    connecting = false;
    vTaskDelay(200);
    return true;
}

void scan()
{
    auto bleScan = BLEDevice::getScan();

    auto bleAdvertiseDeviceCallback = new AdvertisedDeviceCallbacks();

    if (bleAdvertiseDeviceCallback == nullptr)
    {
        LPF2_LOG_E("failed to create advertise device callback");
        return;
    }

    bleScan->setScanCallbacks(bleAdvertiseDeviceCallback);

    bleScan->setActiveScan(true);
    bleScan->start(0);
}