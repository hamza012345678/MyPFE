// AutoSystemSim/ecu_power_management/power_monitor.cpp
#include "power_monitor.h"
#include <thread>   // For std::this_thread::sleep_for
#include <chrono>   // For std::chrono::milliseconds
#include <random>   // For simulating voltage fluctuations

namespace ecu_power_management {

PowerMonitor::PowerMonitor() :
    current_battery_voltage_V_(12.6), // Nominal fully charged battery
    system_stable_(true),
    critical_load_events_count_(0)
{
    LOG_INFO("PowerMonitor: Initializing. Battery Voltage: %.2fV. System Stable: %s",
             current_battery_voltage_V_, system_stable_ ? "true" : "false");
}

PowerMonitor::~PowerMonitor() {
    LOG_INFO("PowerMonitor: Shutting down. Final Battery Voltage: %.2fV", current_battery_voltage_V_);
}

bool PowerMonitor::isPowerStable() const {
    // This function is called by EngineManager, so it's an important inter-ECU communication point.
    LOG_DEBUG("PowerMonitor: isPowerStable() called. Current stability: %s", system_stable_ ? "true" : "false");
    if (!system_stable_) {
        LOG_WARNING("PowerMonitor: Reporting system power as UNSTABLE.");
    }
    return system_stable_;
}

double PowerMonitor::getBatteryVoltage() const {
    LOG_DEBUG("PowerMonitor: getBatteryVoltage() called. Voltage: %.2fV", current_battery_voltage_V_);
    return current_battery_voltage_V_;
}

void PowerMonitor::checkVoltageLevels() {
    LOG_DEBUG("PowerMonitor: Checking voltage levels. Current: %.2fV", current_battery_voltage_V_);

    // Simulate slight discharge or charging based on "engine running" (not explicitly modeled here, so random)
    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_real_distribution<> distr(-0.1, 0.05); // Slight fluctuation
    current_battery_voltage_V_ += distr(gen);

    // Clamp voltage to realistic limits
    if (current_battery_voltage_V_ > 14.8) current_battery_voltage_V_ = 14.8; // Alternator charging max
    if (current_battery_voltage_V_ < 9.0) current_battery_voltage_V_ = 9.0;   // Deep discharge

    if (current_battery_voltage_V_ < 10.5) {
        LOG_WARNING("PowerMonitor: Battery voltage critically low: %.2fV!", current_battery_voltage_V_);
        system_stable_ = false; // Low voltage makes the system unstable
    } else if (current_battery_voltage_V_ < 11.8) {
        LOG_INFO("PowerMonitor: Battery voltage low: %.2fV. Consider charging.", current_battery_voltage_V_);
        system_stable_ = true; // May still be stable but needs attention
    } else {
        LOG_VERBOSE("PowerMonitor: Battery voltage nominal: %.2fV", current_battery_voltage_V_);
        // system_stable_ remains true unless other factors make it unstable
    }
}

void PowerMonitor::assessSystemStability() {
    LOG_DEBUG("PowerMonitor: Assessing overall system stability.");
    // Stability can be affected by more than just voltage (e.g., shorts, high current draw - simplified here)
    if (critical_load_events_count_ > 2) {
        LOG_ERROR("PowerMonitor: Multiple consecutive high load events detected. System declared UNSTABLE.");
        system_stable_ = false;
    } else if (current_battery_voltage_V_ < 10.5) {
        // Already handled by checkVoltageLevels, but good to re-iterate
        LOG_WARNING("PowerMonitor: System unstable due to critically low voltage (%.2fV).", current_battery_voltage_V_);
        system_stable_ = false;
    } else {
        // If no critical conditions, assume stable for now
        if (!system_stable_ && current_battery_voltage_V_ >= 11.8) {
             LOG_INFO("PowerMonitor: System stability RESTORED. Voltage: %.2fV", current_battery_voltage_V_);
        }
        system_stable_ = true;
    }
}

void PowerMonitor::updatePowerStatus() {
    LOG_INFO("PowerMonitor: Updating power status cycle.");
    checkVoltageLevels(); // Internal call

    // Simulate other checks
    std::this_thread::sleep_for(std::chrono::milliseconds(30));
    LOG_DEBUG("PowerMonitor: Performing peripheral power checks (simulated).");

    assessSystemStability(); // Internal call

    if (system_stable_) {
        LOG_INFO("PowerMonitor: Power status update complete. System is STABLE. Voltage: %.2fV", current_battery_voltage_V_);
    } else {
        LOG_WARNING("PowerMonitor: Power status update complete. System is UNSTABLE. Voltage: %.2fV", current_battery_voltage_V_);
    }
}

void PowerMonitor::simulateHighLoadEvent(bool start_event) {
    if (start_event) {
        LOG_WARNING("PowerMonitor: High electrical load event STARTED (e.g., AC compressor, multiple window motors).");
        current_battery_voltage_V_ -= 0.5; // Simulate voltage drop
        critical_load_events_count_++;
        checkVoltageLevels(); // Re-check voltage immediately
        assessSystemStability();
        if (!system_stable_) {
            LOG_ERROR("PowerMonitor: System became UNSTABLE during high load event!");
        }
    } else {
        LOG_INFO("PowerMonitor: High electrical load event ENDED.");
        // Voltage might recover slightly
        current_battery_voltage_V_ += 0.2;
        critical_load_events_count_ = std::max(0, critical_load_events_count_ -1); // Decrease count, but not below 0
        checkVoltageLevels();
        assessSystemStability();
    }
}

} // namespace ecu_power_management