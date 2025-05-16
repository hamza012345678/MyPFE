// AutoSystemSim/ecu_body_control_module/window_control.h
#ifndef WINDOW_CONTROL_H
#define WINDOW_CONTROL_H

#include "../common/logger.h"
#include "../common/datatypes.h" // Potentially for VehicleState if window operation is speed-dependent

// Forward declaration for potential dependency (e.g. PowerMonitor for load)
namespace ecu_power_management {
    class PowerMonitor;
}

namespace ecu_body_control_module {

enum class WindowPosition { // Could be numerical (0-100%) or discrete
    FULLY_CLOSED,
    SLIGHTLY_OPEN, // For ventilation
    HALF_OPEN,
    FULLY_OPEN,
    MOVING_UP,
    MOVING_DOWN,
    OBSTRUCTION_DETECTED // Anti-pinch active
};

enum class WindowID {
    FRONT_LEFT,
    FRONT_RIGHT,
    REAR_LEFT,
    REAR_RIGHT,
    SUNROOF // Optional
};

struct SingleWindowState {
    WindowID id;
    WindowPosition current_pos;
    WindowPosition target_pos; // For one-touch operation
    bool motor_active;
    int obstruction_counter; // How many times pinch was detected recently

    SingleWindowState(WindowID win_id) :
        id(win_id), current_pos(WindowPosition::FULLY_CLOSED),
        target_pos(WindowPosition::FULLY_CLOSED), motor_active(false),
        obstruction_counter(0) {}
};

class WindowControl {
public:
    WindowControl(ecu_power_management::PowerMonitor* pm); // Dependency for power load/availability
    ~WindowControl();

    // --- User Commands ---
    // target_position: 0.0 (closed) to 1.0 (fully open).
    // one_touch: if true, moves to target automatically. If false, moves only while command active (not simulated here).
    bool moveWindow(WindowID id, double target_position_percent, bool one_touch);
    bool stopWindowMovement(WindowID id); // User releases button or obstruction

    // Master controls
    bool setChildLock(WindowID id, bool locked); // Lock a specific rear window
    bool setAllWindowsLock(bool locked);         // Driver master lock for all passenger windows

    // --- Status & Diagnostics ---
    WindowPosition getWindowPosition(WindowID id) const;
    void updateWindowStates(); // Simulates periodic updates, motor movement, anti-pinch checks

private:
    std::vector<SingleWindowState> windows_;
    ecu_power_management::PowerMonitor* power_monitor_; // For checking power load/availability

    bool all_windows_locked_by_driver_; // Master lock switch

    SingleWindowState* findWindow(WindowID id);
    const SingleWindowState* findWindow(WindowID id) const;

    void simulateMotorMovement(SingleWindowState& window, double step_change_percent);
    bool checkAntiPinch(SingleWindowState& window); // Simplified anti-pinch
    bool canOperateWindow(const SingleWindowState& window, bool opening) const; // Checks locks, power etc.
};

} // namespace ecu_body_control_module

#endif // WINDOW_CONTROL_H