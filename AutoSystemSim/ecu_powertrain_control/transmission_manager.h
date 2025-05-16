// AutoSystemSim/ecu_powertrain_control/transmission_manager.h
#ifndef TRANSMISSION_MANAGER_H
#define TRANSMISSION_MANAGER_H

#include "../common/logger.h"
#include "../common/datatypes.h" // For VehicleState (speed, etc.)

// Forward declaration for dependency on EngineManager (within the same ECU/Application)
namespace ecu_powertrain_control {
    class EngineManager;
}

namespace ecu_powertrain_control {

enum class TransmissionMode {
    PARK,
    REVERSE,
    NEUTRAL,
    DRIVE,
    SPORT,
    MANUAL
};

enum class GearShiftQuality {
    SMOOTH,
    ACCEPTABLE,
    ROUGH,
    FAILED_SHIFT
};

class TransmissionManager {
public:
    // Constructor might take EngineManager if tight coupling is needed for torque requests.
    // Or EngineManager could call into TransmissionManager. Let's start simple.
    TransmissionManager(EngineManager* engine_mgr);
    ~TransmissionManager();

    // --- Commands ---
    bool setTransmissionMode(TransmissionMode mode);
    bool shiftUp();   // For MANUAL or SPORT mode if applicable
    bool shiftDown(); // For MANUAL or SPORT mode if applicable
    bool requestNeutral(); // Safety feature or specific request

    // --- Status & Operations ---
    TransmissionMode getCurrentMode() const;
    int getCurrentGear() const; // 1, 2, 3, ... for DRIVE/MANUAL. 0 for N, -1 for R, etc.
    void updateState(const VehicleState& vehicle_state, int engine_rpm); // Periodic update
    bool isShiftInProgress() const;

private:
    EngineManager* engine_manager_; // Dependency on another component in the same ECU

    TransmissionMode current_mode_;
    TransmissionMode requested_mode_;
    int current_gear_;         // Actual physical gear engaged
    int target_gear_;          // Gear being shifted to
    int max_gears_;            // e.g., 6 for a 6-speed transmission
    bool shift_in_progress_;
    double transmission_oil_temp_celsius_;

    // Internal helper methods
    bool canShiftToMode(TransmissionMode new_mode, const VehicleState& vehicle_state) const;
    bool canShiftGear(bool up_shift, const VehicleState& vehicle_state) const;
    GearShiftQuality performGearShift(int to_gear);
    void manageAutomaticShifting(const VehicleState& vehicle_state, int engine_rpm);
    void checkTransmissionHealth(); // Simulates diagnostics
    void updateTransmissionTemperature(const VehicleState& vehicle_state);
};

} // namespace ecu_powertrain_control

#endif // TRANSMISSION_MANAGER_H