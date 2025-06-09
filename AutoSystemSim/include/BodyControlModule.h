// File: include/BodyControlModule.h
#pragma once

#include <string> // Pour l'exemple de setWelcomeMessage

namespace Automotive {
namespace ECUs {

class BodyControlModule {
public:
    BodyControlModule();
    ~BodyControlModule();

    bool initialize();
    void processComfortRequests(); // e.g., lights, wipers, windows
    bool runDiagnostics(int level);

    // Fonctions spécifiques au BCM
    void setHeadlightsState(int state); // 0=OFF, 1=Parking, 2=ON
    void controlWipers(int speed); // 0=OFF, 1=Intermittent, 2=Low, 3=High
    void manageCentralLocking(bool lock);
    std::string getCurrentAmbientTemperature() const; // Exemple de retour de chaîne

private:
    bool m_isInitialized;
    int m_headlightStatus;
    bool m_doorsLocked;
    // ... autres états internes ...

    void readLightSensorValue();
    void checkDoorStatus();
};

} // namespace ECUs
} // namespace Automotive