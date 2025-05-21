import json
import os
import re

# --- CONFIGURATION ---
INPUT_JSON_FILE = "static_analysis_scheme_clang.json"
OUTPUT_LOGS_TEXT_DIR = "logsText" 
MAX_TRACE_DEPTH = 8 

IGNORE_CALL_DISPLAY_PREFIXES = (
    "std::", "__gnu_cxx::", "printf", "rand", "srand", "exit", "abort", "malloc", "free",
    "getCurrentTimestamp", 
    "std::chrono", "std::this_thread", "std::vector", "std::map", "std::list", "std::set",
    "std::basic_string", "std::basic_ostream", "std::basic_istream", "std::cout", "std::cerr",
    "std::uniform_int_distribution", "std::uniform_real_distribution",
    "std::mersenne_twister_engine", "std::random_device", "std::to_string",
    "os::", "re::", "json::", 
    "clang::"
)
# --- FIN CONFIGURATION ---

def sanitize_for_filename(text):
    if not text: return "_empty_or_none_"
    text = str(text)
    text = text.replace("::", "_NS_")
    text = re.sub(r'[<>:"/\\|?*]', '_', text)
    text = re.sub(r"[^\w_.-]", "_", text)
    text = text.strip('_.- ')
    max_len = 100
    if len(text) > max_len: text = text[:max_len] + "_TRUNC"
    return text if text else "_sanitized_empty_"

def macro_name_from_raw(raw_statement): 
    if not raw_statement: return None
    match = re.match(r"(\w+)\s*\(", raw_statement)
    return match.group(1) if match else None

LOG_MACRO_NAMES = ["LOG_FATAL", "LOG_ERROR", "LOG_WARNING", "LOG_INFO", "LOG_DEBUG"]
LOG_ARG_EXTRACT_PATTERNS = {
    name: re.compile(r"{}\s*\((.*)\)\s*(;)?".format(re.escape(name)), re.DOTALL)
    for name in LOG_MACRO_NAMES
}

def parse_log_arguments_from_string(args_str_combined):
    args_str_combined = args_str_combined.strip()
    format_string = args_str_combined 
    argument_expressions = []
    fmt_str_match = re.match(r'\s*(L?"(?:\\.|[^"\\])*")', args_str_combined)
    if fmt_str_match:
        format_string = fmt_str_match.group(1).strip()
        remaining_args_str = args_str_combined[fmt_str_match.end():].strip()
        if remaining_args_str.startswith(','):
            remaining_args_str = remaining_args_str[1:].strip()
        if remaining_args_str:
            current_arg = ""
            paren_level = 0
            angle_bracket_level = 0 
            in_string_literal = False
            in_char_literal = False
            for char_idx, char in enumerate(remaining_args_str):
                if char == '"' and (char_idx == 0 or remaining_args_str[char_idx-1] != '\\'):
                    in_string_literal = not in_string_literal
                elif char == "'" and (char_idx == 0 or remaining_args_str[char_idx-1] != '\\'):
                    in_char_literal = not in_char_literal
                if not in_string_literal and not in_char_literal:
                    if char == '(': paren_level += 1
                    elif char == ')': paren_level = max(0, paren_level - 1)
                    elif char == '<': angle_bracket_level +=1
                    elif char == '>': angle_bracket_level = max(0, angle_bracket_level -1)
                    elif char == ',' and paren_level == 0 and angle_bracket_level == 0:
                        if current_arg.strip(): argument_expressions.append(current_arg.strip())
                        current_arg = ""
                        continue
                current_arg += char
            if current_arg.strip():
                argument_expressions.append(current_arg.strip())
    return format_string, argument_expressions

def find_matching_usr_keys(all_functions_data_dict, patterns_or_criteria):
    matched_usr_keys = set()
    for criteria_pattern in patterns_or_criteria:
        for func_key, data in all_functions_data_dict.items(): 
            display_name = data.get("display_signature", "") 
            if criteria_pattern == display_name or criteria_pattern == func_key: 
                matched_usr_keys.add(func_key)
                continue
            if display_name and display_name.endswith(criteria_pattern):
                matched_usr_keys.add(func_key)
                continue
            if display_name and "::" not in criteria_pattern and display_name.endswith("::" + criteria_pattern):
                matched_usr_keys.add(func_key)
                continue
            if display_name and criteria_pattern in display_name: 
                matched_usr_keys.add(func_key)
    
    if not matched_usr_keys and patterns_or_criteria:
        print(f"⚠️ Aucune clé de fonction trouvée correspondant aux critères : {patterns_or_criteria}")
        print("   Considérez ces signatures d'affichage de votre code base (la clé pour le script est 'function_id_key'):")
        count = 0
        for usr_k_ex, data_example in all_functions_data_dict.items(): 
            sig_display_example = data_example.get("display_signature", "N/A")
            if any( (patt_part in sig_display_example) for patt in patterns_or_criteria for patt_part in patt.split("::") if patt_part) or \
               any( (patt == sig_display_example.split("::")[-1]) for patt in patterns_or_criteria if "::" not in patt):
                print(f"     - Affichage: \"{sig_display_example}\" (Clé pour le script: \"{usr_k_ex}\")")
                count += 1
                if count >= 10: break
        if count == 0 and len(all_functions_data_dict) > 0: 
            print("   (Affichage de quelques exemples généraux car aucune correspondance spécifique trouvée)")
            for i, (usr_k_ex, data_ex) in enumerate(all_functions_data_dict.items()):
                if i >= 5: break
                print(f"     - Affichage: \"{data_ex.get('display_signature', 'N/A')}\" (Clé pour le script: \"{usr_k_ex}\")")
    return list(matched_usr_keys)

def generate_text_log_sequence_from_data(entry_point_key, all_functions_dict, output_filepath, max_depth=10):
    entry_point_func_data = all_functions_dict.get(entry_point_key)
    if not entry_point_func_data:
        print(f"⚠️ Clé de point d'entrée '{entry_point_key}' non trouvée pour la génération de la séquence de logs textuelle.")
        return 
    entry_point_display_name = entry_point_func_data.get('display_signature', entry_point_key)

    log_sequence_output = [] 
    call_stack_for_recursion_check = [] 

    def trace_recursive(current_func_key, depth, indent_level):
        func_data = all_functions_dict.get(current_func_key)
        func_display_name = func_data.get("display_signature", current_func_key) if func_data else current_func_key

        if depth > max_depth:
            log_sequence_output.append(f"{'  ' * indent_level} L? PROFONDEUR MAX ATTEINTE à {func_display_name}")
            return
        
        if current_func_key in call_stack_for_recursion_check:
             log_sequence_output.append(f"{'  ' * indent_level} L? APPEL RÉCURSIF SAUTÉ vers {func_display_name} (déjà dans la pile d'appels)")
             return
        
        call_stack_for_recursion_check.append(current_func_key)
        
        if not func_data: 
            log_sequence_output.append(f"{'  ' * indent_level} L? APPEL EXTERNE/NON ANALYSÉ: {func_display_name} (Clé: {current_func_key})")
            if current_func_key in call_stack_for_recursion_check: call_stack_for_recursion_check.pop()
            return

        log_sequence_output.append(f"{'  ' * indent_level}>> ENTRÉE DANS: {func_display_name} ({os.path.basename(func_data['file'])}:{func_data['line']})")

        items_in_function = []
        for log_item in func_data.get("logs_in_order", []): 
            items_in_function.append({'type': 'log', 'line': log_item.get("line",0), 'data': log_item})
        for call_item in func_data.get("calls_in_order", []): 
            resolved_key_for_trace = call_item.get("callee_resolved_key")
            display_name_for_filter = call_item.get("callee_resolved_display_name", call_item.get("callee_expression",""))
            is_ignorable = any(display_name_for_filter.startswith(p) for p in IGNORE_CALL_DISPLAY_PREFIXES)
            
            if not is_ignorable or (resolved_key_for_trace and resolved_key_for_trace in all_functions_dict):
                 items_in_function.append({'type': 'call', 'line': call_item.get("line",0), 'data': call_item})
        
        items_in_function.sort(key=lambda x: x['line'])

        for item in items_in_function:
            item_line = item['line']
            item_data = item['data']
            if item['type'] == "log":
                log_level = item_data['level']
                fmt_str = item_data.get('log_format_string', item_data.get('message_args_str_combined', '[MsgLogErreur]'))
                args_list = item_data.get('log_arguments', [])
                if fmt_str.startswith("[RawMacroText:") and not args_list : 
                    raw_text_content = item_data.get('message_args_str_combined', fmt_str) 
                    if raw_text_content.startswith("[RawMacroText:"): 
                         raw_text_content = raw_text_content[len("[RawMacroText: "):-1]
                    actual_macro_name_from_raw = macro_name_from_raw(item_data.get('raw_log_statement', raw_text_content))
                    if actual_macro_name_from_raw:
                        arg_pattern_fallback = LOG_ARG_EXTRACT_PATTERNS.get(actual_macro_name_from_raw)
                        if arg_pattern_fallback:
                            match_fallback = arg_pattern_fallback.search(item_data.get('raw_log_statement', raw_text_content))
                            if match_fallback and match_fallback.group(1) is not None:
                                fallback_combined_args = match_fallback.group(1).strip()
                                fmt_str_fb, args_list_fb = parse_log_arguments_from_string(fallback_combined_args)
                                if fmt_str_fb != fallback_combined_args or args_list_fb : 
                                    fmt_str = fmt_str_fb
                                    args_list = args_list_fb
                                else: 
                                    fmt_str = fallback_combined_args 
                                    args_list = []
                            else:
                                fmt_str = f"[ArgsNonParsésDe: {raw_text_content[:50]}...]"
                                args_list = []
                        else:
                            fmt_str = f"[PasDeRegexPour {actual_macro_name_from_raw} sur: {raw_text_content[:50]}...]"
                            args_list = []
                    else:
                        fmt_str = f"[MacroNonID Dans: {raw_text_content[:50]}...]"
                        args_list = []
                log_msg_for_trace = fmt_str
                if args_list:
                    log_msg_for_trace = f"{fmt_str}, {', '.join(args_list)}"
                log_sequence_output.append(f"{'  ' * (indent_level + 1)} L{item_line}: {log_level}: {log_msg_for_trace}")
            elif item['type'] == "call":
                callee_expr = item_data["callee_expression"]
                resolved_callee_key = item_data.get("callee_resolved_key") 
                display_callee_name = item_data.get("callee_resolved_display_name", callee_expr)
                should_trace_deeper = False
                if resolved_callee_key and resolved_callee_key in all_functions_dict:
                    if not any(display_callee_name.startswith(p) for p in IGNORE_CALL_DISPLAY_PREFIXES):
                        should_trace_deeper = True
                if should_trace_deeper:
                    log_sequence_output.append(f"{'  ' * (indent_level + 1)} L{item_line}: -> APPEL: {callee_expr} (Résolu à: {display_callee_name})")
                    trace_recursive(resolved_callee_key, depth + 1, indent_level + 2)
        
        log_sequence_output.append(f"{'  ' * indent_level}<< SORTIE DE: {func_display_name}")
        if current_func_key in call_stack_for_recursion_check: call_stack_for_recursion_check.pop()

    trace_recursive(entry_point_key, 0, 0)
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
    codebase_path = script_dir 
    if os.path.basename(script_dir) == "AutoSystemSim":
        pass 
    elif os.path.isdir(os.path.join(script_dir, "AutoSystemSim")):
        codebase_path = os.path.join(script_dir, "AutoSystemSim")
    else: 
        print(f"⚠️ Impossible de déterminer automatiquement le chemin vers 'AutoSystemSim' depuis '{script_dir}'.")
        print(f"    Utilisation du répertoire courant '{os.getcwd()}' comme base pour '{OUTPUT_LOGS_TEXT_DIR}'.")
        codebase_path = os.getcwd() 

    print(f"ℹ️ Chemin de base pour la sortie : {os.path.abspath(codebase_path)}")

    # S'assurer que le fichier JSON d'entrée est relatif au répertoire du script,
    # ou à un chemin absolu si vous préférez.
    # Si parsingCodeBaseClang.py est à la racine de AutoSystemSim, et generateLogTraces.py aussi,
    # alors INPUT_JSON_FILE devrait être juste le nom du fichier.
    # Si generateLogTraces.py est dans un sous-dossier, ajustez le chemin.
    # Pour cet exemple, supposons qu'il est au même niveau que parsingCodeBaseClang.py
    # et que parsingCodeBaseClang.py a généré le JSON là.
    
    # Déterminer le chemin d'entrée du JSON par rapport à l'emplacement de ce script
    # Si parsingCodeBaseClang.py et ce script sont dans le même dossier (racine de AutoSystemSim)
    # Alors json_input_path peut être juste INPUT_JSON_FILE
    # S'ils sont dans des dossiers différents, il faut ajuster.
    # Pour la simplicité, on suppose que static_analysis_scheme_clang.json est dans le même dossier
    # que ce script generateLogTraces.py (ou que vous ajustez INPUT_JSON_FILE avec un chemin relatif/absolu)
    json_input_path = INPUT_JSON_FILE
    if not os.path.isabs(json_input_path):
         json_input_path = os.path.join(os.path.dirname(os.path.abspath(__file__)), INPUT_JSON_FILE)


    if not os.path.exists(json_input_path):
        print(f"❌ ERREUR : Le fichier JSON d'analyse statique '{json_input_path}' n'a pas été trouvé.")
        print("   Veuillez d'abord exécuter 'parsingCodeBaseClang.py'.")
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
    
    # IMPORTANT: Mettez à jour ces patterns avec les 'display_signature' exactes de votre JSON
    #            ou utilisez le fichier généré par `extract_signatures.py` pour peupler cette liste.
    entry_point_display_patterns = [
        "ecu_body_control_module::BulbState::BulbState",
        "ecu_body_control_module::ClimateControl",
        "ecu_body_control_module::LightingControl",
        "ecu_body_control_module::SingleWindowState::SingleWindowState",
        "ecu_body_control_module::WindowControl",
        "ecu_body_control_module::acStatusToString",
    ]
    actual_entry_point_keys = find_matching_usr_keys(all_functions_dict, entry_point_display_patterns)


    if not actual_entry_point_keys:
        print("\n⚠️ Aucun point d'entrée clé n'a correspondu à vos patterns pour la génération des traces textuelles.")
    else:
        print(f"\nTrouvé {len(actual_entry_point_keys)} points d'entrée correspondants pour la génération des traces de logs:")
        for usr_key in actual_entry_point_keys:
            print(f"  - Tracera: {all_functions_dict.get(usr_key, {}).get('signature_display', 'N/A')} (Clé: {usr_key})")

    print("\nGenerating text-based log sequence traces...")
    
    # Le chemin de sortie est relatif à codebase_path, qui devrait être la racine de AutoSystemSim
    output_logs_text_dir_full_path = os.path.join(codebase_path, OUTPUT_LOGS_TEXT_DIR) 

    if not os.path.isdir(output_logs_text_dir_full_path):
        print(f"ℹ️ Le dossier '{OUTPUT_LOGS_TEXT_DIR}' n'existe pas dans '{codebase_path}'. Création...")
        try:
            os.makedirs(output_logs_text_dir_full_path, exist_ok=True)
            print(f"✅ Dossier '{output_logs_text_dir_full_path}' créé.")
        except Exception as e_dir:
            print(f"❌ ERREUR: Impossible de créer le dossier '{output_logs_text_dir_full_path}': {e_dir}")
            print("    Les fichiers de trace textuelle seront sauvegardés dans le répertoire du script.")
            output_logs_text_dir_full_path = os.path.dirname(os.path.abspath(__file__)) # Fallback
    else:
        print(f"ℹ️ Utilisation du dossier existant pour les traces textuelles : {os.path.abspath(output_logs_text_dir_full_path)}")

    for entry_key in actual_entry_point_keys: 
        display_name_for_file = all_functions_dict.get(entry_key, {}).get('signature_display', entry_key)
        sanitized_filename_part = sanitize_for_filename(display_name_for_file)
        output_txt_filepath = os.path.join(output_logs_text_dir_full_path, f"log_trace_{sanitized_filename_part}.txt")
        generate_text_log_sequence_from_data(entry_key, all_functions_dict, 
                                             output_txt_filepath, max_depth=MAX_TRACE_DEPTH)

if __name__ == "__main__":
    main_generate_traces()