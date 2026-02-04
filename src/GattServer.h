#pragma once

#include <sdbus-c++/sdbus-c++.h>
#include "GattService1_adaptor.h"
#include "GattCharacteristic1_adaptor.h"
#include "LEAdvertisement1_adaptor.h"
#include "MediaEndpoint1_adaptor.h"

#include <atomic>
#include <cstdint>
#include <map>
#include <memory>
#include <thread>
#include <string>
#include <vector>

// Implementation Classes
class TemperatureService : public sdbus::AdaptorInterfaces<org::bluez::GattService1_adaptor>
{
public:
    TemperatureService(sdbus::IConnection& connection, std::string objectPath, std::string uuid, bool primary);
    ~TemperatureService();

    std::string UUID() override { return uuid_; }
    bool Primary() override { return primary_; }

private:
    std::string uuid_;
    bool primary_;
};

class TemperatureCharacteristic : public sdbus::AdaptorInterfaces<org::bluez::GattCharacteristic1_adaptor>
{
public:
    TemperatureCharacteristic(sdbus::IConnection& connection, std::string objectPath, std::string uuid, std::string servicePath);
    ~TemperatureCharacteristic();

    // Adaptor overrides
    std::vector<uint8_t> ReadValue(const std::map<std::string, sdbus::Variant>& options) override;
    void WriteValue(const std::vector<uint8_t>& value, const std::map<std::string, sdbus::Variant>& options) override;
    void StartNotify() override;
    void StopNotify() override;

    std::string UUID() override { return uuid_; }
    sdbus::ObjectPath Service() override { return sdbus::ObjectPath(servicePath_); }
    std::vector<uint8_t> Value() override;
    std::vector<std::string> Flags() override;
    bool Notifying() override { return notifying_; }

    void updateValue(const std::vector<uint8_t>& newValue);

private:
    std::string uuid_;
    std::string servicePath_;
    std::vector<uint8_t> value_;
    std::atomic<bool> notifying_{false};
};

class OurAdvertisement : public sdbus::AdaptorInterfaces<org::bluez::LEAdvertisement1_adaptor>
{
public:
    OurAdvertisement(sdbus::IConnection& connection, std::string objectPath, std::string type, std::string localName, std::string serviceUuid);
    ~OurAdvertisement();

    void Release() override;
    std::string Type() override { return type_; }
    std::vector<std::string> ServiceUUIDs() override { return {serviceUuid_}; }
    std::string LocalName() override { return localName_; }
    // bool Discoverable() was not in the XML, so it's not in the generated adaptor
    // bool Discoverable() override { return true; }

private:
    std::string type_;
    std::string localName_;
    std::string serviceUuid_;
};

class A2dpEndpoint : public sdbus::AdaptorInterfaces<org::bluez::MediaEndpoint1_adaptor>
{
public:
    A2dpEndpoint(sdbus::IConnection& connection, std::string objectPath);
    ~A2dpEndpoint();

    void SetConfiguration(const sdbus::ObjectPath& transport, const std::map<std::string, sdbus::Variant>& properties) override;
    std::vector<uint8_t> SelectConfiguration(const std::vector<uint8_t>& capabilities) override;
    void ClearConfiguration(const sdbus::ObjectPath& transport) override;
    void Release() override;
};

class GattServer
{
public:
    GattServer();
    ~GattServer();

    void start();
    void stop();

private:
    using DictSV = std::map<std::string, sdbus::Variant>;

    void exportApplicationObjectManager();

    void ensureAdapterPoweredOn();
    void unregisterFromBlueZ();
    void notifyValueChanged();

    std::unique_ptr<sdbus::IConnection> conn_;
    std::unique_ptr<sdbus::IProxy> adapterProxy_;

    std::unique_ptr<sdbus::IObject> appObj_; // ObjectManager
    
    // Components
    std::unique_ptr<TemperatureService> serviceObj_;
    std::unique_ptr<TemperatureCharacteristic> charObj_;
    std::unique_ptr<OurAdvertisement> advObj_;
    std::unique_ptr<A2dpEndpoint> endpointObj_;

    std::atomic<bool> started_{false};

    // Temperature sampling thread
    std::thread tempThread_;
    std::atomic<bool> tempThreadRunning_{false};

    int readCpuTemperatureMilliC();
    void startTemperatureThread();
    void stopTemperatureThread();

    const sdbus::ObjectPath adapterPath_{"/org/bluez/hci0"};
    const sdbus::ObjectPath appPath_{"/com/example/gatt/app"};
    const sdbus::ObjectPath servicePath_{"/com/example/gatt/app/service0"};
    const sdbus::ObjectPath charPath_{"/com/example/gatt/app/service0/char0"};
    const sdbus::ObjectPath advPath_{"/com/example/gatt/advertisement0"};
    const sdbus::ObjectPath endpointPath_{"/com/example/a2dp/endpoint0"};

    // Temperature Service config
    const std::string serviceUuid_{"00001809-0000-1000-8000-00805f9b34fb"};
    const std::string charUuid_{"00002A1C-0000-1000-8000-00805f9b34fb"};
    const std::string localName_{"PiGattServer"};
};
