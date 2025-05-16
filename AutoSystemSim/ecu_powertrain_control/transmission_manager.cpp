// AutoSystemSim/ecu_powertrain_control/transmission_manager.cpp
#include "transmission_manager.h"
#include "engine_manager.h" // Include actual header for EngineManager
#include <thread>           // For std::this_thread::sleep_for
#include <chrono>           // For std::chrono::milliseconds
#include <random>           // For simulating issues

namespace ecu_powertrain_control {

TransmissionManager::TransmissionManager(EngineManager* engine_mgr) :
    engine_manager_(engine_mgr),
    current_mode_(TransmissionMode::PARK),
    requested_mode_(TransmissionMode::PARK),
    current_gear_(0), // Park often implies a gear lock, 0 for Neutral/Park
    target_gear_(0),
    max_gears_(6),    // Assuming a 6-speed automatic for Drive mode
    shift_in_progress_(false),
    transmission_oil_temp_celsius_(30.0)
{
    LOG_INFO("TransmissionManager: Initializing. Mode: PARK, Gear: %d", current_gear_);
    if (!engine_manager_) {
        LOG_WARNING("TransmissionManager: EngineManager instance is NULL. Some functionalities might be limited.");
        // This could be a configuration error.
    }
}

TransmissionManager::~TransmissionManager() {
    LOG_INFO("TransmissionManager: Shutting down. Mode: %d, Gear: %d",
             static_cast<int>(current_mode_), current_gear_);
}

bool TransmissionManager::canShiftToMode(TransmissionMode new_mode, const VehicleState& vehicle_state) const {
    LOG_DEBUG("TransmissionManager: Checking if can shift from mode %d to %d. Speed: %.1f km/h",
              static_cast<int>(current_mode_), static_cast<int>(new_mode), vehicle_state.speed_kmh);

    if (current_mode_ == new_mode) {
        LOG_INFO("TransmissionManager: Already in requested mode %d.", static_cast<int>(new_mode));
        return false; // No shift needed
    }

    // Basic safety checks (can be much more complex)
    switch (new_mode) {
        case TransmissionMode::PARK:
            if (vehicle_state.speed_kmh > 2.0) {
                LOG_WARNING("TransmissionManager: Cannot shift to PARK. Vehicle speed %.1f km/h is too high.", vehicle_state.speed_kmh);
                return false;
            }
            // Could also check if brake pedal is pressed
            break;
        case TransmissionMode::REVERSE:
            if (vehicle_state.speed_kmh > 5.0 && current_mode_ != TransmissionMode::NEUTRAL) { // Allow slight roll
                LOG_WARNING("TransmissionManager: Cannot shift to REVERSE. Vehicle speed %.1f km/h is too high.", vehicle_state.speed_kmh);
                return false;
            }
             // Must be from Park or Neutral, or very low speed Drive
            if (current_mode_ == TransmissionMode::DRIVE && vehicle_state.speed_kmh > 1.0) {
                 LOG_WARNING("TransmissionManager: Cannot shift to REVERSE from DRIVE at %.1f km/h.", vehicle_state.speed_kmh);
                 return false;
            }
            break;
        case TransmissionMode::DRIVE:
        case TransmissionMode::SPORT:
            // Can shift from P, R, N.
            // If from R, speed should be very low or zero.
            if (current_mode_ == TransmissionMode::REVERSE && vehicle_state.speed_kmh < -1.0) { // Speed is negative when reversing
                 LOG_WARNING("TransmissionManager: Cannot shift to DRIVE/SPORT from REVERSE at %.1f km/h.", vehicle_state.speed_kmh);
                 return false;
            }
            break;
        case TransmissionMode::NEUTRAL:
            // Usually allowed from P, R, D
            break;
        case TransmissionMode::MANUAL:
            if (current_mode_ != TransmissionMode::DRIVE && current_mode_ != TransmissionMode::SPORT) {
                LOG_WARNING("TransmissionManager: Can only shift to MANUAL from DRIVE or SPORT. Current mode: %d", static_cast<int>(current_mode_));
                return false;
            }
            break;
        default:
            LOG_ERROR("TransmissionManager: Unknown target mode %d for shift check.", static_cast<int>(new_mode));
            return false;
    }
    LOG_INFO("TransmissionManager: Mode shift to %d is permissible.", static_cast<int>(new_mode));
    return true;
}

bool TransmissionManager::setTransmissionMode(TransmissionMode mode) {
    LOG_INFO("TransmissionManager: Request to set transmission mode to %d.", static_cast<int>(mode));
    if (shift_in_progress_) {
        LOG_WARNING("TransmissionManager: Cannot change mode. Gear shift currently in progress.");
        return false;
    }

    // VehicleState would ideally be passed here, or fetched. For now, we assume updateState provides it.
    // We'll use a dummy state for this specific check if not readily available.
    // This is a simplification; in reality, it would use fresh sensor data.
    VehicleState temp_state; // DUMMY state for canShiftToMode if not updated recently
    temp_state.speed_kmh = engine_manager_ ? (engine_manager_->getEngineState().speed_kmh) : 0.0; // Get speed if possible

    if (!canShiftToMode(mode, temp_state)) { // Pass a representation of current vehicle state
        LOG_ERROR("TransmissionManager: Mode change to %d denied by safety checks.", static_cast<int>(mode));
        return false;
    }

    requested_mode_ = mode;
    LOG_INFO("TransmissionManager: Transmission mode change to %d initiated. Will apply on next update cycle.", static_cast<int>(requested_mode_));
    // Actual mode change and gear selection happens in updateState or a dedicated shift logic function.
    // For simplicity, we can make it more direct here if we assume updateState is called very frequently.

    // Direct mode change for this simulation:
    TransmissionMode old_mode = current_mode_;
    current_mode_ = requested_mode_;
    LOG_INFO("TransmissionManager: Mode changed from %d to %d.", static_cast<int>(old_mode), static_cast<int>(current_mode_));

    switch (current_mode_) {
        case TransmissionMode::PARK:
            current_gear_ = 0; // Or a specific Park gear representation
            LOG_INFO("TransmissionManager: Engaged PARK.");
            break;
        case TransmissionMode::REVERSE:
            current_gear_ = -1; // Represent reverse gear
            LOG_INFO("TransmissionManager: Engaged REVERSE (Gear %d).", current_gear_);
            break;
        case TransmissionMode::NEUTRAL:
            current_gear_ = 0;
            LOG_INFO("TransmissionManager: Engaged NEUTRAL (Gear %d).", current_gear_);
            break;
        case TransmissionMode::DRIVE:
        case TransmissionMode::SPORT:
            current_gear_ = 1; // Start in 1st gear
            LOG_INFO("TransmissionManager: Engaged %s mode, starting in Gear %d.",
                     (current_mode_ == TransmissionMode::DRIVE ? "DRIVE" : "SPORT"), current_gear_);
            break;
        case TransmissionMode::MANUAL:
            // Keep current gear if shifting from D/S, or default to 1 if from P/N (though canShiftToMode prevents P/N->M)
            if (old_mode != TransmissionMode::DRIVE && old_mode != TransmissionMode::SPORT) {
                current_gear_ = 1;
            }
            LOG_INFO("TransmissionManager: Engaged MANUAL mode, current Gear %d.", current_gear_);
            break;
    }
    return true;
}

GearShiftQuality TransmissionManager::performGearShift(int to_gear) {
    LOG_INFO("TransmissionManager: Attempting to shift from gear %d to gear %d.", current_gear_, to_gear);
    shift_in_progress_ = true;
    target_gear_ = to_gear;

    // Simulate interaction with EngineManager for torque reduction (conceptual)
    if (engine_manager_) {
        LOG_DEBUG("TransmissionManager: Requesting torque reduction from EngineManager for gear shift.");
        // bool torque_reduced = engine_manager_->requestTorqueReduction(50); // Example call
        // if (!torque_reduced) {
        //     LOG_WARNING("TransmissionManager: EngineManager denied torque reduction. Shift might be rough.");
        // }
    } else {
        LOG_WARNING("TransmissionManager: EngineManager not available. Cannot request torque reduction for shift.");
    }

    // Simulate shift time and potential issues
    LOG_DEBUG("TransmissionManager: Shifting to gear %d...", target_gear_);
    std::this_thread::sleep_for(std::chrono::milliseconds(100 + (rand() % 200))); // Simulate 100-300ms shift time

    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_int_distribution<> distrib(1, 100);
    int shift_outcome = distrib(gen);

    GearShiftQuality quality;
    if (shift_outcome <= 80) { // 80% chance of smooth shift
        current_gear_ = target_gear_;
        quality = GearShiftQuality::SMOOTH;
        LOG_INFO("TransmissionManager: Shift to gear %d successful and SMOOTH.", current_gear_);
    } else if (shift_outcome <= 95) { // 15% chance of acceptable/rough shift
        current_gear_ = target_gear_;
        quality = (shift_outcome <= 90 ? GearShiftQuality::ACCEPTABLE : GearShiftQuality::ROUGH);
        LOG_WARNING("TransmissionManager: Shift to gear %d complete but %s.",
                    current_gear_, (quality == GearShiftQuality::ACCEPTABLE ? "ACCEPTABLE" : "ROUGH"));
    } else { // 5% chance of failed shift
        quality = GearShiftQuality::FAILED_SHIFT;
        LOG_ERROR("TransmissionManager: FAILED to shift to gear %d! Staying in gear %d.", target_gear_, current_gear_);
        // Could trigger a fault state or limp mode
        if (engine_manager_){
            // engine_manager_->reportSystemFault("TRANSMISSION_SHIFT_FAILURE", SystemError::Severity::CRITICAL);
        }
    }

    // Simulate releasing torque reduction request
    if (engine_manager_) {
        LOG_DEBUG("TransmissionManager: Signaling EngineManager to restore torque.");
        // engine_manager_->restoreNormalTorque();
    }

    shift_in_progress_ = false;
    target_gear_ = 0; // Reset target
    return quality;
}

bool TransmissionManager::canShiftGear(bool up_shift, const VehicleState& vehicle_state) const {
    if (current_mode_ != TransmissionMode::DRIVE &&
        current_mode_ != TransmissionMode::SPORT &&
        current_mode_ != TransmissionMode::MANUAL) {
        LOG_WARNING("TransmissionManager: Cannot shift gears. Not in DRIVE, SPORT, or MANUAL mode. Current mode: %d",
                    static_cast<int>(current_mode_));
        return false;
    }

    int next_gear = current_gear_ + (up_shift ? 1 : -1);

    if (up_shift) {
        if (current_gear_ >= max_gears_) {
            LOG_INFO("TransmissionManager: Already in highest gear (%d). Cannot upshift.", current_gear_);
            return false;
        }
        // Add RPM/speed based upshift constraints if needed (e.g., engine RPM too low)
        if (engine_manager_ && engine_manager_->getCurrentRPM() < 1500 && current_gear_ > 1) { // Simplified
            // LOG_WARNING("TransmissionManager: Cannot upshift. Engine RPM (%d) too low for next gear.", engine_manager_->getCurrentRPM());
            // return false;
        }
    } else { // Downshift
        if (current_gear_ <= 1) {
            LOG_INFO("TransmissionManager: Already in lowest gear (1). Cannot downshift further.", current_gear_);
            return false;
        }
        // Add RPM/speed based downshift constraints (e.g., prevent over-revving)
        if (engine_manager_ && engine_manager_->getCurrentRPM() > 5000 && next_gear > 0) { // Simplified over-rev protection
            // LOG_WARNING("TransmissionManager: Cannot downshift. Potential over-rev at %d RPM into gear %d.", engine_manager_->getCurrentRPM(), next_gear);
            // return false;
        }
        // Prevent downshift if speed is too low for the target gear (bogging the engine)
        // Example: if speed < 20 km/h, don't allow downshift from 3rd to 2nd if 2nd is too "tall"
    }
    LOG_DEBUG("TransmissionManager: Gear shift to %d is permissible.", next_gear);
    return true;
}

bool TransmissionManager::shiftUp() {
    LOG_INFO("TransmissionManager: Request to SHIFT UP from gear %d.", current_gear_);
    if (shift_in_progress_) {
        LOG_WARNING("TransmissionManager: Cannot SHIFT UP. Another shift is already in progress.");
        return false;
    }
    if (current_mode_ != TransmissionMode::MANUAL && current_mode_ != TransmissionMode::SPORT) {
        LOG_WARNING("TransmissionManager: Manual SHIFT UP ignored. Not in MANUAL or SPORT mode.");
        return false;
    }

    VehicleState temp_state; // Dummy state for canShiftGear
    if (engine_manager_) temp_state.speed_kmh = engine_manager_->getEngineState().speed_kmh;

    if (canShiftGear(true, temp_state)) {
        performGearShift(current_gear_ + 1);
        return true;
    }
    return false;
}

bool TransmissionManager::shiftDown() {
    LOG_INFO("TransmissionManager: Request to SHIFT DOWN from gear %d.", current_gear_);
    if (shift_in_progress_) {
        LOG_WARNING("TransmissionManager: Cannot SHIFT DOWN. Another shift is already in progress.");
        return false;
    }
    if (current_mode_ != TransmissionMode::MANUAL && current_mode_ != TransmissionMode::SPORT) {
        LOG_WARNING("TransmissionManager: Manual SHIFT DOWN ignored. Not in MANUAL or SPORT mode.");
        return false;
    }

    VehicleState temp_state; // Dummy state for canShiftGear
    if (engine_manager_) temp_state.speed_kmh = engine_manager_->getEngineState().speed_kmh;

    if (canShiftGear(false, temp_state)) {
        performGearShift(current_gear_ - 1);
        return true;
    }
    return false;
}

bool TransmissionManager::requestNeutral() {
    LOG_INFO("TransmissionManager: Neutral requested explicitly.");
    VehicleState temp_state;
    if (engine_manager_) temp_state.speed_kmh = engine_manager_->getEngineState().speed_kmh;

    if (canShiftToMode(TransmissionMode::NEUTRAL, temp_state)) {
        return setTransmissionMode(TransmissionMode::NEUTRAL);
    }
    LOG_WARNING("TransmissionManager: Explicit request for NEUTRAL denied by safety checks.");
    return false;
}

TransmissionMode TransmissionManager::getCurrentMode() const {
    LOG_DEBUG("TransmissionManager: getCurrentMode() called. Mode: %d", static_cast<int>(current_mode_));
    return current_mode_;
}

int TransmissionManager::getCurrentGear() const {
    LOG_DEBUG("TransmissionManager: getCurrentGear() called. Gear: %d", current_gear_);
    return current_gear_;
}

bool TransmissionManager::isShiftInProgress() const {
    return shift_in_progress_;
}

void TransmissionManager::manageAutomaticShifting(const VehicleState& vehicle_state, int engine_rpm) {
    if (shift_in_progress_ || (current_mode_ != TransmissionMode::DRIVE && current_mode_ != TransmissionMode::SPORT)) {
        return; // No auto shifting if manual, park, reverse, neutral, or shift already happening
    }

    LOG_DEBUG("TransmissionManager: Auto-shift logic. Speed: %.1f km/h, RPM: %d, Gear: %d, Mode: %d",
              vehicle_state.speed_kmh, engine_rpm, current_gear_, static_cast<int>(current_mode_));

    // Simplified shift points (can be very complex with maps, load, throttle position, etc.)
    // Upshift logic
    int upshift_rpm = (current_mode_ == TransmissionMode::SPORT) ? 3500 : 2500;
    if (engine_rpm > upshift_rpm && current_gear_ < max_gears_) {
        if (canShiftGear(true, vehicle_state)) {
            LOG_INFO("TransmissionManager: Auto UP-SHIFTING from %d. RPM: %d, Speed: %.1f km/h",
                     current_gear_, engine_rpm, vehicle_state.speed_kmh);
            performGearShift(current_gear_ + 1);
        }
    }
    // Downshift logic (e.g., for acceleration demand or slowing down)
    int downshift_rpm = (current_mode_ == TransmissionMode::SPORT) ? 1500 : 1000;
    if (engine_rpm < downshift_rpm && current_gear_ > 1) {
         // Also consider vehicle speed to prevent downshifting too early when coasting
        if (vehicle_state.speed_kmh > (current_gear_ - 1) * 15.0 ) { // Arbitrary speed threshold per gear
            if (canShiftGear(false, vehicle_state)) {
                LOG_INFO("TransmissionManager: Auto DOWN-SHIFTING from %d. RPM: %d, Speed: %.1f km/h",
                         current_gear_, engine_rpm, vehicle_state.speed_kmh);
                performGearShift(current_gear_ - 1);
            }
        } else {
            LOG_DEBUG("TransmissionManager: RPM low for downshift, but speed %.1f km/h is also low for current gear %d. Holding gear.",
                vehicle_state.speed_kmh, current_gear_);
        }
    }
    // Add kickdown logic (if throttle > 90% and RPM not too high, downshift for power)
}

void TransmissionManager::updateTransmissionTemperature(const VehicleState& vehicle_state) {
    // Simulate temperature changes based on load/speed
    double temp_increase = 0.0;
    if (current_gear_ != 0) { // If in gear
        temp_increase = (vehicle_state.speed_kmh / 100.0) * 0.1; // Higher speed = more heat
        if (shift_in_progress_) temp_increase += 0.2; // Shifting generates heat
    }
    double temp_decrease = 0.05; // Natural cooling

    transmission_oil_temp_celsius_ += temp_increase - temp_decrease;

    if (transmission_oil_temp_celsius_ < 20.0) transmission_oil_temp_celsius_ = 20.0; // Min temp

    if (transmission_oil_temp_celsius_ > 120.0) {
        LOG_WARNING("TransmissionManager: Oil temperature HIGH: %.1f C", transmission_oil_temp_celsius_);
        if (transmission_oil_temp_celsius_ > 135.0) {
            LOG_ERROR("TransmissionManager: Oil temperature CRITICAL: %.1f C! Risk of damage. Limiting performance.", transmission_oil_temp_celsius_);
            // Implement limp mode: e.g., restrict shifting, force neutral if stopped.
            // For now, just log.
            if (engine_manager_) {
                // engine_manager_->reportSystemFault("TRANSMISSION_OVERHEAT", SystemError::Severity::CRITICAL);
            }
        }
    } else if (transmission_oil_temp_celsius_ > 90.0 && static_cast<int>(transmission_oil_temp_celsius_) % 5 == 0) { // Log every 5 degrees once hot
        LOG_INFO("TransmissionManager: Oil temperature elevated: %.1f C", transmission_oil_temp_celsius_);
    } else {
        LOG_VERBOSE("TransmissionManager: Oil temperature: %.1f C", transmission_oil_temp_celsius_);
    }
}


void TransmissionManager::checkTransmissionHealth() {
    LOG_DEBUG("TransmissionManager: Performing transmission health check.");
    // Simulate checking for slip, pressure issues, sensor faults etc.
    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_int_distribution<> distrib(1, 200); // Lower chance for faults than bulb check

    if (distrib(gen) == 1) { // 0.5% chance of a new fault
        int fault_code = 300 + (rand() % 50);
        LOG_ERROR("TransmissionManager: DIAGNOSTIC FAULT DETECTED! Code: DTC_TRN_%03d. Possible clutch slip or solenoid issue.", fault_code);
        // Could set an internal fault flag, notify main controller.
        if (engine_manager_){
            // engine_manager_->reportSystemFault("TRANSMISSION_INTERNAL_FAULT_" + std::to_string(fault_code), SystemError::Severity::WARNING);
        }
    } else {
        LOG_VERBOSE("TransmissionManager: Transmission health check OK.");
    }
}


void TransmissionManager::updateState(const VehicleState& vehicle_state, int engine_rpm) {
    LOG_DEBUG("TransmissionManager: Updating state. Mode: %d, Gear: %d, Speed: %.1f km/h, RPM: %d",
              static_cast<int>(current_mode_), current_gear_, vehicle_state.speed_kmh, engine_rpm);

    // Handle pending mode change request
    if (requested_mode_ != current_mode_) {
        LOG_INFO("TransmissionManager: Processing pending mode change from %d to %d.",
                 static_cast<int>(current_mode_), static_cast<int>(requested_mode_));
        // This is simplified; a real system might have a state machine for mode transitions.
        // The direct mode change was already done in setTransmissionMode for this simulation.
        // Here we could log post-conditions or final checks.
    }

    // Automatic gear shifting logic
    if (!shift_in_progress_) { // Don't try to auto-shift if a manual/commanded shift is happening
        manageAutomaticShifting(vehicle_state, engine_rpm); // Internal call
    } else {
        LOG_DEBUG("TransmissionManager: Skipping auto-shift logic as a shift is already in progress.");
    }

    updateTransmissionTemperature(vehicle_state); // Internal call

    // Periodic health check
    static int update_cycle_count = 0;
    if (++update_cycle_count % 15 == 0) { // Run health check less frequently
        checkTransmissionHealth(); // Internal call
    }

    LOG_DEBUG("TransmissionManager: State update cycle complete.");
}

} // namespace ecu_powertrain_control
