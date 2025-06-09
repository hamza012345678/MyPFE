// File: src/VehicleController.cpp
#include "VehicleController.h"
#include "common/LoggingUtil.h"
#include "EngineControlModule.h"
#include "TransmissionControlModule.h"
#include "BrakingSystemModule.h"
#include "BodyControlModule.h"
#include "InfotainmentModule.h"

using namespace Automotive::Controllers;
using namespace Automotive::ECUs; 

VehicleController::VehicleController() :
    m_engineControl(nullptr), 
    m_transmissionControl(nullptr),
    m_brakingSystem(nullptr),
    m_bodyControl(nullptr),
    m_infotainmentControl(nullptr),
    m_systemInitialized(false),
    m_vehicleState(0) // 0=OFF
{
    ECU_LOG_INFO(APID_VCTRL, CTID_INIT, "VehicleController constructor. System state: OFF (0).");

    m_engineControl = new EngineControlModule();
    m_transmissionControl = new TransmissionControlModule();
    m_brakingSystem = new BrakingSystemModule();
    m_bodyControl = new BodyControlModule();
    m_infotainmentControl = new InfotainmentModule();

    ECU_LOG_DEBUG(APID_VCTRL, CTID_INIT, "ECU Modules instantiated.");
}

VehicleController::~VehicleController() {
    ECU_LOG_INFO(APID_VCTRL, CTID_SHUTDOWN, "VehicleController destructor. Releasing ECU modules.");

    delete m_engineControl;
    delete m_transmissionControl;
    delete m_brakingSystem;
    delete m_bodyControl;
    delete m_infotainmentControl;

    m_engineControl = nullptr;
    m_transmissionControl = nullptr;
    m_brakingSystem = nullptr;
    m_bodyControl = nullptr;
    m_infotainmentControl = nullptr;

    ECU_LOG_INFO(APID_VCTRL, CTID_SHUTDOWN, "ECU Modules released. Vehicle Controller shutdown complete.");
}

void VehicleController::initializeSystem() {
    ECU_LOG_INFO(APID_VCTRL, CTID_VCTRL_STARTUP, "Vehicle system initialization: STARTED.");
    m_vehicleState = 1; // 1=INITIALIZING, on peut loguer cette valeur fixe si besoin

    performPowerOnSelfTest(); 

    bool ecm_ok = m_engineControl->initialize(0); // Appel avec littéral (Petrol)
    if (!ecm_ok) {
        ECU_LOG_FATAL(APID_VCTRL, CTID_VCTRL_STARTUP, "CRITICAL FAILURE: ECM init FAILED. Aborting system startup.");
        m_vehicleState = 3; // 3=ERROR
        return; 
    }

    bool tcu_ok = m_transmissionControl->initialize(1); // Appel avec littéral (Automatic)
    if (!tcu_ok) {
        ECU_LOG_ERROR(APID_VCTRL, CTID_VCTRL_STARTUP, "ERROR: TCU init FAILED. Limited functionality.");
    }

    if (!m_brakingSystem->initialize()) { 
        ECU_LOG_ERROR(APID_VCTRL, CTID_VCTRL_STARTUP, "ERROR: ABS init FAILED.");
    }

    if (!m_bodyControl->initialize()) {
        ECU_LOG_ERROR(APID_VCTRL, CTID_VCTRL_STARTUP, "ERROR: BCM init FAILED.");
    }
    
    // Appel avec littéral pour la langue
    if (!m_infotainmentControl->initialize("EN_US")) {
         ECU_LOG_WARN(APID_VCTRL, CTID_VCTRL_STARTUP, "WARNING: IHU init FAILED. User experience affected.");
    }

    bool init_failed_globally = (m_vehicleState == 3); // Capturer avant de potentiellement changer
    if (!init_failed_globally) { 
        m_systemInitialized = true;
        m_vehicleState = 2; // 2=RUNNING
        ECU_LOG_INFO(APID_VCTRL, CTID_VCTRL_STARTUP, "Vehicle system initialization: COMPLETED. State: RUNNING (2).");
    } else { // Implicitement m_vehicleState == 3
        ECU_LOG_ERROR(APID_VCTRL, CTID_VCTRL_STARTUP, "Vehicle system initialization: FAILED. State: ERROR (3). See logs.");
    }
    communicateNetworkStatus(); 
}

void VehicleController::shutdownSystem() {
    // m_vehicleState est une variable, c'est OK de la loguer.
    ECU_LOG_INFO(APID_VCTRL, CTID_SHUTDOWN, "Vehicle system shutdown: INITIATED. Current State: %d.", m_vehicleState);
    m_vehicleState = 4; // 4=SHUTTING_DOWN

    ECU_LOG_DEBUG(APID_VCTRL, CTID_SHUTDOWN, "Requesting IHU shutdown.");
    m_infotainmentControl->shutdownDisplay(); 

    ECU_LOG_DEBUG(APID_VCTRL, CTID_SHUTDOWN, "Requesting ECM shutdown.");
    m_engineControl->requestEngineShutdown();

    ECU_LOG_DEBUG(APID_VCTRL, CTID_SHUTDOWN, "TCU, ABS, BCM will shutdown on power off (simulated).");

    m_systemInitialized = false;
    m_vehicleState = 0; // 0=OFF
    ECU_LOG_INFO(APID_VCTRL, CTID_SHUTDOWN, "Vehicle system shutdown: COMPLETED. System state: OFF (0).");
}

void VehicleController::triggerDiagnosticSequence(int diagnosticLevel_param) { // Renommer
    ECU_LOG_INFO(APID_VCTRL, CTID_DIAG, "Global diagnostic sequence triggered. Requested Level: %d.", diagnosticLevel_param);
    if (!m_systemInitialized && diagnosticLevel_param > 0) {
        ECU_LOG_WARN(APID_VCTRL, CTID_DIAG, "Cannot run detailed diagnostics (system not init). Basic checks only (L0 sim).");
        // On pourrait forcer diagnosticLevel_param = 0 ici pour la suite des appels
    }

    ECU_LOG_INFO(APID_VCTRL, CTID_DIAG, "Running diagnostics for ECM (Level %d).", diagnosticLevel_param);
    m_engineControl->runDiagnostics(diagnosticLevel_param);

    ECU_LOG_INFO(APID_VCTRL, CTID_DIAG, "Running diagnostics for TCU (Level %d).", diagnosticLevel_param);
    m_transmissionControl->runDiagnostics(diagnosticLevel_param);

    ECU_LOG_INFO(APID_VCTRL, CTID_DIAG, "Running diagnostics for ABS (Level %d).", diagnosticLevel_param);
    m_brakingSystem->runDiagnostics(diagnosticLevel_param);

    ECU_LOG_INFO(APID_VCTRL, CTID_DIAG, "Running diagnostics for BCM (Level %d).", diagnosticLevel_param);
    m_bodyControl->runDiagnostics(diagnosticLevel_param);

    ECU_LOG_INFO(APID_VCTRL, CTID_DIAG, "Running diagnostics for IHU (Level %d).", diagnosticLevel_param);
    m_infotainmentControl->runDiagnostics(diagnosticLevel_param);

    ECU_LOG_INFO(APID_VCTRL, CTID_DIAG, "Global diagnostic sequence (L%d) completed. Check ECU logs.", diagnosticLevel_param);
}

void VehicleController::performPowerOnSelfTest() {
    ECU_LOG_INFO(APID_VCTRL, CTID_VCTRL_PWRMGMT, "Performing Power-On Self-Test (POST).");
    ECU_LOG_DEBUG(APID_VCTRL, CTID_VCTRL_PWRMGMT, "Main ECU voltage: 12.6V (OK).");
    ECU_LOG_DEBUG(APID_VCTRL, CTID_VCTRL_PWRMGMT, "CAN bus A termination: 60 Ohm (Nominal).");
    ECU_LOG_DEBUG(APID_VCTRL, CTID_VCTRL_PWRMGMT, "CAN bus B termination: 60 Ohm (Nominal).");
    ECU_LOG_INFO(APID_VCTRL, CTID_VCTRL_PWRMGMT, "POST completed successfully.");
}

void VehicleController::manageVehicleState() {
    // m_vehicleState est une variable, son usage dans le log est OK.
    ECU_LOG_DEBUG(APID_VCTRL, CTID_STATE, "Managing vehicle state. Current internal state val: %d.", m_vehicleState);
}

void VehicleController::communicateNetworkStatus() {
    ECU_LOG_INFO(APID_VCTRL, CTID_VCTRL_NETMGMT, "Broadcasting network frame: System Active. Node ID: 0x01 (VCTRL).");
    ECU_LOG_DEBUG(APID_VCTRL, CTID_COMM, "Sending heartbeat on CAN Bus A. Cycle: 100ms (Simulated).");
}

void VehicleController::runMainVehicleLoop() {
    if (!m_systemInitialized || m_vehicleState != 2) { 
        // m_vehicleState est une variable, OK pour ce log.
        ECU_LOG_WARN(APID_VCTRL, CTID_VCTRL_LOOP, "Main loop skipped. System not init or not RUNNING. Current State val: %d.", m_vehicleState);
        return;
    }

    // m_vehicleState est une variable, OK ici.
    ECU_LOG_DEBUG(APID_VCTRL, CTID_VCTRL_LOOP, "Running main vehicle loop iteration. Vehicle State val: %d.", m_vehicleState);

    m_engineControl->processEngineData();
    m_transmissionControl->processTransmissionRequests();
    m_brakingSystem->monitorWheelSpeeds();
    m_bodyControl->processComfortRequests();
    m_infotainmentControl->processUserInput(1, 10); // Appel avec des littéraux

    manageVehicleState(); 

    // m_engineControl->getCurrentRPM() est un appel de fonction, le résultat est dynamique.
    if (m_engineControl->getCurrentRPM() > 3000) { 
        ECU_LOG_INFO(APID_VCTRL, CTID_VCTRL_LOOP, "Engine RPM high (>3000, e.g. 3200 RPM). Activating BCM sports mode light (sim).");
        m_bodyControl->setHeadlightsState(2); // Appel avec littéral
    }

    ECU_LOG_INFO(APID_VCTRL, CTID_VCTRL_LOOP, "Testing Switch statement in ECM with mode 1 and 5 (fixed).");
    m_engineControl->someFunctionWithSwitch(1); // Appel avec littéral
    m_engineControl->someFunctionWithSwitch(5); // Appel avec littéral

    ECU_LOG_INFO(APID_VCTRL, CTID_VCTRL_LOOP, "Testing Do-While loop in TCU.");
    m_transmissionControl->testDoWhileLoop();
    
    ECU_LOG_DEBUG(APID_VCTRL, CTID_VCTRL_LOOP, "Main vehicle loop iteration COMPLETED.");
}