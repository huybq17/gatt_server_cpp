#include "GattServer.h"
#include "Logger.h"

#include <iostream>
#include <stdexcept>
#include <condition_variable>
#include <mutex>
#include <thread>
#include <fstream>
#include <chrono>
#include <sstream>

namespace {
constexpr const char* kBluezService = "org.bluez";

constexpr const char* kIfaceProps = "org.freedesktop.DBus.Properties";
constexpr const char* kIfaceObjMgr = "org.freedesktop.DBus.ObjectManager";

constexpr const char* kIfaceAdapter = "org.bluez.Adapter1";
constexpr const char* kIfaceGattMgr = "org.bluez.GattManager1";
constexpr const char* kIfaceAdvMgr = "org.bluez.LEAdvertisingManager1";
constexpr const char* kIfaceMedia = "org.bluez.Media1";

constexpr const char* kMethodRegisterApp = "RegisterApplication";
constexpr const char* kMethodUnregisterApp = "UnregisterApplication";
constexpr const char* kMethodRegisterAdv = "RegisterAdvertisement";
constexpr const char* kMethodUnregisterAdv = "UnregisterAdvertisement";
constexpr const char* kMethodRegisterEndpoint = "RegisterEndpoint";
constexpr const char* kMethodUnregisterEndpoint = "UnregisterEndpoint";

constexpr const char* kPropPowered = "Powered";

constexpr const char* kUuidA2dpSink = "0000110B-0000-1000-8000-00805F9B34FB";
constexpr uint8_t kCodecSbc = 0x00;

} // namespace

// ===========================================
// Temperature Service Implementation
// ===========================================
TemperatureService::TemperatureService(sdbus::IConnection& connection, std::string objectPath, std::string uuid, bool primary)
    : AdaptorInterfaces(connection, sdbus::ObjectPath(std::move(objectPath))), uuid_(std::move(uuid)), primary_(primary)
{
    registerAdaptor();
}

TemperatureService::~TemperatureService()
{
    unregisterAdaptor();
}

// ===========================================
// Temperature Characteristic Implementation
// ===========================================
TemperatureCharacteristic::TemperatureCharacteristic(sdbus::IConnection& connection, std::string objectPath, std::string uuid, std::string servicePath)
    : AdaptorInterfaces(connection, sdbus::ObjectPath(std::move(objectPath))), uuid_(std::move(uuid)), servicePath_(std::move(servicePath))
{
    value_ = {0x00};
    registerAdaptor();
}

TemperatureCharacteristic::~TemperatureCharacteristic()
{
    unregisterAdaptor();
}

std::vector<uint8_t> TemperatureCharacteristic::ReadValue(const std::map<std::string, sdbus::Variant>&)
{
    std::ostringstream oss;
    oss << "[BLE] ReadValue";
    LOG_DEBUG(oss.str());
    return value_;
}

void TemperatureCharacteristic::WriteValue(const std::vector<uint8_t>& value, const std::map<std::string, sdbus::Variant>&)
{
    std::ostringstream oss;
    oss << "[BLE] WriteValue: " << value.size() << " bytes";
    LOG_DEBUG(oss.str());
    value_ = value;
    
    // Emit signal
    // emitPropertiesChangedSignal is available via ObjectHolder -> IObject
    getObject().emitPropertiesChangedSignal(sdbus::InterfaceName(org::bluez::GattCharacteristic1_adaptor::INTERFACE_NAME), {sdbus::PropertyName("Value")});
}

void TemperatureCharacteristic::StartNotify()
{
    LOG_INFO("[BLE] StartNotify");
    notifying_ = true;
    getObject().emitPropertiesChangedSignal(sdbus::InterfaceName(org::bluez::GattCharacteristic1_adaptor::INTERFACE_NAME), {sdbus::PropertyName("Notifying")});
}

void TemperatureCharacteristic::StopNotify()
{
    LOG_INFO("[BLE] StopNotify");
    notifying_ = false;
    getObject().emitPropertiesChangedSignal(sdbus::InterfaceName(org::bluez::GattCharacteristic1_adaptor::INTERFACE_NAME), {sdbus::PropertyName("Notifying")});
}

std::vector<uint8_t> TemperatureCharacteristic::Value()
{
    return value_;
}

std::vector<std::string> TemperatureCharacteristic::Flags()
{
    return {"read", "write", "notify"};
}

void TemperatureCharacteristic::updateValue(const std::vector<uint8_t>& newValue)
{
    value_ = newValue;
    if (notifying_) {
        getObject().emitPropertiesChangedSignal(sdbus::InterfaceName(org::bluez::GattCharacteristic1_adaptor::INTERFACE_NAME), {sdbus::PropertyName("Value")});
    }
}

// ===========================================
// Advertisement Implementation
// ===========================================
OurAdvertisement::OurAdvertisement(sdbus::IConnection& connection, std::string objectPath, std::string type, std::string localName, std::string serviceUuid)
    : AdaptorInterfaces(connection, sdbus::ObjectPath(std::move(objectPath))), type_(std::move(type)), localName_(std::move(localName)), serviceUuid_(std::move(serviceUuid))
{
    registerAdaptor();
}

OurAdvertisement::~OurAdvertisement()
{
    unregisterAdaptor();
}

void OurAdvertisement::Release()
{
    LOG_INFO("Advertisement released");
}

// ===========================================
// Media Endpoint Implementation
// ===========================================
A2dpEndpoint::A2dpEndpoint(sdbus::IConnection& connection, std::string objectPath)
    : AdaptorInterfaces(connection, sdbus::ObjectPath(std::move(objectPath)))
{
    registerAdaptor();
}

A2dpEndpoint::~A2dpEndpoint()
{
    unregisterAdaptor();
}

void A2dpEndpoint::SetConfiguration(const sdbus::ObjectPath& transport, const std::map<std::string, sdbus::Variant>& properties)
{
    LOG_INFO("MediaEndpoint: SetConfiguration called via Adaptor");
    LOG_INFO("  Transport: ", transport);
}

std::vector<uint8_t> A2dpEndpoint::SelectConfiguration(const std::vector<uint8_t>& capabilities)
{
    LOG_INFO("MediaEndpoint: SelectConfiguration called via Adaptor");
    return capabilities;
}

void A2dpEndpoint::ClearConfiguration(const sdbus::ObjectPath& transport)
{
    LOG_INFO("MediaEndpoint: ClearConfiguration called via Adaptor");
}

void A2dpEndpoint::Release()
{
    LOG_INFO("MediaEndpoint: Release called via Adaptor");
}


// ===========================================
// GattServer Implementation
// ===========================================

GattServer::GattServer()
{
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
    std::mutex m;
    std::condition_variable cv;
    bool gattOk = false;
    bool advOk = false;
    bool endpointOk = false;
    std::string err;

    if (started_.exchange(true))
        return;

    try {
        conn_ = sdbus::createSystemBusConnection();
        LOG_DEBUG("System D-Bus connection established");
    } catch (const sdbus::Error& e) {
        LOG_ERROR("Failed to connect to system D-Bus: [", e.getName(), "] ", e.getMessage());
        throw;
    }

    try {
        LOG_INFO("Export ObjectManager...");
        exportApplicationObjectManager();
        
        LOG_INFO("Creating Service Adaptors...");
        serviceObj_ = std::make_unique<TemperatureService>(*conn_, servicePath_, serviceUuid_, true);
        charObj_ = std::make_unique<TemperatureCharacteristic>(*conn_, charPath_, charUuid_, servicePath_);
        advObj_ = std::make_unique<OurAdvertisement>(*conn_, advPath_, "peripheral", localName_, serviceUuid_);
        endpointObj_ = std::make_unique<A2dpEndpoint>(*conn_, endpointPath_);
        
        LOG_INFO("Adaptors exported successfully");
    } catch (const std::exception& e) {
        LOG_ERROR("Export failed: ", e.what());
        throw;
    }

    conn_->enterEventLoopAsync();
    adapterProxy_ = sdbus::createProxy(*conn_, sdbus::ServiceName{kBluezService}, adapterPath_);
    ensureAdapterPoweredOn();
    conn_->enterEventLoopAsync();

    auto onError = [&](const std::string& where, const std::optional<sdbus::Error>& e) {
        if (e) {
            err = where + ": [" + e->getName() + "] " + e->getMessage();
            LOG_ERROR("D-Bus error in ", where, ": [", e->getName(), "] ", e->getMessage());
        } else {
            err = where + ": unknown error";
            LOG_ERROR("Unknown error in ", where);
        }
    };

    // 1) Register GATT application
    adapterProxy_->callMethodAsync(kMethodRegisterApp)
        .onInterface(kIfaceGattMgr)
        .withArguments(appPath_, DictSV{})
        .uponReplyInvoke([&](std::optional<sdbus::Error> e) {
            std::lock_guard<std::mutex> lk(m);
            if (e) onError("RegisterApplication", e); else gattOk = true;
            cv.notify_all();
        });

    {
        std::unique_lock<std::mutex> lk(m);
        cv.wait_for(lk, std::chrono::seconds(10), [&]{ return gattOk || !err.empty(); });
    }
    if (!err.empty()) throw std::runtime_error(err);

    // 2) Register Advertisement
    adapterProxy_->callMethodAsync(kMethodRegisterAdv)
        .onInterface(kIfaceAdvMgr)
        .withArguments(advPath_, DictSV{})
        .uponReplyInvoke([&](std::optional<sdbus::Error> e) {
            std::lock_guard<std::mutex> lk(m);
            if (e) onError("RegisterAdvertisement", e); else advOk = true;
            cv.notify_all();
        });

    {
        std::unique_lock<std::mutex> lk(m);
        cv.wait_for(lk, std::chrono::seconds(10), [&]{ return advOk || !err.empty(); });
    }
    if (!err.empty()) throw std::runtime_error(err);
    
    // 3) Register Media Endpoint
    DictSV endpointProps;
    endpointProps["UUID"] = sdbus::Variant(std::string(kUuidA2dpSink));
    endpointProps["Codec"] = sdbus::Variant(kCodecSbc);
    endpointProps["Capabilities"] = sdbus::Variant(std::vector<uint8_t>{0x3F, 0xFF, 0x02, 0xFF}); 

    adapterProxy_->callMethodAsync(kMethodRegisterEndpoint)
        .onInterface(kIfaceMedia)
        .withArguments(endpointPath_, endpointProps)
        .uponReplyInvoke([&](std::optional<sdbus::Error> e) {
            std::lock_guard<std::mutex> lk(m);
            if (e) onError("RegisterEndpoint", e); else endpointOk = true;
            cv.notify_all();
        });

    {
        std::unique_lock<std::mutex> lk(m);
        cv.wait_for(lk, std::chrono::seconds(10), [&]{ return endpointOk || !err.empty(); });
    }
    if (!err.empty()) throw std::runtime_error(err);

    startTemperatureThread();
}

void GattServer::stop()
{
    if (!started_.exchange(false))
        return;

    try { unregisterFromBlueZ(); } catch (...) {}
    if (conn_) {
        try { conn_->leaveEventLoop(); } catch (...) {}
    }
    try { stopTemperatureThread(); } catch (...) {}

    // Destructors of adaptors handle unregisterAdaptor()
    endpointObj_.reset();
    advObj_.reset();
    charObj_.reset();
    serviceObj_.reset();
    
    appObj_.reset();
    adapterProxy_.reset();
    conn_.reset();
}

int GattServer::readCpuTemperatureMilliC()
{
    int milli = -1;
    std::ifstream f("/sys/class/thermal/thermal_zone0/temp");
    if (!f.is_open()) {
        LOG_WARNING("Failed to open /sys/class/thermal/thermal_zone0/temp");
        return -1;
    }

    f >> milli;
    if (f.fail()) {
        LOG_WARNING("Failed to read temperature value from thermal zone");
        return -1;
    }
    return milli;
}

void GattServer::startTemperatureThread()
{
    if (tempThreadRunning_.exchange(true))
        return;

    tempThread_ = std::thread([this]() {
        int lastMilli = -1;
        while (tempThreadRunning_.load()) {
            int milli = readCpuTemperatureMilliC();
            if (milli != -1 && milli != lastMilli) {
                lastMilli = milli;

                // Simple IEEE-11073 conversion
                uint8_t flags = 0x00;
                int32_t mantissa = static_cast<int32_t>(milli);
                int8_t exponent = -3;

                std::vector<std::uint8_t> data(5);
                data[0] = flags;
                uint32_t mant = static_cast<uint32_t>(mantissa & 0xFFFFFF);
                data[1] = static_cast<std::uint8_t>(mant & 0xFF);
                data[2] = static_cast<std::uint8_t>((mant >> 8) & 0xFF);
                data[3] = static_cast<std::uint8_t>((mant >> 16) & 0xFF);
                data[4] = static_cast<std::uint8_t>(static_cast<uint8_t>(exponent));

                // Update via Adaptor Access
                if (charObj_) {
                    charObj_->updateValue(data);
                }
            }
            std::this_thread::sleep_for(std::chrono::seconds(2));
        }
    });
}

void GattServer::stopTemperatureThread()
{
    tempThreadRunning_.store(false);
    if (tempThread_.joinable())
        tempThread_.join();
}

void GattServer::exportApplicationObjectManager()
{
    appObj_ = sdbus::createObject(*conn_, appPath_);
    appObj_->addObjectManager();
}

void GattServer::ensureAdapterPoweredOn()
{
    if (!adapterProxy_) return;
    try {
        sdbus::Variant powered;
        adapterProxy_->callMethod("Get").onInterface(kIfaceProps).withArguments(std::string{kIfaceAdapter}, std::string{kPropPowered}).storeResultsTo(powered);
        if (!(bool)powered) {
             adapterProxy_->callMethod("Set").onInterface(kIfaceProps).withArguments(std::string{kIfaceAdapter}, std::string{kPropPowered}, sdbus::Variant(true)).storeResultsTo();
        }
    } catch (...) {}
}

void GattServer::unregisterFromBlueZ()
{
    if (!adapterProxy_) return;
    try {
        adapterProxy_->callMethod(kMethodUnregisterEndpoint).onInterface(kIfaceMedia).withArguments(endpointPath_).storeResultsTo();
        adapterProxy_->callMethod(kMethodUnregisterAdv).onInterface(kIfaceAdvMgr).withArguments(advPath_).storeResultsTo();
        adapterProxy_->callMethod(kMethodUnregisterApp).onInterface(kIfaceGattMgr).withArguments(appPath_).storeResultsTo();
    } catch (...) {}
}

void GattServer::notifyValueChanged()
{
    // Now handled inside TemperatureCharacteristic
}
