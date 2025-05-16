// AutoSystemSim/ecu_safety_systems/abs_control.h
#ifndef ABS_CONTROL_H
#define ABS_CONTROL_H

#include "../common/logger.h"
#include "../common/datatypes.h" // For VehicleState (speed), SensorData (wheel speeds)
#include <vector>

// Forward declaration for potential dependency (e.g., a BrakeActuator interface)
// namespace ecu_actuators {
// class BrakeActuatorInterface;
// }

namespace ecu_safety_systems {

enum class ABSState {
    INACTIVE,       // Normal braking, no ABS intervention
    MONITORING,     // Conditions are such that ABS might be needed (e.g., hard braking)
    INTERVENING,    // ABS is actively modulating brake pressure
    FAULT_DETECTED, // A fault in the ABS system has been detected
    INITIALIZING
};

struct WheelSensorData {
    int wheel_id; // 0:FL, 1:FR, 2:RL, 3:RR
    double speed_kmh;
    bool is_locking; // True if this wheel is detected to be locking or near locking
    double applied_brake_pressure_bar; // Current pressure applied by this channel
};

class ABSControl {
public:
    ABSControl(/* ecu_actuators::BrakeActuatorInterface* brake_actuator */);
    ~ABSControl();

    // Main processing function, called very frequently
    void processBraking(const VehicleState& vehicle_state,
                        const std::vector<SensorData>& wheel_speed_sensors, // Raw wheel speeds
                        double brake_pedal_pressure_input); // From brake pedal sensor

    ABSState getCurrentState() const;
    bool isABSInterventionActive() const;
    void runDiagnostics(); // Self-check routine

private:
    ABSState current_abs_state_;
    std::vector<WheelSensorData> wheel_data_; // Stores processed data for each wheel
    // ecu_actuators::BrakeActuatorInterface* brake_actuator_; // Interface to control brake pressure

    double vehicle_reference_speed_kmh_; // Estimated actual vehicle speed (e.g., from non-braking wheels or GPS)
    int cycles_since_last_intervention_;
    int fault_code_;

    // Internal helper methods
    void initialize();
    void updateVehicleReferenceSpeed(const std::vector<SensorData>& wheel_speed_sensors, double current_vehicle_speed);
    bool detectWheelLockup(const WheelSensorData& wheel, double deceleration_threshold);
    void modulateBrakePressure(WheelSensorData& wheel_to_modulate);
    void releasePressure(WheelSensorData& wheel);
    void holdPressure(WheelSensorData& wheel);
    void reapplyPressure(WheelSensorData& wheel); // Gradual reapplication
    void checkForSystemFaults(const std::vector<SensorData>& wheel_speed_sensors);
};

} // namespace ecu_safety_systems

#endif // ABS_CONTROL_H