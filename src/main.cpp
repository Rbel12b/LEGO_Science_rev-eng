#include <Arduino.h>
#include <unordered_map>

#include <NimBLEDevice.h>
#include <Lpf2/config.hpp>
#include <Lpf2/Util/Values.hpp>

#include <SerialRenderer.hpp>

#define SERVICE_UUID "0000fd02-0000-1000-8000-00805f9b34fb"
#define WRITE_CHARACHTERISTIC_UUID "0000fd02-0001-1000-8000-00805f9b34fb"
#define READ_CHARACHTERISTIC_UUID "0000fd02-0002-1000-8000-00805f9b34fb"

// bool subscribed = false;
bool connecting = false;
bool connected = false;
NimBLEAddress bleServerAddress;
NimBLERemoteCharacteristic *bleWriteCharacteristic = nullptr;
NimBLECharacteristic *bleReadCharacteristic = nullptr;

SerialRenderer renderer(Serial);

class WriteCharacteristicCallbacks : public NimBLECharacteristicCallbacks
{
public:
    WriteCharacteristicCallbacks() : NimBLECharacteristicCallbacks() {}
    // void onSubscribe(NimBLECharacteristic *pCharacteristic, NimBLEConnInfo &connInfo, uint16_t subValue) override;
    void onWrite(NimBLECharacteristic *pCharacteristic, NimBLEConnInfo &connInfo) override
    {
        std::vector<uint8_t> data(pCharacteristic->getValue());
        LPF2_LOG_I("APP -> DEVICE: %s", Lpf2::Utils::bytes_to_hexString(data).c_str());
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

namespace std {
    template<>
    struct hash<NimBLEAddress> {
        size_t operator()(const NimBLEAddress& addr) const {
            return std::hash<std::string>()(addr.toString());
        }
    };
}

std::unordered_map<NimBLEAddress, const NimBLEAdvertisedDevice*> foundDevices;

bool scanning = false;
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
        // LPF2_LOG_D("advertised device: %s", advertisedDevice->toString().c_str());

        // if (advertisedDevice->haveServiceUUID() && advertisedDevice->getServiceUUID().equals(NimBLEUUID(SERVICE_UUID)))
        // {
        //     advertisedDevice->getScan()->stop();

        //     if (advertisedDevice->haveManufacturerData())
        //     {
        //         LPF2_LOG_D("advertisement payload: %s", Lpf2::Utils::bytes_to_hexString(advertisedDevice->getPayload()).c_str());
        //         LPF2_LOG_D("manufacturer data: %s", Lpf2::Utils::bytes_to_hexString(advertisedDevice->getManufacturerData()).c_str());
        //         bleServerAddress = BLEAddress(advertisedDevice->getAddress());
        //         connecting = true;
        //         scanning = false;
        //     }
        // }

        auto addr = advertisedDevice->getAddress();
        foundDevices[addr] = advertisedDevice;
    }
};

bool connect();
void scan();
void renderDevices();

std::vector<uint8_t> manufacturerData = {0x02, 0x01, 0x06, 0x0f, 0x16, 0x02, 0xfd};

uint8_t devType = 0x03, color = 0x07, val1 = 0x03, val2 = 0x00;
uint16_t cardNum = 0x08BC;
uint32_t timer = 0;

NimBLEAdvertising *bleAdvertising;

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

    bleAdvertising = NimBLEDevice::getAdvertising();

    bleAdvertising->enableScanResponse(true);

    auto advertisementData = NimBLEAdvertisementData();
    auto scanResponseData = NimBLEAdvertisementData();

    // Raw advertisement payload
    std::vector<uint8_t> advPayload = {
        devType, color, 0x00, (uint8_t)(cardNum & 0xFF), (uint8_t)((cardNum >> 8) & 0xFF), val1, val2, 0x00, 0x7e, (uint8_t)timer, (uint8_t)(timer >> 8), (uint8_t)(timer >> 16)
    };

    advPayload.insert(advPayload.begin(), manufacturerData.begin(), manufacturerData.end());

    // Raw scan response payload
    std::vector<uint8_t> scanRespPayload = {};

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

    Serial.print("\033[2J");
    Serial.print("\033[H");
}

void loop()
{
    // vTaskDelay(1);

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

    if (!connecting && !connected && !scanning)
    {
        scan();
        scanning = true;
    }

    renderDevices();

    // Raw advertisement payload
    std::vector<uint8_t> advPayload = {
        // devType, color, 0x00, (uint8_t)(cardNum & 0xFF), (uint8_t)((cardNum >> 8) & 0xFF), val1, val2, 0x00, 0x7e, (uint8_t)timer, (uint8_t)(timer >> 8), (uint8_t)(timer >> 16)
        0x02, 0x01, 0x06, 0x0f, 0x16, 0x02, 0xfd, 0x03, 0x06, 0x00, 0xbc, 0x08, 0x03, 0x0d, 0x00, 0x83, 0x60, 0x60, 0x02
    };

    // advPayload.insert(advPayload.begin(), manufacturerData.begin(), manufacturerData.end());

    // Raw scan response payload
    std::vector<uint8_t> scanRespPayload = {};

    // Assign raw data
    auto advertisementData = NimBLEAdvertisementData();
    advertisementData.addData(advPayload);
    bleAdvertising->setAdvertisementData(advertisementData);

    timer = millis();
}

void renderDevices()
{
    std::vector<std::string> lines;

    std::string device_addrs;
    for (const auto &[addr, device] : foundDevices)
    {
        device_addrs += device->getAddress().toString() + " ";
    }

    lines.push_back(
        std::string(LPF2_LOG_COLOR(LPF2_LOG_COLOR_MAGENTA)) +
        "Devices: " + device_addrs +
        LPF2_LOG_RESET_COLOR
    );

    for (const auto &[addr, device] : foundDevices)
    {
        auto &payload = device->getPayload();

        if (payload.size() < manufacturerData.size() ||
            !std::equal(manufacturerData.begin(), manufacturerData.end(), payload.begin()))
        {
            continue;
        }

        lines.push_back(
            std::string(LPF2_LOG_COLOR(LPF2_LOG_COLOR_MAGENTA)) +
            "device: " + device->getAddress().toString() +
            ", advertisement data: " +
            Lpf2::Utils::bytes_to_hexString(payload) +
            LPF2_LOG_RESET_COLOR
        );
    }

    renderer.textBlock(lines);
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

    bleScan->setScanCallbacks(bleAdvertiseDeviceCallback, true);

    bleScan->setActiveScan(true);
    bleScan->start(0);
}