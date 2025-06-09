// File: include/VehicleController.h
#pragma once

// Forward declarations pour éviter les dépendances cycliques d'include dans les headers
// si les ECUs avaient besoin de connaître le VehicleController (moins probable ici).
// On inclura les headers des ECUs dans le .cpp du VehicleController.
namespace Automotive {
namespace ECUs {
    class EngineControlModule;
    class TransmissionControlModule;
    class BrakingSystemModule;
    class BodyControlModule;
    class InfotainmentModule;
} // namespace ECUs
} // namespace Automotive


namespace Automotive {
namespace Controllers {

class VehicleController {
public:
    VehicleController();
    ~VehicleController();

    void initializeSystem();
    void runMainVehicleLoop();
    void shutdownSystem();
    void triggerDiagnosticSequence(int diagnosticLevel);

private:
    // Instances des modules ECU gérés par ce contrôleur
    // Les #include pour ces types seront dans VehicleController.cpp
    ECUs::EngineControlModule* m_engineControl;
    ECUs::TransmissionControlModule* m_transmissionControl;
    ECUs::BrakingSystemModule* m_brakingSystem;
    ECUs::BodyControlModule* m_bodyControl;
    ECUs::InfotainmentModule* m_infotainmentControl;
    // Note: Utilisation de pointeurs ici pour potentiellement gérer leur création/destruction
    // de manière plus explicite dans le constructeur/destructeur.
    // Pour une version plus simple, on pourrait avoir des objets membres directement.
    // Mais les pointeurs sont courants pour les objets complexes ou polymorphiques.

    bool m_systemInitialized;
    int m_vehicleState; // Exemple: 0=OFF, 1=ACCESSORY, 2=RUNNING, 3=ERROR

    void performPowerOnSelfTest();
    void manageVehicleState();
    void communicateNetworkStatus();
};

} // namespace Controllers
} // namespace Automotive