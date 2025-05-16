// AutoSystemSim/ecu_powertrain_control/fuel_system.cpp
#include "fuel_system.h"
#include <thread> // For std::this_thread::sleep_for
#include <chrono> // For std::chrono::milliseconds

namespace ecu_powertrain_control {

FuelSystem::FuelSystem() : pump_primed_(false), current_fuel_level_(85.0), required_pressure_psi_(45.0) {
    LOG_INFO("FuelSystem: Initializing. Fuel level: %.1f%%", current_fuel_level_);
}

FuelSystem::~FuelSystem() {
    LOG_INFO("FuelSystem: Shutting down.");
}

void FuelSystem::primePump() {
    LOG_DEBUG("FuelSystem: Priming fuel pump...");
    // Simulate some work
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    pump_primed_ = true;
    LOG_INFO("FuelSystem: Fuel pump primed.");
}

bool FuelSystem::checkFuelPressure() {
    LOG_DEBUG("FuelSystem: Checking fuel pressure.");
    if (!pump_primed_) {
        LOG_WARNING("FuelSystem: Fuel pump not primed, cannot check pressure accurately.");
        return false;
    }
    // Simulate pressure check
    double current_pressure = required_pressure_psi_ - (rand() % 5); // Simulate slight variations
    if (current_pressure < required_pressure_psi_ * 0.9) {
        LOG_ERROR("FuelSystem: Low fuel pressure detected: %.2f PSI. Required: %.2f PSI", current_pressure, required_pressure_psi_);
        return false;
    }
    LOG_INFO("FuelSystem: Fuel pressure OK: %.2f PSI", current_pressure);
    return true;
}

bool FuelSystem::injectFuel(int cylinder_id, double amount_ml) {
    LOG_DEBUG("FuelSystem: Attempting to inject %.2f ml of fuel into cylinder %d.", amount_ml, cylinder_id);

    if (current_fuel_level_ <= 0) {
        LOG_ERROR("FuelSystem: Cannot inject fuel. Fuel tank empty!");
        return false;
    }

    if (!pump_primed_) {
        LOG_WARNING("FuelSystem: Fuel pump not primed. Priming now.");
        primePump();
        if (!pump_primed_) { // Check again after priming attempt
             LOG_ERROR("FuelSystem: Failed to prime pump. Cannot inject fuel for cylinder %d.", cylinder_id);
             return false;
        }
    }

    if (!checkFuelPressure()) {
        LOG_ERROR("FuelSystem: Fuel injection aborted for cylinder %d due to low pressure.", cylinder_id);
        return false;
    }

    // Simulate injection
    LOG_INFO("FuelSystem: Injecting %.2f ml fuel into cylinder %d.", amount_ml, cylinder_id);
    simulateFuelConsumption(amount_ml);
    return true;
}

double FuelSystem::getFuelLevel() const {
    LOG_DEBUG("FuelSystem: Current fuel level requested: %.1f%%", current_fuel_level_);
    return current_fuel_level_;
}

void FuelSystem::simulateFuelConsumption(double amount_ml) {
    // Very simplistic consumption model
    double consumption_percentage = (amount_ml / 5000.0) * 100.0; // Assume 50L tank, 1ml is 0.002%
    current_fuel_level_ -= consumption_percentage / 10.0; // Divide by 10 to make it less drastic for small injections
    if (current_fuel_level_ < 0) current_fuel_level_ = 0;
    LOG_VERBOSE("FuelSystem: Fuel consumed. New level: %.2f%%", current_fuel_level_); // Will be ignored by parser
}

} // namespace ecu_powertrain_control