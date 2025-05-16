// AutoSystemSim/common/datatypes.h
#ifndef DATATYPES_H
#define DATATYPES_H

#include <string>
#include <vector>

// Données de capteur génériques
struct SensorData {
    int id;
    double value;
    std::string unit;
    long timestamp_ms; // Millisecondes depuis l'epoch ou démarrage
};

// État du véhicule
struct VehicleState {
    double speed_kmh;
    int engine_rpm;
    int current_gear;
    bool lights_on;
    double battery_voltage;
    std::string status_message;
};

// Erreurs système
struct SystemError {
    int error_code;
    std::string description;
    std::string component_origin; // ex: "EngineManager", "ABSControl"
    enum class Severity {
        INFO,
        WARNING,
        CRITICAL
    } severity_level;
};

#endif // DATATYPES_H