// File: src/main.cpp
#include "VehicleController.h"
#include "common/LoggingUtil.h" // Pour les logs globaux

using namespace Automotive::Controllers;

int main(int argc, char* argv[]) {
    ECU_LOG_INFO(APID_SYSTEM, CTID_SYS_BOOT, "Dummy Automotive Application STARTING. Version: 1.0.1 (fixed).");

    // argc est un paramètre, argv aussi. Leur usage dans le log est OK.
    if (argc > 1) {
        ECU_LOG_INFO(APID_SYSTEM, CTID_SYS_BOOT, "App started with %d arguments. First arg: '%s'.", argc -1 , argv[1]);
    } else {
        ECU_LOG_INFO(APID_SYSTEM, CTID_SYS_BOOT, "App started with no command-line arguments.");
    }

    ECU_LOG_DEBUG(APID_SYSTEM, CTID_SYS_MAIN, "Creating VehicleController instance...");
    VehicleController vehicleCtrl;
    ECU_LOG_DEBUG(APID_SYSTEM, CTID_SYS_MAIN, "VehicleController instance created.");

    ECU_LOG_INFO(APID_SYSTEM, CTID_SYS_MAIN, "Initiating system initialization sequence...");
    vehicleCtrl.initializeSystem();
    ECU_LOG_INFO(APID_SYSTEM, CTID_SYS_MAIN, "System initialization sequence finished by VehicleController.");

    // system_ready_for_loop est une variable locale, c'est OK.
    // On va la fixer pour cet exemple de WAD.
    bool system_ready_for_loop_simulated = true; 
    // Idéalement, l'état réel viendrait de vehicleCtrl, mais pour simplifier le log:
    if (vehicleCtrl.isSystemInitialized()) { // Supposons une telle méthode existe pour l'exemple
         system_ready_for_loop_simulated = true;
    } else {
         system_ready_for_loop_simulated = false; // Si l'initialisation a vraiment échoué
    }


    if (system_ready_for_loop_simulated) { 
        ECU_LOG_INFO(APID_SYSTEM, CTID_SYS_MAIN, "Entering main application loop (simulated 2 iterations)."); // Nombre fixe d'itérations
        for (int i = 0; i < 2; ++i) { // Simuler 2 itérations
            // i est une variable locale, son usage dans le log est OK.
            ECU_LOG_DEBUG(APID_SYSTEM, CTID_VCTRL_LOOP, "Main loop - Iteration #%d.", i + 1); 
            vehicleCtrl.runMainVehicleLoop();

            if (i == 0) {
                 ECU_LOG_DEBUG(APID_SYSTEM, CTID_VCTRL_LOOP, "Simulated delay after iteration 1 (e.g., 100ms task).");
            }
        }
        ECU_LOG_INFO(APID_SYSTEM, CTID_SYS_MAIN, "Exited main application loop.");

        ECU_LOG_INFO(APID_SYSTEM, CTID_SYS_MAIN, "Simulating external diagnostic tool request (Level 2 fixed).");
        vehicleCtrl.triggerDiagnosticSequence(2); // Appel avec littéral
    } else {
        ECU_LOG_ERROR(APID_SYSTEM, CTID_SYS_MAIN, "System not ready for main loop. Check initialization logs (simulated state).");
    }

    ECU_LOG_INFO(APID_SYSTEM, CTID_SYS_MAIN, "Initiating system shutdown sequence...");
    vehicleCtrl.shutdownSystem();
    ECU_LOG_INFO(APID_SYSTEM, CTID_SYS_MAIN, "System shutdown sequence finished by VehicleController.");

    ECU_LOG_INFO(APID_SYSTEM, CTID_SYS_BOOT, "Dummy Automotive Application FINISHED. Exiting main.");
    return 0;
}