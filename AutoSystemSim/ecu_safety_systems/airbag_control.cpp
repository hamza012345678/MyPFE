// AutoSystemSim/ecu_safety_systems/airbag_control.cpp
#include "airbag_control.h"
#include <map> // For mapping AirbagID to string easily for logging
#include <thread> // For std::this_thread::sleep_for
#include <random> // For simulating sensor faults

namespace ecu_safety_systems {

// Helper to convert AirbagSystemState enum to string for logging
const char* airbagSysStateToString(AirbagSystemState state) {
    switch (state) {
        case AirbagSystemState::SYSTEM_OFF: return "SYSTEM_OFF";
        case AirbagSystemState::SYSTEM_READY: return "SYSTEM_READY";
        case AirbagSystemState::CRASH_DETECTED: return "CRASH_DETECTED";
        case AirbagSystemState::DEPLOYMENT_TRIGGERED: return "DEPLOYMENT_TRIGGERED";
        case AirbagSystemState::POST_CRASH_SAFE: return "POST_CRASH_SAFE";
        case AirbagSystemState::FAULT_SYSTEM_INOPERATIVE: return "FAULT_SYSTEM_INOPERATIVE";
        case AirbagSystemState::FAULT_SENSOR_ISSUE: return "FAULT_SENSOR_ISSUE";
        case AirbagSystemState::FAULT_DEPLOYMENT_CIRCUIT: return "FAULT_DEPLOYMENT_CIRCUIT";
        default: return "UNKNOWN_AIRBAG_SYSTEM_STATE";
    }
}

// Helper to convert AirbagID enum to string for logging
std::string airbagIdToString(AirbagID id) {
    static const std::map<AirbagID, std::string> airbag_names = {
        {AirbagID::DRIVER_FRONT, "DRIVER_FRONT"},
        {AirbagID::PASSENGER_FRONT, "PASSENGER_FRONT"},
        {AirbagID::DRIVER_SIDE_THORAX, "DRIVER_SIDE_THORAX"},
        {AirbagID::PASSENGER_SIDE_THORAX, "PASSENGER_SIDE_THORAX"},
        {AirbagID::DRIVER_SIDE_CURTAIN, "DRIVER_SIDE_CURTAIN"},
        {AirbagID::PASSENGER_SIDE_CURTAIN, "PASSENGER_SIDE_CURTAIN"},
        {AirbagID::DRIVER_KNEE, "DRIVER_KNEE"},
        {AirbagID::PASSENGER_KNEE, "PASSENGER_KNEE"}
    };
    auto it = airbag_names.find(id);
    return (it != airbag_names.end()) ? it->second : "UNKNOWN_AIRBAG_ID";
}


AirbagControl::AirbagControl() :
    system_state_(AirbagSystemState::SYSTEM_OFF),
    crash_event_id_counter_(0),
    fault_code_(0)
{
    LOG_INFO("AirbagControl: Initializing Airbag Control Unit (ACU)...");
    // Initialize airbag_deployed_status_ vector size based on AirbagID enum count
    // Assuming AirbagID enums are contiguous from 0.
    // This is a bit fragile; a better way might be to use a map or fixed-size array with a known max.
    airbag_deployed_status_.resize(static_cast<int>(AirbagID::PASSENGER_KNEE) + 1, false);
    initializeSystem(); // Call private init
}

AirbagControl::~AirbagControl() {
    LOG_INFO("AirbagControl: Shutting down ACU. Final system state: %s.", airbagSysStateToString(system_state_));
}

void AirbagControl::initializeSystem() {
    LOG_INFO("AirbagControl: Performing ACU power-on self-test (POST)...");
    system_state_ = AirbagSystemState::SYSTEM_OFF; // Or a specific POST state

    // Simulate POST checks
    std::this_thread::sleep_for(std::chrono::milliseconds(100)); // Simulate POST duration

    runSystemCheck(); // Perform diagnostic checks

    if (system_state_ == AirbagSystemState::FAULT_SYSTEM_INOPERATIVE ||
        system_state_ == AirbagSystemState::FAULT_SENSOR_ISSUE ||
        system_state_ == AirbagSystemState::FAULT_DEPLOYMENT_CIRCUIT) {
        LOG_FATAL("AirbagControl: ACU POST FAILED. Fault Code: %d. Airbag system is INOPERATIVE.", fault_code_);
    } else {
        system_state_ = AirbagSystemState::SYSTEM_READY;
        LOG_INFO("AirbagControl: ACU POST successful. System is READY and ARMED.");
    }
}

void AirbagControl::detectSystemFaults(const CrashSensorInput& impact_data) {
    // This would run continuously or periodically to check sensor health, squib circuits, etc.
    // Simplified: occasional random fault simulation for sensors or circuits
    std::random_device rd;
    std::mt19937 gen(rd());

    if (system_state_ == AirbagSystemState::FAULT_SYSTEM_INOPERATIVE ||
        system_state_ == AirbagSystemState::FAULT_SENSOR_ISSUE ||
        system_state_ == AirbagSystemState::FAULT_DEPLOYMENT_CIRCUIT) {
        // Already in a fault state, might periodically re-log or check if recoverable
        if (std::uniform_int_distribution<>(1, 100)(gen) == 1) { // Low chance to re-log
            LOG_WARNING("AirbagControl: System remains in FAULT state. Code: %d. State: %s",
                        fault_code_, airbagSysStateToString(system_state_));
        }
        return;
    }

    // Simulate a sensor fault (e.g., accelerometer out of range or no signal)
    if (std::uniform_int_distribution<>(1, 500)(gen) == 1) { // 1 in 500 chance per cycle
        fault_code_ = 100 + (rand() % 10); // Example sensor fault code
        system_state_ = AirbagSystemState::FAULT_SENSOR_ISSUE;
        LOG_ERROR("AirbagControl: FAULT DETECTED - Sensor issue (e.g., accelerometer G-sensor %d failure). Fault Code: %d. System degraded.",
                  (rand()%3 + 1), fault_code_);
        // Depending on fault, system might still be partially operative or fully inoperative.
        return;
    }

    // Simulate a deployment circuit fault (e.g., open circuit to a squib)
    if (std::uniform_int_distribution<>(1, 1000)(gen) == 1) { // 1 in 1000 chance per cycle
        AirbagID faulty_airbag = static_cast<AirbagID>(rand() % airbag_deployed_status_.size());
        fault_code_ = 200 + static_cast<int>(faulty_airbag);
        system_state_ = AirbagSystemState::FAULT_DEPLOYMENT_CIRCUIT;
        LOG_ERROR("AirbagControl: FAULT DETECTED - Deployment circuit issue for airbag %s. Fault Code: %d. Specific airbag may not deploy.",
                  airbagIdToString(faulty_airbag).c_str(), fault_code_);
        return;
    }
    LOG_VERBOSE("AirbagControl: Continuous fault monitoring: No new faults detected.");
}


bool AirbagControl::evaluateCrashSeverity(const CrashSensorInput& impact_data, const VehicleState& vehicle_state) {
    // This is the core crash detection algorithm. In reality, extremely complex involving:
    // - Multiple sensor inputs (accelerometers, gyros, pressure sensors in doors).
    // - Filtering and signal processing.
    // - Sophisticated algorithms to distinguish crash from non-crash events (potholes, door slams).
    // - Calibration tables for different vehicle types and crash scenarios.

    // Simplified criteria for demonstration:
    bool deploy_criteria_met = false;
    std::string crash_type_desc = "NONE";

    // Frontal impact
    if (impact_data.longitudinal_g < -20.0 && vehicle_state.speed_kmh > 20.0) { // Significant negative G (deceleration) at speed
        deploy_criteria_met = true;
        crash_type_desc = "SEVERE FRONTAL IMPACT";
        LOG_WARNING("AirbagControl: CRITERIA MET - Potential severe frontal impact. G-long: %.1f, Speed: %.1f km/h",
                    impact_data.longitudinal_g, vehicle_state.speed_kmh);
    }
    // Side impact
    else if (fabs(impact_data.lateral_g) > 15.0 && vehicle_state.speed_kmh > 15.0) {
        deploy_criteria_met = true;
        crash_type_desc = (impact_data.lateral_g > 0) ? "SEVERE LEFT SIDE IMPACT" : "SEVERE RIGHT SIDE IMPACT";
        LOG_WARNING("AirbagControl: CRITERIA MET - Potential severe side impact. G-lat: %.1f, Speed: %.1f km/h",
                    impact_data.lateral_g, vehicle_state.speed_kmh);
    }
    // Rollover (very simplified)
    else if (fabs(impact_data.roll_rate_deg_s) > 100.0 && fabs(impact_data.vertical_g) > 2.0) { // High roll rate and vertical G
        deploy_criteria_met = true;
        crash_type_desc = "POTENTIAL ROLLOVER EVENT";
        LOG_WARNING("AirbagControl: CRITERIA MET - Potential rollover. RollRate: %.1f deg/s, G-vert: %.1f",
                    impact_data.roll_rate_deg_s, impact_data.vertical_g);
    }

    if (deploy_criteria_met) {
        crash_event_id_counter_++;
        LOG_FATAL("AirbagControl: Event ID %d: CRASH EVENT DETECTED! Type: %s. Preparing for airbag deployment.",
                  crash_event_id_counter_, crash_type_desc.c_str());
        system_state_ = AirbagSystemState::CRASH_DETECTED;
        return true;
    }
    LOG_VERBOSE("AirbagControl: Impact data evaluated. G-long: %.1f, G-lat: %.1f. No crash criteria met for deployment.",
               impact_data.longitudinal_g, impact_data.lateral_g);
    return false;
}

void AirbagControl::fireAirbag(AirbagID airbag_to_fire, int event_id) {
    int airbag_idx = static_cast<int>(airbag_to_fire);
    if (airbag_idx < 0 || airbag_idx >= static_cast<int>(airbag_deployed_status_.size())) {
        LOG_ERROR("AirbagControl: Event ID %d: Invalid AirbagID %d specified for firing.", event_id, airbag_idx);
        return;
    }

    if (system_state_ == AirbagSystemState::FAULT_DEPLOYMENT_CIRCUIT && fault_code_ == 200 + airbag_idx) {
         LOG_ERROR("AirbagControl: Event ID %d: CANNOT DEPLOY AIRBAG %s. Fault detected in its deployment circuit (Code: %d).",
                   event_id, airbagIdToString(airbag_to_fire).c_str(), fault_code_);
        return;
    }


    if (!airbag_deployed_status_[airbag_idx]) {
        // Simulate firing the squib
        std::this_thread::sleep_for(std::chrono::milliseconds(5)); // Squib fire + airbag inflation start is very fast
        airbag_deployed_status_[airbag_idx] = true;
        deployed_airbags_list_.push_back(airbag_to_fire);
        LOG_FATAL("AirbagControl: Event ID %d: FIRING AIRBAG %s!",
                  event_id, airbagIdToString(airbag_to_fire).c_str());
        // In a real system, this would send a high current pulse to the squib.
        // It would also log to non-volatile memory ("black box").
    } else {
        LOG_WARNING("AirbagControl: Event ID %d: Airbag %s already deployed or commanded.",
                    event_id, airbagIdToString(airbag_to_fire).c_str());
    }
}

void AirbagControl::triggerDeploymentSequence(const CrashSensorInput& impact_data) {
    LOG_INFO("AirbagControl: Event ID %d: Initiating airbag deployment sequence based on impact data.", crash_event_id_counter_);
    system_state_ = AirbagSystemState::DEPLOYMENT_TRIGGERED;

    // Determine which airbags to deploy based on impact type, severity, and occupant status.
    // This is highly simplified.

    // Frontal airbags
    if (impact_data.longitudinal_g < -15.0) { // Threshold for frontal deployment
        fireAirbag(AirbagID::DRIVER_FRONT, crash_event_id_counter_);
        if (impact_data.passenger_seat_occupied && impact_data.seatbelt_fastened_passenger) { // Only if passenger present and belted (simplified)
            fireAirbag(AirbagID::PASSENGER_FRONT, crash_event_id_counter_);
        } else if (impact_data.passenger_seat_occupied) {
             LOG_WARNING("AirbagControl: Event ID %d: Passenger front airbag NOT deployed (passenger unbelted - simplified rule).", crash_event_id_counter_);
        } else {
             LOG_INFO("AirbagControl: Event ID %d: Passenger front airbag NOT deployed (passenger seat unoccupied).", crash_event_id_counter_);
        }
        // Knee airbags for severe frontal
        if (impact_data.longitudinal_g < -25.0) {
            fireAirbag(AirbagID::DRIVER_KNEE, crash_event_id_counter_);
            if (impact_data.passenger_seat_occupied) {
                fireAirbag(AirbagID::PASSENGER_KNEE, crash_event_id_counter_);
            }
        }
    }

    // Side airbags (curtain and thorax)
    if (fabs(impact_data.lateral_g) > 10.0) {
        if (impact_data.lateral_g > 10.0) { // Left side impact
            fireAirbag(AirbagID::DRIVER_SIDE_THORAX, crash_event_id_counter_);
            fireAirbag(AirbagID::DRIVER_SIDE_CURTAIN, crash_event_id_counter_);
        } else if (impact_data.lateral_g < -10.0) { // Right side impact
            fireAirbag(AirbagID::PASSENGER_SIDE_THORAX, crash_event_id_counter_);
            fireAirbag(AirbagID::PASSENGER_SIDE_CURTAIN, crash_event_id_counter_);
        }
    }

    // Rollover - typically curtain airbags, maybe others
    if (fabs(impact_data.roll_rate_deg_s) > 90.0) {
        LOG_INFO("AirbagControl: Event ID %d: Rollover detected, deploying curtain airbags.", crash_event_id_counter_);
        fireAirbag(AirbagID::DRIVER_SIDE_CURTAIN, crash_event_id_counter_);
        fireAirbag(AirbagID::PASSENGER_SIDE_CURTAIN, crash_event_id_counter_);
    }


    LOG_INFO("AirbagControl: Event ID %d: Airbag deployment sequence commands issued.", crash_event_id_counter_);
    enterPostCrashSafeMode(crash_event_id_counter_); // Internal call
}

void AirbagControl::enterPostCrashSafeMode(int event_id) {
    LOG_WARNING("AirbagControl: Event ID %d: Entering POST-CRASH SAFE MODE.", event_id);
    system_state_ = AirbagSystemState::POST_CRASH_SAFE;

    // In a real system, the ACU would send CAN messages to:
    // - Unlock doors
    // - Activate hazard lights
    // - Cut fuel pump (if not already done by engine ECU)
    // - Notify telematics system (eCall)
    LOG_INFO("AirbagControl: Event ID %d: Simulating post-crash actions: Doors unlocked, Hazards ON, Fuel pump OFF, eCall initiated.", event_id);
    // These are just logs; no actual CAN messages are sent in this simulation.

    // Log deployed airbags to "black box" (simulated by console log here)
    std::string deployed_summary = "Deployed Airbags for Event " + std::to_string(event_id) + ": ";
    if (deployed_airbags_list_.empty()) {
        deployed_summary += "NONE (Possible deployment failure or very specific crash type)";
    } else {
        for (size_t i = 0; i < deployed_airbags_list_.size(); ++i) {
            deployed_summary += airbagIdToString(deployed_airbags_list_[i]);
            if (i < deployed_airbags_list_.size() - 1) deployed_summary += ", ";
        }
    }
    LOG_FATAL("AirbagControl: %s", deployed_summary.c_str()); // Using FATAL to highlight this important summary
}


void AirbagControl::processImpactData(const CrashSensorInput& impact_data, const VehicleState& vehicle_state) {
    LOG_DEBUG("AirbagControl: Processing impact data. G-long: %.2f, G-lat: %.2f, G-vert: %.2f, Roll: %.2f, Speed: %.1f",
              impact_data.longitudinal_g, impact_data.lateral_g, impact_data.vertical_g,
              impact_data.roll_rate_deg_s, vehicle_state.speed_kmh);

    // Check for system faults first
    detectSystemFaults(impact_data); // Internal call

    // ACU only active if system is READY or already in a crash/post-crash sequence.
    // Or if it's in a fault state that doesn't prevent basic monitoring.
    if (system_state_ == AirbagSystemState::SYSTEM_OFF ||
        system_state_ == AirbagSystemState::FAULT_SYSTEM_INOPERATIVE) {
        LOG_INFO("AirbagControl: System is OFF or Inoperative. Impact data processing skipped.");
        return;
    }
     if (vehicle_state.speed_kmh < 5.0 && system_state_ == AirbagSystemState::SYSTEM_READY) { // Very low speed, system might be armed but less sensitive
        LOG_VERBOSE("AirbagControl: Vehicle speed %.1f km/h is very low. Crash sensitivity might be reduced.", vehicle_state.speed_kmh);
        // Don't evaluate for crash at very low speeds unless it's a specific scenario (e.g., pole impact algorithm)
        return;
     }


    // If not already in a crash sequence, evaluate for new crash
    if (system_state_ == AirbagSystemState::SYSTEM_READY || system_state_ == AirbagSystemState::FAULT_SENSOR_ISSUE || system_state_ == AirbagSystemState::FAULT_DEPLOYMENT_CIRCUIT) {
        // Even with some faults, might still try to detect crashes, but deployment could be affected.
        if (evaluateCrashSeverity(impact_data, vehicle_state)) { // Internal call, changes state to CRASH_DETECTED
            triggerDeploymentSequence(impact_data); // Internal call
        }
    } else if (system_state_ == AirbagSystemState::CRASH_DETECTED || system_state_ == AirbagSystemState::DEPLOYMENT_TRIGGERED) {
        // Already in a crash event, might be processing multi-stage deployments or waiting for sequence to complete.
        // For simplicity, our triggerDeploymentSequence is fairly immediate.
        LOG_INFO("AirbagControl: Currently in crash/deployment state (%s). Monitoring for stability or secondary events (not fully simulated).",
                 airbagSysStateToString(system_state_));
    } else if (system_state_ == AirbagSystemState::POST_CRASH_SAFE) {
        LOG_INFO("AirbagControl: System in POST_CRASH_SAFE mode. No further impact processing for this event.");
        // Would require a system reset (e.g., ignition cycle, dealer tool) to become READY again.
    }
    LOG_DEBUG("AirbagControl: Impact data processing cycle complete. System state: %s", airbagSysStateToString(system_state_));
}

AirbagSystemState AirbagControl::getSystemState() const {
    LOG_DEBUG("AirbagControl: getSystemState() -> %s", airbagSysStateToString(system_state_));
    return system_state_;
}

std::vector<AirbagID> AirbagControl::getDeployedAirbags() const {
    LOG_DEBUG("AirbagControl: getDeployedAirbags() called. Count: %zu", deployed_airbags_list_.size());
    return deployed_airbags_list_;
}

void AirbagControl::runSystemCheck() {
    LOG_INFO("AirbagControl: Performing ACU ad-hoc system check...");
    // This could be triggered by a diagnostic tool or specific vehicle conditions.
    // Re-run some parts of initialization checks.
    fault_code_ = 0; // Reset for this check
    AirbagSystemState previous_state_if_not_fault = (system_state_ == AirbagSystemState::SYSTEM_READY || system_state_ == AirbagSystemState::SYSTEM_OFF) ? system_state_ : AirbagSystemState::SYSTEM_READY;
    std::random_device rd;
    std::mt19937 gen(rd());

    // Check critical sensors again (simulated)
    if (std::uniform_int_distribution<>(1, 50)(gen) == 1) { // Higher chance to find fault in ad-hoc check
        fault_code_ = 150 + (rand() % 10);
        system_state_ = AirbagSystemState::FAULT_SENSOR_ISSUE;
        LOG_ERROR("AirbagControl: AD-HOC CHECK FAULT: Main G-Sensor unresponsive. Code: %d", fault_code_);
    } else {
        LOG_INFO("AirbagControl: AD-HOC CHECK: G-Sensors OK.");
    }

    // Check squib circuits continuity (simulated)
    if (std::uniform_int_distribution<>(1, 20)(gen) == 1 && system_state_ != AirbagSystemState::FAULT_SENSOR_ISSUE) { // Higher chance
        AirbagID faulty_airbag = static_cast<AirbagID>(rand() % airbag_deployed_status_.size());
        fault_code_ = 250 + static_cast<int>(faulty_airbag);
        system_state_ = AirbagSystemState::FAULT_DEPLOYMENT_CIRCUIT;
        LOG_ERROR("AirbagControl: AD-HOC CHECK FAULT: Open circuit detected for airbag %s. Code: %d",
                  airbagIdToString(faulty_airbag).c_str(), fault_code_);
    } else if (system_state_ != AirbagSystemState::FAULT_SENSOR_ISSUE) {
        LOG_INFO("AirbagControl: AD-HOC CHECK: Squib circuits OK.");
    }

    if (system_state_ == AirbagSystemState::FAULT_SENSOR_ISSUE || system_state_ == AirbagSystemState::FAULT_DEPLOYMENT_CIRCUIT) {
        LOG_WARNING("AirbagControl: Ad-hoc system check complete. NEW FAULT(s) DETECTED. System state: %s, Code: %d",
                    airbagSysStateToString(system_state_), fault_code_);
    } else {
        system_state_ = previous_state_if_not_fault; // Revert to previous good state if no new fault
        LOG_INFO("AirbagControl: Ad-hoc system check complete. No new faults. System state: %s", airbagSysStateToString(system_state_));
    }
}


} // namespace ecu_safety_systems