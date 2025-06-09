// File: src/InfotainmentModule.cpp
#include "InfotainmentModule.h"
#include "common/LoggingUtil.h"
#include <string>

using namespace Automotive::ECUs;

InfotainmentModule::InfotainmentModule() :
    m_isInitialized(false),
    m_currentLanguage("EN_US"),
    m_currentVolume(50),
    m_nowPlaying("FM Radio - 98.5 MHz")
{
    ECU_LOG_INFO(APID_IHU, CTID_INIT, "InfotainmentModule constructor. Lang: EN_US, Vol: 50, NowPlaying: FM Radio - 98.5 MHz.");
}

InfotainmentModule::~InfotainmentModule() {
    bool logged = false;
    if (m_nowPlaying == "Bohemian Rhapsody - USB") {
        ECU_LOG_INFO(APID_IHU, CTID_SHUTDOWN, "InfotainmentModule destructor. Last playing: 'Bohemian Rhapsody - USB'.");
        logged = true;
    }
    if (!logged && m_nowPlaying == "FM Radio - 98.5 MHz") {
        ECU_LOG_INFO(APID_IHU, CTID_SHUTDOWN, "InfotainmentModule destructor. Last playing: 'FM Radio - 98.5 MHz'.");
        logged = true;
    }
    if (!logged) {
        ECU_LOG_INFO(APID_IHU, CTID_SHUTDOWN, "InfotainmentModule destructor. Last playing: [Other Media Source].");
    }
}

bool InfotainmentModule::initialize(const std::string& systemLanguageParam) {
    bool languageSet = false;
    m_currentLanguage = "EN_US"; // Default avant vérification

    if (systemLanguageParam == "FR_CA") {
        ECU_LOG_INFO(APID_IHU, CTID_INIT, "Initializing IHU. Requested Lang: 'FR_CA'. Setting current.");
        m_currentLanguage = "FR_CA";
        ECU_LOG_DEBUG(APID_IHU, CTID_CONFIG, "HMI assets for 'FR_CA'. Load time: 250 ms.");
        languageSet = true;
    }

    if (!languageSet && systemLanguageParam == "EN_US") {
        ECU_LOG_INFO(APID_IHU, CTID_INIT, "Initializing IHU. Requested Lang: 'EN_US'. Setting current.");
        m_currentLanguage = "EN_US"; // Déjà le défaut, mais explicite ici
        ECU_LOG_DEBUG(APID_IHU, CTID_CONFIG, "HMI assets for 'EN_US'. Load time: 220 ms.");
        languageSet = true;
    }

    if (!languageSet) {
        ECU_LOG_WARN(APID_IHU, CTID_CONFIG, "Unsupported language requested. Defaulting to EN_US.");
        // m_currentLanguage est déjà EN_US
        ECU_LOG_INFO(APID_IHU, CTID_INIT, "Initializing IHU with default language: 'EN_US'.");
    }

    ECU_LOG_DEBUG(APID_IHU, CTID_IHU_CONNECT, "Bluetooth module status: OK. Paired devices: 0 (initial).");
    checkMediaSources();

    m_isInitialized = true;

    bool log_init_lang_done = false;
    if (m_currentLanguage == "FR_CA") {
        ECU_LOG_INFO(APID_IHU, CTID_INIT, "IHU Initialized. Active Language is 'FR_CA'.");
        log_init_lang_done = true;
    }
    if (!log_init_lang_done && m_currentLanguage == "EN_US") { // Plus explicite
        ECU_LOG_INFO(APID_IHU, CTID_INIT, "IHU Initialized. Active Language is 'EN_US'.");
    }
    return true;
}

void InfotainmentModule::processUserInput(int inputType, int inputValue) {
    if (!m_isInitialized) {
        ECU_LOG_WARN(APID_IHU, CTID_PROCESS, "ProcessUserInput: IHU not initialized. Skipping.");
        return;
    }
    bool inputHandled = false;

    if (inputType == 1 && inputValue == 10) {
        ECU_LOG_DEBUG(APID_IHU, CTID_IHU_HMI, "User input: VolumeKnob UP. Target vol: 60pct.");
        setVolumeLevel(60);
        inputHandled = true;
    }

    if (!inputHandled && inputType == 1 && inputValue == -10) {
        ECU_LOG_DEBUG(APID_IHU, CTID_IHU_HMI, "User input: VolumeKnob DOWN. Target vol: 40pct.");
        setVolumeLevel(40);
        inputHandled = true;
    }

    if (!inputHandled && inputType == 4 && inputValue == 101) {
        ECU_LOG_INFO(APID_IHU, CTID_IHU_HMI, "User input: Button NAV (ID 101). Switching to Nav.");
        displayNavigationRoute("123 Main St, Anytown");
        inputHandled = true;
    }

    if (!inputHandled && inputType == 2 && inputValue == 320) {
         ECU_LOG_DEBUG(APID_IHU, CTID_IHU_HMI, "User input: Touchscreen press at X=320, Y=240 (Example values).");
         inputHandled = true;
    }

    if (!inputHandled) {
        // Garder les variables pour ce log de débogage car elles sont importantes pour comprendre l'échec
        ECU_LOG_DEBUG(APID_IHU, CTID_IHU_HMI, "User input: Unhandled type/value. Type: %d, Value: %d.", inputType, inputValue);
    }

    updateDisplayContent();
    ECU_LOG_DEBUG(APID_IHU, CTID_PROCESS, "Finished processing user input cycle.");
}

bool InfotainmentModule::runDiagnostics(int level_param) {
    if (!m_isInitialized && level_param > 0) {
        ECU_LOG_ERROR(APID_IHU, CTID_DIAG, "Cannot run IHU diagnostics (L%d req), module not init.", level_param);
        return false;
    }
    bool success = true;
    bool level_checked = false;

    if (level_param == 0) {
        ECU_LOG_INFO(APID_IHU, CTID_DIAG, "Running basic IHU diagnostics (L0). Display: OK. Touch: OK.");
        level_checked = true;
    }
    
    if (!level_checked && level_param == 1) {
        ECU_LOG_INFO(APID_IHU, CTID_DIAG, "Running IHU peripheral checks (L1).");
        ECU_LOG_DEBUG(APID_IHU, CTID_IHU_CONNECT, "GPS Antenna: -75dBm, Sats: 8 (fixed sim values).");
        ECU_LOG_WARN(APID_IHU, CTID_IHU_MEDIA, "USB Port 1: No device. Status: 0xFF (NoPwr, fixed sim).");
        level_checked = true;
    }
    
    if (!level_checked && level_param >= 2) {
        ECU_LOG_INFO(APID_IHU, CTID_DIAG, "Running IHU internal component tests (L%d).", level_param);
        ECU_LOG_DEBUG(APID_IHU, CTID_IHU_AUDIO, "Audio DSP self-test: PASS. Channels: 4 (fixed sim).");
        ECU_LOG_ERROR(APID_IHU, CTID_IHU_HMI, "Touchscreen controller calibrate FAILED. Code: 0xE10F. Attempts: 3 (fixed sim).");
        success = false;
    }

    if(success){
        ECU_LOG_INFO(APID_IHU, CTID_DIAG, "IHU Diagnostics (L%d) completed: PASS.", level_param);
    } else {
        ECU_LOG_WARN(APID_IHU, CTID_DIAG, "IHU Diagnostics (L%d) completed: ISSUES FOUND.", level_param);
    }
    return success;
}

void InfotainmentModule::shutdownDisplay() {
    ECU_LOG_INFO(APID_IHU, CTID_IHU_HMI, "Shutting down main display. Panel power: OFF. Standby mode: ACTIVATED.");
}

void InfotainmentModule::playAudioTrack(const std::string& trackNameParam) {
    bool trackPlayed = false;
    if (trackNameParam == "Bohemian Rhapsody") {
        ECU_LOG_INFO(APID_IHU, CTID_IHU_MEDIA, "Playing audio: 'Bohemian Rhapsody'. Src: USB. Len: 354s.");
        m_nowPlaying = "Bohemian Rhapsody - USB"; 
        trackPlayed = true;
    }
    
    if (!trackPlayed && trackNameParam == "FM Radio - 101.1 MHz") {
        ECU_LOG_INFO(APID_IHU, CTID_IHU_MEDIA, "Playing radio: 'FM Radio - 101.1 MHz'. RDS: 'Rock Classics'.");
        m_nowPlaying = "FM Radio - 101.1 MHz";
        trackPlayed = true;
    }
    
    if (!trackPlayed) {
        ECU_LOG_INFO(APID_IHU, CTID_IHU_MEDIA, "Playing audio: [Dynamic Track from Bluetooth]. Codec: AAC.");
        m_nowPlaying = trackNameParam + " - Bluetooth"; 
    }
    ECU_LOG_DEBUG(APID_IHU, CTID_IHU_AUDIO, "Audio output routed. Current Volume: %d pct.", m_currentVolume);
}

void InfotainmentModule::setVolumeLevel(int volume) {
    if (volume < 0) volume = 0;
    if (volume > 100) volume = 100;
    m_currentVolume = volume;

    ECU_LOG_INFO(APID_IHU, CTID_IHU_AUDIO, "Volume level set to: %d pct.", m_currentVolume);
    
    bool volume_log_done = false;
    if (m_currentVolume == 0) {
        ECU_LOG_INFO(APID_IHU, CTID_IHU_AUDIO, "Audio MUTED (volume is 0).");
        volume_log_done = true;
    }
    if (!volume_log_done && m_currentVolume == 100) {
        ECU_LOG_WARN(APID_IHU, CTID_IHU_AUDIO, "Volume at MAX (100pct). Amplifier gain: 0dB (sim).");
    }
}

void InfotainmentModule::displayNavigationRoute(const std::string& destinationParam) {
    bool routeDisplayed = false;
    if (destinationParam == "Home") {
        ECU_LOG_INFO(APID_IHU, CTID_IHU_NAV, "Displaying nav route to: 'Home'. ETA: 15min. Dist: 12.3km.");
        routeDisplayed = true;
    }
    
    if (!routeDisplayed && destinationParam == "Work") {
        ECU_LOG_INFO(APID_IHU, CTID_IHU_NAV, "Displaying nav route to: 'Work Office'. ETA: 25min. Dist: 22.7km.");
        routeDisplayed = true;
    }
    
    if (!routeDisplayed) {
        // Remplacer le %s par un message fixe pour éviter .c_str() pour ce cas.
        ECU_LOG_INFO(APID_IHU, CTID_IHU_NAV, "Displaying nav route to: [User Defined Address]. Calculating... ETA: N/A.");
    }
    ECU_LOG_DEBUG(APID_IHU, CTID_IHU_NAV, "Map data version: '2024.Q1_EU'. GPS: Strong. Satellites: 9.");
}

void InfotainmentModule::showSystemMessage(const std::string& messageParam, int durationMs_param) {
    bool messageShown = false;
    if (messageParam == "LowFuel") {
         ECU_LOG_INFO(APID_IHU, CTID_IHU_HMI, "SysMsg: 'Warning: Low Fuel'. Duration: 5000ms (fixed). Prio: HIGH.");
         messageShown = true;
    }
    
    if (!messageShown && messageParam == "UpdateComplete") {
         ECU_LOG_INFO(APID_IHU, CTID_IHU_HMI, "SysMsg: 'Software Update Completed'. Duration: 3000ms (fixed). New Ver: 2.3.1.");
         messageShown = true;
    }
    
    if (!messageShown) {
        // Remplacer le %s par un message fixe et fixer la durée aussi pour éviter les variables ici.
        ECU_LOG_INFO(APID_IHU, CTID_IHU_HMI, "SysMsg: [User-defined content received]. Duration: 4000ms (fixed). Type: Gen.");
    }
}

// --- Fonctions privées ---
void InfotainmentModule::updateDisplayContent() {
    ECU_LOG_DEBUG(APID_IHU, CTID_IHU_HMI, "Updating display. Screen: 'Main Menu'. Widgets: 3. Brightness: 80pct (sim).");
    if (m_nowPlaying.find("Radio") != std::string::npos) {
        ECU_LOG_DEBUG(APID_IHU, CTID_IHU_HMI, "Display: Radio widget active. Info: Default Station. Signal: -65dBm.");
    } else { 
        ECU_LOG_DEBUG(APID_IHU, CTID_IHU_HMI, "Display: Media player widget active. Info: Default Track. Progress: 35pct.");
    }
}

void InfotainmentModule::manageBluetoothConnections() {
    ECU_LOG_DEBUG(APID_IHU, CTID_IHU_CONNECT, "BT Mgmt. Connected: 1 ('MyPhone_Pixel8'). Profile: A2DP/HFP. RSSI: -55dBm.");
    bool new_device_pairing_attempt = false; 
    if (new_device_pairing_attempt) { 
        ECU_LOG_INFO(APID_IHU, CTID_IHU_CONNECT, "New BT pair req from 'UnknownDev_BT5.2'. PIN: 1234. Status: PendingUserAuth.");
    }
}

void InfotainmentModule::checkMediaSources() {
    ECU_LOG_DEBUG(APID_IHU, CTID_IHU_MEDIA, "Checking media sources (USB/SD/AUX).");
    ECU_LOG_INFO(APID_IHU, CTID_IHU_MEDIA, "USB1: 'SanDisk_32GB' (exFAT). Tracks: 250. Status: Mounted,Readable.");
    ECU_LOG_INFO(APID_IHU, CTID_IHU_MEDIA, "SDCard: No card inserted. Slot Status: Empty,Ready.");
    ECU_LOG_INFO(APID_IHU, CTID_IHU_MEDIA, "AUX: No signal detected. Line input level: 0.0V (checked).");
}