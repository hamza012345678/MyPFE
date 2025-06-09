// File: include/common/LoggingUtil.h
#pragma once

#include <stdio.h> // Pour un printf simple en guise de log.
                   // Dans un vrai système, ce serait une lib de log spécifique (ex: DLT).

// Niveaux de log (alignés sur DLT autant que possible)
// Ces valeurs numériques sont arbitraires ici, mais pourraient correspondre
// à celles d'une vraie bibliothèque de log si nécessaire.
#define LOG_LEVEL_FATAL   0x1
#define LOG_LEVEL_ERROR   0x2
#define LOG_LEVEL_WARN    0x3 // DLT utilise WARN, pas WARNING
#define LOG_LEVEL_INFO    0x4
#define LOG_LEVEL_DEBUG   0x5
// VERBOSE (0x6 DLT_LOG_VERBOSE) est négligé comme demandé.

// Macros de log SIMPLIFIÉES.
// Le format est : ECU_LOG_LEVEL(ApID, CtID, FormatString, ...)
// ApID et CtID sont des chaînes littérales.
// Les arguments de FormatString DOIVENT être des littéraux (nombres, chaînes)
// pour que notre parseur statique puisse les extraire facilement pour les DLT simulés.
// L'utilisation de do {...} while(0) est une astuce pour rendre la macro "safe"
// dans tous les contextes (ex: if/else sans accolades).

#define ECU_LOG_FATAL(APID, CTID, ...)   do { \
                                            printf("[FATAL] [%s:%s] ", APID, CTID); \
                                            printf(__VA_ARGS__); \
                                            printf("\n"); \
                                         } while(0)

#define ECU_LOG_ERROR(APID, CTID, ...)   do { \
                                            printf("[ERROR] [%s:%s] ", APID, CTID); \
                                            printf(__VA_ARGS__); \
                                            printf("\n"); \
                                         } while(0)

#define ECU_LOG_WARN(APID, CTID, ...)    do { \
                                            printf("[WARN ] [%s:%s] ", APID, CTID); \
                                            printf(__VA_ARGS__); \
                                            printf("\n"); \
                                         } while(0)

#define ECU_LOG_INFO(APID, CTID, ...)    do { \
                                            printf("[INFO ] [%s:%s] ", APID, CTID); \
                                            printf(__VA_ARGS__); \
                                            printf("\n"); \
                                         } while(0)

#define ECU_LOG_DEBUG(APID, CTID, ...)   do { \
                                            printf("[DEBUG] [%s:%s] ", APID, CTID); \
                                            printf(__VA_ARGS__); \
                                            printf("\n"); \
                                         } while(0)

/*
 * Note sur les printf:
 * Maintenant qu'ils sont décommentés, ils permettront à libclang de voir
 * des CALL_EXPR concrets dans l'expansion des macros, ce qui devrait
 * grandement faciliter l'extraction des arguments par l'analyseur statique.
 */

// --- Application IDs (ApID) ---
#define APID_VCTRL     "VCTRL"  // Vehicle Controller
#define APID_ECM       "ECM"    // Engine Control Module
#define APID_TCU       "TCU"    // Transmission Control Module
#define APID_ABS       "ABS"    // Braking System Module (ABS/ESP)
#define APID_BCM       "BCM"    // Body Control Module
#define APID_IHU       "IHU"    // Infotainment Head Unit
#define APID_SYSTEM    "SYSTEM" // Pour les logs de main.cpp ou très généraux

// --- Context IDs (CtID) - Génériques ---
#define CTID_DEFAULT   "CTRL"   // Contexte par défaut pour un module
#define CTID_INIT      "INIT"   // Initialisation
#define CTID_SHUTDOWN  "SHTDWN" // Séquence d'arrêt
#define CTID_CONFIG    "CONF"   // Configuration
#define CTID_COMM      "COMM"   // Communication (inter-ECU, bus CAN etc.)
#define CTID_PROCESS   "PROC"   // Traitement général / boucle principale
#define CTID_DIAG      "DIAG"   // Diagnostics
#define CTID_STATE     "STATE"  // Changements d'état, gestion d'état
#define CTID_ERROR     "ERR"    // Gestion d'erreur spécifique non couverte par le niveau de log

// --- Context IDs (CtID) - Spécifiques à certains ApIDs (exemples) ---

// Pour APID_ECM (Engine Control Module)
#define CTID_ECM_FUEL    "FUEL"   // Système de carburant
#define CTID_ECM_IGN     "IGNIT"  // Système d'allumage
#define CTID_ECM_SENSOR  "SENSOR" // Capteurs moteur (RPM, temp, pression)
#define CTID_ECM_TORQUE  "TORQUE" // Gestion du couple moteur
#define CTID_ECM_EMISS   "EMISS"  // Contrôle des émissions

// Pour APID_TCU (Transmission Control Module)
#define CTID_TCU_GEAR    "GEAR"   // Changement de vitesse, logique de sélection
#define CTID_TCU_HYD     "HYDRAU" // Système hydraulique (si applicable)
#define CTID_TCU_CLUTCH  "CLUTCH" // Embrayage
#define CTID_TCU_MODE    "MODE"   // Mode de conduite (Eco, Sport)

// Pour APID_ABS (Braking System Module)
#define CTID_ABS_WHEEL   "WHEEL"  // Capteurs de vitesse de roue, contrôle individuel
#define CTID_ABS_PUMP    "PUMP"   // Pompe hydraulique ABS
#define CTID_ABS_STABIL  "STABIL" // Logique ESP / Contrôle de stabilité

// Pour APID_BCM (Body Control Module)
#define CTID_BCM_LIGHT   "LIGHT"  // Gestion des lumières (phares, clignotants)
#define CTID_BCM_ACCESS  "ACCESS" // Accès véhicule (portes, verrouillage)
#define CTID_BCM_WIPER   "WIPER"  // Essuie-glaces
#define CTID_BCM_WINDOW  "WINDOW" // Lève-vitres
#define CTID_BCM_HVAC    "HVAC"   // Chauffage, Ventilation, Climatisation (contrôles de base)

// Pour APID_IHU (Infotainment Head Unit)
#define CTID_IHU_MEDIA   "MEDIA"  // Lecteur Média (Audio/Vidéo)
#define CTID_IHU_HMI     "HMI"    // Interface Homme-Machine (Écran, boutons)
#define CTID_IHU_NAV     "NAV"    // Système de Navigation
#define CTID_IHU_CONNECT "CNCT"   // Connectivité (Bluetooth, WiFi, USB) // Notez bien ce nom pour vos .cpp
#define CTID_IHU_AUDIO   "AUDIO"  // Gestion du son (volume, sources)

// Pour APID_VCTRL (Vehicle Controller)
#define CTID_VCTRL_STARTUP  "START"  // Séquence de démarrage du véhicule
#define CTID_VCTRL_LOOP     "LOOP"   // Boucle principale du véhicule
#define CTID_VCTRL_PWRMGMT  "PWR"    // Gestion de l'alimentation
#define CTID_VCTRL_NETMGMT  "NET"    // Gestion du réseau (ex: mode veille CAN)

// Pour APID_SYSTEM (main.cpp, etc.)
#define CTID_SYS_BOOT    "BOOT"   // Démarrage de l'application
#define CTID_SYS_MAIN    "MAIN"   // Exécution dans la fonction main

// #endif // COMMON_LOGGINGUTIL_H // Cette ligne était déjà correctement supprimée.