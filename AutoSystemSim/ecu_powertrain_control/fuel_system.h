// AutoSystemSim/ecu_powertrain_control/fuel_system.h
#ifndef FUEL_SYSTEM_H
#define FUEL_SYSTEM_H

#include "../common/logger.h"
#include "../common/datatypes.h" // For potential shared types

namespace ecu_powertrain_control {

class FuelSystem {
public:
    FuelSystem();
    ~FuelSystem();

    // Attempts to inject fuel for a given cylinder
    // Returns true on success, false on failure (e.g., low fuel pressure)
    bool injectFuel(int cylinder_id, double amount_ml);
    bool checkFuelPressure();
    void primePump();
    double getFuelLevel() const; // Percentage

private:
    bool pump_primed_;
    double current_fuel_level_; // Percentage
    double required_pressure_psi_;

    void simulateFuelConsumption(double amount_ml);
};

} // namespace ecu_powertrain_control

#endif // FUEL_SYSTEM_H