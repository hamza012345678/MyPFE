// File: include/BrakingSystemModule.h
#pragma once

namespace Automotive {
namespace ECUs {

class BrakingSystemModule {
public:
    BrakingSystemModule();
    ~BrakingSystemModule();

    bool initialize();
    void monitorWheelSpeeds();
    bool runDiagnostics(int level);
    void activateEmergencyBraking(bool active);

    // Fonctions spécifiques au freinage
    void applyAntiLockBraking();
    void manageStabilityControl(); // ESP
    float getBrakeFluidLevel() const;

private:
    bool m_isInitialized;
    bool m_absActive;
    bool m_espActive;
    float m_wheelSpeedFL, m_wheelSpeedFR, m_wheelSpeedRL, m_wheelSpeedRR; // FrontLeft, etc.
    // ... autres états internes ...

    void checkBrakePadsWear();
    void controlBrakePressure();
};

} // namespace ECUs
} // namespace Automotive