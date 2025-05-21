// AutoSystemSim/ecu_safety_systems/airbag_control.h
#ifndef AIRBAG_CONTROL_H
#define AIRBAG_CONTROL_H

#include "../common/logger.h"
#include "../common/datatypes.h" // For SensorData (accelerometers, seat occupancy)
#include <vector>
#include <string>

namespace ecu_safety_systems {

enum class AirbagSystemState {
    SYSTEM_OFF,         // E.g., vehicle off, or very low speed initial state
    SYSTEM_READY,       // Initialized, armed, and monitoring for crash
    CRASH_DETECTED,     // A crash event meeting deployment criteria has been identified
    DEPLOYMENT_TRIGGERED, // Squibs fired for relevant airbags
    POST_CRASH_SAFE,    // System has completed deployment and entered a safe state (e.g., unlock doors, hazard lights - via CAN)
    FAULT_SYSTEM_INOPERATIVE,
    FAULT_SENSOR_ISSUE,
    FAULT_DEPLOYMENT_CIRCUIT
};

enum class AirbagID {
    DRIVER_FRONT,
    PASSENGER_FRONT,
    DRIVER_SIDE_THORAX,
    PASSENGER_SIDE_THORAX,
    DRIVER_SIDE_CURTAIN,
    PASSENGER_SIDE_CURTAIN,
    DRIVER_KNEE,
    PASSENGER_KNEE
    // ... and others like rear side, center etc.
};

struct CrashSensorInput {
    double longitudinal_g; // Forward/backward acceleration/deceleration
    double lateral_g;      // Sideways acceleration
    double vertical_g;     // Up/down acceleration (e.g., rollover)
    bool seatbelt_fastened_driver;
    bool seatbelt_fastened_passenger;
    // Occupancy sensors (e.g., weight sensor for passenger seat)
    bool passenger_seat_occupied;
    // Rollover sensor data (if separate)
    double roll_rate_deg_s;
    double pitch_rate_deg_s;
};


class AirbagControl {
public:
    AirbagControl();
    ~AirbagControl();

    // Main processing function, called very frequently with sensor data
    void processImpactData(const CrashSensorInput& impact_data, const VehicleState& vehicle_state);

    AirbagSystemState getSystemState() const;
    std::vector<AirbagID> getDeployedAirbags() const;
    void runSystemCheck(); // Performs self-diagnostics

private:
    AirbagSystemState system_state_;
    std::vector<bool> airbag_deployed_status_; // Indexed by AirbagID enum cast to int
    std::vector<AirbagID> deployed_airbags_list_; // List of actually deployed airbags

    int crash_event_id_counter_; // To uniquely identify crash events
    int fault_code_;

    // Internal helper methods
    void initializeSystem();
    bool evaluateCrashSeverity(const CrashSensorInput& impact_data, const VehicleState& vehicle_state);
    void triggerDeploymentSequence(const CrashSensorInput& impact_data);
    void fireAirbag(AirbagID airbag_to_fire, int event_id);
    void enterPostCrashSafeMode(int event_id);
    void detectSystemFaults(const CrashSensorInput& impact_data); // Continuous monitoring
};
 const char* airbagSysStateToString(AirbagSystemState state);

} // namespace ecu_safety_systems

#endif // AIRBAG_CONTROL_H