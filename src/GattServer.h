#pragma once

#include <sdbus-c++/sdbus-c++.h>

#include <atomic>
#include <cstdint>
#include <map>
#include <memory>
#include <string>
#include <vector>

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
    void exportGattService();
    void exportGattCharacteristic();
    void exportAdvertisement();

    void ensureAdapterPoweredOn();
    void registerWithBlueZ();
    void unregisterFromBlueZ();
    void notifyValueChanged();

    std::unique_ptr<sdbus::IConnection> conn_;
    std::unique_ptr<sdbus::IProxy> adapterProxy_;

    std::unique_ptr<sdbus::IObject> appObj_;
    std::unique_ptr<sdbus::IObject> serviceObj_;
    std::unique_ptr<sdbus::IObject> charObj_;
    std::unique_ptr<sdbus::IObject> advObj_;

    std::atomic<bool> started_{false};
    std::atomic<bool> notifying_{false};

    std::vector<std::uint8_t> value_;

    const sdbus::ObjectPath adapterPath_{"/org/bluez/hci0"};
    const sdbus::ObjectPath appPath_{"/com/example/gatt/app"};
    const sdbus::ObjectPath servicePath_{"/com/example/gatt/app/service0"};
    const sdbus::ObjectPath charPath_{"/com/example/gatt/app/service0/char0"};
    const sdbus::ObjectPath advPath_{"/com/example/gatt/advertisement0"};

    const std::string serviceUuid_{"12345678-1234-5678-1234-56789abcdef0"};
    const std::string charUuid_{"12345678-1234-5678-1234-56789abcdef1"};
    const std::string localName_{"PiGattServer"};
};
