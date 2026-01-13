#include "GattServer.h"

#include <iostream>
#include <stdexcept>
#include <condition_variable>
#include <mutex>

namespace {
constexpr const char* kBluezService = "org.bluez";

constexpr const char* kIfaceProps = "org.freedesktop.DBus.Properties";
constexpr const char* kIfaceObjMgr = "org.freedesktop.DBus.ObjectManager";

constexpr const char* kIfaceAdapter = "org.bluez.Adapter1";
constexpr const char* kIfaceGattMgr = "org.bluez.GattManager1";
constexpr const char* kIfaceAdvMgr = "org.bluez.LEAdvertisingManager1";

constexpr const char* kIfaceGattService = "org.bluez.GattService1";
constexpr const char* kIfaceGattChar = "org.bluez.GattCharacteristic1";
constexpr const char* kIfaceAdv = "org.bluez.LEAdvertisement1";

constexpr const char* kMethodRegisterApp = "RegisterApplication";
constexpr const char* kMethodUnregisterApp = "UnregisterApplication";
constexpr const char* kMethodRegisterAdv = "RegisterAdvertisement";
constexpr const char* kMethodUnregisterAdv = "UnregisterAdvertisement";

constexpr const char* kMethodReadValue = "ReadValue";
constexpr const char* kMethodWriteValue = "WriteValue";
constexpr const char* kMethodStartNotify = "StartNotify";
constexpr const char* kMethodStopNotify = "StopNotify";
constexpr const char* kMethodRelease = "Release";

constexpr const char* kPropPowered = "Powered";

constexpr const char* kPropUUID = "UUID";
constexpr const char* kPropPrimary = "Primary";
constexpr const char* kPropIncludes = "Includes";
constexpr const char* kPropCharacteristics = "Characteristics";

constexpr const char* kPropService = "Service";
constexpr const char* kPropFlags = "Flags";
constexpr const char* kPropValue = "Value";
constexpr const char* kPropNotifying = "Notifying";
constexpr const char* kPropDescriptors = "Descriptors";

constexpr const char* kPropType = "Type";
constexpr const char* kPropServiceUUIDs = "ServiceUUIDs";
constexpr const char* kPropLocalName = "LocalName";
constexpr const char* kPropDiscoverable = "Discoverable";

} // namespace

GattServer::GattServer()
{
    value_ = {0x00};
}

GattServer::~GattServer()
{
    try {
        stop();
    } catch (...) {
    }
}

void GattServer::start()
{
    if (started_.exchange(true))
        return;

    conn_ = sdbus::createSystemBusConnection();

    try {
        std::cout << "Export ObjectManager..." << std::endl;
        exportApplicationObjectManager();
        std::cout << "OK" << std::endl;

        std::cout << "Export GattService1..." << std::endl;
        exportGattService();
        std::cout << "OK" << std::endl;

        std::cout << "Export GattCharacteristic1..." << std::endl;
        exportGattCharacteristic();
        std::cout << "OK" << std::endl;

        std::cout << "Export LEAdvertisement1..." << std::endl;
        exportAdvertisement();
        std::cout << "OK" << std::endl;
    } catch (const std::exception& e) {
        std::cerr << "Export failed: " << e.what() << std::endl;
        throw;
    }

    // BlueZ will call back into our application (ObjectManager + properties/methods) during registration.
    // We must therefore run the connection event loop before calling RegisterApplication/RegisterAdvertisement.
    conn_->enterEventLoopAsync();

    adapterProxy_ = sdbus::createProxy(*conn_, sdbus::ServiceName{kBluezService}, adapterPath_);

    ensureAdapterPoweredOn();

    // Important: BlueZ calls back into our app (GetManagedObjects, property getters) during RegisterApplication.
    // Doing a synchronous RegisterApplication from the same thread can deadlock. Run the loop async and register async.
    conn_->enterEventLoopAsync();

    std::mutex m;
    std::condition_variable cv;
    bool gattOk = false;
    bool advOk = false;
    std::string err;

    auto onError = [&](const std::string& where, const std::optional<sdbus::Error>& e) {
        if (e)
            err = where + ": [" + e->getName() + "] " + e->getMessage();
        else
            err = where + ": unknown error";
    };

    // 1) Register GATT application
    adapterProxy_->callMethodAsync(kMethodRegisterApp)
        .onInterface(kIfaceGattMgr)
        .withArguments(appPath_, DictSV{})
        .uponReplyInvoke([&](std::optional<sdbus::Error> e) {
            std::lock_guard<std::mutex> lk(m);
            if (e) {
                onError("RegisterApplication", e);
            } else {
                gattOk = true;
            }
            cv.notify_all();
        });

    {
        std::unique_lock<std::mutex> lk(m);
        cv.wait_for(lk, std::chrono::seconds(10), [&]{ return gattOk || !err.empty(); });
    }
    if (!err.empty())
        throw std::runtime_error(err);

    // 2) Register Advertisement
    adapterProxy_->callMethodAsync(kMethodRegisterAdv)
        .onInterface(kIfaceAdvMgr)
        .withArguments(advPath_, DictSV{})
        .uponReplyInvoke([&](std::optional<sdbus::Error> e) {
            std::lock_guard<std::mutex> lk(m);
            if (e) {
                onError("RegisterAdvertisement", e);
            } else {
                advOk = true;
            }
            cv.notify_all();
        });

    {
        std::unique_lock<std::mutex> lk(m);
        cv.wait_for(lk, std::chrono::seconds(10), [&]{ return advOk || !err.empty(); });
    }
    if (!err.empty())
        throw std::runtime_error(err);

    std::cout << "Started GATT server: LocalName='" << localName_ << "'" << std::endl;
    std::cout << "Service UUID: " << serviceUuid_ << std::endl;
    std::cout << "Char UUID   : " << charUuid_ << " (read/write/notify)" << std::endl;
}

void GattServer::stop()
{
    if (!started_.exchange(false))
        return;

    try {
        unregisterFromBlueZ();
    } catch (const std::exception& e) {
        std::cerr << "Unregister warning: " << e.what() << std::endl;
    }

    if (conn_) {
        try { conn_->leaveEventLoop(); } catch (...) {}
    }

    advObj_.reset();
    charObj_.reset();
    serviceObj_.reset();
    appObj_.reset();
    adapterProxy_.reset();
    conn_.reset();
}

void GattServer::exportApplicationObjectManager()
{
    appObj_ = sdbus::createObject(*conn_, appPath_);

    // Use sd-bus built-in ObjectManager implementation (GetManagedObjects + tracking).
    // BlueZ's GattManager1.RegisterApplication expects an ObjectManager at the app path.
    appObj_->addObjectManager();
}

void GattServer::exportGattService()
{
    serviceObj_ = sdbus::createObject(*conn_, servicePath_);

    auto getUuid = [this]() { return serviceUuid_; };
    auto getPrimary = []() { return true; };
    auto getIncludes = []() { return std::vector<sdbus::ObjectPath>{}; };
    auto getCharacteristics = [this]() { return std::vector<sdbus::ObjectPath>{charPath_}; };

    serviceObj_->addVTable(
                sdbus::registerProperty(kPropUUID).withGetter(getUuid),
                sdbus::registerProperty(kPropPrimary).withGetter(getPrimary),
                sdbus::registerProperty(kPropIncludes).withGetter(getIncludes),
                sdbus::registerProperty(kPropCharacteristics).withGetter(getCharacteristics))
        .forInterface(kIfaceGattService);
}

void GattServer::exportGattCharacteristic()
{
    charObj_ = sdbus::createObject(*conn_, charPath_);

    auto readValue = [this](const DictSV& /*options*/) -> std::vector<std::uint8_t> {
        std::cout << "[BLE] ReadValue: Client is reading characteristic value. Data: [";
        for (size_t i = 0; i < value_.size(); ++i) {
            std::cout << "0x" << std::hex << static_cast<int>(value_[i]);
            if (i < value_.size() - 1) std::cout << ", ";
        }
        std::cout << std::dec << "]" << std::endl;
        return value_;
    };

    auto writeValue = [this](const std::vector<std::uint8_t>& value, const DictSV& /*options*/) {
        std::cout << "[BLE] WriteValue: Client wrote " << value.size() << " bytes. Data: [";
        for (size_t i = 0; i < value.size(); ++i) {
            std::cout << "0x" << std::hex << static_cast<int>(value[i]);
            if (i < value.size() - 1) std::cout << ", ";
        }
        std::cout << std::dec << "]" << std::endl;
        value_ = value;
        notifyValueChanged();
    };

    auto startNotify = [this]() {
        std::cout << "[BLE] StartNotify: Client subscribed to notifications" << std::endl;
        const bool wasNotifying = notifying_.exchange(true);
        if (!wasNotifying && charObj_) {
            charObj_->emitPropertiesChangedSignal(kIfaceGattChar, {sdbus::PropertyName{kPropNotifying}});
        }
        notifyValueChanged();
    };

    auto stopNotify = [this]() {
        std::cout << "[BLE] StopNotify: Client unsubscribed from notifications" << std::endl;
        const bool wasNotifying = notifying_.exchange(false);
        if (wasNotifying && charObj_) {
            charObj_->emitPropertiesChangedSignal(kIfaceGattChar, {sdbus::PropertyName{kPropNotifying}});
        }
    };

    auto getUuid = [this]() { return charUuid_; };
    auto getService = [this]() { return servicePath_; };
    auto getFlags = []() { return std::vector<std::string>{"read", "write", "notify"}; };
    auto getValue = [this]() { return value_; };
    auto getNotifying = [this]() { return notifying_.load(); };
    auto getDescriptors = []() { return std::vector<sdbus::ObjectPath>{}; };

    charObj_->addVTable(
                sdbus::registerMethod(kMethodReadValue).implementedAs(readValue),
                sdbus::registerMethod(kMethodWriteValue).implementedAs(writeValue),
                sdbus::registerMethod(kMethodStartNotify).implementedAs(startNotify),
                sdbus::registerMethod(kMethodStopNotify).implementedAs(stopNotify),
                sdbus::registerProperty(kPropUUID).withGetter(getUuid),
                sdbus::registerProperty(kPropService).withGetter(getService),
                sdbus::registerProperty(kPropFlags).withGetter(getFlags),
                sdbus::registerProperty(kPropValue).withGetter(getValue),
                sdbus::registerProperty(kPropNotifying).withGetter(getNotifying),
                sdbus::registerProperty(kPropDescriptors).withGetter(getDescriptors))
        .forInterface(kIfaceGattChar);
}

void GattServer::exportAdvertisement()
{
    advObj_ = sdbus::createObject(*conn_, advPath_);

    auto release = []() {
        std::cout << "Advertisement released" << std::endl;
    };

    auto getType = []() { return std::string{"peripheral"}; };
    auto getServiceUUIDs = [this]() { return std::vector<std::string>{serviceUuid_}; };
    auto getLocalName = [this]() { return localName_; };
    auto getDiscoverable = []() { return true; };

    advObj_->addVTable(
              sdbus::registerMethod(kMethodRelease).implementedAs(release),
              sdbus::registerProperty(kPropType).withGetter(getType),
              sdbus::registerProperty(kPropServiceUUIDs).withGetter(getServiceUUIDs),
              sdbus::registerProperty(kPropLocalName).withGetter(getLocalName),
              sdbus::registerProperty(kPropDiscoverable).withGetter(getDiscoverable))
        .forInterface(kIfaceAdv);
}

void GattServer::ensureAdapterPoweredOn()
{
    if (!adapterProxy_)
        throw std::runtime_error("Adapter proxy not initialized");

    sdbus::Variant powered;
    adapterProxy_->callMethod("Get")
        .onInterface(kIfaceProps)
        .withArguments(std::string{kIfaceAdapter}, std::string{kPropPowered})
        .storeResultsTo(powered);

    if (!(bool)powered)
    {
        adapterProxy_->callMethod("Set")
            .onInterface(kIfaceProps)
            .withArguments(std::string{kIfaceAdapter}, std::string{kPropPowered}, sdbus::Variant(true))
            .storeResultsTo();
    }
}

void GattServer::registerWithBlueZ()
{
    DictSV options;

    adapterProxy_->callMethod(kMethodRegisterApp)
        .onInterface(kIfaceGattMgr)
        .withArguments(appPath_, options)
        .storeResultsTo();

    adapterProxy_->callMethod(kMethodRegisterAdv)
        .onInterface(kIfaceAdvMgr)
        .withArguments(advPath_, options)
        .storeResultsTo();
}

void GattServer::unregisterFromBlueZ()
{
    if (!adapterProxy_)
        return;

    try {
        adapterProxy_->callMethod(kMethodUnregisterAdv)
            .onInterface(kIfaceAdvMgr)
            .withArguments(advPath_)
            .storeResultsTo();
    } catch (const sdbus::Error&) {
    }

    try {
        adapterProxy_->callMethod(kMethodUnregisterApp)
            .onInterface(kIfaceGattMgr)
            .withArguments(appPath_)
            .storeResultsTo();
    } catch (const sdbus::Error&) {
    }
}

void GattServer::notifyValueChanged()
{
    if (!charObj_)
        return;

    try {
        charObj_->emitPropertiesChangedSignal(kIfaceGattChar, {sdbus::PropertyName{kPropValue}});
    } catch (const std::exception& e) {
        std::cerr << "PropertiesChanged(Value) warning: " << e.what() << std::endl;
    }
}
