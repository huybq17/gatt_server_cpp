#include "GattServer.h"

#include <atomic>
#include <chrono>
#include <csignal>
#include <iostream>
#include <thread>

namespace {
std::atomic<bool> gStop{false};

void onSignal(int) {
    gStop = true;
}
}

int main()
{
    std::signal(SIGINT, onSignal);
    std::signal(SIGTERM, onSignal);

    try
    {
        GattServer server;
        server.start();

        while (!gStop.load())
            std::this_thread::sleep_for(std::chrono::milliseconds(200));

        server.stop();
        return 0;
    }
    catch (const std::exception& e)
    {
        std::cerr << "Fatal: " << e.what() << std::endl;
        return 1;
    }
}
