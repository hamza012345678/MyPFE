// AutoSystemSim/ecu_body_control_module/climate_control.cpp
#include "climate_control.h"
#include "../ecu_power_management/power_monitor.h" // Actual include
#include <cmath>     // For fabs, std::min/max
#include <random>    // For simulating sensor drift / small variations

namespace ecu_body_control_module {

// Helper to convert AirDistributionMode enum to string for logging
const char* airDistModeToString(AirDistributionMode mode) {
    switch (mode) {
        case AirDistributionMode::OFF: return "OFF";
        case AirDistributionMode::FACE_VENTS: return "FACE_VENTS";
        case AirDistributionMode::FEET_VENTS: return "FEET_VENTS";
        case AirDistributionMode::FACE_AND_FEET: return "FACE_AND_FEET";
        case AirDistributionMode::WINDSHIELD_DEFROST: return "WINDSHIELD_DEFROST";
        case AirDistributionMode::WINDSHIELD_AND_FEET: return "WINDSHIELD_AND_FEET";
        default: return "UNKNOWN_DISTRIBUTION";
    }
}

// Helper to convert ACCompressorStatus enum to string for logging
const char* acStatusToString(ACCompressorStatus status) {
    switch (status) {
        case ACCompressorStatus::OFF: return "OFF";
        case ACCompressorStatus::ON_REQUESTED: return "ON_REQUESTED";
        case ACCompressorStatus::ON_ACTIVE: return "ON_ACTIVE";
        case ACCompressorStatus::OFF_BY_POWER_MANAGEMENT: return "OFF_BY_POWER_MANAGEMENT";
        case ACCompressorStatus::OFF_BY_ENGINE_NOT_RUNNING: return "OFF_BY_ENGINE_NOT_RUNNING";
        case ACCompressorStatus::FAULTY: return "FAULTY";
        default: return "UNKNOWN_AC_STATUS";
    }
}

ClimateControl::ClimateControl(ecu_power_management::PowerMonitor* pm) :
    power_monitor_(pm),
    target_temperature_celsius_(22.0), // Comfortable default
    fan_speed_level_(0), // Off by default
    current_air_distribution_(AirDistributionMode::OFF),
    ac_requested_by_user_(true), // Default to AC available if user wants it
    recirculation_active_(false),
    auto_mode_enabled_(false), // Default to manual control
    current_interior_temperature_celsius_(25.0), // Simulate initial warmer temp
    current_exterior_temperature_celsius_(20.0),
    ac_compressor_status_(ACCompressorStatus::OFF),
    ac_power_denial_counter_(0)
{
    LOG_INFO("ClimateControl: Initializing. Target Temp: %.1fC, Fan: %d, AC User Req: %s, Auto: %s",
             target_temperature_celsius_, fan_speed_level_, ac_requested_by_user_ ? "ON" : "OFF", auto_mode_enabled_ ? "ON" : "OFF");
    if (!power_monitor_) {
        LOG_WARNING("ClimateControl: PowerMonitor service is NULL. AC compressor management might be impaired.");
    }
}

ClimateControl::~ClimateControl() {
    LOG_INFO("ClimateControl: Shutting down. Final Target Temp: %.1fC, AC Status: %s",
             target_temperature_celsius_, acStatusToString(ac_compressor_status_));
}

bool ClimateControl::setTargetTemperature(double celsius) {
    // Typical range for climate control might be 16C to 30C
    celsius = std::max(16.0, std::min(30.0, celsius));
    LOG_INFO("ClimateControl: Set target temperature to %.1fC (was %.1fC).", celsius, target_temperature_celsius_);
    if (fabs(target_temperature_celsius_ - celsius) < 0.1 && !auto_mode_enabled_) {
        LOG_DEBUG("ClimateControl: Target temperature already set to %.1fC or auto mode is off.", celsius);
        // If auto mode is on, even same temp can trigger recalcs
    }
    target_temperature_celsius_ = celsius;
    if (auto_mode_enabled_) {
        LOG_DEBUG("ClimateControl: Auto mode is ON. Temperature change may trigger recalculation of fan/distribution.");
        // manageAutomaticOperation(); // Could be called immediately, or let updateClimateState handle it
    }
    return true;
}

bool ClimateControl::setFanSpeed(int level) {
    level = std::max(0, std::min(5, level)); // Assuming 0-5 levels
    LOG_INFO("ClimateControl: Set fan speed to level %d (was %d).", level, fan_speed_level_);
    if (fan_speed_level_ == level) {
        LOG_DEBUG("ClimateControl: Fan speed already at level %d.", level);
    }
    fan_speed_level_ = level;
    if (level == 0) {
        LOG_INFO("ClimateControl: Fan turned OFF. This might also turn off AC compressor if it's not needed for defrost.");
        current_air_distribution_ = AirDistributionMode::OFF; // Typically fan off means no distribution
        // AC compressor logic will be handled in update or controlACCompressor
    }
    if (auto_mode_enabled_ && level != 0) { // If user manually changes fan, disable auto mode
        LOG_INFO("ClimateControl: Manual fan speed change. Disabling AUTO mode.");
        auto_mode_enabled_ = false;
    }
    return true;
}

bool ClimateControl::setAirDistribution(AirDistributionMode mode) {
    LOG_INFO("ClimateControl: Set air distribution to %s (was %s).",
             airDistModeToString(mode), airDistModeToString(current_air_distribution_));
    if (current_air_distribution_ == mode) {
        LOG_DEBUG("ClimateControl: Air distribution already %s.", airDistModeToString(mode));
    }
    current_air_distribution_ = mode;
    if (auto_mode_enabled_ && mode != AirDistributionMode::OFF) { // If user manually changes distribution, disable auto mode
        LOG_INFO("ClimateControl: Manual air distribution change. Disabling AUTO mode.");
        auto_mode_enabled_ = false;
    }
    if (mode == AirDistributionMode::WINDSHIELD_DEFROST) {
        LOG_INFO("ClimateControl: Windshield defrost selected. AC might be activated for dehumidification.");
        // AC compressor logic will handle this.
    }
    return true;
}

bool ClimateControl::setACActive(bool active) {
    LOG_INFO("ClimateControl: User request to set AC to %s (was %s).",
             active ? "ON" : "OFF", ac_requested_by_user_ ? "ON" : "OFF");
    if (ac_requested_by_user_ == active) {
        LOG_DEBUG("ClimateControl: AC user request already %s.", active ? "ON" : "OFF");
    }
    ac_requested_by_user_ = active;
    if (auto_mode_enabled_ && !active) { // If user manually turns AC off, disable auto mode
        LOG_INFO("ClimateControl: Manual AC OFF request. Disabling AUTO mode.");
        auto_mode_enabled_ = false;
    }
    // Actual compressor state handled by controlACCompressor via updateClimateState
    return true;
}

bool ClimateControl::setRecirculationActive(bool active) {
    LOG_INFO("ClimateControl: Set air recirculation to %s (was %s).",
             active ? "ON" : "OFF", recirculation_active_ ? "ON" : "OFF");
    if (recirculation_active_ == active) {
        LOG_DEBUG("ClimateControl: Recirculation already %s.", active ? "ON" : "OFF");
    }
    recirculation_active_ = active;
    // Some systems disable recirculation automatically in defrost mode
    if (current_air_distribution_ == AirDistributionMode::WINDSHIELD_DEFROST && recirculation_active_) {
        LOG_WARNING("ClimateControl: Recirculation requested during WINDSHIELD_DEFROST. This is often overridden to FRESH AIR for safety/effectiveness.");
        // recirculation_active_ = false; // Could force it off
    }
    return true;
}

bool ClimateControl::setAutoMode(bool enabled) {
    LOG_INFO("ClimateControl: AUTO mode set to %s (was %s).",
             enabled ? "ENABLED" : "DISABLED", auto_mode_enabled_ ? "ENABLED" : "DISABLED");
    if (auto_mode_enabled_ == enabled) {
        LOG_DEBUG("ClimateControl: AUTO mode already %s.", enabled ? "ENABLED" : "DISABLED");
    }
    auto_mode_enabled_ = enabled;
    if (enabled) {
        LOG_INFO("ClimateControl: AUTO mode enabled. System will now manage fan, distribution, and AC.");
        // manageAutomaticOperation(); // Or let update handle
    } else {
        LOG_INFO("ClimateControl: AUTO mode disabled. System reverts to last manual settings.");
    }
    return true;
}

double ClimateControl::getCurrentInteriorTemp() const {
    LOG_DEBUG("ClimateControl: getCurrentInteriorTemp() -> %.1fC", current_interior_temperature_celsius_);
    return current_interior_temperature_celsius_;
}
double ClimateControl::getTargetTemperature() const {
    LOG_DEBUG("ClimateControl: getTargetTemperature() -> %.1fC", target_temperature_celsius_);
    return target_temperature_celsius_;
}
int ClimateControl::getFanSpeed() const {
    LOG_DEBUG("ClimateControl: getFanSpeed() -> Level %d", fan_speed_level_);
    return fan_speed_level_;
}
AirDistributionMode ClimateControl::getAirDistribution() const {
    LOG_DEBUG("ClimateControl: getAirDistribution() -> %s", airDistModeToString(current_air_distribution_));
    return current_air_distribution_;
}
bool ClimateControl::isACActive() const { // Effective status
    bool active = (ac_compressor_status_ == ACCompressorStatus::ON_ACTIVE);
    LOG_DEBUG("ClimateControl: isACActive() (effective) -> %s (Compressor status: %s)",
              active ? "YES" : "NO", acStatusToString(ac_compressor_status_));
    return active;
}
ACCompressorStatus ClimateControl::getACCompressorStatus() const {
    LOG_DEBUG("ClimateControl: getACCompressorStatus() -> %s", acStatusToString(ac_compressor_status_));
    return ac_compressor_status_;
}
bool ClimateControl::isRecirculationActive() const {
    LOG_DEBUG("ClimateControl: isRecirculationActive() -> %s", recirculation_active_ ? "YES" : "NO");
    return recirculation_active_;
}
bool ClimateControl::isAutoModeEnabled() const {
    LOG_DEBUG("ClimateControl: isAutoModeEnabled() -> %s", auto_mode_enabled_ ? "YES" : "NO");
    return auto_mode_enabled_;
}


bool ClimateControl::canActivateAC(bool engine_running, bool high_engine_load_simulated) const {
    if (!engine_running) {
        LOG_INFO("ClimateControl: Cannot activate AC. Engine is not running.");
        return false;
    }
    if (high_engine_load_simulated) { // e.g. from Engine ECU during hard acceleration
        LOG_INFO("ClimateControl: Cannot activate AC. High engine load detected (e.g. full throttle).");
        return false;
    }
    // Check power monitor for system voltage / stability (A/C is a big load)
    if (power_monitor_) {
        if (!power_monitor_->isPowerStable()) {
            LOG_WARNING("ClimateControl: Power system unstable. AC activation deferred by PowerMonitor.");
            return false;
        }
        if (power_monitor_->getBatteryVoltage() < 11.0) { // Example threshold
            LOG_WARNING("ClimateControl: Battery voltage (%.2fV) too low for AC compressor. Activation deferred.", power_monitor_->getBatteryVoltage());
            return false;
        }
    }
    if (current_exterior_temperature_celsius_ < 2.0) { // AC compressor clutch might disengage at very low ambients
        LOG_INFO("ClimateControl: Exterior temperature (%.1fC) very low. AC compressor might not engage for cooling.", current_exterior_temperature_celsius_);
        // Still allow for defrost, dehumidification.
    }
    return true;
}

void ClimateControl::controlACCompressor(bool engine_running, bool high_engine_load_simulated) {
    ACCompressorStatus old_status = ac_compressor_status_;
    bool ac_should_be_on = false;

    if (fan_speed_level_ == 0 && current_air_distribution_ != AirDistributionMode::WINDSHIELD_DEFROST) {
        // If fan is off and not defrosting, AC should be off.
        LOG_DEBUG("ClimateControl: Fan is OFF and not defrosting. AC compressor should be OFF.");
        ac_should_be_on = false;
    } else if (ac_requested_by_user_ || (auto_mode_enabled_ && current_interior_temperature_celsius_ > target_temperature_celsius_ + 0.5) ||
        current_air_distribution_ == AirDistributionMode::WINDSHIELD_DEFROST) {
        // Conditions where AC might be needed: user request, auto mode cooling, or defrost.
        LOG_DEBUG("ClimateControl: AC potentially needed. UserReq: %d, AutoCool: %d, Defrost: %d",
                  ac_requested_by_user_,
                  (auto_mode_enabled_ && current_interior_temperature_celsius_ > target_temperature_celsius_ + 0.5),
                  (current_air_distribution_ == AirDistributionMode::WINDSHIELD_DEFROST)
                  );
        ac_should_be_on = true;
    }


    if (ac_should_be_on) {
        if (canActivateAC(engine_running, high_engine_load_simulated)) { // Internal call
            ac_compressor_status_ = ACCompressorStatus::ON_ACTIVE;
            ac_power_denial_counter_ = 0; // Reset counter on successful activation
            if (old_status != ACCompressorStatus::ON_ACTIVE) {
                 LOG_INFO("ClimateControl: AC Compressor ACTIVATED.");
                 if (power_monitor_) power_monitor_->simulateHighLoadEvent(true); // Signal high load start
            }
        } else {
            // Cannot activate due to conditions from canActivateAC
            ac_compressor_status_ = engine_running ? ACCompressorStatus::OFF_BY_POWER_MANAGEMENT : ACCompressorStatus::OFF_BY_ENGINE_NOT_RUNNING;
            ac_power_denial_counter_++;
            if (old_status == ACCompressorStatus::ON_ACTIVE && power_monitor_) {
                power_monitor_->simulateHighLoadEvent(false); // Signal high load end if it was running
            }
            LOG_WARNING("ClimateControl: AC Compressor activation DENIED. Reason: %s. Denial count: %d",
                        acStatusToString(ac_compressor_status_), ac_power_denial_counter_);
            if (ac_power_denial_counter_ > 5) {
                LOG_ERROR("ClimateControl: AC compressor denied %d consecutive times. Potential underlying issue or prolonged condition.", ac_power_denial_counter_);
                // Could set a temporary fault or alert user.
            }
        }
    } else { // AC should be OFF
        if (ac_compressor_status_ == ACCompressorStatus::ON_ACTIVE) {
            LOG_INFO("ClimateControl: AC Compressor DEACTIVATED (no longer needed).");
            if (power_monitor_) power_monitor_->simulateHighLoadEvent(false); // Signal high load end
        }
        ac_compressor_status_ = ACCompressorStatus::OFF;
        ac_power_denial_counter_ = 0; // Reset counter
    }

    if (old_status != ac_compressor_status_) {
        LOG_INFO("ClimateControl: AC Compressor status changed from %s to %s.",
                 acStatusToString(old_status), acStatusToString(ac_compressor_status_));
    }
}

void ClimateControl::adjustFanForTemperature() {
    // Simple auto fan logic: higher difference = higher fan
    double temp_diff = current_interior_temperature_celsius_ - target_temperature_celsius_;
    int new_fan_speed = fan_speed_level_;

    if (fabs(temp_diff) < 0.5) { // Close to target
        new_fan_speed = 1; // Low fan
    } else if (fabs(temp_diff) < 2.0) {
        new_fan_speed = 2;
    } else if (fabs(temp_diff) < 4.0) {
        new_fan_speed = 3;
    } else { // Large difference
        new_fan_speed = (fabs(temp_diff) < 6.0) ? 4 : 5; // Max fan
    }

    // If heating is needed (interior colder than target) and exterior is very cold,
    // might limit fan speed until engine coolant is warm (not simulated here).
    if (temp_diff < -1.0 && current_exterior_temperature_celsius_ < 5.0) {
        new_fan_speed = std::min(new_fan_speed, 2); // Limit fan if blowing cold air for heating
        LOG_DEBUG("ClimateControl: Auto fan: Cold exterior/interior, limiting fan speed to %d for heating comfort.", new_fan_speed);
    }


    if (new_fan_speed != fan_speed_level_) {
        LOG_INFO("ClimateControl: AUTO Fan Speed: Adjusting from %d to %d due to temp diff %.1fC.",
                 fan_speed_level_, new_fan_speed, temp_diff);
        fan_speed_level_ = new_fan_speed;
    }
}

void ClimateControl::adjustAirDistributionForMode() {
    AirDistributionMode new_dist_mode = current_air_distribution_;
    double temp_diff = current_interior_temperature_celsius_ - target_temperature_celsius_;

    if (current_exterior_temperature_celsius_ < 3.0 && target_temperature_celsius_ > 18.0) { // Cold outside, heating
        new_dist_mode = AirDistributionMode::WINDSHIELD_AND_FEET; // Prioritize defrost and feet
    } else if (temp_diff > 1.0) { // Cooling needed
        new_dist_mode = AirDistributionMode::FACE_VENTS;
    } else if (temp_diff < -1.0) { // Heating needed
        new_dist_mode = AirDistributionMode::FEET_VENTS; // Heat rises
    } else { // Temp is close to target
        new_dist_mode = AirDistributionMode::FACE_AND_FEET; // Gentle mixed distribution
    }

    if (new_dist_mode != current_air_distribution_) {
        LOG_INFO("ClimateControl: AUTO Air Distribution: Changing from %s to %s.",
                 airDistModeToString(current_air_distribution_), airDistModeToString(new_dist_mode));
        current_air_distribution_ = new_dist_mode;
    }
}

void ClimateControl::manageAutomaticOperation() {
    if (!auto_mode_enabled_) return;

    LOG_INFO("ClimateControl: Managing automatic climate operation. Target: %.1fC, Interior: %.1fC, Exterior: %.1fC.",
             target_temperature_celsius_, current_interior_temperature_celsius_, current_exterior_temperature_celsius_);

    adjustFanForTemperature(); // Internal call
    adjustAirDistributionForMode(); // Internal call

    // AC request in auto mode: on if cooling needed, or for defrost/dehumidify
    if ((current_interior_temperature_celsius_ > target_temperature_celsius_ + 0.5) ||
        current_air_distribution_ == AirDistributionMode::WINDSHIELD_DEFROST ||
        (current_exterior_temperature_celsius_ > 15.0 && current_interior_temperature_celsius_ > 15.0 && recirculation_active_)) { // Dehumidify if recirc on warm/humid days
        if (!ac_requested_by_user_) {
             LOG_INFO("ClimateControl: AUTO mode requesting AC ON.");
        }
        ac_requested_by_user_ = true; // Auto mode wants AC
    } else if (current_interior_temperature_celsius_ < target_temperature_celsius_ - 1.0 &&
               current_air_distribution_ != AirDistributionMode::WINDSHIELD_DEFROST) {
        // If heating significantly and not defrosting, auto mode might turn off AC user request
        if (ac_requested_by_user_) {
            LOG_INFO("ClimateControl: AUTO mode requesting AC OFF (heating phase).");
        }
        ac_requested_by_user_ = false;
    }
    // If fan speed is auto-set to 0, then effectively system is off
    if (fan_speed_level_ == 0) {
        current_air_distribution_ = AirDistributionMode::OFF;
        ac_requested_by_user_ = false; // Turn off AC request if fan is off
        LOG_INFO("ClimateControl: AUTO mode set fan to 0. System effectively OFF.");
    }
}

void ClimateControl::simulateTemperatureChange() {
    // Very simple model:
    // - If AC is on and fan is on, temp moves towards target_temp (cooling)
    // - If heating (target > interior, simulated by fan > 0 and AC off or exterior cold), temp moves towards target_temp (heating)
    // - Otherwise, drifts towards exterior temp or a fixed "engine heat" temp.

    double rate_of_change = 0.0; // degrees C per update cycle

    if (fan_speed_level_ > 0) {
        if (ac_compressor_status_ == ACCompressorStatus::ON_ACTIVE && current_interior_temperature_celsius_ > target_temperature_celsius_) { // Cooling
            rate_of_change = -0.1 * fan_speed_level_; // More fan = faster cooling
            LOG_VERBOSE("ClimateControl: Cooling active. Temp change rate: %.2f C/cycle", rate_of_change);
        } else if (current_interior_temperature_celsius_ < target_temperature_celsius_) { // Heating
            // Assume heater core is always hot if engine is running (not explicitly checked here for temp change)
            rate_of_change = 0.08 * fan_speed_level_; // More fan = faster heating
            LOG_VERBOSE("ClimateControl: Heating active. Temp change rate: %.2f C/cycle", rate_of_change);
        }
    }

    // Natural drift towards exterior temperature (or a slightly higher cabin temp due to sun/engine)
    double natural_drift_target = current_exterior_temperature_celsius_ + 2.0; // Cabin tends to be a bit warmer
    double natural_drift_rate = 0.02;
    if (current_interior_temperature_celsius_ > natural_drift_target) {
        rate_of_change -= natural_drift_rate;
    } else {
        rate_of_change += natural_drift_rate;
    }

    current_interior_temperature_celsius_ += rate_of_change;

    // Clamp to somewhat realistic bounds
    current_interior_temperature_celsius_ = std::max(-10.0, std::min(50.0, current_interior_temperature_celsius_));

    // Simulate sensor noise/small fluctuations for realism
    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_real_distribution<> distr(-0.1, 0.1);
    current_interior_temperature_celsius_ += distr(gen);


    LOG_DEBUG("ClimateControl: Interior temperature simulated to %.1fC. (Rate: %.2f, Ext: %.1fC)",
              current_interior_temperature_celsius_, rate_of_change, current_exterior_temperature_celsius_);
}


void ClimateControl::updateClimateState(const VehicleState& vehicle_state,
                                        const SensorData& interior_temp_sensor,
                                        const SensorData& exterior_temp_sensor) {
    LOG_DEBUG("ClimateControl: Updating climate state. AutoMode: %s, Target: %.1fC, Fan: %d, AC Req: %s",
              auto_mode_enabled_ ? "ON" : "OFF", target_temperature_celsius_, fan_speed_level_, ac_requested_by_user_ ? "ON" : "OFF");

    // Update internal sensor readings (could add smoothing/filtering here)
    current_interior_temperature_celsius_ = interior_temp_sensor.value; // Assuming SensorData.value is temp
    current_exterior_temperature_celsius_ = exterior_temp_sensor.value;
    LOG_VERBOSE("ClimateControl: Received sensor values: Interior=%.1fC, Exterior=%.1fC",
               current_interior_temperature_celsius_, current_exterior_temperature_celsius_);


    bool engine_is_running = (vehicle_state.engine_rpm > 300); // Simple check for engine running
    // Simulate high engine load if RPM is high and speed is increasing (very rough)
    static double last_speed_for_load_calc = vehicle_state.speed_kmh;
    bool high_engine_load = (vehicle_state.engine_rpm > 4000 && vehicle_state.speed_kmh > last_speed_for_load_calc + 5.0);
    last_speed_for_load_calc = vehicle_state.speed_kmh;
    if(high_engine_load) LOG_DEBUG("ClimateControl: Simulated high engine load detected.");


    if (auto_mode_enabled_) {
        manageAutomaticOperation(); // Internal call
    }

    // This call will decide actual compressor state based on requests and conditions
    controlACCompressor(engine_is_running, high_engine_load); // Internal call

    // Simulate the effect of HVAC settings on cabin temperature
    // In a real system, this would be the actual sensor reading evolving over time.
    // Here, we change our "simulated sensor" value.
    if(fan_speed_level_ > 0 || ac_compressor_status_ == ACCompressorStatus::ON_ACTIVE) {
        simulateTemperatureChange(); // Internal call
    } else {
        // Minimal change if system is off, just drift
        double old_temp = current_interior_temperature_celsius_;
        current_interior_temperature_celsius_ += (current_exterior_temperature_celsius_ - current_interior_temperature_celsius_) * 0.01;
        LOG_VERBOSE("ClimateControl: System off, interior temp drifting from %.1f to %.1f (Exterior: %.1f)",
            old_temp, current_interior_temperature_celsius_, current_exterior_temperature_celsius_);
    }

    // Sanity check: if fan is off, distribution should be off.
    if (fan_speed_level_ == 0 && current_air_distribution_ != AirDistributionMode::OFF) {
        LOG_DEBUG("ClimateControl: Fan is off, ensuring air distribution is also OFF.");
        current_air_distribution_ = AirDistributionMode::OFF;
    }


    LOG_INFO("ClimateControl: Update cycle complete. Interior: %.1fC, Fan: %d, AC: %s, Dist: %s",
             current_interior_temperature_celsius_, fan_speed_level_,
             acStatusToString(ac_compressor_status_), airDistModeToString(current_air_distribution_));
}

} // namespace ecu_body_control_module