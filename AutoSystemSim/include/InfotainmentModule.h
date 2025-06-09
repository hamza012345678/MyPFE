// File: include/InfotainmentModule.h
#pragma once

#include <string> // Pour les messages, noms de pistes, etc.

namespace Automotive {
namespace ECUs {

class InfotainmentModule {
public:
    InfotainmentModule();
    ~InfotainmentModule();

    bool initialize(const std::string& systemLanguage);
    void processUserInput(int inputType, int inputValue); // e.g., button press, touchscreen
    bool runDiagnostics(int level);
    void shutdownDisplay();

    // Fonctions spécifiques à l'IHU
    void playAudioTrack(const std::string& trackName);
    void setVolumeLevel(int volume); // 0-100
    void displayNavigationRoute(const std::string& destination);
    void showSystemMessage(const std::string& message, int durationMs);

private:
    bool m_isInitialized;
    std::string m_currentLanguage;
    int m_currentVolume;
    std::string m_nowPlaying;
    // ... autres états internes ...

    void updateDisplayContent();
    void manageBluetoothConnections();
    void checkMediaSources(); // USB, Radio, etc.
};

} // namespace ECUs
} // namespace Automotive