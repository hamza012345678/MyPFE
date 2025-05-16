// AutoSystemSim/ecu_body_control_module/lighting_control.h
#ifndef LIGHTING_CONTROL_H
#define LIGHTING_CONTROL_H

#include "../common/logger.h"
#include "../common/datatypes.h" // For VehicleState (e.g., to check speed for auto headlights)

// Forward declaration for potential dependency
namespace ecu_power_management {
    class PowerMonitor;
}

namespace ecu_body_control_module {

enum class LightType {
    HEADLIGHT_LOW,
    HEADLIGHT_HIGH,
    PARKING_LIGHT,
    BRAKE_LIGHT,
    REVERSE_LIGHT,
    FOG_LIGHT_FRONT,
    FOG_LIGHT_REAR,
    INDICATOR_LEFT,
    INDICATOR_RIGHT,
    INTERIOR_DOME,
    HAZARD_LIGHTS
};

enum class LightStatus {
    OFF,
    ON,
    FAULTY_BULB,
    FAULTY_CIRCUIT
};

struct BulbState {
    LightType type;
    LightStatus status;
    int fault_code; // 0 if no fault

    BulbState(LightType t) : type(t), status(LightStatus::OFF), fault_code(0) {}
};


class LightingControl {
public:
    LightingControl(ecu_power_management::PowerMonitor* pm); // Dependency on PowerMonitor
    ~LightingControl();

    // --- Commands ---
    bool setLightState(LightType type, bool on);
    bool activateHazardLights(bool activate);
    bool activateIndicator(LightType indicator_type, bool activate); // Left or Right

    // --- Status & Diagnostics ---
    LightStatus getLightStatus(LightType type) const;
    void performBulbCheck(); // Simulates a periodic check for all bulbs
    void updateLighting(const VehicleState& current_vehicle_state); // Update based on vehicle conditions

private:
    std::vector<BulbState> all_lights_;
    ecu_power_management::PowerMonitor* power_monitor_; // Pointer to power monitor service

    bool is_hazard_active_;
    bool is_left_indicator_active_;
    bool is_right_indicator_active_;

    // Internal helper methods
    BulbState* findBulb(LightType type);
    const BulbState* findBulb(LightType type) const;

    void setSpecificLight(LightType type, bool on);
    void handleAutomaticHeadlights(const VehicleState& vehicle_state, bool power_stable);
    void checkBrakeLights(const VehicleState& vehicle_state); // Could be triggered by brake pedal sensor
};

} // namespace ecu_body_control_module

#endif // LIGHTING_CONTROL_H