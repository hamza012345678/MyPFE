#ifndef MAIN_VEHICLE_CONTROLLER_H
#define MAIN_VEHICLE_CONTROLLER_H

#include "../common/logger.h"
#include "../common/datatypes.h"

// Include headers for all major ECU managers/controllers
#include "../ecu_powertrain_control/engine_manager.h"
#include "../ecu_powertrain_control/transmission_manager.h"
// #include "../ecu_powertrain_control/fuel_system.h" // Usually managed by EngineManager

#include "../ecu_body_control_module/climate_control.h"
#include "../ecu_body_control_module/lighting_control.h"
#include "../ecu_body_control_module/window_control.h"
// Potentially a higher-level BCM class if we had one

#include "../ecu_infotainment/media_player.h"
#include "../ecu_infotainment/navigation_system.h"
// Potentially a higher-level Infotainment class

#include "../ecu_safety_systems/abs_control.h"
#include "../ecu_safety_systems/airbag_control.h"
// Potentially a higher-level SafetySystems class

#include "../ecu_power_management/power_monitor.h"

#include <vector>
#include <string>
#include <memory> // For std::unique_ptr or std::shared_ptr if managing ECU lifecycles

namespace main_application {

class MainVehicleController {
public:
    MainVehicleController();
    ~MainVehicleController();

    void initializeAllSystems();
    void runMainLoop(); // Simulates the main vehicle operation loop
    void shutdownAllSystems();

    // Example direct commands or event handlers
    void handleIgnitionOn();
    void handleIgnitionOff();
    void simulateDrivingCycle(); // A sequence of simulated events

private:
    // --- ECU Instances ---
    // Using raw pointers for simplicity in this dummy code.
    // In a real system, std::unique_ptr or std::shared_ptr would be safer.
    ecu_power_management::PowerMonitor* power_monitor_;

    ecu_powertrain_control::EngineManager* engine_manager_;
    ecu_powertrain_control::TransmissionManager* transmission_manager_;
    // ecu_powertrain_control::FuelSystem is part of EngineManager

    // For BCM, we'd typically have a main BCM class that owns these.
    // For simplicity, MainVehicleController directly owns them.
    ecu_body_control_module::ClimateControl* climate_control_;
    ecu_body_control_module::LightingControl* lighting_control_;
    ecu_body_control_module::WindowControl* window_control_;

    ecu_infotainment::MediaPlayer* media_player_;
    ecu_infotainment::NavigationSystem* navigation_system_;

    ecu_safety_systems::ABSControl* abs_control_;
    ecu_safety_systems::AirbagControl* airbag_control_;


    // --- Vehicle State ---
    VehicleState current_vehicle_state_;
    CrashSensorInput current_crash_sensors_input_; // For Airbag system
    std::vector<SensorData> current_wheel_speed_sensors_; // For ABS
    double current_brake_pedal_pressure_; // For ABS

    // --- Internal State ---
    bool ignition_on_;
    int main_loop_cycles_;


    // --- Internal Helper Methods ---
    void updateVehicleStateInputs(); // Simulate sensor updates
    void periodicECUUpdates();       // Call update methods of various ECUs
    void checkSystemHealth();        // Periodically run diagnostics or check status
};

} // namespace main_application

#endif // MAIN_VEHICLE_CONTROLLER_H