// AutoSystemSim/common/logger.h
#ifndef LOGGER_H
#define LOGGER_H

#include <iostream> // Pour une implémentation simple (printf ou std::cout)
#include <string>
#include <chrono>   // Pour un timestamp simple
#include <iomanip>  // Pour std::put_time

// Fonction utilitaire pour le timestamp (pas indispensable pour l'analyse statique, mais bon à avoir)
inline std::string getCurrentTimestamp() {
    auto now = std::chrono::system_clock::now();
    auto in_time_t = std::chrono::system_clock::to_time_t(now);
    std::stringstream ss;
    ss << std::put_time(std::localtime(&in_time_t), "%Y-%m-%d %X");
    return ss.str();
}

// Nos macros de log. L'important pour l'analyse statique est le nom de la macro.
// On ajoute __FILE__ et __LINE__ qui sont souvent utiles et que l'analyseur pourrait vouloir récupérer.
// Pour l'instant, APID et CTID ne sont pas passés explicitement ici,
// on considérera que l'analyseur les déduit du contexte du fichier/module.

#define LOG_IMPL(level, file, line, ...) \
    do { \
        std::cout << "[" << getCurrentTimestamp() << "] [" << level << "] [" \
                  << file << ":" << line << "] "; \
        printf(__VA_ARGS__); /* Utilisation de printf pour gérer les arguments variables facilement */ \
        std::cout << std::endl; \
    } while (0)

#define LOG_FATAL(...)   LOG_IMPL("FATAL", __FILE__, __LINE__, __VA_ARGS__)
#define LOG_ERROR(...)   LOG_IMPL("ERROR", __FILE__, __LINE__, __VA_ARGS__)
#define LOG_WARNING(...) LOG_IMPL("WARNING", __FILE__, __LINE__, __VA_ARGS__)
#define LOG_INFO(...)    LOG_IMPL("INFO", __FILE__, __LINE__, __VA_ARGS__)
#define LOG_DEBUG(...)   LOG_IMPL("DEBUG", __FILE__, __LINE__, __VA_ARGS__)

// On définit aussi LOG_VERBOSE même si on va l'ignorer, pour être complet
#define LOG_VERBOSE(...) LOG_IMPL("VERBOSE", __FILE__, __LINE__, __VA_ARGS__)

#endif // LOGGER_H