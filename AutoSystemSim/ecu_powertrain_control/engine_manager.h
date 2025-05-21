// AutoSystemSim/ecu_powertrain_control/engine_manager.h
#ifndef ENGINE_MANAGER_H
#define ENGINE_MANAGER_H

#include "../common/logger.h"
#include "../common/datatypes.h" // For VehicleState, SensorData
#include "fuel_system.h"        // Dependency within the same "Application" (ECU)

// Forward declaration for a dependency from another "Application" (ECU)
// We will define this later.
namespace ecu_power_management {
    class PowerMonitor; // Assuming a PowerMonitor class in another ECU
}

namespace ecu_powertrain_control {

class EngineManager {
public:
    EngineManager();
    ~EngineManager();

    bool startEngine();
    bool stopEngine();
    bool setTargetRPM(int rpm);
    int getCurrentRPM() const;
    double getEngineTemperature() const; // Celsius
    VehicleState getEngineState() const;

    // This function will call a function in another ECU (PowerMonitor)
    bool checkSystemPower();
    void updateEngineParameters();
private:
    enum class EngineStatus {
        STOPPED,
        STARTING,
        RUNNING,
        STOPPING,
        FAULT
    };

    EngineStatus current_status_;
    int current_rpm_;
    int target_rpm_;
    double engine_temperature_celsius_;
    FuelSystem fuel_system_; // Composition: EngineManager owns a FuelSystem

    // Pointer to a component in another ECU (dependency injection or service locator pattern conceptually)
    ecu_power_management::PowerMonitor* power_monitor_service_; // We'll need to "provide" this

    bool performIgnitionSequence(); // Simulates continuous operation
    bool checkOilPressure();
    void reportCriticalFault(const std::string& fault_description);
};

} // namespace ecu_powertrain_control

#endif // ENGINE_MANAGER_H