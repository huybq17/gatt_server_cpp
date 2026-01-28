#include "GattServer.h"
#include "Logger.h"

#include <atomic>
#include <chrono>
#include <csignal>
#include <iostream>
#include <thread>

namespace {
std::atomic<bool> gStop{false};

void onSignal(int sig) {
    LOG_INFO("Received signal ", sig, ", stopping...");
    gStop = true;
}
}

int main()
{
    // Initialize logger
    Logger::getInstance().setLogLevel(LogLevel::DEBUG);
    Logger::getInstance().setLogFile("/var/log/gatt_server.log");
    Logger::getInstance().setLogToConsole(true);

    LOG_INFO("GATT Server starting...");

    std::signal(SIGINT, onSignal);
    std::signal(SIGTERM, onSignal);

    try
    {
        GattServer server;
        server.start();

        LOG_INFO("GATT Server running. Press Ctrl+C to stop.");

        while (!gStop.load())
            std::this_thread::sleep_for(std::chrono::milliseconds(200));

        LOG_INFO("Stopping GATT Server...");
        server.stop();
        LOG_INFO("GATT Server stopped successfully.");
        return 0;
    }
    catch (const std::exception& e)
    {
        LOG_ERROR("Fatal error: ", e.what());
        return 1;
    }
}
