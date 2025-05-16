#include "main_vehicle_controller.h"
#include "../common/logger.h" // Pour des logs globaux si besoin
#include <iostream>

// Global Log configuration (optional, could be in a config file or logger init function)
// For now, our logger.h writes to std::cout.

int main(int argc, char* argv[]) {
    // Initial log message from the very start of the application
    // This uses std::cout directly as our LOG_XXXX macros might not be fully set up
    // or we might want a log before any ECU objects are created.
    std::cout << "[BOOT] Main Application starting..." << std::endl;
    LOG_INFO("main: Application Entry Point."); // Our macro logger

    main_application::MainVehicleController vehicle_controller;

    LOG_INFO("main: Initializing all vehicle systems via MainVehicleController...");
    vehicle_controller.initializeAllSystems();
    LOG_INFO("main: All systems initialized.");

    // Simulate a full driving cycle
    LOG_INFO("main: Starting simulated driving cycle...");
    vehicle_controller.simulateDrivingCycle();
    LOG_INFO("main: Simulated driving cycle finished.");

    // MainVehicleController destructor will handle shutdown of individual ECUs

    LOG_INFO("main: Application will now exit.");
    std::cout << "[BOOT] Main Application finished." << std::endl;
    return 0;
}