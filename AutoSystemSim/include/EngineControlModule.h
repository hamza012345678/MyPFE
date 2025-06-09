// File: include/EngineControlModule.h
#pragma once

namespace Automotive {
namespace ECUs {

class EngineControlModule {
public:
    EngineControlModule();
    ~EngineControlModule();
    void someFunctionWithSwitch(int mode);
    bool initialize(int engineTypeCode); // e.g., 0 for petrol, 1 for diesel
    void processEngineData();
    bool runDiagnostics(int level);
    void requestEngineShutdown();

    // Fonctions spécifiques au moteur
    void setTargetIdleRPM(int rpm);
    void manageFuelInjection(bool enable);
    void controlIgnition(bool enable);
    int getCurrentRPM() const; // Exemple de getter

private:
    bool m_isInitialized;
    int m_engineType;
    int m_currentRPM;
    float m_coolantTemperature;
    // ... autres états internes ...

    void checkSensors();
    void updateActuators();
    void reportStatusToVehicleController(); // Pourrait être via un callback ou une file de messages
};

} // namespace ECUs
} // namespace Automotive