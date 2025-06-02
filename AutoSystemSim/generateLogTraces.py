# generateLogTraces.py

import json
import os
import re

# --- CONFIGURATION ---
INPUT_JSON_FILE = "static_analysis_scheme_clang.json" # Doit être généré par parsingCodeBaseClang.py avec DEBUG_TARGET_FILE_REL_PATH = None
OUTPUT_LOGS_TEXT_DIR = "logsText_generated_traces" # Nouveau nom pour éviter confusion
MAX_TRACE_DEPTH = 10 # Augmenté un peu
SIMULATE_LOOP_ITERATIONS = 1 # Combien de fois simuler le corps d'une boucle
TRACE_IF_THEN = True
TRACE_IF_ELSE = True # Mettre à False pour ne pas tracer la branche else par défaut

# --- FIN CONFIGURATION ---

# Fonctions utilitaires (sanitize_for_filename, find_matching_usr_keys - peuvent rester les mêmes que votre version)
def sanitize_for_filename(text):
    if not text: return "_empty_or_none_"
    text = str(text)
    text = text.replace("::", "_NS_")
    text = re.sub(r'[<>:"/\\|?*]', '_', text)
    text = re.sub(r"[^\w_.-]", "_", text) # Conserver points et tirets
    text = text.strip('_.- ')
    max_len = 100 # Limiter la longueur pour éviter des noms de fichiers trop longs
    if len(text) > max_len: text = text[:max_len] + "_TRUNC"
    return text if text else "_sanitized_empty_"

# Plus besoin de LOG_MACRO_NAMES, LOG_ARG_EXTRACT_PATTERNS, parse_log_arguments_from_string ici
# car ces infos sont déjà dans le JSON d'entrée.

def find_matching_usr_keys(all_functions_data_dict, patterns_or_criteria):
    # Cette fonction peut rester telle quelle, elle est utile pour sélectionner les points d'entrée
    matched_usr_keys = set()
    if not patterns_or_criteria: # Si vide, tracer toutes les fonctions (peut être beaucoup !)
        print("ℹ️ Aucun critère de point d'entrée fourni, tentative de tracer toutes les fonctions trouvées.")
        return list(all_functions_data_dict.keys())

    for criteria_pattern in patterns_or_criteria:
        for func_key, data in all_functions_data_dict.items(): 
            display_name = data.get("display_signature", "") 
            if criteria_pattern == display_name or criteria_pattern == func_key: 
                matched_usr_keys.add(func_key)
                continue
            # Permettre des correspondances partielles ou de fin de chaîne
            if display_name and display_name.endswith(criteria_pattern):
                matched_usr_keys.add(func_key)
                continue
            # Permettre la correspondance du nom de fonction simple sans namespace
            if display_name and "::" not in criteria_pattern and display_name.endswith("::" + criteria_pattern):
                matched_usr_keys.add(func_key)
                continue
            # Correspondance générique "contains" (peut être trop large)
            # if display_name and criteria_pattern in display_name: 
            # matched_usr_keys.add(func_key)
    
    if not matched_usr_keys and patterns_or_criteria:
        print(f"⚠️ Aucune clé de fonction trouvée correspondant aux critères : {patterns_or_criteria}")
        # ... (le reste de la logique d'aide pour afficher des exemples) ...
    return list(matched_usr_keys)


# Nouvelle fonction récursive pour traiter les execution_elements
def process_execution_elements(elements, all_functions_dict, current_log_sequence, depth, indent_level, call_stack):
    if depth > MAX_TRACE_DEPTH:
        current_log_sequence.append(f"{'  ' * indent_level} L? PROFONDEUR MAX ATTEINTE (dans elements)")
        return

    for item in elements:
        item_type = item.get("type")
        item_line = item.get("line", "?")

        if item_type == "LOG":
            log_level = item['level']
            # Utiliser la chaîne de format nettoyée du JSON
            fmt_str = item.get('log_format_string', "[FormatManquant]") 
            # Les arguments sont maintenant des chaînes représentant le code source
            args_list = item.get('log_arguments', []) 
            
            # Simuler le message de log (remplacement simple des % pour l'affichage)
            # Une simulation plus avancée pourrait générer des valeurs aléatoires basées sur le type
            num_placeholders = fmt_str.count("%")
            simulated_args = []
            for i, arg_code in enumerate(args_list):
                if arg_code == "[complex_arg]":
                    simulated_args.append(f"<val_expr_complexe_{i+1}>")
                elif arg_code.startswith("[UNABLE_TO_GET_SOURCE"):
                     simulated_args.append(f"<val_arg_inconnu_{i+1}>")
                elif arg_code:
                    simulated_args.append(f"<{arg_code.strip()}>") # Mettre entre <> pour montrer que c'est une variable/expression
                else: # Devrait être traité par [complex_arg] ou [UNABLE...]
                    simulated_args.append(f"<val_vide_{i+1}>")

            # Remplacer les placeholders %s, %d, etc.
            # Ceci est une simplification grossière. Une vraie implémentation de formatage serait nécessaire.
            log_msg_for_trace = fmt_str
            try:
                # Essayer un formatage simple si le nombre d'args correspond
                if num_placeholders > 0 and num_placeholders == len(simulated_args):
                    # Remplacer %s, %d, %f etc. par les args simulés
                    # Cette méthode est basique et ne gère pas tous les cas de printf
                    temp_fmt_str = log_msg_for_trace
                    for placeholder in ["%s", "%d", "%zu", "%f", "%.1f", "%.2f", "%X"]: # Ajouter d'autres si besoin
                        while placeholder in temp_fmt_str and simulated_args:
                            temp_fmt_str = temp_fmt_str.replace(placeholder, str(simulated_args.pop(0)), 1)
                    log_msg_for_trace = temp_fmt_str
                elif args_list: # S'il y a des args mais pas de formatage simple
                    log_msg_for_trace += " (Args: " + ", ".join(args_list) + ")"

            except Exception as e_fmt:
                print(f"    [WARN_FORMAT] Erreur lors du formatage du log '{fmt_str}' avec {args_list}: {e_fmt}")
                log_msg_for_trace = f"{fmt_str} [ErreurFormatageArgs: {args_list}]"

            current_log_sequence.append(f"{'  ' * indent_level}L{item_line}: {log_level}: {log_msg_for_trace}")

        elif item_type == "CALL":
            callee_expr = item.get("callee_expression", "N/A")
            resolved_callee_key = item.get("callee_resolved_key")
            display_callee_name = item.get("callee_resolved_display_name", callee_expr)

            if resolved_callee_key and resolved_callee_key in all_functions_dict:
                if resolved_callee_key in call_stack:
                    current_log_sequence.append(f"{'  ' * indent_level}L{item_line}: -> APPEL RÉCURSIF SAUTÉ vers {display_callee_name}")
                else:
                    current_log_sequence.append(f"{'  ' * indent_level}L{item_line}: -> APPEL: {display_callee_name}")
                    call_stack.append(resolved_callee_key)
                    callee_func_data = all_functions_dict[resolved_callee_key]
                    process_execution_elements(callee_func_data.get("execution_elements", []), 
                                               all_functions_dict, current_log_sequence, 
                                               depth + 1, indent_level + 1, call_stack)
                    call_stack.pop()
                    current_log_sequence.append(f"{'  ' * indent_level}L{item_line}: <- RETOUR DE: {display_callee_name}")
            else:
                current_log_sequence.append(f"{'  ' * indent_level}L{item_line}: -> APPEL (Externe/Non Analysé): {display_callee_name}")
        
        elif item_type == "IF_STMT":
            current_log_sequence.append(f"{'  ' * indent_level}L{item_line}: IF ({item.get('condition_expression_text', '')}) {{")
            if TRACE_IF_THEN and item.get("then_branch_elements"):
                process_execution_elements(item["then_branch_elements"], all_functions_dict, 
                                           current_log_sequence, depth, indent_level + 1, call_stack)
            current_log_sequence.append(f"{'  ' * indent_level}}} ") # Fin du then
            if TRACE_IF_ELSE and item.get("else_branch_elements"):
                current_log_sequence.append(f"{'  ' * indent_level}ELSE {{")
                process_execution_elements(item["else_branch_elements"], all_functions_dict, 
                                           current_log_sequence, depth, indent_level + 1, call_stack)
                current_log_sequence.append(f"{'  ' * indent_level}}} ") # Fin du else
        
        elif item_type.endswith("_LOOP") or item_type == "SWITCH_BLOCK": # FOR_LOOP, WHILE_LOOP, etc.
            loop_label = item_type.replace("_LOOP", "").replace("_BLOCK", "")
            current_log_sequence.append(f"{'  ' * indent_level}L{item_line}: {loop_label} {{")
            for i in range(SIMULATE_LOOP_ITERATIONS):
                if SIMULATE_LOOP_ITERATIONS > 1:
                    current_log_sequence.append(f"{'  ' * (indent_level+1)}// Itération de boucle simulée {i+1}")
                process_execution_elements(item.get("body_elements", []), all_functions_dict, 
                                           current_log_sequence, depth, indent_level + 1, call_stack)
            current_log_sequence.append(f"{'  ' * indent_level}}}")
        
        elif item_type in ["CASE_LABEL", "DEFAULT_LABEL"]:
             current_log_sequence.append(f"{'  ' * indent_level}L{item_line}: {item.get('case_expression_text', 'default')}:")
             # Les éléments d'un case sont des frères dans le JSON, ils seront traités par la boucle principale.

        # D'autres types d'éléments pourraient être ajoutés ici


def generate_text_log_sequence_from_data(entry_point_key, all_functions_dict, output_filepath, max_depth=10):
    entry_point_func_data = all_functions_dict.get(entry_point_key)
    if not entry_point_func_data:
        print(f"⚠️ Clé de point d'entrée '{entry_point_key}' non trouvée pour la génération de la séquence de logs textuelle.")
        return 
    entry_point_display_name = entry_point_func_data.get('display_signature', entry_point_key)

    log_sequence_output = [] 
    call_stack = [] # Renommé pour éviter conflit avec le nom de la fonction

    # Entête du log de trace
    log_sequence_output.append(f">> ENTRÉE DANS POINT PRINCIPAL: {entry_point_display_name} ({os.path.basename(entry_point_func_data['file'])}:{entry_point_func_data['line']})")
    call_stack.append(entry_point_key)

    process_execution_elements(entry_point_func_data.get("execution_elements", []), 
                               all_functions_dict, log_sequence_output, 
                               0, 1, call_stack) # depth commence à 0, indent à 1

    call_stack.pop()
    log_sequence_output.append(f"<< SORTIE DE POINT PRINCIPAL: {entry_point_display_name}")

    # Sauvegarde du fichier
    try:
        os.makedirs(os.path.dirname(output_filepath), exist_ok=True)
        with open(output_filepath, "w", encoding="utf-8") as f:
            f.write(f"Trace de Séquence de Logs pour Point d'Entrée: {entry_point_display_name}\n")
            f.write("=" * 80 + "\n")
            for line_entry in log_sequence_output:
                f.write(line_entry + "\n")
        print(f"✅ Trace de logs textuelle sauvegardée dans {output_filepath}")
    except Exception as e:
        print(f"❌ Erreur lors de la sauvegarde de la trace textuelle dans {output_filepath}: {e}")


def main_generate_traces():
    script_dir = os.path.dirname(os.path.abspath(__file__))
    # Déterminer le chemin de la codebase (supposons qu'il est un niveau au-dessus si le script est dans un sous-dossier)
    # ou le même dossier si le script est à la racine.
    # Pour cet exemple, on assume que ce script est à la racine de AutoSystemSim.
    codebase_path = script_dir 
    if os.path.basename(codebase_path).lower() != "autosystemsim" and \
       os.path.isdir(os.path.join(os.path.dirname(codebase_path), "AutoSystemSim")):
        codebase_path = os.path.join(os.path.dirname(codebase_path), "AutoSystemSim")
    elif not os.path.basename(codebase_path).lower() == "autosystemsim":
         print(f"⚠️ Le script ne semble pas être dans 'AutoSystemSim' ou un dossier parent direct.")
         print(f"    Le chemin de base pour la sortie sera : {codebase_path}")


    print(f"ℹ️ Chemin de base pour la sortie des traces : {os.path.abspath(codebase_path)}")
    
    json_input_path = os.path.join(codebase_path, INPUT_JSON_FILE) # S'attendre à ce que le JSON soit à la racine d'AutoSystemSim

    if not os.path.exists(json_input_path):
        print(f"❌ ERREUR : Le fichier JSON d'analyse statique '{json_input_path}' n'a pas été trouvé.")
        print("   Veuillez d'abord exécuter 'parsingCodeBaseClang.py' pour générer ce fichier (avec DEBUG_TARGET_FILE_REL_PATH = None).")
        return

    try:
        with open(json_input_path, "r", encoding="utf-8") as f:
            analysis_data = json.load(f)
    except Exception as e:
        print(f"❌ ERREUR lors du chargement de '{json_input_path}': {e}")
        return

    if "functions" not in analysis_data:
        print(f"❌ ERREUR: Le fichier JSON '{json_input_path}' ne contient pas la clé 'functions'.")
        return
        
    all_functions_dict = {func["function_id_key"]: func for func in analysis_data["functions"]}

    if not all_functions_dict:
        print("⚠️ Aucune donnée de fonction trouvée dans le fichier JSON. Impossible de générer les traces.")
        return
    
    # Définir les points d'entrée pour lesquels générer des traces
    # Vous pouvez les obtenir depuis le fichier généré par extract_signatures.py
    # ou les lister manuellement. Utilisez les "display_signature" ou "function_id_key".
    entry_point_display_patterns = [
        # Exemples (à adapter à votre base de code)
      "ecu_body_control_module::activateHazardLights",
    "ecu_body_control_module::activateIndicator",
   
   

   # Si vous avez une fonction main globale
        # "ecu_safety_systems::AirbagControl::processImpactData", # Exemple de fonction spécifique
    ]
   
    actual_entry_point_keys = find_matching_usr_keys(all_functions_dict, entry_point_display_patterns)
    
    if not actual_entry_point_keys:
        print("\n⚠️ Aucun point d'entrée clé n'a correspondu à vos patterns. Voici quelques suggestions de votre codebase:")
        # Afficher quelques fonctions pour aider l'utilisateur
        count = 0
        for k, v in all_functions_dict.items():
            if "main" in v.get("display_signature","").lower() or "loop" in v.get("display_signature","").lower() or "update" in v.get("display_signature","").lower():
                print(f"  Suggestion: \"{v.get('display_signature')}\" (Clé: \"{k}\")")
                count+=1
            if count >=10: break
        if count == 0: # Si aucune suggestion, montrer les premières
             for i, (k,v) in enumerate(all_functions_dict.items()):
                 if i >= 5: break
                 print(f"  Suggestion (générale): \"{v.get('display_signature')}\" (Clé: \"{k}\")")

        print("Veuillez mettre à jour 'entry_point_display_patterns' dans le script.")
        return
    else:
        print(f"\nTrouvé {len(actual_entry_point_keys)} points d'entrée pour la génération des traces de logs:")
        for usr_key in actual_entry_point_keys:
            print(f"  - Tracera: {all_functions_dict.get(usr_key, {}).get('display_signature', 'N/A')} (Clé: {usr_key})")

    print("\nGenerating text-based log sequence traces...")
    
    output_logs_text_dir_full_path = os.path.join(codebase_path, OUTPUT_LOGS_TEXT_DIR) 

    if not os.path.isdir(output_logs_text_dir_full_path):
        print(f"ℹ️ Le dossier '{OUTPUT_LOGS_TEXT_DIR}' n'existe pas dans '{codebase_path}'. Création...")
        try:
            os.makedirs(output_logs_text_dir_full_path, exist_ok=True)
            print(f"✅ Dossier '{output_logs_text_dir_full_path}' créé.")
        except Exception as e_dir:
            print(f"❌ ERREUR: Impossible de créer le dossier '{output_logs_text_dir_full_path}': {e_dir}")
            print("    Les fichiers de trace textuelle seront sauvegardés dans le répertoire du script.")
            output_logs_text_dir_full_path = os.path.dirname(os.path.abspath(__file__)) 
    else:
        print(f"ℹ️ Utilisation du dossier existant pour les traces textuelles : {os.path.abspath(output_logs_text_dir_full_path)}")

    for entry_key in actual_entry_point_keys: 
        display_name_for_file = all_functions_dict.get(entry_key, {}).get('display_signature', entry_key)
        sanitized_filename_part = sanitize_for_filename(display_name_for_file)
        output_txt_filepath = os.path.join(output_logs_text_dir_full_path, f"log_trace_{sanitized_filename_part}.txt")
        
        print(f"\n--- Génération de la trace pour: {display_name_for_file} ---")
        generate_text_log_sequence_from_data(entry_key, all_functions_dict, 
                                             output_txt_filepath, max_depth=MAX_TRACE_DEPTH)

if __name__ == "__main__":
    main_generate_traces()