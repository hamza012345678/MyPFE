// File: src/BrakingSystemModule.cpp
#include "BrakingSystemModule.h"
#include "common/LoggingUtil.h"
#include <string> // Pour std::to_string si besoin, mais on va essayer de l'éviter dans les logs

using namespace Automotive::ECUs;

BrakingSystemModule::BrakingSystemModule() :
    m_isInitialized(false),
    m_absActive(false),
    m_espActive(false),
    m_wheelSpeedFL(0.0f), m_wheelSpeedFR(0.0f), m_wheelSpeedRL(0.0f), m_wheelSpeedRR(0.0f)
{
    ECU_LOG_INFO(APID_ABS, CTID_INIT, "BrakingSystemModule constructor. State: Not Initialized. ABS/ESP Inactive.");
}

BrakingSystemModule::~BrakingSystemModule() {
    // Utiliser des messages fixes basés sur l'état
    if (m_absActive || m_espActive) {
        ECU_LOG_INFO(APID_ABS, CTID_SHUTDOWN, "BrakingSystemModule destructor. WARNING: ABS/ESP potentially active at exit!");
    } else {
        ECU_LOG_INFO(APID_ABS, CTID_SHUTDOWN, "BrakingSystemModule destructor. ABS/ESP Inactive at exit (Nominal).");
    }
}

bool BrakingSystemModule::initialize() {
    ECU_LOG_INFO(APID_ABS, CTID_INIT, "Initializing Braking Systems (ABS/ESP).");
    ECU_LOG_DEBUG(APID_ABS, CTID_CONFIG, "Hydraulic pump motor check: OK. Pressure: 0.0 bar (standby).");
    ECU_LOG_DEBUG(APID_ABS, CTID_CONFIG, "Wheel speed sensor calibration: Offsets FL:0.01, FR:-0.02, RL:0.00, RR:0.03.");

    m_isInitialized = true;
    ECU_LOG_INFO(APID_ABS, CTID_INIT, "Braking Systems Initialized Successfully.");
    return true;
}

void BrakingSystemModule::monitorWheelSpeeds() {
    if (!m_isInitialized) {
        ECU_LOG_WARN(APID_ABS, CTID_PROCESS, "MonitorWheelSpeeds: Module not initialized. Skipping.");
        return;
    }

    // Simuler des valeurs fixes pour les logs
    m_wheelSpeedFL = 50.2f; 
    m_wheelSpeedFR = 50.1f;
    m_wheelSpeedRL = 49.8f;
    m_wheelSpeedRR = 25.5f; // Problème simulé

    // Log avec des valeurs fixes (arrondies pour le message)
    ECU_LOG_DEBUG(APID_ABS, CTID_ABS_WHEEL, "Wheel Speeds (km/h): FL=50.2, FR=50.1, RL=49.8, RR=25.5 (Simulated).");

    float averageSpeedFront = (m_wheelSpeedFL + m_wheelSpeedFR) / 2.0f; // Logique interne
    bool abs_logic_triggered = false;
    if (m_wheelSpeedRR < (averageSpeedFront * 0.7f) && averageSpeedFront > 10.0f) {
        ECU_LOG_WARN(APID_ABS, CTID_ABS_WHEEL, "Significant speed diff for RR wheel (25.5 km/h vs avg 50.1 km/h). Possible slippage/sensor issue.");
        applyAntiLockBraking(); 
        m_absActive = true;
        abs_logic_triggered = true;
    }
    if (!abs_logic_triggered && m_absActive) { // Remplacer 'else if'
        ECU_LOG_INFO(APID_ABS, CTID_ABS_WHEEL, "Wheel speeds stabilized. ABS deactivated.");
        m_absActive = false;
    }

    bool esp_logic_triggered = false;
    if (m_wheelSpeedFL > 55.0f && m_wheelSpeedFR < 45.0f && !m_espActive) { // Simule sous-virage
        ECU_LOG_INFO(APID_ABS, CTID_ABS_STABIL, "ESP intervention: Understeer detected. Applying brake to FR wheel (20 bar sim).");
        manageStabilityControl();
        m_espActive = true;
        esp_logic_triggered = true;
    }
    if (!esp_logic_triggered && m_espActive && (m_wheelSpeedFL - m_wheelSpeedFR < 5.0f) ) { // Remplacer 'else if'
        ECU_LOG_INFO(APID_ABS, CTID_ABS_STABIL, "ESP intervention ended. Vehicle stable.");
        m_espActive = false;
    }

    ECU_LOG_DEBUG(APID_ABS, CTID_PROCESS, "Finished monitoring wheel speeds for this cycle.");
}

bool BrakingSystemModule::runDiagnostics(int level_param) { // Renommer
    if (!m_isInitialized && level_param > 0) {
        ECU_LOG_ERROR(APID_ABS, CTID_DIAG, "Cannot run ABS/ESP diagnostics (Level %d req), module not init.", level_param);
        return false;
    }
    bool success = true;
    bool level_checked = false;

    if (level_param == 0) {
        ECU_LOG_INFO(APID_ABS, CTID_DIAG, "Running basic ABS/ESP diagnostics (L0). System Status: Nominal.");
        level_checked = true;
    }
    
    if (!level_checked && level_param == 1) {
        ECU_LOG_INFO(APID_ABS, CTID_DIAG, "Running ABS/ESP sensor checks (L1).");
        ECU_LOG_DEBUG(APID_ABS, CTID_DIAG, "Wheel speed sensor FL resistance: 1.2 kOhm (OK).");
        ECU_LOG_WARN(APID_ABS, CTID_DIAG, "Wheel speed sensor RR signal intermittent. Last val: 22.0 km/h. Check conn.");
        level_checked = true;
    }
    
    if (!level_checked && level_param >= 2) {
        if (level_param == 2) {
             ECU_LOG_INFO(APID_ABS, CTID_DIAG, "Running ABS/ESP actuator tests (L2).");
        } else {
             ECU_LOG_INFO(APID_ABS, CTID_DIAG, "Running ABS/ESP actuator tests (L%d, extended).", level_param);
        }
        ECU_LOG_DEBUG(APID_ABS, CTID_DIAG, "ABS pump motor test. Current draw: 5.5A (OK).");
        ECU_LOG_ERROR(APID_ABS, CTID_DIAG, "ESP hydraulic modulator valve (RL wheel) FAILED. Code: 0xCF03.");
        success = false;
    }

    if(success){
        ECU_LOG_INFO(APID_ABS, CTID_DIAG, "ABS/ESP Diagnostics (L%d) completed: PASS.", level_param);
    } else {
        ECU_LOG_WARN(APID_ABS, CTID_DIAG, "ABS/ESP Diagnostics (L%d) completed: ISSUES FOUND.", level_param);
    }
    return success;
}

void BrakingSystemModule::activateEmergencyBraking(bool active_param) { // Renommer
    if (active_param) {
        ECU_LOG_FATAL(APID_ABS, CTID_PROCESS, "EMERGENCY BRAKING ACTIVATED! Max brake pressure (120 bar sim).");
        controlBrakePressure(); 
        m_absActive = true; 
    } else {
        ECU_LOG_INFO(APID_ABS, CTID_PROCESS, "Emergency braking DEACTIVATED by driver/system.");
        m_absActive = false;
    }
}

// --- Fonctions spécifiques ---
void BrakingSystemModule::applyAntiLockBraking() {
    ECU_LOG_INFO(APID_ABS, CTID_ABS_PUMP, "ABS Pump activated. Modulating brake pressure for RR wheel. Target slip: 15pct (sim).");
}

void BrakingSystemModule::manageStabilityControl() {
    ECU_LOG_INFO(APID_ABS, CTID_ABS_STABIL, "ESP system managing stability. Sim Sensors: Yaw=5.2deg/s, Steering=15deg.");
}

float BrakingSystemModule::getBrakeFluidLevel() const {
    // ECU_LOG_DEBUG(APID_ABS, CTID_DIAG, "Getter: getBrakeFluidLevel() -> 85.0 pct");
    return 85.0f;
}

// --- Fonctions privées ---
void BrakingSystemModule::checkBrakePadsWear() {
    ECU_LOG_DEBUG(APID_ABS, CTID_DIAG, "Brake pads wear check. Sim Remaining: FL=75, FR=72, RL=80, RR=78 pct.");
}

void BrakingSystemModule::controlBrakePressure() {
    ECU_LOG_DEBUG(APID_ABS, CTID_ABS_PUMP, "Controlling brake pressure. Target: 60 bar. Current: 58.5 bar. Valve FL: OPEN (Simulated).");
}