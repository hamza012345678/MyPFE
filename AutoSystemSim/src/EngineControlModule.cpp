// File: src/EngineControlModule.cpp
#include "EngineControlModule.h"
#include "common/LoggingUtil.h"

using namespace Automotive::ECUs;

EngineControlModule::EngineControlModule() :
    m_isInitialized(false),
    m_engineType(0),
    m_currentRPM(0),
    m_coolantTemperature(25.0f)
{
    ECU_LOG_INFO(APID_ECM, CTID_INIT, "EngineControlModule constructor. State: Not Initialized. Default Type: 0, RPM: 0, Coolant: 25.0C.");
}

EngineControlModule::~EngineControlModule() {
    bool was_init = m_isInitialized; // Capturer l'état avant de potentiellement le modifier
    bool rpm_high = m_currentRPM > 100;

    if (was_init) {
        if (rpm_high) {
             ECU_LOG_INFO(APID_ECM, CTID_SHUTDOWN, "EngineControlModule destructor. Was Initialized. RPM at exit >100 (Simulated).");
        }
        if (!rpm_high) { // Remplacer else par un if avec condition inverse
             ECU_LOG_INFO(APID_ECM, CTID_SHUTDOWN, "EngineControlModule destructor. Was Initialized. RPM at exit <=100 (Simulated).");
        }
    }
    if (!was_init) { // Remplacer else par un if avec condition inverse
        ECU_LOG_INFO(APID_ECM, CTID_SHUTDOWN, "EngineControlModule destructor. Not initialized at exit.");
    }
}

bool EngineControlModule::initialize(int engineTypeCode) {
    bool typeConfigured = false;
    m_engineType = engineTypeCode; // Garder la variable membre à jour

    if (engineTypeCode == 0) {
        ECU_LOG_INFO(APID_ECM, CTID_INIT, "Initializing Engine Systems for Petrol (Type 0).");
        ECU_LOG_INFO(APID_ECM, CTID_CONFIG, "Configuring Petrol. Setting idle RPM to 800.");
        setTargetIdleRPM(800);
        typeConfigured = true;
    }

    if (!typeConfigured && engineTypeCode == 1) {
        ECU_LOG_INFO(APID_ECM, CTID_INIT, "Initializing Engine Systems for Diesel (Type 1).");
        ECU_LOG_INFO(APID_ECM, CTID_CONFIG, "Configuring Diesel. Setting idle RPM to 750.");
        setTargetIdleRPM(750);
        ECU_LOG_WARN(APID_ECM, CTID_ECM_FUEL, "Diesel fuel pressure sensor: low initial reading (0.5 bar). Priming.");
        typeConfigured = true;
    }

    if (!typeConfigured && engineTypeCode == 2) {
        ECU_LOG_INFO(APID_ECM, CTID_INIT, "Initializing Engine Systems for Electric (Type 2).");
        ECU_LOG_INFO(APID_ECM, CTID_CONFIG, "Configuring Electric. Setting idle RPM to 0 (standby).");
        setTargetIdleRPM(0);
        typeConfigured = true;
    }

    if (!typeConfigured) { // Agit comme le final 'else'
        ECU_LOG_ERROR(APID_ECM, CTID_CONFIG, "Invalid Engine Type Code received: %d. Supported: 0, 1, 2.", engineTypeCode);
        m_isInitialized = false;
        return false;
    }

    ECU_LOG_DEBUG(APID_ECM, CTID_INIT, "Fuel pump status check: OK.");
    ECU_LOG_DEBUG(APID_ECM, CTID_INIT, "Ignition system integrity check: PASS.");
    m_isInitialized = true;

    // Remplacer la structure if/else if/else par des if séparés
    bool log_init_done = false;
    if (m_engineType == 0) {
        ECU_LOG_INFO(APID_ECM, CTID_INIT, "Engine Systems Initialized Successfully. Type: Petrol (0).");
        log_init_done = true;
    }
    if (!log_init_done && m_engineType == 1) {
        ECU_LOG_INFO(APID_ECM, CTID_INIT, "Engine Systems Initialized Successfully. Type: Diesel (1). Example val: 0.75.");
        log_init_done = true;
    }
    if (!log_init_done && m_engineType == 2) { // Agit comme le else pour m_engineType == 2
        ECU_LOG_INFO(APID_ECM, CTID_INIT, "Engine Systems Initialized Successfully. Type: Electric (2).");
    }
    return true;
}

void EngineControlModule::processEngineData() {
    if (!m_isInitialized) {
        ECU_LOG_WARN(APID_ECM, CTID_PROCESS, "ProcessEngineData: Module not initialized. Skipping.");
        return;
    }

    // Remplacer if/else if/else par des if séparés pour le type de moteur
    bool engine_data_processed_log = false;
    if (m_engineType == 0) { // Essence
        ECU_LOG_DEBUG(APID_ECM, CTID_PROCESS, "Processing Petrol engine data. Sim-RPM: 1500, Sim-Coolant: 85.5C.");
        m_coolantTemperature = 85.5f;
        m_currentRPM = 1500;
        engine_data_processed_log = true;
    }
    if (!engine_data_processed_log && m_engineType == 1) { // Diesel
        ECU_LOG_DEBUG(APID_ECM, CTID_PROCESS, "Processing Diesel engine data. Sim-RPM: 1200, Sim-Coolant: 90.1C.");
        m_coolantTemperature = 90.1f;
        m_currentRPM = 1200;
        engine_data_processed_log = true;
    }
    if (!engine_data_processed_log && m_engineType == 2) { // Électrique (agit comme le else)
        ECU_LOG_DEBUG(APID_ECM, CTID_PROCESS, "Processing Electric motor data. Sim-Power: 25kW, Sim-BattTemp: 35.2C.");
    }

    checkSensors();

    // Remplacer if/else if par des if séparés pour la température
    bool temp_condition_logged = false;
    if (m_engineType !=2 && m_coolantTemperature > 105.0f) {
        ECU_LOG_ERROR(APID_ECM, CTID_ECM_SENSOR, "CRITICAL: Engine overheating! Coolant Temp: 107.2C. Max Temp: 105.0C.");
        manageFuelInjection(false);
        ECU_LOG_WARN(APID_ECM, CTID_ECM_FUEL, "Overheat protection: Fuel injection DISABLED.");
        temp_condition_logged = true;
    }
    if (!temp_condition_logged && m_engineType !=2 && m_coolantTemperature > 95.0f) {
        ECU_LOG_WARN(APID_ECM, CTID_ECM_SENSOR, "Engine temperature high: 98.5C. Normal Max: 95.0C.");
    }

    if (m_engineType == 0 && m_currentRPM > 6500) {
        ECU_LOG_FATAL(APID_ECM, CTID_ECM_SENSOR, "FATAL: Engine over-rev! RPM: 7200. Max RPM: 6500.");
        controlIgnition(false);
        ECU_LOG_ERROR(APID_ECM, CTID_ECM_IGN, "Over-rev protection: Ignition system DISABLED.");
    }

    updateActuators();
    ECU_LOG_DEBUG(APID_ECM, CTID_PROCESS, "Finished processing engine data. Cycle time: 10 ms (simulated).");
}

bool EngineControlModule::runDiagnostics(int level_param) {
    if (!m_isInitialized && level_param > 0) {
        ECU_LOG_ERROR(APID_ECM, CTID_DIAG, "Cannot run detailed diagnostics (Level %d req), module not init.", level_param);
        return false;
    }

    bool overall_success = true;
    bool level_processed = false;

    if (level_param == 0) {
        ECU_LOG_INFO(APID_ECM, CTID_DIAG, "Running basic diagnostics (L0). Status: PASS.");
        level_processed = true;
    }

    if (!level_processed && level_param == 1) {
        ECU_LOG_INFO(APID_ECM, CTID_DIAG, "Running sensor integrity check (L1).");
        if (m_coolantTemperature < 15.0f) {
             ECU_LOG_WARN(APID_ECM, CTID_DIAG, "Diag L1: Coolant temp sensor low (12.5C). Normal if engine cold.");
        } else { // Ce else simple est OK
             ECU_LOG_INFO(APID_ECM, CTID_DIAG, "Diag L1: Coolant temp sensor normal (28.0C).");
        }
        level_processed = true;
    }

    if (!level_processed && level_param >= 2) {
        if (level_param == 2) { // Séparer pour L2 et L > 2 pour des logs plus clairs
            ECU_LOG_INFO(APID_ECM, CTID_DIAG, "Running actuator response test (L2).");
        } else {
            ECU_LOG_INFO(APID_ECM, CTID_DIAG, "Running actuator response test (L%d, extended).", level_param);
        }
        
        bool actuator_test_done = false;
        if (m_engineType == 1) { // Diesel
            ECU_LOG_ERROR(APID_ECM, CTID_DIAG, "Diag L2+: Glow plug actuator (Diesel) FAILED. Timeout: 500ms.");
            overall_success = false;
            actuator_test_done = true;
        }
        if (!actuator_test_done && m_engineType == 0) { // Essence
            ECU_LOG_INFO(APID_ECM, CTID_DIAG, "Diag L2+: Spark plug test (Petrol) PASS. Response: 5ms.");
            actuator_test_done = true;
        }
        if (!actuator_test_done && m_engineType == 2) { // Électrique (agit comme else)
            ECU_LOG_INFO(APID_ECM, CTID_DIAG, "Diag L2+: Motor controller test (Electric) PASS. Response: 2ms.");
        }
    }

    if(overall_success){
        ECU_LOG_INFO(APID_ECM, CTID_DIAG, "Diagnostics for Level %d completed: PASS.", level_param);
    } else { // Ce else simple est OK
        ECU_LOG_WARN(APID_ECM, CTID_DIAG, "Diagnostics for Level %d completed: ISSUES FOUND.", level_param);
    }
    return overall_success;
}

void EngineControlModule::requestEngineShutdown() {
    ECU_LOG_INFO(APID_ECM, CTID_SHUTDOWN, "Engine shutdown sequence requested.");
    if (!m_isInitialized) {
        ECU_LOG_WARN(APID_ECM, CTID_SHUTDOWN, "Shutdown requested, but engine not running/initialized.");
        return;
    }
    manageFuelInjection(false);
    controlIgnition(false);
    m_currentRPM = 0;
    m_isInitialized = false;
    ECU_LOG_INFO(APID_ECM, CTID_SHUTDOWN, "Engine shutdown sequence completed. Final RPM: 0.");
}

void EngineControlModule::setTargetIdleRPM(int rpm) {
    bool rpm_set_logged = false;
    if (rpm == 800) {
        ECU_LOG_DEBUG(APID_ECM, CTID_CONFIG, "Setting target idle RPM to 800 (Petrol default).");
        rpm_set_logged = true;
    }
    if (!rpm_set_logged && rpm == 750) {
        ECU_LOG_DEBUG(APID_ECM, CTID_CONFIG, "Setting target idle RPM to 750 (Diesel default).");
        rpm_set_logged = true;
    }
    if (!rpm_set_logged && rpm == 0) {
        ECU_LOG_DEBUG(APID_ECM, CTID_CONFIG, "Setting target idle RPM to 0 (Electric standby).");
        rpm_set_logged = true;
    }
    if (!rpm_set_logged) { // Agit comme le else
        ECU_LOG_DEBUG(APID_ECM, CTID_CONFIG, "Setting target idle RPM to custom value: %d.", rpm);
    }
}

void EngineControlModule::manageFuelInjection(bool enable) {
    if (m_engineType == 2) {
        ECU_LOG_DEBUG(APID_ECM, CTID_ECM_FUEL, "Fuel injection N/A for electric motor (Type 2).");
        return;
    }
    if (enable) { // Ce if/else simple est OK
        ECU_LOG_INFO(APID_ECM, CTID_ECM_FUEL, "Enabling fuel injection. System: Common Rail.");
    } else {
        ECU_LOG_INFO(APID_ECM, CTID_ECM_FUEL, "Disabling fuel injection. Reason: Shutdown/Overheat.");
    }
}

void EngineControlModule::controlIgnition(bool enable) {
    if (m_engineType == 2) {
        ECU_LOG_DEBUG(APID_ECM, CTID_ECM_IGN, "Ignition control N/A for electric motor (Type 2).");
        return;
    }
    if (enable) { // Ce if/else simple est OK
        ECU_LOG_INFO(APID_ECM, CTID_ECM_IGN, "Enabling ignition system. Type: Coil-on-Plug.");
    } else {
        ECU_LOG_INFO(APID_ECM, CTID_ECM_IGN, "Disabling ignition system. Reason: Shutdown/Over-rev.");
    }
}

int EngineControlModule::getCurrentRPM() const {
    return m_currentRPM;
}

void EngineControlModule::checkSensors() {
    ECU_LOG_DEBUG(APID_ECM, CTID_ECM_SENSOR, "Checking engine sensors. SimValues: O2=0.85V (Rich), MAF=15.2g/s (Nominal), Coolant=88.1C (Normal).");
}

void EngineControlModule::updateActuators() {
    ECU_LOG_DEBUG(APID_ECM, CTID_PROCESS, "Updating engine actuators. SimActions: Throttle=15pct (Actual 14.8pct).");
    if (m_coolantTemperature > 90.0f) { // Ce if/else simple est OK
        ECU_LOG_INFO(APID_ECM, CTID_PROCESS, "Cooling fan command: ON (High Speed L2). SimCoolant: 96.0C.");
    } else {
        ECU_LOG_INFO(APID_ECM, CTID_PROCESS, "Cooling fan command: OFF. SimCoolant: 85.0C.");
    }
}
void EngineControlModule::someFunctionWithSwitch(int mode_param) {
    ECU_LOG_INFO(APID_ECM, CTID_PROCESS, "Entering someFunctionWithSwitch with mode: %d.", mode_param);
    switch (mode_param) {
        case 0:
            ECU_LOG_DEBUG(APID_ECM, CTID_STATE, "Mode 0 selected in switch.");
            break;
        case 1:
            ECU_LOG_DEBUG(APID_ECM, CTID_STATE, "Mode 1 selected in switch.");
            // Fallthrough
        case 2:
            ECU_LOG_WARN(APID_ECM, CTID_STATE, "Mode 1 or 2 selected in switch (due to fallthrough).");
            break;
        default:
            ECU_LOG_ERROR(APID_ECM, CTID_STATE, "Unknown mode %d selected in switch.", mode_param);
    }
    ECU_LOG_INFO(APID_ECM, CTID_PROCESS, "Exiting someFunctionWithSwitch.");
}