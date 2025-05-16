#ifndef POWER_MONITOR_H
#define POWER_MONITOR_H

#include "../common/logger.h" // Pour LOG_INFO, etc. utilisé dans le .cpp
// Pas d'autres inclusions de datatypes.h ou autres spécifiques nécessaires pour l'interface publique de cette classe.

namespace ecu_power_management {

class PowerMonitor {
public:
    PowerMonitor();
    ~PowerMonitor();

    // --- Fonctions d'état principales (appelées par d'autres ECUs) ---
    bool isPowerStable() const;
    double getBatteryVoltage() const; // En Volts

    // --- Fonction de mise à jour principale (appelée par une boucle principale ou un scheduler) ---
    void updatePowerStatus();

    // --- Simulation d'événements externes affectant la puissance ---
    // Appelé par d'autres ECUs (par ex. ClimateControl, WindowControl) pour signaler une charge élevée
    void simulateHighLoadEvent(bool start_event);

private:
    // --- Membres d'état internes ---
    double current_battery_voltage_V_;
    bool system_stable_;
    int critical_load_events_count_; // Pour suivre les événements de charge élevée consécutifs

    // --- Méthodes d'aide internes ---
    void checkVoltageLevels();      // Vérifie et met à jour la tension de la batterie, simule les fluctuations
    void assessSystemStability();   // Évalue la stabilité globale du système basée sur la tension et les charges
};

} // namespace ecu_power_management

#endif // POWER_MONITOR_H