import json
import time
import datetime
import os
import re
import random
import copy
import glob

# --- CONFIGURATION (inchangée) ---
STATIC_ANALYSIS_FILE = "static_analysis_output.json"
OUTPUT_WAD_DLT_DIR = "simulated_dlts_json_wad"
OUTPUT_BUG_DLT_DIR = "simulated_dlts_json_bugs"
os.makedirs(OUTPUT_WAD_DLT_DIR, exist_ok=True)
os.makedirs(OUTPUT_BUG_DLT_DIR, exist_ok=True)

# REMPLACEZ la classe DLTWriter existante dans generate_sim_dlts.py par celle-ci

class DLTWriter:
    """
    Responsable de la mise en forme finale des logs DLT.
    Prend une liste de logs 'bruts' et produit le fichier JSON final.
    """
    def __init__(self, records, session_id, start_timestamp):
        self.records = records
        self.session_id = session_id
        self.start_timestamp = start_timestamp

    def _get_fallback_value(self, placeholder_char):
        """Fournit une valeur de substitution plausible basée sur le type de placeholder."""
        if placeholder_char in ['d', 'i', 'u', 'x', 'X', 'o']:
            return random.randint(0, 100) # Un entier plausible
        if placeholder_char in ['f', 'e', 'E', 'g', 'G']:
            return round(random.uniform(0.0, 100.0), 2) # Un flottant plausible
        if placeholder_char == 's':
            return "[SIM_STRING]" # Une chaîne plausible
        if placeholder_char == 'p':
            return f"0x{random.randint(0x1000, 0xFFFF):04x}" # Un pointeur plausible
        if placeholder_char == 'c':
            return 'S' # Un caractère plausible
        return "[?]"

    def _format_payload(self, template, values):
        """Remplace les placeholders par des valeurs réelles ou des valeurs de substitution."""
        if not isinstance(template, str):
            return "[INVALID_TEMPLATE]"

        # Remplacer d'abord les %% pour ne pas les confondre
        template = template.replace("%%", "\u001A")
        
        # Trouver tous les placeholders dans l'ordre
        placeholders = re.findall(r"%([.\-\+\s#0]*[\d\.]*[hljztL]*[cdeEfgGosuxXpn])", template)
        
        output_parts = re.split(r"%[.\-\+\s#0]*[\d\.]*[hljztL]*[cdeEfgGosuxXpn]", template)
        
        final_payload = output_parts[0]
        
        for i, placeholder_format in enumerate(placeholders):
            value_to_insert = None
            placeholder_type = placeholder_format[-1]

            # A-t-on une valeur littérale valide depuis l'analyse statique ?
            if i < len(values):
                val = values[i]
                # Vérifier si la valeur est un littéral et non le nom d'une variable
                is_literal = isinstance(val, (int, float, bool)) or \
                             (isinstance(val, str) and not re.fullmatch(r'[a-zA-Z_][a-zA-Z0-9_]*', val))
                
                if is_literal:
                    # Tenter de caster la valeur au bon type pour le formatage
                    try:
                        if placeholder_type in ['d', 'i', 'u', 'x', 'X', 'o']:
                            value_to_insert = int(val)
                        elif placeholder_type in ['f', 'e', 'E', 'g', 'G']:
                            value_to_insert = float(val)
                        else:
                            value_to_insert = str(val)
                    except (ValueError, TypeError):
                        value_to_insert = None # Le cast a échoué

            # Si on n'a pas de valeur valide, on utilise une valeur de substitution
            if value_to_insert is None:
                value_to_insert = self._get_fallback_value(placeholder_type)

            # Formater cette seule partie
            try:
                formatted_part = ("%" + placeholder_format) % value_to_insert
                final_payload += formatted_part
            except TypeError:
                final_payload += f"[FMT_ERR:{value_to_insert}]"

            # Ajouter la partie statique suivante du template
            if i + 1 < len(output_parts):
                final_payload += output_parts[i+1]
                
        # Restaurer les %%
        return final_payload.replace("\u001A", "%")


    def _format_record(self, raw_record, index, timestamp):
        # Cette fonction reste la même, elle appelle juste la nouvelle _format_payload
        dt_object = datetime.datetime.fromtimestamp(timestamp)
        time_str = dt_object.strftime("%Y-%m-%d %H:%M:%S.") + str(dt_object.microsecond // 1000).zfill(3)
        payload = self._format_payload(raw_record['message_template'], raw_record.get('values', []))
        return {
            "Index": index, "Timestamp": timestamp, "Time": time_str,
            "ECUId": "SIM_ECU_01", "ApID": raw_record['apId'], "CtID": raw_record['ctId'],
            "SessionID": self.session_id, "Type": "LOG", "LogLevel": raw_record['level'],
            "Payload": payload
        }

    def write_to_file(self, filepath):
        # Cette fonction reste la même
        final_dlt_records = []
        current_timestamp = self.start_timestamp
        for i, raw_record in enumerate(self.records):
            current_timestamp += random.uniform(0.001, 0.015)
            final_dlt_records.append(self._format_record(raw_record, i + 1, current_timestamp))
        try:
            with open(filepath, "w", encoding="utf-8") as f:
                json.dump(final_dlt_records, f, indent=2, ensure_ascii=False)
            # On peut commenter ce print pour rendre la sortie plus propre
            # print(f"  -> DLT file saved: {os.path.basename(filepath)}")
        except Exception as e:
            print(f"  -> ERROR saving DLT file {filepath}: {e}")
# --- NOUVELLE VERSION CORRIGÉE DU PATH TRACER ---
class PathTracer:
    def __init__(self, static_data):
        self.static_data = static_data["functions"]
        self.final_paths = []

    def trace_scenario(self, scenario_actions, base_config):
        print(f"\n--- Tracing Scenario: {base_config.get('name', 'unnamed_scenario')} ---")
        self.final_paths = []
        initial_state = {"logs": [], "call_stack": [], "config": base_config}

        self._process_actions(scenario_actions, initial_state)
        
        return self.final_paths

    def _process_actions(self, actions, state):
        current_state = state
        for action_type, action_details in actions:
            if action_type == "call_function":
                self._explore(action_details["function_name"], current_state)
        
        # Une fois toutes les actions terminées, si aucun chemin n'a été finalisé, on finalise l'état actuel
        if not self.final_paths:
            self.final_paths.append(current_state["logs"])

    def _explore(self, func_fqn, state):
        if func_fqn in state["call_stack"] or func_fqn not in self.static_data:
            return

        state["call_stack"].append(func_fqn)
        
        elements = self.static_data[func_fqn]["execution_elements_structured"]
        self._process_elements(elements, state)
        
        state["call_stack"].pop()

    def _process_elements(self, elements, state):
        for element in elements:
            el_type = element.get("type", "")
            
            if el_type == "LOG":
                if element.get("level") == "FATAL" and not state["config"].get("allow_fatal", False):
                    continue
                state["logs"].append(element)
            
            elif el_type == "CALL":
                self._explore(element["callee"], state)
                
            elif el_type == "IF_STMT":
                stable_id = element.get("id_in_function")
                func_name = state["call_stack"][-1]
                branch_choice = state["config"].get("if_choices", {}).get(func_name, {}).get(stable_id, "both")

                # Créer une copie de l'état avant de se diviser
                state_before_if = copy.deepcopy(state)
                
                if branch_choice == "both":
                    # Explorer la branche 'then'
                    state_for_then = state_before_if
                    if element.get("then_branch_elements"):
                        self._process_elements(element["then_branch_elements"], state_for_then)
                    # Sauvegarder ce chemin comme une possibilité finale
                    self.final_paths.append(state_for_then["logs"])

                    # Explorer la branche 'else' à partir de l'état d'origine
                    state_for_else = copy.deepcopy(state_before_if) # Cloner à nouveau pour une branche propre
                    if element.get("else_branch_elements"):
                        self._process_elements(element["else_branch_elements"], state_for_else)
                    # L'état actuel continue sur ce chemin
                    state.update(state_for_else)

                elif branch_choice == "then" and element.get("then_branch_elements"):
                    self._process_elements(element["then_branch_elements"], state)
                elif branch_choice == "else" and element.get("else_branch_elements"):
                    self._process_elements(element["else_branch_elements"], state)

            elif el_type.endswith("_LOOP"):
                stable_id = element.get("id_in_function")
                func_name = state["call_stack"][-1]
                num_iter = state["config"].get("loop_iterations", {}).get(func_name, {}).get(stable_id, 1)

                for _ in range(num_iter):
                    self._process_elements(element.get("body_elements", []), state)

# --- MOTEUR DE SCÉNARIOS (inchangé) ---
def define_and_run_scenarios(tracer):
    all_wad_filepaths = []
    scenarios_to_run = [
        {
            "actions": [("call_function", {"function_name": "main"})],
            "config": {
                "name": "WAD_Normal_Startup_Success",
                "loop_iterations": {"main": {"loop_0": 2}}
            }
        },
        {
            "actions": [("call_function", {"function_name": "main"})],
            "config": {
                "name": "WAD_Startup_ECM_Init_Fails",
                "if_choices": {
                    "Automotive::Controllers::VehicleController::initializeSystem": {"if_0": "then"}
                },
                "loop_iterations": {"main": {"loop_0": 0}}
            }
        },
        {
            "actions": [("call_function", {"function_name": "Automotive::Controllers::VehicleController::triggerDiagnosticSequence"})],
            "config": {"name": "WAD_Diagnostic_Sequence_L2"}
        }
    ]

    for scenario_spec in scenarios_to_run:
        generated_logs_lists = tracer.trace_scenario(scenario_spec["actions"], scenario_spec["config"])
        print(f"  -> Scenario '{scenario_spec['config']['name']}' produced {len(generated_logs_lists)} execution path(s).")
        
        for i, logs in enumerate(generated_logs_lists):
            if not logs: continue
            session_id = int(time.time()) + random.randint(1000, 9999)
            start_ts = time.time() - random.uniform(100, 1000)
            
            base_filename = f"{scenario_spec['config']['name']}_path{i}"
            filepath = os.path.join(OUTPUT_WAD_DLT_DIR, f"{base_filename}.json")
            
            writer = DLTWriter(logs, session_id, start_ts)
            writer.write_to_file(filepath)
            all_wad_filepaths.append(filepath)
            
    return all_wad_filepaths

# --- MOTEUR DE MUTATIONS (inchangé) ---
def mutate_and_generate_bugs(wad_files):
    # ... (le code de cette fonction reste le même) ...
    print("\n--- Mutating WADs to generate BUGs ---")
    if not wad_files:
        print("  No WAD files to mutate.")
        return

    for wad_filepath in wad_files:
        print(f"  Mutating {os.path.basename(wad_filepath)}...")
        with open(wad_filepath, 'r') as f:
            wad_records = json.load(f)
        
        if len(wad_records) < 3:
            print("    Skipping (too few records).")
            continue

        # Mutation 1: Log manquant
        records_missing = copy.deepcopy(wad_records)
        if len(records_missing) > 1:
            idx_to_del = random.randint(1, len(records_missing) - 2)
            del records_missing[idx_to_del]
            save_bug_file(wad_filepath, "MissingLog", records_missing)

        # Mutation 2: Log inattendu
        records_extra = copy.deepcopy(wad_records)
        idx_to_add = random.randint(1, len(records_extra) - 1)
        extra_log = { "apId": "INJECT", "ctId": "UNEXPECTED", "level": "WARN", "message_template": "An unexpected event occurred.", "values": [] }
        records_extra.insert(idx_to_add, extra_log)
        save_bug_file(wad_filepath, "ExtraLog", records_extra)
        
        # Mutation 3: Logs dans le désordre
        if len(wad_records) > 4:
            records_reorder = copy.deepcopy(wad_records)
            idx_swap = random.randint(1, len(records_reorder) - 3)
            records_reorder[idx_swap], records_reorder[idx_swap+1] = records_reorder[idx_swap+1], records_reorder[idx_swap]
            save_bug_file(wad_filepath, "OutOfOrder", records_reorder)
            
        # Mutation 4: Données corrompues
        records_corrupt = copy.deepcopy(wad_records)
        idx_corrupt = random.randint(0, len(records_corrupt) - 1)
        records_corrupt[idx_corrupt]['message_template'] = "Corrupted data detected: val=%.2f, status=0x%X"
        records_corrupt[idx_corrupt]['values'] = [-9999.99, 0xFFFFFFFF]
        save_bug_file(wad_filepath, "DataCorruption", records_corrupt)

# REMPLACEZ cette fonction dans generate_sim_dlts.py
# REMPLACEZ cette fonction dans generate_sim_dlts.py

def save_bug_file(original_wad_path, mutation_type, mutated_records):
    base_name = os.path.basename(original_wad_path).replace(".json", "")
    bug_filename = f"{base_name}_BUG_{mutation_type}.json"
    bug_filepath = os.path.join(OUTPUT_BUG_DLT_DIR, bug_filename)
    
    if not mutated_records:
        return

    raw_records_for_writer = []
    for r in mutated_records:
        # Priorité : si le log a été muté (il a un message_template), on l'utilise.
        # Sinon, on utilise le Payload existant.
        if 'message_template' in r and 'values' in r:
            # Cas d'un log muté (DataCorruption) ou injecté (ExtraLog)
            raw_rec = {
                "apId": r.get("ApID"),
                "ctId": r.get("CtID"),
                "level": r.get("LogLevel"),
                "message_template": r.get('message_template'),
                "values": r.get("values", [])
            }
        else:
            # Cas d'un log non modifié venant du WAD original
            # On le transforme en "raw" pour que le DLTWriter puisse le formater.
            raw_rec = {
                "apId": r.get("ApID"),
                "ctId": r.get("CtID"),
                "level": r.get("LogLevel"),
                "message_template": r.get("Payload"), # Le Payload devient le nouveau template
                "values": [] # Il n'y a plus de valeurs à formater
            }
        raw_records_for_writer.append(raw_rec)
        
    session_id = mutated_records[0].get("SessionID", int(time.time()))
    start_ts = mutated_records[0].get("Timestamp", time.time()) - 1.0
    
    writer = DLTWriter(raw_records_for_writer, session_id, start_ts)
    writer.write_to_file(bug_filepath)

# --- MAIN (inchangé) ---
if __name__ == "__main__":
    if not os.path.exists(STATIC_ANALYSIS_FILE):
        print(f"CRITICAL: Static analysis file '{STATIC_ANALYSIS_FILE}' not found. Run the updated static_analysis.py first.")
    else:
        print("Loading static analysis data...")
        with open(STATIC_ANALYSIS_FILE, "r", encoding="utf-8") as f:
            static_data = json.load(f)
        
        path_tracer = PathTracer(static_data)
        wad_files_generated = define_and_run_scenarios(path_tracer)
        mutate_and_generate_bugs(wad_files_generated)

        print("\nDLT generation process finished.")