// File: include/TransmissionControlModule.h
#pragma once

namespace Automotive {
namespace ECUs {

class TransmissionControlModule {
public:
    TransmissionControlModule();
    ~TransmissionControlModule();
    void testDoWhileLoop();

    bool initialize(int transmissionTypeCode); // e.g., 0 for manual, 1 for automatic
    void processTransmissionRequests(); // e.g., from gear stick or paddles
    bool runDiagnostics(int level);
    void requestSafeState(); // Mettre la transmission en sécurité

    // Fonctions spécifiques à la transmission
    void shiftGearUp();
    void shiftGearDown();
    void engagePark();
    int getCurrentGear() const;

private:
    bool m_isInitialized;
    int m_transmissionType;
    int m_selectedGear; // e.g., 0=Neutral, 1-6=Forward, -1=Reverse, 100=Park
    float m_oilTemperature;
    // ... autres états internes ...

    void monitorHydraulicPressure();
    void controlSolenoids();
};

} // namespace ECUs
} // namespace Automotive