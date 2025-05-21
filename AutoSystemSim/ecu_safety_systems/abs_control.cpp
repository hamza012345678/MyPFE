// AutoSystemSim/ecu_safety_systems/abs_control.cpp
#include "abs_control.h"
#include <numeric>   // For std::accumulate
#include <algorithm> // For std::min/max, std::sort
#include <cmath>     // For fabs
#include <thread>

namespace ecu_safety_systems {

// Helper to convert ABSState enum to string for logging
const char* absStateToString(ABSState state) {
    switch (state) {
        case ABSState::INACTIVE: return "INACTIVE";
        case ABSState::MONITORING: return "MONITORING";
        case ABSState::INTERVENING: return "INTERVENING";
        case ABSState::FAULT_DETECTED: return "FAULT_DETECTED";
        case ABSState::INITIALIZING: return "INITIALIZING";
        default: return "UNKNOWN_ABS_STATE";
    }
}


ABSControl::ABSControl(/* ecu_actuators::BrakeActuatorInterface* brake_actuator */) :
    current_abs_state_(ABSState::INITIALIZING),
    // brake_actuator_(brake_actuator),
    vehicle_reference_speed_kmh_(0.0),
    cycles_since_last_intervention_(0),
    fault_code_(0)
{
    LOG_INFO("ABSControl: Initializing...");
    // if (!brake_actuator_) {
    //     LOG_FATAL("ABSControl: BrakeActuatorInterface is NULL! ABS cannot function.");
    //     current_abs_state_ = ABSState::FAULT_DETECTED;
    //     fault_code_ = 1; // Critical dependency missing
    // }
    initialize(); // Call private init
}

ABSControl::~ABSControl() {
    LOG_INFO("ABSControl: Shutting down. Final state: %s.", absStateToString(current_abs_state_));
}

void ABSControl::initialize() {
    LOG_INFO("ABSControl: Performing system initialization and self-checks.");
    current_abs_state_ = ABSState::INITIALIZING;
    wheel_data_.clear();
    for (int i = 0; i < 4; ++i) { // Assuming 4 wheels
        wheel_data_.push_back({i, 0.0, false, 0.0});
        LOG_DEBUG("ABSControl: Initialized data for wheel %d.", i);
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(50)); // Simulate init time

    runDiagnostics(); // Run initial diagnostics

    if (current_abs_state_ != ABSState::FAULT_DETECTED) {
        current_abs_state_ = ABSState::INACTIVE;
        LOG_INFO("ABSControl: Initialization complete. System INACTIVE.");
    } else {
        LOG_ERROR("ABSControl: Initialization failed due to fault %d found during diagnostics. System in FAULT_DETECTED state.", fault_code_);
    }
}


void ABSControl::updateVehicleReferenceSpeed(const std::vector<SensorData>& wheel_speed_sensors, double current_vehicle_speed_from_state) {
    // Estimate vehicle speed. Can be complex: average of non-slipping wheels, GPS, IMU.
    // Simplified: Use fastest wheel speed if plausible, or vehicle_state.speed_kmh.
    // Or average of wheel speeds if they are close.

    double sum_speeds = 0;
    int valid_sensors = 0;
    double max_wheel_speed = 0;

    for (size_t i = 0; i < wheel_speed_sensors.size() && i < wheel_data_.size(); ++i) {
        // Update individual wheel speeds in our internal structure
        wheel_data_[i].speed_kmh = wheel_speed_sensors[i].value; // Assuming SensorData.value is speed
        if (wheel_speed_sensors[i].value >= 0) { // Basic validity
            sum_speeds += wheel_speed_sensors[i].value;
            valid_sensors++;
            if (wheel_speed_sensors[i].value > max_wheel_speed) {
                max_wheel_speed = wheel_speed_sensors[i].value;
            }
        } else {
            LOG_WARNING("ABSControl: Invalid speed reading (%.2f km/h) for wheel %d.", wheel_speed_sensors[i].value, static_cast<int>(i));
            // Could mark this sensor as faulty for this cycle
        }
    }

    if (valid_sensors > 0) {
        double avg_wheel_speed = sum_speeds / valid_sensors;
        // If current_vehicle_speed_from_state (e.g. from CAN via Engine ECU) is available and plausible:
        if (current_vehicle_speed_from_state > 0 && fabs(current_vehicle_speed_from_state - avg_wheel_speed) < 20.0) { // 20km/h tolerance
             vehicle_reference_speed_kmh_ = (current_vehicle_speed_from_state + avg_wheel_speed) / 2.0; // Blend them
        } else {
             vehicle_reference_speed_kmh_ = avg_wheel_speed; // Fallback to average wheel speed
        }
        // Further sanity: reference speed shouldn't be much lower than max wheel speed unless all wheels are slipping.
        if (max_wheel_speed > vehicle_reference_speed_kmh_ && vehicle_reference_speed_kmh_ > 5.0) {
             vehicle_reference_speed_kmh_ = (vehicle_reference_speed_kmh_ + max_wheel_speed) / 2.0; // Adjust towards max wheel speed
        }

    } else {
        LOG_WARNING("ABSControl: No valid wheel speed sensors to calculate reference speed. Using last known or zero.");
        // vehicle_reference_speed_kmh_ remains unchanged or reset to 0 if critical
        vehicle_reference_speed_kmh_ = 0; // Or use last known good if safer.
    }

    // Clamp to a max realistic speed if needed (e.g. 300 km/h)
    vehicle_reference_speed_kmh_ = std::min(vehicle_reference_speed_kmh_, 300.0);
    LOG_VERBOSE("ABSControl: Updated vehicle reference speed to %.2f km/h.", vehicle_reference_speed_kmh_);
}


bool ABSControl::detectWheelLockup(const WheelSensorData& wheel, double deceleration_threshold_g) {
    if (vehicle_reference_speed_kmh_ < 5.0) { // ABS typically inactive at very low speeds
        return false;
    }

    // Calculate slip ratio: (VehicleSpeed - WheelSpeed) / VehicleSpeed
    double slip_ratio = 0.0;
    if (vehicle_reference_speed_kmh_ > 1.0) { // Avoid division by zero or near-zero
        slip_ratio = (vehicle_reference_speed_kmh_ - wheel.speed_kmh) / vehicle_reference_speed_kmh_;
    }

    // Simplified lockup detection:
    // 1. High slip ratio (e.g., > 0.2 or 20% slip)
    // 2. Wheel speed significantly lower than reference speed
    // 3. High rate of wheel deceleration (more complex to calculate without previous state history here)

    bool is_locking = false;
    if (slip_ratio > 0.20 && wheel.speed_kmh < vehicle_reference_speed_kmh_ * 0.85) {
        is_locking = true;
        LOG_DEBUG("ABSControl: Wheel %d potential lockup. Speed: %.1f, RefSpeed: %.1f, Slip: %.2f",
                  wheel.wheel_id, wheel.speed_kmh, vehicle_reference_speed_kmh_, slip_ratio);
    }

    // Add deceleration check (conceptual - needs previous speed)
    // double wheel_deceleration = (previous_wheel_speed[wheel.wheel_id] - wheel.speed_kmh) / time_delta_s;
    // if (wheel_deceleration > deceleration_threshold_g * 9.81) {
    //     is_locking = true;
    //     LOG_DEBUG("ABSControl: Wheel %d high deceleration detected, potential lockup.", wheel.wheel_id);
    // }

    if (is_locking) {
        LOG_WARNING("ABSControl: LOCKUP DETECTED for wheel %d! Speed: %.1f km/h, Ref: %.1f km/h, Slip: %.2f.",
                    wheel.wheel_id, wheel.speed_kmh, vehicle_reference_speed_kmh_, slip_ratio);
    }
    return is_locking;
}

void ABSControl::releasePressure(WheelSensorData& wheel) {
    LOG_INFO("ABSControl: INTERVENTION - Releasing brake pressure for wheel %d.", wheel.wheel_id);
    // Command brake_actuator_ to release pressure for this wheel's channel
    // wheel.applied_brake_pressure_bar *= 0.1; // Drastic reduction for simulation
    wheel.applied_brake_pressure_bar = std::max(0.0, wheel.applied_brake_pressure_bar - 50.0); // Reduce by 50 bar
    if (wheel.applied_brake_pressure_bar < 0) wheel.applied_brake_pressure_bar = 0;
    LOG_DEBUG("ABSControl: Wheel %d pressure reduced to %.1f bar.", wheel.wheel_id, wheel.applied_brake_pressure_bar);
}

void ABSControl::holdPressure(WheelSensorData& wheel) {
    LOG_INFO("ABSControl: INTERVENTION - Holding brake pressure for wheel %d at %.1f bar.",
             wheel.wheel_id, wheel.applied_brake_pressure_bar);
    // Command brake_actuator_ to hold current pressure
}

void ABSControl::reapplyPressure(WheelSensorData& wheel) {
    LOG_INFO("ABSControl: INTERVENTION - Reapplying brake pressure for wheel %d.", wheel.wheel_id);
    // Command brake_actuator_ to gradually reapply pressure
    // wheel.applied_brake_pressure_bar *= 1.5; // Gradual increase
    wheel.applied_brake_pressure_bar += 20.0; // Increase by 20 bar
    // Max pressure should be limited by pedal input or system max
    // For now, we don't have original pedal_pressure mapped per wheel, so it might exceed.
    LOG_DEBUG("ABSControl: Wheel %d pressure increased to %.1f bar.", wheel.wheel_id, wheel.applied_brake_pressure_bar);
}


void ABSControl::modulateBrakePressure(WheelSensorData& wheel_to_modulate) {
    // This is the core ABS logic loop for an individual wheel that is locking
    LOG_DEBUG("ABSControl: Modulating pressure for wheel %d. Current speed: %.1f, Lock: %s, Pressure: %.1f",
              wheel_to_modulate.wheel_id, wheel_to_modulate.speed_kmh,
              wheel_to_modulate.is_locking ? "YES" : "NO",
              wheel_to_modulate.applied_brake_pressure_bar);

    if (wheel_to_modulate.is_locking) {
        // If wheel is locking, release pressure
        releasePressure(wheel_to_modulate); // Internal call
        // Next cycle, it might be reapply or hold depending on wheel speed recovery
    } else {
        // Wheel is not locking (or has recovered), try to reapply pressure to maintain braking
        // This needs to be intelligent: if wheel speed is recovering fast, hold. If slow, reapply gently.
        // Simplified:
        if (wheel_to_modulate.speed_kmh < vehicle_reference_speed_kmh_ * 0.95) {
            // Still significantly slower than vehicle, hold or reapply very gently
            holdPressure(wheel_to_modulate); // Internal call
        } else {
            // Wheel speed has recovered well, can reapply more aggressively
            reapplyPressure(wheel_to_modulate); // Internal call
        }
    }
    // Ensure pressure doesn't go negative or excessively high (actuator limits)
    wheel_to_modulate.applied_brake_pressure_bar = std::max(0.0, std::min(wheel_to_modulate.applied_brake_pressure_bar, 200.0)); // e.g. max 200 bar
}


void ABSControl::checkForSystemFaults(const std::vector<SensorData>& wheel_speed_sensors) {
    // Example fault checks:
    // 1. Sensor rationality: Are wheel speeds vastly different when not braking hard?
    // 2. Sensor failure: No signal or erratic signal from a wheel speed sensor.
    // 3. Actuator feedback (if available): Pump motor issues, valve stuck.

    int valid_sensor_count = 0;
    for (size_t i = 0; i < wheel_speed_sensors.size(); ++i) {
        if (wheel_speed_sensors[i].id != static_cast<int>(i)) { // Assuming SensorData.id matches wheel index
            LOG_ERROR("ABSControl: FAULT - Wheel speed sensor ID mismatch or data error for sensor %d. Expected ID %d.",
                      wheel_speed_sensors[i].id, static_cast<int>(i));
            current_abs_state_ = ABSState::FAULT_DETECTED;
            fault_code_ = 10 + static_cast<int>(i); // Example fault code
            return;
        }
        if (wheel_speed_sensors[i].value < -10.0 || wheel_speed_sensors[i].value > 350.0) { // Gross range check
             LOG_ERROR("ABSControl: FAULT - Irrational speed value (%.1f km/h) from wheel sensor %d.",
                       wheel_speed_sensors[i].value, static_cast<int>(i));
             current_abs_state_ = ABSState::FAULT_DETECTED;
             fault_code_ = 20 + static_cast<int>(i);
             return;
        }
        if (wheel_speed_sensors[i].value >= -1.0) { // Consider 0 or slightly negative as potentially valid if moving backward slowly.
            valid_sensor_count++;
        }
    }

    if (valid_sensor_count < wheel_speed_sensors.size() && vehicle_reference_speed_kmh_ > 10.0) {
         LOG_WARNING("ABSControl: One or more wheel speed sensors may be providing invalid data or no data. Valid: %d/%zu",
            valid_sensor_count, wheel_speed_sensors.size());
        // Potentially degrade to a simpler braking mode or set a less critical fault if some sensors still work.
        // For now, any persistent issue with all sensors is critical.
        if (valid_sensor_count == 0 && wheel_speed_sensors.size() > 0) {
             LOG_FATAL("ABSControl: FAULT - All wheel speed sensors are providing invalid data or no data! ABS disabled.");
             current_abs_state_ = ABSState::FAULT_DETECTED;
             fault_code_ = 30;
             return;
        }
    }

    // Conceptual: check actuator feedback
    // if (brake_actuator_ && brake_actuator_->getPumpMotorStatus() == ActuatorStatus::FAULT) {
    //     LOG_FATAL("ABSControl: FAULT - Brake actuator pump motor reports fault! ABS disabled.");
    //     current_abs_state_ = ABSState::FAULT_DETECTED;
    //     fault_code_ = 40;
    // }
    LOG_VERBOSE("ABSControl: System fault check complete. No new faults detected in this cycle.");
}


void ABSControl::processBraking(const VehicleState& vehicle_state,
                                const std::vector<SensorData>& wheel_speed_sensors,
                                double brake_pedal_pressure_input) {

    LOG_DEBUG("ABSControl: Processing braking cycle. Vehicle Speed: %.1f km/h, Pedal Pressure: %.1f bar.",
              vehicle_state.speed_kmh, brake_pedal_pressure_input);

    if (current_abs_state_ == ABSState::FAULT_DETECTED) {
        LOG_WARNING("ABSControl: System in FAULT state. ABS intervention disabled. Pedal Pressure: %.1f", brake_pedal_pressure_input);
        // Pass through brake pressure or go to a failsafe mode.
        // For simulation, all wheel_data_.applied_brake_pressure_bar could be set to brake_pedal_pressure_input
        for (auto& wd : wheel_data_) {
            wd.applied_brake_pressure_bar = brake_pedal_pressure_input;
        }
        return;
    }
     if (current_abs_state_ == ABSState::INITIALIZING) {
        LOG_INFO("ABSControl: System still initializing. Braking commands ignored for this cycle.");
        return;
    }


    // 1. Update internal state (reference speed, current wheel speeds)
    updateVehicleReferenceSpeed(wheel_speed_sensors, vehicle_state.speed_kmh); // Internal call

    // 2. Check for system faults (sensors, actuators etc.)
    checkForSystemFaults(wheel_speed_sensors); // Internal call
    if (current_abs_state_ == ABSState::FAULT_DETECTED) { // Re-check as checkForSystemFaults can change state
        LOG_ERROR("ABSControl: FAULT detected during cycle. Aborting ABS logic for this cycle. Fault code: %d", fault_code_);
        return;
    }

    // 3. Determine if ABS needs to be active
    // ABS typically engages above a certain speed and brake pressure threshold
    bool potential_intervention_needed = (vehicle_reference_speed_kmh_ > 10.0 && brake_pedal_pressure_input > 20.0); // e.g. >10km/h and >20bar pedal

    if (!potential_intervention_needed && current_abs_state_ == ABSState::INTERVENING) {
        LOG_INFO("ABSControl: Conditions no longer require ABS intervention (speed or pressure too low). Transitioning to INACTIVE.");
        current_abs_state_ = ABSState::INACTIVE;
        cycles_since_last_intervention_ = 0;
    } else if (potential_intervention_needed && current_abs_state_ == ABSState::INACTIVE) {
        LOG_INFO("ABSControl: Conditions (Speed: %.1f, Pedal: %.1f) warrant ABS MONITORING.", vehicle_reference_speed_kmh_, brake_pedal_pressure_input);
        current_abs_state_ = ABSState::MONITORING;
    }


    // 4. Main ABS logic: Detect lockup and modulate pressure
    bool any_wheel_locking = false;
    if (current_abs_state_ == ABSState::MONITORING || current_abs_state_ == ABSState::INTERVENING) {
        LOG_DEBUG("ABSControl: State is %s. Analyzing wheel speeds for lockup.", absStateToString(current_abs_state_));
        for (size_t i = 0; i < wheel_data_.size(); ++i) {
            // Update applied brake pressure based on pedal input if not actively modulating this wheel
            // This is a simplification; real systems have complex pressure mapping.
            if (current_abs_state_ != ABSState::INTERVENING || !wheel_data_[i].is_locking) { // only if not already intervening on this wheel.
                 wheel_data_[i].applied_brake_pressure_bar = brake_pedal_pressure_input;
            }

            wheel_data_[i].is_locking = detectWheelLockup(wheel_data_[i], 1.0 /*g threshold, placeholder*/); // Internal call
            if (wheel_data_[i].is_locking) {
                any_wheel_locking = true;
            }
        }

        if (any_wheel_locking) {
            if (current_abs_state_ != ABSState::INTERVENING) {
                LOG_WARNING("ABSControl: Transitioning to INTERVENING state due to wheel lockup!");
                current_abs_state_ = ABSState::INTERVENING;
            }
            cycles_since_last_intervention_ = 0;
            for (WheelSensorData& wd : wheel_data_) {
                if (wd.is_locking || (vehicle_reference_speed_kmh_ - wd.speed_kmh) > vehicle_reference_speed_kmh_ * 0.15) { // Modulate if locking OR significant slip
                    modulateBrakePressure(wd); // Internal call
                } else {
                    // If this wheel isn't locking but others are, we might still want to adjust its pressure
                    // relative to the pedal input, or hold it. For now, keep pedal pressure if not locking.
                    wd.applied_brake_pressure_bar = brake_pedal_pressure_input; // Follow pedal if not locking
                    LOG_VERBOSE("ABSControl: Wheel %d not locking, applying pedal pressure %.1f bar.", wd.wheel_id, brake_pedal_pressure_input);
                }
            }
        } else if (current_abs_state_ == ABSState::INTERVENING) {
            // No wheel is locking, but we were intervening. Gradually return to normal.
            cycles_since_last_intervention_++;
            LOG_INFO("ABSControl: No wheel lockup detected in INTERVENING state. Cycle: %d", cycles_since_last_intervention_);
            // Reapply pressure towards pedal request gradually
            for (WheelSensorData& wd : wheel_data_) {
                 // If pressure is below pedal, gradually increase
                 if (wd.applied_brake_pressure_bar < brake_pedal_pressure_input) {
                     reapplyPressure(wd); // This function adds a fixed amount, could be smarter
                     wd.applied_brake_pressure_bar = std::min(wd.applied_brake_pressure_bar, brake_pedal_pressure_input); // Don't exceed pedal request
                 } else {
                     // If pressure is above pedal (shouldn't happen often unless pedal was released quickly), reduce to pedal
                     wd.applied_brake_pressure_bar = brake_pedal_pressure_input;
                 }
                 LOG_DEBUG("ABSControl: Wheel %d (no lock), pressure adjusted to %.1f bar (pedal: %.1f).",
                           wd.wheel_id, wd.applied_brake_pressure_bar, brake_pedal_pressure_input);
            }

            if (cycles_since_last_intervention_ > 10) { // After N cycles with no lockup, transition out
                LOG_INFO("ABSControl: INTERVENTION complete after %d cycles. Transitioning to MONITORING/INACTIVE.", cycles_since_last_intervention_);
                current_abs_state_ = (potential_intervention_needed) ? ABSState::MONITORING : ABSState::INACTIVE;
                cycles_since_last_intervention_ = 0;
            }
        } else {
            // Not intervening and no wheel is locking, so standard braking applies
            current_abs_state_ = (potential_intervention_needed) ? ABSState::MONITORING : ABSState::INACTIVE;
            for (auto& wd : wheel_data_) {
                wd.applied_brake_pressure_bar = brake_pedal_pressure_input; // Direct pass-through
            }
             LOG_VERBOSE("ABSControl: No ABS intervention. Applying pedal pressure %.1f bar to all wheels.", brake_pedal_pressure_input);
        }
    } else {
        // ABS is INACTIVE, direct pass-through of brake pressure
        for (auto& wd : wheel_data_) {
            wd.applied_brake_pressure_bar = brake_pedal_pressure_input;
        }
        LOG_DEBUG("ABSControl: System INACTIVE. Applying pedal pressure %.1f bar to all wheels.", brake_pedal_pressure_input);
    }

    // Ensure all applied pressures are within physical limits
    for (auto& wd : wheel_data_) {
        wd.applied_brake_pressure_bar = std::max(0.0, std::min(wd.applied_brake_pressure_bar, 200.0)); // Example max 200 bar
    }

    LOG_DEBUG("ABSControl: Braking cycle processing complete. Final ABS State: %s.", absStateToString(current_abs_state_));
}


ABSState ABSControl::getCurrentState() const {
    LOG_DEBUG("ABSControl: getCurrentState() -> %s.", absStateToString(current_abs_state_));
    return current_abs_state_;
}

bool ABSControl::isABSInterventionActive() const {
    bool active = (current_abs_state_ == ABSState::INTERVENING);
    LOG_DEBUG("ABSControl: isABSInterventionActive() -> %s.", active ? "YES" : "NO");
    return active;
}

void ABSControl::runDiagnostics() {
    LOG_INFO("ABSControl: Running system diagnostics...");
    current_abs_state_ = ABSState::INITIALIZING; // Mark as initializing during diagnostics
    fault_code_ = 0; // Reset fault code

    // Simulate sensor checks
    std::this_thread::sleep_for(std::chrono::milliseconds(20)); // Simulate time for sensor checks
    // Conceptual: Check connectivity to all 4 wheel speed sensors
    bool sensor_conn_ok = (rand() % 100) > 2; // 98% chance sensors are OK
    if (!sensor_conn_ok) {
        LOG_ERROR("ABSControl: DIAGNOSTIC FAULT - Wheel speed sensor connectivity check failed. Sensor_ID: %d", (rand()%4));
        current_abs_state_ = ABSState::FAULT_DETECTED;
        fault_code_ = 50 + (rand()%4) ; // Example fault code for sensor connectivity
    } else {
        LOG_DEBUG("ABSControl: Wheel speed sensor connectivity OK.");
    }

    // Simulate actuator checks (pump motor, valves)
    // if (brake_actuator_) {
    //    if(!brake_actuator_->performSelfTest()) {
    //        LOG_ERROR("ABSControl: DIAGNOSTIC FAULT - Brake actuator self-test failed. Code: %d", brake_actuator_->getLastErrorCode());
    //        current_abs_state_ = ABSState::FAULT_DETECTED;
    //        fault_code_ = 60 + brake_actuator_->getLastErrorCode();
    //    } else {
    //        LOG_DEBUG("ABSControl: Brake actuator self-test PASSED.");
    //    }
    // } else if (current_abs_state_ != ABSState::FAULT_DETECTED) { // If not already faulted by missing actuator
    //     LOG_FATAL("ABSControl: BrakeActuatorInterface is NULL during diagnostics! This is a critical configuration fault.");
    //     current_abs_state_ = ABSState::FAULT_DETECTED;
    //     fault_code_ = 1; // Critical dependency missing
    // }
    std::this_thread::sleep_for(std::chrono::milliseconds(30)); // Simulate time for actuator checks
    bool actuator_ok = (rand() % 100) > 3; // 97% chance actuators are OK
    if (!actuator_ok && current_abs_state_ != ABSState::FAULT_DETECTED) {
        LOG_ERROR("ABSControl: DIAGNOSTIC FAULT - ABS hydraulic unit/valve check failed (simulated).");
        current_abs_state_ = ABSState::FAULT_DETECTED;
        fault_code_ = 70;
    } else if (actuator_ok) {
         LOG_DEBUG("ABSControl: Actuator checks PASSED (simulated).");
    }


    if (current_abs_state_ == ABSState::FAULT_DETECTED) {
        LOG_WARNING("ABSControl: Diagnostics complete. FAULT DETECTED. Code: %d. System remains in FAULT_DETECTED state.", fault_code_);
    } else {
        current_abs_state_ = ABSState::INACTIVE; // If diagnostics pass, system is ready and inactive.
        LOG_INFO("ABSControl: Diagnostics complete. All systems nominal. System is INACTIVE.");
    }
}


} // namespace ecu_safety_systems