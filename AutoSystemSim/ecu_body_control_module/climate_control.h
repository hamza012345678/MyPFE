// AutoSystemSim/ecu_body_control_module/climate_control.h
#ifndef CLIMATE_CONTROL_H
#define CLIMATE_CONTROL_H

#include "../common/logger.h"
#include "../common/datatypes.h" // For SensorData (temp sensors), VehicleState (engine running for AC)

// Forward declaration for PowerMonitor dependency
namespace ecu_power_management {
    class PowerMonitor;
}

namespace ecu_body_control_module {

enum class AirDistributionMode {
    OFF,
    FACE_VENTS,
    FEET_VENTS,
    FACE_AND_FEET,
    WINDSHIELD_DEFROST, // Often implies AC on for dehumidification
    WINDSHIELD_AND_FEET
};

enum class ACCompressorStatus {
    OFF,
    ON_REQUESTED, // Requested by user or auto mode
    ON_ACTIVE,    // Actually running
    OFF_BY_POWER_MANAGEMENT, // Disabled due to low power or high engine load
    OFF_BY_ENGINE_NOT_RUNNING,
    FAULTY
};

class ClimateControl {
public:
    ClimateControl(ecu_power_management::PowerMonitor* pm);
    ~ClimateControl();

    // --- User Settings ---
    bool setTargetTemperature(double celsius); // For automatic mode
    bool setFanSpeed(int level); // 0 (off) to N (max)
    bool setAirDistribution(AirDistributionMode mode);
    bool setACActive(bool active); // User request for A/C on/off
    bool setRecirculationActive(bool active);
    bool setAutoMode(bool enabled); // Enable/disable fully automatic climate control

    // --- Status ---
    double getCurrentInteriorTemp() const; // From simulated sensor
    double getTargetTemperature() const;
    int getFanSpeed() const;
    AirDistributionMode getAirDistribution() const;
    bool isACActive() const; // Is compressor effectively running
    ACCompressorStatus getACCompressorStatus() const;
    bool isRecirculationActive() const;
    bool isAutoModeEnabled() const;

    // --- System Update ---
    // vehicle_state: to check if engine is running (for AC), speed (for recirculation logic)
    // external_temp_c: from an external temperature sensor
    void updateClimateState(const VehicleState& vehicle_state, const SensorData& interior_temp_sensor, const SensorData& exterior_temp_sensor);

private:
    ecu_power_management::PowerMonitor* power_monitor_;

    // Settings
    double target_temperature_celsius_;
    int fan_speed_level_; // 0 = off, 1-5 example levels
    AirDistributionMode current_air_distribution_;
    bool ac_requested_by_user_; // User's explicit AC on/off setting
    bool recirculation_active_;
    bool auto_mode_enabled_;

    // Internal State
    double current_interior_temperature_celsius_; // Simulated
    double current_exterior_temperature_celsius_; // Simulated
    ACCompressorStatus ac_compressor_status_;
    int ac_power_denial_counter_; // How many times PM denied AC

    // Internal helper methods
    void manageAutomaticOperation(); // Logic for auto mode
    void controlACCompressor(bool engine_running, bool high_engine_load_simulated);
    void adjustFanForTemperature();
    void adjustAirDistributionForMode();
    void simulateTemperatureChange(); // Simple model of interior temp changing
    bool canActivateAC(bool engine_running, bool high_engine_load_simulated) const;
};

} // namespace ecu_body_control_module

#endif // CLIMATE_CONTROL_H