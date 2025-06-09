// File: src/BodyControlModule.cpp
#include "BodyControlModule.h"
#include "common/LoggingUtil.h"
#include <string> // Pour std::string

using namespace Automotive::ECUs;

BodyControlModule::BodyControlModule() :
    m_isInitialized(false),
    m_headlightStatus(0), 
    m_doorsLocked(false)
{
    ECU_LOG_INFO(APID_BCM, CTID_INIT, "BodyControlModule constructor. State: Not Initialized. Headlights: OFF, Doors: UNLOCKED.");
}

BodyControlModule::~BodyControlModule() {
    // Utiliser une valeur fixe pour le booléen si on simplifie
    if (m_doorsLocked) {
        ECU_LOG_INFO(APID_BCM, CTID_SHUTDOWN, "BodyControlModule destructor. Doors were LOCKED at exit.");
    } else {
        ECU_LOG_INFO(APID_BCM, CTID_SHUTDOWN, "BodyControlModule destructor. Doors were UNLOCKED at exit.");
    }
}

bool BodyControlModule::initialize() {
    ECU_LOG_INFO(APID_BCM, CTID_INIT, "Initializing Body Control Systems.");
    ECU_LOG_DEBUG(APID_BCM, CTID_CONFIG, "LIN bus for lighting: OK. Window motor power: Nominal (12.5V).");
    readLightSensorValue();

    m_isInitialized = true;
    ECU_LOG_INFO(APID_BCM, CTID_INIT, "Body Control Systems Initialized Successfully.");
    return true;
}

void BodyControlModule::processComfortRequests() {
    if (!m_isInitialized) {
        ECU_LOG_WARN(APID_BCM, CTID_PROCESS, "ProcessComfortRequests: BCM not initialized. Skipping.");
        return;
    }

    ECU_LOG_DEBUG(APID_BCM, CTID_PROCESS, "Processing comfort and access requests.");

    bool is_dark_outside_simulated = true; 
    if (is_dark_outside_simulated && m_headlightStatus == 0) {
        ECU_LOG_INFO(APID_BCM, CTID_BCM_LIGHT, "Ambient light dark. Auto-activating headlights to ON (State 2).");
        setHeadlightsState(2); // Appel avec littéral
    }

    bool central_locking_request_simulated = true;
    if (central_locking_request_simulated && !m_doorsLocked) {
        ECU_LOG_INFO(APID_BCM, CTID_BCM_ACCESS, "Central locking request. Locking all doors.");
        manageCentralLocking(true); // Appel avec littéral
    }
    
    checkDoorStatus();

    ECU_LOG_DEBUG(APID_BCM, CTID_PROCESS, "Finished processing comfort requests for this cycle.");
}

bool BodyControlModule::runDiagnostics(int level_param) { // Renommer
    if (!m_isInitialized && level_param > 0) {
        ECU_LOG_ERROR(APID_BCM, CTID_DIAG, "Cannot run BCM diagnostics (Level %d req), module not init.", level_param);
        return false;
    }
    bool success = true;
    bool level_checked = false;

    if (level_param == 0) {
        ECU_LOG_INFO(APID_BCM, CTID_DIAG, "Running basic BCM diagnostics (L0). Comm check: PASS.");
        level_checked = true;
    }
    
    if (!level_checked && level_param == 1) {
        ECU_LOG_INFO(APID_BCM, CTID_DIAG, "Running BCM sensor checks (L1).");
        ECU_LOG_DEBUG(APID_BCM, CTID_DIAG, "Rain sensor: Dry (0.0V).");
        ECU_LOG_WARN(APID_BCM, CTID_DIAG, "Driver window sensor timeout. Last pos: 50pct open.");
        level_checked = true;
    }
    
    if (!level_checked && level_param >= 2) {
        if (level_param == 2) {
            ECU_LOG_INFO(APID_BCM, CTID_DIAG, "Running BCM actuator tests (L2).");
        } else {
            ECU_LOG_INFO(APID_BCM, CTID_DIAG, "Running BCM actuator tests (L%d, extended).", level_param);
        }
        ECU_LOG_DEBUG(APID_BCM, CTID_DIAG, "Wiper motor test: Low speed OK, High speed OK.");
        ECU_LOG_ERROR(APID_BCM, CTID_DIAG, "Rear right door lock actuator FAILED. Error: Short to Gnd (0xDA01).");
        success = false;
    }

    if(success){
        ECU_LOG_INFO(APID_BCM, CTID_DIAG, "BCM Diagnostics (L%d) completed: PASS.", level_param);
    } else {
        ECU_LOG_WARN(APID_BCM, CTID_DIAG, "BCM Diagnostics (L%d) completed: ISSUES FOUND.", level_param);
    }
    return success;
}

void BodyControlModule::setHeadlightsState(int state_param) { // Renommer
    bool state_handled = false;
    m_headlightStatus = state_param; // Mettre à jour le membre pour le log final si besoin

    if (state_param == 0) {
        ECU_LOG_INFO(APID_BCM, CTID_BCM_LIGHT, "Headlights set to OFF (State 0).");
        m_headlightStatus = 0; // Assurer la valeur pour la logique interne
        state_handled = true;
    }
    
    if (!state_handled && state_param == 1) {
        ECU_LOG_INFO(APID_BCM, CTID_BCM_LIGHT, "Headlights set to PARKING (State 1).");
        m_headlightStatus = 1;
        state_handled = true;
    }
    
    if (!state_handled && state_param == 2) {
        ECU_LOG_INFO(APID_BCM, CTID_BCM_LIGHT, "Headlights set to ON (State 2 - Low Beam).");
        m_headlightStatus = 2;
        state_handled = true;
    }
    
    if (!state_handled) { 
        ECU_LOG_WARN(APID_BCM, CTID_BCM_LIGHT, "Invalid headlight state requested: %d. No action.", state_param);
        return; 
    }
    // Utiliser la valeur m_headlightStatus mise à jour (qui est maintenant fixe pour cette branche)
    ECU_LOG_DEBUG(APID_BCM, CTID_BCM_LIGHT, "LIN command sent to headlight module. New state: %d.", m_headlightStatus);
}

void BodyControlModule::controlWipers(int speed_param) { // Renommer
    bool speed_processed = false;
    int current_speed_for_log = speed_param; // Pour le log final

    if (speed_param == 0) {
        ECU_LOG_INFO(APID_BCM, CTID_BCM_WIPER, "Wipers set to OFF (Speed 0).");
        current_speed_for_log = 0;
        speed_processed = true;
    }
    
    if (!speed_processed && speed_param == 1) {
        ECU_LOG_INFO(APID_BCM, CTID_BCM_WIPER, "Wipers set to INTERMITTENT (Speed 1). Interval: 5s.");
        current_speed_for_log = 1;
        speed_processed = true;
    }
    
    if (!speed_processed && speed_param == 2) {
        ECU_LOG_INFO(APID_BCM, CTID_BCM_WIPER, "Wipers set to LOW speed (Speed 2).");
        current_speed_for_log = 2;
        speed_processed = true;
    }
    
    if (!speed_processed && speed_param == 3) {
        ECU_LOG_INFO(APID_BCM, CTID_BCM_WIPER, "Wipers set to HIGH speed (Speed 3).");
        current_speed_for_log = 3;
        speed_processed = true;
    }
    
    if (!speed_processed) { 
        ECU_LOG_WARN(APID_BCM, CTID_BCM_WIPER, "Invalid wiper speed requested: %d. No action.", speed_param);
        return; 
    }
    ECU_LOG_DEBUG(APID_BCM, CTID_BCM_WIPER, "Wiper motor relay. New speed state: %d.", current_speed_for_log);
}

void BodyControlModule::manageCentralLocking(bool lock_param) { // Renommer
    if (lock_param) {
        ECU_LOG_INFO(APID_BCM, CTID_BCM_ACCESS, "Locking all doors. Command: LOCK.");
        m_doorsLocked = true;
        ECU_LOG_DEBUG(APID_BCM, CTID_BCM_ACCESS, "Door lock actuators status: FL:L, FR:L, RL:L, RR:L.");
    } else {
        ECU_LOG_INFO(APID_BCM, CTID_BCM_ACCESS, "Unlocking all doors. Command: UNLOCK.");
        m_doorsLocked = false;
        ECU_LOG_DEBUG(APID_BCM, CTID_BCM_ACCESS, "Door lock actuators status: FL:U, FR:U, RL:U, RR:U.");
    }
}

std::string BodyControlModule::getCurrentAmbientTemperature() const {
    // Le log ici n'est pas critique, on peut le supprimer ou le garder si besoin de tracer l'appel au getter
    // ECU_LOG_DEBUG(APID_BCM, CTID_BCM_HVAC, "Getter: getCurrentAmbientTemperature() -> 22.5 C");
    return "22.5 C"; // Valeur fixe
}

// --- Fonctions privées ---
void BodyControlModule::readLightSensorValue() {
    ECU_LOG_DEBUG(APID_BCM, CTID_BCM_LIGHT, "Reading ambient light sensor. Value: 350 lux (Simulated Daylight).");
}

void BodyControlModule::checkDoorStatus() {
    ECU_LOG_DEBUG(APID_BCM, CTID_BCM_ACCESS, "Checking door status sensors.");
    if (m_doorsLocked) {
        ECU_LOG_DEBUG(APID_BCM, CTID_BCM_ACCESS, "Door status report: ALL_CLOSED_LOCKED.");
    } else {
        ECU_LOG_DEBUG(APID_BCM, CTID_BCM_ACCESS, "Door status report: FR_OPEN_UNLOCKED, others_CLOSED_UNLOCKED (Simulated).");
        ECU_LOG_WARN(APID_BCM, CTID_BCM_ACCESS, "Front Right door reported OPEN while system active.");
    }
}