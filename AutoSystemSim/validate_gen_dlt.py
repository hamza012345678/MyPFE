import json
import os
import glob # Pour trouver les fichiers
import datetime

# --- CONFIGURATION ---
DLT_JSON_DIR = "simulated_dlts_json_profiles" # Le dossier où sont vos DLTs JSON
EXPECTED_LOG_LEVELS = {"FATAL", "ERROR", "WARN", "INFO", "DEBUG"}
EXPECTED_DLT_TYPE = "LOG"

# Optionnel: Liste des ApID/CtID connus (pourrait être chargé d'un fichier)
# KNOWN_APIDS = {"VCTRL", "ECM", "TCU", "ABS", "BCM", "IHU", "SYSTEM", "SCENARIO"}
# KNOWN_CTIDS = {"INIT", "PROC", "DIAG", ...}


def validate_dlt_record(record, record_idx, filename):
    """Valide un seul enregistrement DLT."""
    errors = []
    warnings = []

    # 1. Vérification des champs obligatoires
    expected_fields = ["Index", "Timestamp", "Time", "ECUId", "ApID", "CtID", "SessionID", "Type", "LogLevel", "Payload"]
    for field in expected_fields:
        if field not in record:
            errors.append(f"Champ manquant '{field}'")

    if errors: # Si des champs clés manquent, inutile de continuer pour ce record
        return errors, warnings

    # 2. Vérification des types (basique)
    if not isinstance(record["Index"], int): errors.append(f"Index n'est pas un entier: {record['Index']}")
    if not isinstance(record["Timestamp"], float): errors.append(f"Timestamp n'est pas un float: {record['Timestamp']}")
    if not isinstance(record["Time"], str): errors.append(f"Time n'est pas une chaîne: {record['Time']}")
    # ... (vérifier les autres types si nécessaire, ex: ApID, CtID, etc. sont des chaînes)

    # 3. Vérification de LogLevel
    if record["LogLevel"] not in EXPECTED_LOG_LEVELS:
        errors.append(f"LogLevel inconnu: '{record['LogLevel']}'")

    # 4. Vérification du Type DLT
    if record["Type"] != EXPECTED_DLT_TYPE:
        warnings.append(f"Type DLT inattendu: '{record['Type']}', attendu '{EXPECTED_DLT_TYPE}'")
    
    # 5. Vérification du Payload
    if "%" in record["Payload"] and not "%%" in record["Payload"].replace("\u001A","%%"): # Vérifier les placeholders non résolus (sauf %%)
        # \u001A est notre substitution temporaire pour %%
        # On revérifie s'il reste des % simples qui ne sont pas des %%
        # Ceci est une heuristique, un vrai % dans le message original pourrait causer un faux positif
        cleaned_payload_for_percent_check = record["Payload"].replace("\u001A", "") # Enlever les substituts
        if "%" in cleaned_payload_for_percent_check:
             warnings.append(f"Payload contient potentiellement un placeholder non résolu: '{record['Payload'][:50]}...'")
    if "[UNHANDLED_PH" in record["Payload"] or "[FMT_ERR" in record["Payload"] or "[ERR_PH" in record["Payload"]:
        warnings.append(f"Payload contient un indicateur d'erreur de formatage: '{record['Payload'][:50]}...'")
    if "_FALLBACK_STR]" in record["Payload"] or "[VAR]" in record["Payload"]: # Ajuster selon vos fallbacks
        warnings.append(f"Payload utilise une valeur de fallback générique: '{record['Payload'][:50]}...'")

    return errors, warnings


def validate_dlt_file(filepath):
    """Valide un fichier DLT JSON entier."""
    print(f"\nValidating: {filepath}...")
    errors = []
    warnings = []
    
    try:
        with open(filepath, "r", encoding="utf-8") as f:
            dlt_data = json.load(f)
    except json.JSONDecodeError as e:
        errors.append(f"Erreur de décodage JSON: {e}")
        return errors, warnings # Inutile de continuer si le JSON n'est pas valide
    except Exception as e:
        errors.append(f"Erreur lors de l'ouverture/lecture du fichier: {e}")
        return errors, warnings

    if not isinstance(dlt_data, list):
        errors.append("Le contenu du JSON n'est pas une liste d'enregistrements.")
        return errors, warnings

    if not dlt_data:
        warnings.append("Le fichier DLT est vide (aucune trace).")
        return errors, warnings

    last_timestamp = 0.0
    last_index = 0
    expected_session_id = None

    for i, record in enumerate(dlt_data):
        record_errors, record_warnings = validate_dlt_record(record, i, os.path.basename(filepath))
        if record_errors:
            errors.append(f"  Record {i+1} (Index {record.get('Index','N/A')}): {', '.join(record_errors)}")
        if record_warnings:
            warnings.append(f"  Record {i+1} (Index {record.get('Index','N/A')}): {', '.join(record_warnings)}")

        # Vérifications sur l'ensemble du fichier
        current_idx = record.get("Index")
        if current_idx is not None:
            if i == 0 and current_idx != 1:
                errors.append(f"  Le premier Index devrait être 1, obtenu {current_idx}")
            elif i > 0 and current_idx != last_index + 1:
                errors.append(f"  Séquence d'Index rompue: attendu {last_index + 1}, obtenu {current_idx}")
            last_index = current_idx
        
        current_ts = record.get("Timestamp")
        if current_ts is not None:
            if current_ts < last_timestamp and i > 0 : # Permettre le premier timestamp d'être ce qu'il est
                warnings.append(f"  Timestamp non croissant: {current_ts} < {last_timestamp} à l'Index {current_idx}")
            last_timestamp = current_ts
            
            # Vérifier la cohérence Time vs Timestamp
            time_str = record.get("Time")
            if time_str:
                try:
                    dt_from_str = datetime.datetime.strptime(time_str.split('.')[0], "%Y-%m-%d %H:%M:%S")
                    # Comparaison approximative car la conversion peut avoir de petites différences de float
                    if abs(dt_from_str.timestamp() - float(int(current_ts))) > 1: # Comparer la partie entière en secondes
                         warnings.append(f"  Incohérence Time string vs Timestamp: '{time_str}' vs {current_ts} à l'Index {current_idx}")
                except ValueError:
                    errors.append(f"  Format de chaîne 'Time' incorrect: '{time_str}' à l'Index {current_idx}")


        if i == 0:
            expected_session_id = record.get("SessionID")
        elif record.get("SessionID") != expected_session_id:
            errors.append(f"  SessionID incohérent dans le fichier: vu {record.get('SessionID')}, attendu {expected_session_id} à l'Index {current_idx}")
            
    if not errors and not warnings:
        print("  Validation: OK")
    else:
        if errors:
            print(f"  Validation FAILED with {len(errors)} errors.")
        if warnings:
            print(f"  Validation PASSED with {len(warnings)} warnings.")
            
    return errors, warnings

# --- MAIN EXECUTION ---
if __name__ == "__main__":
    all_files_errors = {}
    all_files_warnings = {}
    
    json_files = glob.glob(os.path.join(DLT_JSON_DIR, "*.json"))
    
    if not json_files:
        print(f"Aucun fichier .json trouvé dans le dossier: {DLT_JSON_DIR}")
    else:
        print(f"Found {len(json_files)} DLT JSON files to validate.")

        for filepath in json_files:
            errors, warnings = validate_dlt_file(filepath)
            if errors:
                all_files_errors[os.path.basename(filepath)] = errors
            if warnings:
                all_files_warnings[os.path.basename(filepath)] = warnings
        
        print("\n--- Validation Summary ---")
        if not all_files_errors and not all_files_warnings:
            print("All DLT files validated successfully with no errors or warnings.")
        else:
            if all_files_errors:
                print(f"\n{len(all_files_errors)} file(s) with ERRORS:")
                for filename, err_list in all_files_errors.items():
                    print(f"  File: {filename}")
                    for err in err_list:
                        print(f"    - {err}")
            if all_files_warnings:
                print(f"\n{len(all_files_warnings)} file(s) with WARNINGS:")
                for filename, warn_list in all_files_warnings.items():
                    print(f"  File: {filename}")
                    for warn in warn_list:
                        print(f"    - {warn}")
    
    print("\nDLT validation process finished.")