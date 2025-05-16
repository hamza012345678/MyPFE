// AutoSystemSim/ecu_body_control_module/lighting_control.cpp
#include "lighting_control.h"
#include "../ecu_power_management/power_monitor.h" // Actual include for dependency
#include <algorithm> // For std::find_if
#include <random>    // For simulating faults

namespace ecu_body_control_module {

LightingControl::LightingControl(ecu_power_management::PowerMonitor* pm) :
    power_monitor_(pm),
    is_hazard_active_(false),
    is_left_indicator_active_(false),
    is_right_indicator_active_(false)
{
    LOG_INFO("LightingControl: Initializing...");
    if (!power_monitor_) {
        LOG_ERROR("LightingControl: PowerMonitor service is NULL! Lighting functions may be impaired.");
        // This is a critical dependency. We could throw or enter a safe state.
    }

    // Initialize all known light types
    all_lights_.emplace_back(LightType::HEADLIGHT_LOW);
    all_lights_.emplace_back(LightType::HEADLIGHT_HIGH);
    all_lights_.emplace_back(LightType::PARKING_LIGHT);
    all_lights_.emplace_back(LightType::BRAKE_LIGHT);
    all_lights_.emplace_back(LightType::REVERSE_LIGHT);
    all_lights_.emplace_back(LightType::FOG_LIGHT_FRONT);
    all_lights_.emplace_back(LightType::FOG_LIGHT_REAR);
    all_lights_.emplace_back(LightType::INDICATOR_LEFT);
    all_lights_.emplace_back(LightType::INDICATOR_RIGHT);
    all_lights_.emplace_back(LightType::INTERIOR_DOME);
    // HAZARD_LIGHTS is a function, not a single bulb state here.

    LOG_INFO("LightingControl: Initialization complete. %zu light types registered.", all_lights_.size());
    performBulbCheck(); // Initial bulb check on startup
}

LightingControl::~LightingControl() {
    LOG_INFO("LightingControl: Shutting down.");
}

BulbState* LightingControl::findBulb(LightType type) {
    auto it = std::find_if(all_lights_.begin(), all_lights_.end(),
                           [type](const BulbState& bs){ return bs.type == type; });
    if (it != all_lights_.end()) {
        return &(*it);
    }
    LOG_WARNING("LightingControl: findBulb: LightType %d not found.", static_cast<int>(type));
    return nullptr;
}

const BulbState* LightingControl::findBulb(LightType type) const {
    // const version
    auto it = std::find_if(all_lights_.begin(), all_lights_.end(),
                           [type](const BulbState& bs){ return bs.type == type; });
    if (it != all_lights_.end()) {
        return &(*it);
    }
    LOG_WARNING("LightingControl: findBulb (const): LightType %d not found.", static_cast<int>(type));
    return nullptr;
}


void LightingControl::setSpecificLight(LightType type, bool on) {
    BulbState* bulb = findBulb(type);
    if (!bulb) {
        LOG_ERROR("LightingControl: Cannot set state for unknown LightType %d.", static_cast<int>(type));
        return;
    }

    if (bulb->status == LightStatus::FAULTY_BULB || bulb->status == LightStatus::FAULTY_CIRCUIT) {
        LOG_WARNING("LightingControl: Cannot turn %s LightType %d. It's faulty (Status: %d).",
                    on ? "ON" : "OFF", static_cast<int>(type), static_cast<int>(bulb->status));
        return;
    }

    LightStatus new_status = on ? LightStatus::ON : LightStatus::OFF;
    if (bulb->status == new_status) {
        LOG_DEBUG("LightingControl: LightType %d already %s.", static_cast<int>(type), on ? "ON" : "OFF");
        return;
    }

    // Check power stability before turning ON a significant light
    if (on && (type == LightType::HEADLIGHT_LOW || type == LightType::HEADLIGHT_HIGH || type == LightType::FOG_LIGHT_FRONT)) {
        if (power_monitor_ && !power_monitor_->isPowerStable()) { // Inter-ECU call
            LOG_WARNING("LightingControl: Power system unstable. Deferring turning ON LightType %d.", static_cast<int>(type));
            // Could also reduce brightness or prevent activation
            if(power_monitor_->getBatteryVoltage() < 10.0) { // Inter-ECU call
                 LOG_ERROR("LightingControl: CRITICAL: Battery too low (%.2fV) to activate LightType %d.", power_monitor_->getBatteryVoltage(), static_cast<int>(type));
                 return;
            }
        }
    }

    bulb->status = new_status;
    LOG_INFO("LightingControl: LightType %d turned %s.", static_cast<int>(type), on ? "ON" : "OFF");

    // Example of a dependent action: If high beams are turned on, ensure low beams are also on (common logic)
    if (type == LightType::HEADLIGHT_HIGH && on) {
        BulbState* low_beam = findBulb(LightType::HEADLIGHT_LOW);
        if (low_beam && low_beam->status == LightStatus::OFF) {
            LOG_DEBUG("LightingControl: High beams activated, ensuring low beams are also ON.");
            setSpecificLight(LightType::HEADLIGHT_LOW, true); // Recursive call (simple case) or direct state change
        }
    }
    // If low beams are turned OFF and high beams are ON, turn high beams OFF (safety)
    if (type == LightType::HEADLIGHT_LOW && !on) {
        BulbState* high_beam = findBulb(LightType::HEADLIGHT_HIGH);
        if (high_beam && high_beam->status == LightStatus::ON) {
            LOG_INFO("LightingControl: Low beams turned OFF while high beams were ON. Turning OFF high beams for safety.");
            setSpecificLight(LightType::HEADLIGHT_HIGH, false);
        }
    }
}


bool LightingControl::setLightState(LightType type, bool on) {
    LOG_DEBUG("LightingControl: Request to set LightType %d to %s.", static_cast<int>(type), on ? "ON" : "OFF");

    // Hazard lights override individual indicators if active
    if (is_hazard_active_ && (type == LightType::INDICATOR_LEFT || type == LightType::INDICATOR_RIGHT)) {
        LOG_INFO("LightingControl: Hazard lights are active. Ignoring individual indicator request for LightType %d.", static_cast<int>(type));
        return false; // Or true, indicating command processed but no change due to override
    }

    // Special handling for indicators as they are mutually exclusive (unless hazards are on)
    if (type == LightType::INDICATOR_LEFT && on) {
        if (is_right_indicator_active_) setSpecificLight(LightType::INDICATOR_RIGHT, false);
        is_left_indicator_active_ = true;
        is_right_indicator_active_ = false;
    } else if (type == LightType::INDICATOR_RIGHT && on) {
        if (is_left_indicator_active_) setSpecificLight(LightType::INDICATOR_LEFT, false);
        is_right_indicator_active_ = true;
        is_left_indicator_active_ = false;
    } else if ((type == LightType::INDICATOR_LEFT || type == LightType::INDICATOR_RIGHT) && !on) {
        // Only turn off if it was the active one
        if (type == LightType::INDICATOR_LEFT && is_left_indicator_active_) is_left_indicator_active_ = false;
        if (type == LightType::INDICATOR_RIGHT && is_right_indicator_active_) is_right_indicator_active_ = false;
    }

    setSpecificLight(type, on);
    return true;
}

bool LightingControl::activateHazardLights(bool activate) {
    LOG_INFO("LightingControl: Hazard lights requested to %s.", activate ? "ACTIVATE" : "DEACTIVATE");
    if (is_hazard_active_ == activate) {
        LOG_DEBUG("LightingControl: Hazard lights already in requested state (%s).", activate ? "ACTIVE" : "INACTIVE");
        return true;
    }

    is_hazard_active_ = activate;
    if (activate) {
        // Turn off individual indicators if they were on
        if (is_left_indicator_active_) {
            setSpecificLight(LightType::INDICATOR_LEFT, false);
            is_left_indicator_active_ = false;
        }
        if (is_right_indicator_active_) {
            setSpecificLight(LightType::INDICATOR_RIGHT, false);
            is_right_indicator_active_ = false;
        }
        // Flash both indicators
        LOG_INFO("LightingControl: Activating hazard sequence (both indicators ON).");
        setSpecificLight(LightType::INDICATOR_LEFT, true);
        setSpecificLight(LightType::INDICATOR_RIGHT, true);
        // In reality, this would involve a flasher relay/logic for blinking
    } else {
        LOG_INFO("LightingControl: Deactivating hazard sequence (both indicators OFF).");
        setSpecificLight(LightType::INDICATOR_LEFT, false);
        setSpecificLight(LightType::INDICATOR_RIGHT, false);
    }
    return true;
}

bool LightingControl::activateIndicator(LightType indicator_type, bool activate) {
    if (indicator_type != LightType::INDICATOR_LEFT && indicator_type != LightType::INDICATOR_RIGHT) {
        LOG_ERROR("LightingControl: Invalid LightType %d for indicator.", static_cast<int>(indicator_type));
        return false;
    }
    LOG_INFO("LightingControl: Indicator %s requested to %s.",
             (indicator_type == LightType::INDICATOR_LEFT ? "LEFT" : "RIGHT"),
             activate ? "ACTIVATE" : "DEACTIVATE");

    if (is_hazard_active_) {
        LOG_WARNING("LightingControl: Hazard lights are active. Cannot set individual indicator %s.",
                    (indicator_type == LightType::INDICATOR_LEFT ? "LEFT" : "RIGHT"));
        return false;
    }

    return setLightState(indicator_type, activate); // Reuses the logic in setLightState
}


LightStatus LightingControl::getLightStatus(LightType type) const {
    const BulbState* bulb = findBulb(type);
    if (bulb) {
        LOG_DEBUG("LightingControl: Status for LightType %d is %d.", static_cast<int>(type), static_cast<int>(bulb->status));
        return bulb->status;
    }
    LOG_WARNING("LightingControl: getLightStatus: Could not find LightType %d. Reporting OFF.", static_cast<int>(type));
    return LightStatus::OFF; // Default or error status
}

void LightingControl::performBulbCheck() {
    LOG_INFO("LightingControl: Performing diagnostic bulb check sequence...");
    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_int_distribution<> distrib(1, 100); // For fault simulation

    for (BulbState& bulb : all_lights_) {
        // Skip if already known to be faulty to avoid spamming logs, or re-check sometimes
        if (bulb.status == LightStatus::FAULTY_BULB || bulb.status == LightStatus::FAULTY_CIRCUIT) {
            if (distrib(gen) > 90) { // 10% chance to re-log a known fault
                 LOG_WARNING("LightingControl: Re-confirming fault for LightType %d. Status: %d, Code: %d",
                    static_cast<int>(bulb.type), static_cast<int>(bulb.status), bulb.fault_code);
            }
            continue;
        }

        int chance = distrib(gen);
        if (chance <= 2) { // 2% chance of new bulb fault
            bulb.status = LightStatus::FAULTY_BULB;
            bulb.fault_code = 100 + static_cast<int>(bulb.type); // Example fault code
            LOG_ERROR("LightingControl: BULB FAULT DETECTED for LightType %d! Code: %d",
                      static_cast<int>(bulb.type), bulb.fault_code);
        } else if (chance <= 3) { // 1% chance of new circuit fault
            bulb.status = LightStatus::FAULTY_CIRCUIT;
            bulb.fault_code = 200 + static_cast<int>(bulb.type); // Example fault code
            LOG_ERROR("LightingControl: CIRCUIT FAULT DETECTED for LightType %d! Code: %d",
                      static_cast<int>(bulb.type), bulb.fault_code);
        } else {
            // If bulb was OFF and is not faulty, just log verbosely
            if (bulb.status == LightStatus::OFF) {
                 LOG_VERBOSE("LightingControl: Bulb check OK for LightType %d (currently OFF).", static_cast<int>(bulb.type));
            } else { // Bulb is ON and not faulty
                 LOG_DEBUG("LightingControl: Bulb check OK for LightType %d (currently ON).", static_cast<int>(bulb.type));
            }
        }
    }
    LOG_INFO("LightingControl: Bulb check sequence complete.");
}

void LightingControl::handleAutomaticHeadlights(const VehicleState& vehicle_state, bool power_stable) {
    // Dummy logic for automatic headlights: turn on if dark (not simulated) and speed > 0 and power is stable
    // For simplicity, we'll assume it's "dark enough" if this function is called by a higher logic.
    LOG_DEBUG("LightingControl: Evaluating automatic headlights. Speed: %.1f km/h, Power Stable: %s",
              vehicle_state.speed_kmh, power_stable ? "true" : "false");

    BulbState* low_beam = findBulb(LightType::HEADLIGHT_LOW);
    if (!low_beam || low_beam->status == LightStatus::FAULTY_BULB || low_beam->status == LightStatus::FAULTY_CIRCUIT) {
        LOG_WARNING("LightingControl: Auto Headlights: Low beam bulb faulty or not found. Cannot operate automatically.");
        return;
    }

    // This is a simplified logic
    bool should_be_on = (vehicle_state.speed_kmh > 1.0 && power_stable); // Simplified: on if moving and power okay

    if (should_be_on && low_beam->status == LightStatus::OFF) {
        LOG_INFO("LightingControl: Automatic Headlights: Turning ON low beams. Speed: %.1f km/h.", vehicle_state.speed_kmh);
        setSpecificLight(LightType::HEADLIGHT_LOW, true);
    } else if (!should_be_on && low_beam->status == LightStatus::ON) {
        // Only turn off if they were turned on by this auto logic (more complex state needed for that)
        // For now, let's assume any "ON" state can be turned "OFF" by auto if conditions not met.
        LOG_INFO("LightingControl: Automatic Headlights: Turning OFF low beams. Speed: %.1f km/h or power unstable.", vehicle_state.speed_kmh);
        setSpecificLight(LightType::HEADLIGHT_LOW, false);
    } else {
        LOG_VERBOSE("LightingControl: Automatic Headlights: No change in low beam state required.");
    }
}

void LightingControl::checkBrakeLights(const VehicleState& vehicle_state) {
    // This would normally be triggered by a brake pedal sensor signal.
    // Here, we might infer it from drastic speed changes or a direct "brake_pedal_pressed" flag in VehicleState.
    // For now, let's assume vehicle_state might have a "braking_intensity" or similar.
    // Simplified: turn on brake lights if speed is decreasing rapidly (a rough proxy).
    // A better VehicleState would have `bool brake_pedal_active;`

    LOG_DEBUG("LightingControl: Checking brake light status based on vehicle state.");
    bool activate_brake_lights = false; // Placeholder for actual logic

    // Example: if vehicle_state had a direct flag:
    // if (vehicle_state.brake_pedal_pressed) { activate_brake_lights = true; }

    // If we only have speed, it's harder. This is a poor simulation:
    static double last_speed = vehicle_state.speed_kmh;
    if ((last_speed - vehicle_state.speed_kmh) > 5.0) { // Decelerating by more than 5km/h since last check
        LOG_INFO("LightingControl: Significant deceleration detected (%.1f -> %.1f km/h). Activating brake lights.", last_speed, vehicle_state.speed_kmh);
        activate_brake_lights = true;
    }
    last_speed = vehicle_state.speed_kmh;

    if (activate_brake_lights) {
        if (getLightStatus(LightType::BRAKE_LIGHT) == LightStatus::OFF) {
             LOG_INFO("LightingControl: Activating brake lights.");
        }
        setSpecificLight(LightType::BRAKE_LIGHT, true);
    } else {
        // Turn off brake lights ONLY if they were on and no longer need to be.
        // This requires state to know *why* they were on.
        // Simple version: if not actively braking, turn them off (might flicker with above logic)
        if (getLightStatus(LightType::BRAKE_LIGHT) == LightStatus::ON) {
            LOG_INFO("LightingControl: Deactivating brake lights (no braking condition detected).");
        }
        setSpecificLight(LightType::BRAKE_LIGHT, false);
    }
}


void LightingControl::updateLighting(const VehicleState& current_vehicle_state) {
    LOG_INFO("LightingControl: Updating lighting based on vehicle state. Speed: %.1f km/h, RPM: %d",
             current_vehicle_state.speed_kmh, current_vehicle_state.engine_rpm);

    bool power_is_stable = true; // Assume stable unless PM says otherwise
    if (power_monitor_) {
        power_is_stable = power_monitor_->isPowerStable(); // Call to PowerMonitor
        if (!power_is_stable) {
            LOG_WARNING("LightingControl: Power system is UNSTABLE. Some lighting functions might be limited.");
            // Example: dim interior lights or prevent high-power lights
            BulbState* interior = findBulb(LightType::INTERIOR_DOME);
            if (interior && interior->status == LightStatus::ON) {
                LOG_INFO("LightingControl: Dimming interior light due to unstable power (simulated).");
                // In a real system, would command a dimmer. Here, just a log.
            }
        }
    } else {
        LOG_WARNING("LightingControl: updateLighting: PowerMonitor not available. Assuming power is stable.");
    }

    // --- Automatic Headlights Logic ---
    // Assuming it's "dark" for this simulation to trigger auto headlights logic.
    // In a real car, this would come from an ambient light sensor.
    handleAutomaticHeadlights(current_vehicle_state, power_is_stable); // Internal call

    // --- Brake Lights Logic ---
    // This is typically event-driven, but we can poll VehicleState for changes.
    checkBrakeLights(current_vehicle_state); // Internal call

    // --- Other potential logic ---
    // e.g., turn on parking lights if vehicle is off but doors are unlocked
    // e.g., flash lights if alarm is triggered (would need an alarm state)

    // Periodically re-check bulbs if not done too frequently by external call
    static int update_counter = 0;
    if (++update_counter % 10 == 0) { // Every 10 updates, do a bulb check
        LOG_DEBUG("LightingControl: Periodic bulb check triggered during update cycle.");
        performBulbCheck(); // Internal call
    }

    LOG_INFO("LightingControl: Lighting update cycle complete.");
}


} // namespace ecu_body_control_module