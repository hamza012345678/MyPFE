// AutoSystemSim/ecu_body_control_module/window_control.cpp
#include "window_control.h"
#include "../ecu_power_management/power_monitor.h" // Actual include
#include <algorithm> // For std::find_if
#include <cmath>     // For fabs
#include <random>    // For simulating anti-pinch

namespace ecu_body_control_module {

// Helper to convert WindowPosition enum to string for logging
const char* windowPosToString(WindowPosition pos) {
    switch (pos) {
        case WindowPosition::FULLY_CLOSED: return "FULLY_CLOSED";
        case WindowPosition::SLIGHTLY_OPEN: return "SLIGHTLY_OPEN";
        case WindowPosition::HALF_OPEN: return "HALF_OPEN";
        case WindowPosition::FULLY_OPEN: return "FULLY_OPEN";
        case WindowPosition::MOVING_UP: return "MOVING_UP";
        case WindowPosition::MOVING_DOWN: return "MOVING_DOWN";
        case WindowPosition::OBSTRUCTION_DETECTED: return "OBSTRUCTION_DETECTED";
        default: return "UNKNOWN_POSITION";
    }
}
// Helper to convert WindowID enum to string for logging
const char* windowIdToString(WindowID id) {
    switch(id) {
        case WindowID::FRONT_LEFT: return "FRONT_LEFT";
        case WindowID::FRONT_RIGHT: return "FRONT_RIGHT";
        case WindowID::REAR_LEFT: return "REAR_LEFT";
        case WindowID::REAR_RIGHT: return "REAR_RIGHT";
        case WindowID::SUNROOF: return "SUNROOF";
        default: return "UNKNOWN_WINDOW_ID";
    }
}


WindowControl::WindowControl(ecu_power_management::PowerMonitor* pm) :
    power_monitor_(pm), all_windows_locked_by_driver_(false)
{
    LOG_INFO("WindowControl: Initializing...");
    if (!power_monitor_) {
        LOG_WARNING("WindowControl: PowerMonitor service is NULL. Window operations might be affected by power availability.");
    }

    windows_.emplace_back(WindowID::FRONT_LEFT);
    windows_.emplace_back(WindowID::FRONT_RIGHT);
    windows_.emplace_back(WindowID::REAR_LEFT);
    windows_.emplace_back(WindowID::REAR_RIGHT);
    // windows_.emplace_back(WindowID::SUNROOF); // Optionally add sunroof

    LOG_INFO("WindowControl: Initialization complete. %zu windows registered.", windows_.size());
}

WindowControl::~WindowControl() {
    LOG_INFO("WindowControl: Shutting down.");
}

SingleWindowState* WindowControl::findWindow(WindowID id) {
    auto it = std::find_if(windows_.begin(), windows_.end(),
                           [id](const SingleWindowState& ws){ return ws.id == id; });
    if (it != windows_.end()) {
        return &(*it);
    }
    LOG_WARNING("WindowControl: findWindow: WindowID %s not found.", windowIdToString(id));
    return nullptr;
}

const SingleWindowState* WindowControl::findWindow(WindowID id) const {
    auto it = std::find_if(windows_.begin(), windows_.end(),
                           [id](const SingleWindowState& ws){ return ws.id == id; });
    if (it != windows_.end()) {
        return &(*it);
    }
    LOG_WARNING("WindowControl: findWindow (const): WindowID %s not found.", windowIdToString(id));
    return nullptr;
}

bool WindowControl::canOperateWindow(const SingleWindowState& window, bool opening_direction) const {
    if (window.id != WindowID::FRONT_LEFT && all_windows_locked_by_driver_) { // Assuming FRONT_LEFT is driver
        LOG_INFO("WindowControl: Operation for window %s denied by driver master lock.", windowIdToString(window.id));
        return false;
    }
    // Child lock check (simplified: assume child lock only affects specific windows)
    // A real system would have individual child lock states. Here, we don't store them yet.
    // if ((window.id == WindowID::REAR_LEFT || window.id == WindowID::REAR_RIGHT) && window.child_locked) {
    //     LOG_INFO("WindowControl: Operation for window %s denied by child lock.", windowIdToString(window.id));
    //     return false;
    // }

    if (power_monitor_ && !power_monitor_->isPowerStable()) {
        LOG_WARNING("WindowControl: Power system unstable. Window %s operation might be slow or denied.", windowIdToString(window.id));
        if (power_monitor_->getBatteryVoltage() < 10.5) { // Inter-ECU call
            LOG_ERROR("WindowControl: Battery too low (%.2fV) to operate window %s.",
                      power_monitor_->getBatteryVoltage(), windowIdToString(window.id));
            return false;
        }
    }
    // Speed inhibit (e.g., sunroof above certain speed)
    // if (window.id == WindowID::SUNROOF && vehicle_state.speed_kmh > 80.0 && opening_direction) {
    //     LOG_INFO("WindowControl: Sunroof %s operation denied due to high speed (%.1f km/h).",
    //              windowIdToString(window.id), vehicle_state.speed_kmh);
    //     return false;
    // }
    return true;
}


bool WindowControl::moveWindow(WindowID id, double target_position_percent, bool one_touch) {
    LOG_INFO("WindowControl: Request to move window %s to %.0f%%, one-touch: %s.",
             windowIdToString(id), target_position_percent * 100.0, one_touch ? "YES" : "NO");

    SingleWindowState* window = findWindow(id);
    if (!window) {
        return false; // Error already logged by findWindow
    }

    // Convert percent to WindowPosition enum (simplified mapping)
    WindowPosition target_enum_pos;
    if (target_position_percent <= 0.01) target_enum_pos = WindowPosition::FULLY_CLOSED;
    else if (target_position_percent >= 0.99) target_enum_pos = WindowPosition::FULLY_OPEN;
    else if (target_position_percent < 0.5) target_enum_pos = WindowPosition::SLIGHTLY_OPEN; // or map more granularly
    else target_enum_pos = WindowPosition::HALF_OPEN; // or map more granularly


    bool opening = (target_enum_pos > window->current_pos); // Simplified comparison
    if (target_enum_pos == WindowPosition::FULLY_CLOSED && window->current_pos != WindowPosition::FULLY_CLOSED) opening = false;
    if (target_enum_pos == WindowPosition::FULLY_OPEN && window->current_pos != WindowPosition::FULLY_OPEN) opening = true;


    if (!canOperateWindow(*window, opening)) {
        LOG_WARNING("WindowControl: Move request for window %s denied by operational checks.", windowIdToString(id));
        return false;
    }

    if (window->motor_active && window->target_pos == target_enum_pos) {
        LOG_DEBUG("WindowControl: Window %s already moving towards target %s.",
                  windowIdToString(id), windowPosToString(target_enum_pos));
        return true;
    }

    window->target_pos = target_enum_pos;
    window->motor_active = true;
    window->current_pos = (opening) ? WindowPosition::MOVING_DOWN : WindowPosition::MOVING_UP;
    window->obstruction_counter = 0; // Reset obstruction counter on new deliberate move

    LOG_INFO("WindowControl: Window %s motor activated. Current: %s, Target: %s.",
             windowIdToString(id), windowPosToString(window->current_pos), windowPosToString(window->target_pos));

    // For non-one-touch, the motor would only be active while the button is pressed.
    // Our updateWindowStates will handle the movement for one-touch.
    // If !one_touch, we'd need a separate "button released" signal to set motor_active = false.
    // We simplify and assume one_touch is the primary mode for this dummy.
    if (!one_touch) {
        LOG_DEBUG("WindowControl: Non-one-touch for %s. Motor will stop if not periodically re-commanded (simulated).", windowIdToString(id));
        // In a real system, this would require continuous command or timeout.
        // For this simulation, we'll let updateWindowStates handle it, but a real system would differ.
    }

    return true;
}

bool WindowControl::stopWindowMovement(WindowID id) {
    LOG_INFO("WindowControl: Request to STOP movement for window %s.", windowIdToString(id));
    SingleWindowState* window = findWindow(id);
    if (!window) {
        return false;
    }

    if (!window->motor_active) {
        LOG_DEBUG("WindowControl: Window %s motor is not active. No action needed for stop.", windowIdToString(id));
        return true;
    }

    window->motor_active = false;
    // Current position becomes whatever it was when stopped (more complex to track precise % here)
    // We'll let updateWindowStates set a more definitive current_pos when motor stops.
    LOG_INFO("WindowControl: Window %s motor DEACTIVATED by stop request.", windowIdToString(id));
    return true;
}

bool WindowControl::setChildLock(WindowID id, bool locked) {
    // This is a simplified placeholder. A real system would store child lock state per window.
    LOG_INFO("WindowControl: Child lock for window %s set to %s (SIMULATED - not fully implemented).",
             windowIdToString(id), locked ? "LOCKED" : "UNLOCKED");
    if (id == WindowID::REAR_LEFT || id == WindowID::REAR_RIGHT || id == WindowID::SUNROOF) {
        // findWindow(id)->child_locked = locked; // If we had such a member
        return true;
    }
    LOG_WARNING("WindowControl: Child lock typically applies to rear windows or sunroof. Ignored for %s.", windowIdToString(id));
    return false;
}

bool WindowControl::setAllWindowsLock(bool locked) {
    LOG_INFO("WindowControl: Driver master window lock set to %s.", locked ? "LOCKED" : "UNLOCKED");
    all_windows_locked_by_driver_ = locked;
    if (locked) {
        LOG_DEBUG("WindowControl: All passenger windows (except driver) are now locked from local operation.");
        // Optionally, stop any ongoing movement of locked windows
        for (auto& win : windows_) {
            if (win.id != WindowID::FRONT_LEFT && win.motor_active) { // Assuming FRONT_LEFT is driver
                LOG_INFO("WindowControl: Stopping passenger window %s due to master lock activation.", windowIdToString(win.id));
                stopWindowMovement(win.id);
            }
        }
    }
    return true;
}

WindowPosition WindowControl::getWindowPosition(WindowID id) const {
    const SingleWindowState* window = findWindow(id);
    if (window) {
        LOG_DEBUG("WindowControl: Position of window %s is %s.", windowIdToString(id), windowPosToString(window->current_pos));
        return window->current_pos;
    }
    LOG_WARNING("WindowControl: getWindowPosition: Could not find window %s. Reporting FULLY_CLOSED.", windowIdToString(id));
    return WindowPosition::FULLY_CLOSED;
}

bool WindowControl::checkAntiPinch(SingleWindowState& window) {
    // Simplified: 10% chance of obstruction if moving up and not fully closed
    if (window.current_pos == WindowPosition::MOVING_UP && window.target_pos != WindowPosition::FULLY_OPEN) {
        std::random_device rd;
        std::mt19937 gen(rd());
        std::uniform_int_distribution<> distrib(1, 100);
        if (distrib(gen) <= 10) { // 10% chance
            LOG_WARNING("WindowControl: ANTI-PINCH DETECTED for window %s! Reversing direction.", windowIdToString(window.id));
            window.current_pos = WindowPosition::OBSTRUCTION_DETECTED;
            window.motor_active = true; // Keep motor active for reversal
            // Reverse direction: set target to slightly more open than current (approximate)
            // This logic is very simplified. A real system uses motor current/encoder.
            window.target_pos = WindowPosition::HALF_OPEN; // Or some other "safe" open position
            window.obstruction_counter++;

            if (window.obstruction_counter >= 3) {
                LOG_ERROR("WindowControl: Window %s has detected obstruction %d times. Disabling one-touch for safety.",
                          windowIdToString(window.id), window.obstruction_counter);
                // Disable one-touch, require manual hold (not fully simulated here)
                window.motor_active = false; // Stop movement after multiple attempts
            }
            return true; // Obstruction detected
        }
    }
    return false; // No obstruction
}

void WindowControl::simulateMotorMovement(SingleWindowState& window, double step_change_percent) {
    // This is highly simplified. Real windows have precise position sensors (encoders).
    // We are using discrete states. A step change would move it towards the next discrete state.
    WindowPosition old_discrete_pos = window.current_pos;

    if (window.current_pos == WindowPosition::MOVING_UP) {
        if (window.target_pos == WindowPosition::FULLY_CLOSED) window.current_pos = WindowPosition::FULLY_CLOSED;
        else if (window.target_pos == WindowPosition::SLIGHTLY_OPEN) window.current_pos = WindowPosition::SLIGHTLY_OPEN;
        // ... more granularity needed for smooth transition via discrete states
        else { // Default catch-all if moving up towards something not fully closed
            window.current_pos = WindowPosition::SLIGHTLY_OPEN; // Or some intermediate if target is HALF_OPEN etc.
        }
        // If it was moving up and reached target (or passed it), set to target.
        if (window.current_pos == window.target_pos || window.current_pos == WindowPosition::FULLY_CLOSED && window.target_pos != WindowPosition::FULLY_OPEN) {
             window.current_pos = window.target_pos; // Snap to target
             window.motor_active = false;
             LOG_INFO("WindowControl: Window %s reached target %s (was MOVING_UP). Motor stopped.",
                      windowIdToString(window.id), windowPosToString(window.current_pos));
        }
    } else if (window.current_pos == WindowPosition::MOVING_DOWN) {
        if (window.target_pos == WindowPosition::FULLY_OPEN) window.current_pos = WindowPosition::FULLY_OPEN;
        else if (window.target_pos == WindowPosition::HALF_OPEN) window.current_pos = WindowPosition::HALF_OPEN;
        // ...
        else {
             window.current_pos = WindowPosition::HALF_OPEN;
        }
        if (window.current_pos == window.target_pos || window.current_pos == WindowPosition::FULLY_OPEN && window.target_pos != WindowPosition::FULLY_CLOSED) {
             window.current_pos = window.target_pos;
             window.motor_active = false;
             LOG_INFO("WindowControl: Window %s reached target %s (was MOVING_DOWN). Motor stopped.",
                      windowIdToString(window.id), windowPosToString(window.current_pos));
        }
    } else if (window.current_pos == WindowPosition::OBSTRUCTION_DETECTED) {
        // Reversing due to anti-pinch. Let's say it reverses to HALF_OPEN.
        window.current_pos = WindowPosition::MOVING_DOWN; // Now moving down
        window.target_pos = WindowPosition::HALF_OPEN; // New target after obstruction
        LOG_DEBUG("WindowControl: Window %s reversing due to obstruction. New target: HALF_OPEN.", windowIdToString(window.id));
        // The next call to simulateMotorMovement will handle the MOVING_DOWN state.
    }

    if (old_discrete_pos != window.current_pos && window.motor_active) {
        LOG_DEBUG("WindowControl: Window %s moved from %s to %s. Target: %s",
                  windowIdToString(window.id), windowPosToString(old_discrete_pos),
                  windowPosToString(window.current_pos), windowPosToString(window.target_pos));
    }
}


void WindowControl::updateWindowStates() {
    LOG_DEBUG("WindowControl: Updating all window states...");
    bool any_motor_active_start = false;
    for(const auto& win : windows_) if(win.motor_active) any_motor_active_start = true;

    if (power_monitor_ && any_motor_active_start) {
        // Simulate high load on power system if any window motor is active
        power_monitor_->simulateHighLoadEvent(true); // Call to PowerMonitor
        LOG_INFO("WindowControl: Signaled high power load to PowerMonitor due to active window motor(s).");
    }

    for (SingleWindowState& window : windows_) {
        if (!window.motor_active) {
            continue; // Skip if motor isn't running for this window
        }

        LOG_DEBUG("WindowControl: Updating active window %s. Current: %s, Target: %s",
                  windowIdToString(window.id), windowPosToString(window.current_pos), windowPosToString(window.target_pos));

        // Check anti-pinch first if moving up
        if (window.current_pos == WindowPosition::MOVING_UP) {
            if (checkAntiPinch(window)) { // Internal call, anti-pinch detected and handled
                // Anti-pinch logic in checkAntiPinch has already changed state to OBSTRUCTION_DETECTED
                // and set a new target for reversal. It might have also stopped the motor if too many retries.
                // The simulateMotorMovement will then pick up the OBSTRUCTION_DETECTED state.
                LOG_DEBUG("WindowControl: Anti-pinch for %s modified state. Continuing update.", windowIdToString(window.id));
            }
        }

        // Simulate movement only if motor still active (anti-pinch might have stopped it)
        if (window.motor_active) {
            simulateMotorMovement(window, 0.1); // Simulate 10% movement step (conceptual for discrete states)
        }

        // If motor was stopped by simulateMotorMovement (reached target)
        if (!window.motor_active && window.current_pos != WindowPosition::MOVING_UP && window.current_pos != WindowPosition::MOVING_DOWN) {
             LOG_INFO("WindowControl: Window %s has stopped at %s.",
                      windowIdToString(window.id), windowPosToString(window.current_pos));
        }
    }

    bool any_motor_active_end = false;
    for(const auto& win : windows_) if(win.motor_active) any_motor_active_end = true;

    if (power_monitor_ && any_motor_active_start && !any_motor_active_end) {
        // If motors were running but now all stopped, signal end of high load
        power_monitor_->simulateHighLoadEvent(false); // Call to PowerMonitor
        LOG_INFO("WindowControl: Signaled end of high power load to PowerMonitor as all window motors stopped.");
    }
    LOG_DEBUG("WindowControl: Window states update cycle complete.");
}


} // namespace ecu_body_control_module