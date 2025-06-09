// File: src/TransmissionControlModule.cpp
#include "TransmissionControlModule.h"
#include "common/LoggingUtil.h"
#include <string> // Pour std::to_string si besoin

using namespace Automotive::ECUs;

TransmissionControlModule::TransmissionControlModule() :
    m_isInitialized(false),
    m_transmissionType(0), 
    m_selectedGear(0),     
    m_oilTemperature(30.0f)
{
    ECU_LOG_INFO(APID_TCU, CTID_INIT, "TransmissionControlModule constructor. State: Not Initialized. Type: Manual, Gear: N, OilTemp: 30.0C.");
}

TransmissionControlModule::~TransmissionControlModule() {
    if (m_isInitialized) {
        ECU_LOG_INFO(APID_TCU, CTID_SHUTDOWN, "TransmissionControlModule destructor. Was Initialized. Final Gear: N (0 sim).");
    } else {
        ECU_LOG_INFO(APID_TCU, CTID_SHUTDOWN, "TransmissionControlModule destructor. Was NOT initialized.");
    }
}

bool TransmissionControlModule::initialize(int transmissionTypeCode_param) { // Renommer
    bool typeSet = false;
    m_transmissionType = transmissionTypeCode_param; // Mettre à jour le membre

    if (transmissionTypeCode_param == 0) { 
        ECU_LOG_INFO(APID_TCU, CTID_INIT, "Initializing Transmission Systems. Type: Manual (0).");
        ECU_LOG_DEBUG(APID_TCU, CTID_CONFIG, "Manual transmission selected. Clutch monitoring enabled.");
        typeSet = true;
    }
    
    if (!typeSet && transmissionTypeCode_param == 1) { 
        ECU_LOG_INFO(APID_TCU, CTID_INIT, "Initializing Transmission Systems. Type: Automatic (1).");
        ECU_LOG_DEBUG(APID_TCU, CTID_CONFIG, "Automatic transmission. Hydraulic pressure check: Nominal (15.0 bar sim).");
        monitorHydraulicPressure();
        typeSet = true;
    }
    
    if (!typeSet) { 
        ECU_LOG_ERROR(APID_TCU, CTID_CONFIG, "Invalid Transmission Type Code: %d. Supported: 0 (Man), 1 (Auto).", transmissionTypeCode_param);
        m_isInitialized = false;
        return false;
    }

    m_selectedGear = 0; 
    m_isInitialized = true;
    ECU_LOG_INFO(APID_TCU, CTID_INIT, "Transmission Systems Initialized. Current Gear: Neutral (0).");
    return true;
}

void TransmissionControlModule::processTransmissionRequests() {
    if (!m_isInitialized) {
        ECU_LOG_WARN(APID_TCU, CTID_PROCESS, "ProcessTransmissionRequests: Module not initialized. Skipping.");
        return;
    }

    bool type_processed_log = false;
    if (m_transmissionType == 1) { // Automatic
        ECU_LOG_DEBUG(APID_TCU, CTID_PROCESS, "Automatic: Processing shift logic. Sim-RPM: 2200, Sim-Speed: 60km/h.");
        // La logique interne de shiftGearUp utilise m_selectedGear, qui est mis à jour.
        // Les logs dans shiftGearUp utiliseront la valeur mise à jour.
        if (m_selectedGear == 3) { 
             shiftGearUp(); 
        } else if (m_selectedGear == 0 && m_oilTemperature > 40.0f){
             shiftGearUp(); 
        }
        type_processed_log = true;
    }
    if (!type_processed_log && m_transmissionType == 0) { // Manual (agit comme else)
        ECU_LOG_DEBUG(APID_TCU, CTID_PROCESS, "Manual: Awaiting gear shift. Clutch pos: 90pct (Engaged sim).");
    }

    m_oilTemperature += 0.2f; // Logique interne
    bool oil_temp_logged = false;
    if (m_oilTemperature > 120.0f) {
        ECU_LOG_ERROR(APID_TCU, CTID_TCU_HYD, "CRITICAL: Transmission oil OVERHEATING! Temp: 122.5C. Max: 120.0C.");
        requestSafeState();
        oil_temp_logged = true;
    }
    if (!oil_temp_logged && m_oilTemperature > 100.0f) {
        ECU_LOG_WARN(APID_TCU, CTID_TCU_HYD, "Transmission oil temperature HIGH: 105.0C. Recommended Max: 100.0C.");
    }

    ECU_LOG_DEBUG(APID_TCU, CTID_PROCESS, "Finished processing transmission requests for this cycle.");
}

bool TransmissionControlModule::runDiagnostics(int level_param) { // Renommer
    if (!m_isInitialized && level_param > 0) {
        ECU_LOG_ERROR(APID_TCU, CTID_DIAG, "Cannot run TCU diagnostics (Level %d req), module not init.", level_param);
        return false;
    }
    bool success = true;
    bool level_checked = false;

    if (level_param == 0) {
        ECU_LOG_INFO(APID_TCU, CTID_DIAG, "Running basic TCU diagnostics (L0). Solenoid check: PASS.");
        level_checked = true;
    }
    
    if (!level_checked && level_param == 1) {
        ECU_LOG_INFO(APID_TCU, CTID_DIAG, "Running TCU sensor checks (L1).");
        if (m_oilTemperature < 20.0f) { // m_oilTemperature est une variable membre
            ECU_LOG_WARN(APID_TCU, CTID_DIAG, "Diag L1: Oil temp sensor low (18.5C). Normal if cold.");
        } else {
            ECU_LOG_INFO(APID_TCU, CTID_DIAG, "Diag L1: Oil temp sensor nominal (45.0C).");
        }
        level_checked = true;
    }
    
    if (!level_checked && level_param >= 2) {
        if (level_param == 2) {
            ECU_LOG_INFO(APID_TCU, CTID_DIAG, "Running TCU actuator tests (L2).");
        } else {
            ECU_LOG_INFO(APID_TCU, CTID_DIAG, "Running TCU actuator tests (L%d, extended).", level_param);
        }
        
        if (m_transmissionType == 1) { // Automatic
            ECU_LOG_DEBUG(APID_TCU, CTID_DIAG, "Testing shift solenoid 'A'. Resp time: 12ms (OK).");
            ECU_LOG_ERROR(APID_TCU, CTID_DIAG, "Shift solenoid 'B' FAILED. Code: 0xAB12. Current: 0.0A.");
            success = false;
        } else { // Manual
            ECU_LOG_INFO(APID_TCU, CTID_DIAG, "Manual transmission diag (L2+): Clutch sensor calib check: PASS.");
        }
    }

    if(success){
        ECU_LOG_INFO(APID_TCU, CTID_DIAG, "TCU Diagnostics (L%d) completed: PASS.", level_param);
    } else {
        ECU_LOG_WARN(APID_TCU, CTID_DIAG, "TCU Diagnostics (L%d) completed: ISSUES FOUND.", level_param);
    }
    return success;
}

void TransmissionControlModule::requestSafeState() {
    ECU_LOG_WARN(APID_TCU, CTID_STATE, "Transmission safe state requested. Forcing Neutral.");
    m_selectedGear = 0; 
    // m_oilTemperature est une variable membre. Si on veut un log fixe, on fixe la valeur ici aussi.
    ECU_LOG_INFO(APID_TCU, CTID_STATE, "Transmission now in Neutral (Safe State). SimOilTemp: %.1fC.", m_oilTemperature); // Garder la variable est OK
}

void TransmissionControlModule::shiftGearUp() {
    if (!m_isInitialized || m_transmissionType == 0) {
        ECU_LOG_WARN(APID_TCU, CTID_TCU_GEAR, "ShiftGearUp ignored. Reason: Not Auto or Not Initialized.");
        return;
    }
    if (m_selectedGear < 6) { 
        m_selectedGear++;
        // m_selectedGear est une variable membre mise à jour.
        ECU_LOG_INFO(APID_TCU, CTID_TCU_GEAR, "Shifted UP. New gear: %d.", m_selectedGear); 
    } else {
        ECU_LOG_INFO(APID_TCU, CTID_TCU_GEAR, "Already in highest gear (6). Shift up ignored.");
    }
    controlSolenoids();
}

void TransmissionControlModule::shiftGearDown() {
    if (!m_isInitialized || m_transmissionType == 0) {
        ECU_LOG_WARN(APID_TCU, CTID_TCU_GEAR, "ShiftGearDown ignored. Reason: Not Auto or Not Initialized.");
        return;
    }
    if (m_selectedGear > 0) {
        m_selectedGear--;
        ECU_LOG_INFO(APID_TCU, CTID_TCU_GEAR, "Shifted DOWN. New gear: %d.", m_selectedGear);
    } else {
        ECU_LOG_INFO(APID_TCU, CTID_TCU_GEAR, "Already in lowest gear/Neutral. Shift down ignored.");
    }
    controlSolenoids();
}

void TransmissionControlModule::engagePark() {
    if (!m_isInitialized || m_transmissionType == 0) {
        ECU_LOG_WARN(APID_TCU, CTID_TCU_GEAR, "EngagePark ignored. Reason: Not Auto or Not Initialized.");
        return;
    }
    m_selectedGear = 100; // Code pour Park
    ECU_LOG_INFO(APID_TCU, CTID_TCU_GEAR, "Park engaged. Mechanical lock: SECURED.");
}

int TransmissionControlModule::getCurrentGear() const {
    return m_selectedGear;
}

void TransmissionControlModule::monitorHydraulicPressure() {
    ECU_LOG_DEBUG(APID_TCU, CTID_TCU_HYD, "Monitoring hydraulic pressure. Main: 14.8 bar, Clutch: 12.1 bar (Sim).");
    if (m_oilTemperature < 35.0f) { 
        ECU_LOG_INFO(APID_TCU, CTID_TCU_HYD, "Hydraulic pressure low due to cold oil (10.5 bar sim). Warming up.");
    }
}

void TransmissionControlModule::controlSolenoids() {
    // m_selectedGear est une variable membre.
    ECU_LOG_DEBUG(APID_TCU, CTID_TCU_GEAR, "Controlling shift solenoids for gear: %d.", m_selectedGear);
    // Les logs suivants sont conditionnés par la valeur de m_selectedGear, ce qui est OK.
    // Leurs messages sont fixes.
    if (m_selectedGear == 1) {
        ECU_LOG_DEBUG(APID_TCU, CTID_TCU_GEAR, "Solenoid A: ON, Solenoid B: OFF (For Gear 1).");
    } else if (m_selectedGear == 2) {
        ECU_LOG_DEBUG(APID_TCU, CTID_TCU_GEAR, "Solenoid A: OFF, Solenoid B: ON (For Gear 2).");
    } 
}

void TransmissionControlModule::testDoWhileLoop() {
    int counter = 0;
    ECU_LOG_INFO(APID_TCU, CTID_PROCESS, "Starting testDoWhileLoop.");
    do {
        // counter est une variable locale, son usage dans le log est OK.
        ECU_LOG_DEBUG(APID_TCU, CTID_PROCESS, "Do-while iteration: %d.", counter);
        counter++;
        if (counter == 1) {
            ECU_LOG_INFO(APID_TCU, CTID_PROCESS, "Counter is 1 in do-while (fixed message).");
        }
    } while (counter < 3);
    ECU_LOG_INFO(APID_TCU, CTID_PROCESS, "Finished testDoWhileLoop.");
}