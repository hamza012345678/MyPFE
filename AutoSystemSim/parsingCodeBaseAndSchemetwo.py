import os
import re
import json
from graphviz import Digraph

# Define patterns for logs, function defs, function calls
LOG_PATTERNS = {
    "FATAL": re.compile(r"LOG_FATAL\((.*?)\);", re.DOTALL),
    "ERROR": re.compile(r"LOG_ERROR\((.*?)\);", re.DOTALL),
    "WARNING": re.compile(r"LOG_WARNING\((.*?)\);", re.DOTALL),
    "INFO": re.compile(r"LOG_INFO\((.*?)\);", re.DOTALL),
    "DEBUG": re.compile(r"LOG_DEBUG\((.*?)\);", re.DOTALL),
}

# Improved (but still basic) regex for C++ function definitions
FUNCTION_DEF_PATTERN = re.compile(
    r"([\w:<>,\s\*&]+?)\s+"
    r"((?:[\w_]+::)*[\w_~]+)"
    r"\s*\((.*?)\)\s*(const)?\s*(noexcept)?\s*(override)?\s*([=;{])",
    re.MULTILINE
)

CALL_PATTERNS = [
    re.compile(r"\b([\w.-]+(?:->|\.)[\w_]+)\s*\((.*?)\);"),
    re.compile(r"\b((?:[\w_]+::)*[\w_]+)\s*\((.*?)\);")
]

def sanitize_for_graphviz_id(text):
    """Replaces characters problematic for Graphviz IDs."""
    if not text: # Handle cases where text might be None or empty
        return "_empty_or_none_node_"
    text = str(text) # Ensure it's a string
    text = re.sub(r"::", "_NS_", text)
    text = re.sub(r"[^a-zA-Z0-9_]", "_", text)
    if text and text[0].isdigit():
        text = "_" + text
    return text if text else "_unknown_node_"


def find_source_files(root_dir):
    source_file_extensions = ('.cpp', '.h', '.hpp', '.c', '.cc')
    for subdir, dirs, files in os.walk(root_dir):
        dirs[:] = [d for d in dirs if not d.startswith('.')]
        for file in files:
            if file.lower().endswith(source_file_extensions):
                yield os.path.join(subdir, file)

def parse_file_content(filepath, content):
    functions_data = {} # Stores data for functions defined in THIS file
    
    # Identify all function definitions in the current file first
    # This helps in creating a list of known function scopes within this file
    defined_funcs_in_file = {} # Store start line and signature
    for func_match in FUNCTION_DEF_PATTERN.finditer(content):
        full_name_with_class = func_match.group(2).strip()
        start_char_index = func_match.start()
        start_line_num = content.count('\n', 0, start_char_index) + 1
        
        relative_path = os.path.relpath(filepath, ".").replace(os.sep, "_").replace(".","_")
        signature = f"{relative_path}::{full_name_with_class}" # File-specific signature
        
        defined_funcs_in_file[start_line_num] = signature
        if signature not in functions_data:
            functions_data[signature] = {
                "logs": [],
                "calls": [],
                "file": filepath,
                "line": start_line_num,
                "raw_name": full_name_with_class,
                "end_line": -1 # To be determined
            }

    # Try to associate logs and calls with functions based on line numbers and brace counting
    # This is a very simplified scope detection
    lines = content.splitlines()
    active_function_signature = None
    active_function_start_line = -1
    brace_count = 0

    for line_num, line_text in enumerate(lines):
        current_line_abs = line_num + 1

        # Check if we are starting a new function found earlier
        if current_line_abs in defined_funcs_in_file and '{' in line_text:
            if active_function_signature and brace_count > 0:
                # This means a nested block, or parser error, for now, assume we finish previous
                if active_function_signature in functions_data:
                     functions_data[active_function_signature]["end_line"] = current_line_abs -1

            active_function_signature = defined_funcs_in_file[current_line_abs]
            active_function_start_line = current_line_abs
            brace_count = line_text.count('{') - line_text.count('}')
            continue # Continue to process lines within this function

        if active_function_signature:
            brace_count += line_text.count('{')
            brace_count -= line_text.count('}')

            # Extract logs
            for level, pattern in LOG_PATTERNS.items():
                for log_match in pattern.finditer(line_text):
                    log_entry = {
                        "level": level,
                        "message_args_str": log_match.group(1).strip(),
                        "line": current_line_abs,
                        "raw_log_statement": log_match.group(0).strip()
                    }
                    if active_function_signature in functions_data: # Should always be true here
                        functions_data[active_function_signature]["logs"].append(log_entry)
            
            # Extract calls
            for call_pattern in CALL_PATTERNS:
                for call_match in call_pattern.finditer(line_text):
                    if not any(call_match.group(1).startswith(log_macro) for log_macro in LOG_PATTERNS.keys()):
                        call_entry = {
                            "callee_expression": call_match.group(1).strip(),
                            "callee_args_str": call_match.group(2).strip(),
                            "line": current_line_abs
                        }
                        if active_function_signature in functions_data:
                            functions_data[active_function_signature]["calls"].append(call_entry)
            
            if brace_count <= 0 and active_function_start_line != -1:
                if active_function_signature in functions_data:
                    functions_data[active_function_signature]["end_line"] = current_line_abs
                active_function_signature = None
                active_function_start_line = -1
                brace_count = 0
    
    # If a function was started but not properly closed by brace_count (e.g. end of file)
    if active_function_signature and active_function_start_line != -1 and functions_data[active_function_signature]["end_line"] == -1:
        functions_data[active_function_signature]["end_line"] = len(lines)

    return functions_data

# --- Visualization Functions ---
def generate_call_graph_viz(all_functions_data, output_filename_base="call_graph_full"):
    dot = Digraph(comment='Full Static Call Graph', format='pdf')
    dot.attr(rankdir='LR', size='30,20', overlap='prism', splines='true', nodesep='0.6', ranksep='1.2', fontsize='10') # Adjusted for potentially large graphs
    added_nodes = set()

    for func_sig, data in all_functions_data.items():
        node_id = sanitize_for_graphviz_id(func_sig)
        if node_id not in added_nodes:
            label_parts = func_sig.split("::")
            # Try to get a shorter, more readable label
            if len(label_parts) > 1 and label_parts[0].count('_') > 1: # Heuristic for filepath_based_ns
                short_label = "::".join(label_parts[1:]) # Use Class::Method or Method
            else:
                short_label = "::".join(label_parts[-2:]) if len(label_parts) >=2 else label_parts[-1]
            
            short_label += f"\\n({os.path.basename(data['file'])}:{data['line']})"
            dot.node(node_id, label=short_label, fontsize='8')
            added_nodes.add(node_id)

    for func_sig, data in all_functions_data.items():
        caller_node_id = sanitize_for_graphviz_id(func_sig)
        for call in data.get("calls_in_order", []):
            callee_expr = call["callee_expression"]
            
            potential_callee_sig_found = None
            # Attempt a slightly better match for callee based on raw_name
            # Extract method/function name from expression: e.g., "obj->startEngine" -> "startEngine"
            clean_callee_name_match = re.search(r'(?:->|\.)?(\w+)$', callee_expr.split('(')[0])
            simple_callee_name_from_expr = clean_callee_name_match.group(1) if clean_callee_name_match else callee_expr.split('(')[0]


            for def_sig, def_data in all_functions_data.items():
                # Check if the raw name from definition ends with the extracted simple name
                if def_data.get("raw_name", "").split('::')[-1] == simple_callee_name_from_expr:
                    potential_callee_sig_found = def_sig
                    break # Take the first plausible match (very naive)
            
            if potential_callee_sig_found:
                callee_node_id = sanitize_for_graphviz_id(potential_callee_sig_found)
                if callee_node_id not in added_nodes: # Should be added if it's a defined function
                     # This might happen if the function definition wasn't parsed correctly
                    dot.node(callee_node_id, label=potential_callee_sig_found.split("::")[-1], shape="ellipse", style="dashed", color="orange", fontsize='8')
                    added_nodes.add(callee_node_id)
                dot.edge(caller_node_id, callee_node_id)
            else:
                unresolved_callee_node_id = sanitize_for_graphviz_id(f"EXT_{callee_expr}") # Prefix to avoid clashes
                if unresolved_callee_node_id not in added_nodes:
                    dot.node(unresolved_callee_node_id, label=callee_expr, shape="box", style="dashed", color="grey", fontsize='8')
                    added_nodes.add(unresolved_callee_node_id)
                dot.edge(caller_node_id, unresolved_callee_node_id, style="dashed", color="grey")
    try:
        dot.render(output_filename_base, view=False, cleanup=True)
        print(f"✅ Full call graph saved to {output_filename_base}.pdf")
    except Exception as e:
        if "make sure the Graphviz executables are on your systems' PATH" in str(e).lower() or isinstance(e, FileNotFoundError) or "failed to execute" in str(e).lower() :
            print(f"❌ Error rendering full call graph: {e}")
            print("Ensure Graphviz is installed and its 'bin' directory is in your system's PATH.")
            print("You can download Graphviz from: https://graphviz.org/download/")
        else:
            print(f"❌ Error rendering full call graph (unexpected): {e}")


def generate_path_log_visualization(entry_point_signature, all_functions_data, output_filename_base, max_depth=7):
    if entry_point_signature not in all_functions_data:
        print(f"⚠️ Entry point '{entry_point_signature}' not found in parsed data for log flow visualization.")
        return

    dot = Digraph(comment=f'Log Flow for {entry_point_signature}', format='pdf')
    dot.attr(rankdir='TB', overlap='false', splines='true', nodesep='0.5', ranksep='0.8', fontsize='10')
    
    # Use a unique ID for nodes in this specific path viz to avoid clashes if called multiple times
    path_node_counter = 0
    def get_unique_path_node_id(base_sig):
        nonlocal path_node_counter
        path_node_counter += 1
        return f"{sanitize_for_graphviz_id(base_sig)}_{path_node_counter}"

    # visited_in_path for this specific trace to handle recursion within this trace
    # key: function_signature, value: graphviz node_id used in this path
    traced_nodes_in_current_path = {} 

    def trace_and_draw(current_sig_to_trace, current_depth, parent_gv_node_id=None):
        nonlocal path_node_counter

        if current_depth > max_depth:
            if parent_gv_node_id:
                max_depth_node_id = get_unique_path_node_id(f"max_depth_{current_sig_to_trace}")
                dot.node(max_depth_node_id, label="Max Depth Reached", shape="box", style="dotted")
                dot.edge(parent_gv_node_id, max_depth_node_id, style="dotted")
            return

        # Handle recursion for this specific trace
        if current_sig_to_trace in traced_nodes_in_current_path:
            if parent_gv_node_id:
                dot.edge(parent_gv_node_id, traced_nodes_in_current_path[current_sig_to_trace], 
                         label="Recursive Call", style="dashed", color="blue", fontcolor="blue", fontsize="8")
            return
        
        func_data = all_functions_data.get(current_sig_to_trace)
        current_gv_node_id = get_unique_path_node_id(current_sig_to_trace)
        traced_nodes_in_current_path[current_sig_to_trace] = current_gv_node_id

        if not func_data:
            # External/Unparsed function call
            label_parts = current_sig_to_trace.split("::")
            short_label = "::".join(label_parts[-2:]) if len(label_parts) >=2 else label_parts[-1]
            dot.node(current_gv_node_id, label=f"{short_label}\\n(External/Unresolved)", shape="box", style="filled", color="lightgrey", fontsize="8")
            if parent_gv_node_id:
                dot.edge(parent_gv_node_id, current_gv_node_id, style="dotted")
            del traced_nodes_in_current_path[current_sig_to_trace] # Backtrack
            return

        # Create a record-shaped node for function and its logs
        label_parts_sig = current_sig_to_trace.split("::")
        if len(label_parts_sig) > 1 and label_parts_sig[0].count('_') > 1: # Heuristic for filepath_based_ns
            func_short_name = "::".join(label_parts_sig[1:]) 
        else:
            func_short_name = "::".join(label_parts_sig[-2:]) if len(label_parts_sig) >=2 else label_parts_sig[-1]
        
        log_texts = []
        for log in func_data.get("logs_in_order", []):
            # Escape special characters for Graphviz label string
            msg_preview = log['message_args_str'].replace('|', '\|').replace('{', '\{').replace('}', '\}').replace('<', '\<').replace('>', '\>')
            log_texts.append(f"<L{log['line']}> {log['level']}: {msg_preview[:40]}{'...' if len(msg_preview) > 40 else ''}")
        
        logs_label_part = " | ".join(log_texts) if log_texts else "No direct logs"
        node_label = f"{{ <func> {func_short_name}\\n({os.path.basename(func_data['file'])}:{func_data['line']}) | {logs_label_part} }}"
        
        dot.node(current_gv_node_id, label=node_label, shape="record", fontsize="8")

        if parent_gv_node_id:
            dot.edge(parent_gv_node_id, current_gv_node_id)

        for call_info in func_data.get("calls_in_order", []):
            callee_expr = call_info["callee_expression"]
            potential_callee_sig_found = None
            clean_callee_name_match = re.search(r'(?:->|\.)?(\w+)$', callee_expr.split('(')[0])
            simple_callee_name_from_expr = clean_callee_name_match.group(1) if clean_callee_name_match else callee_expr.split('(')[0]

            for def_sig_candidate, def_data_candidate in all_functions_data.items():
                if def_data_candidate.get("raw_name", "").split('::')[-1] == simple_callee_name_from_expr:
                    potential_callee_sig_found = def_sig_candidate
                    break 
            
            target_sig_for_next_trace = potential_callee_sig_found if potential_callee_sig_found else callee_expr 
            trace_and_draw(target_sig_for_next_trace, current_depth + 1, current_gv_node_id)
        
        del traced_nodes_in_current_path[current_sig_to_trace] # Backtrack for this path


    trace_and_draw(entry_point_signature, 0)

    try:
        dot.render(output_filename_base, view=False, cleanup=True)
        print(f"✅ Log flow graph for '{entry_point_signature.split('::')[-1]}' saved to {output_filename_base}.pdf")
    except Exception as e:
        if "make sure the Graphviz executables are on your systems' PATH" in str(e).lower() or isinstance(e, FileNotFoundError) or "failed to execute" in str(e).lower():
            print(f"❌ Error rendering log flow graph for '{entry_point_signature}': {e}")
            print("Ensure Graphviz is installed and its 'bin' directory is in your system's PATH.")
        else:
            print(f"❌ Error rendering log flow graph for '{entry_point_signature}': {e}")

# --- Main Execution ---
def main():
    codebase_path = "." 
    all_functions_data = {}

    for filepath in find_source_files(codebase_path):
        if os.path.basename(filepath) == os.path.basename(__file__):
            continue
        print(f"Scanning: {filepath}")
        try:
            with open(filepath, "r", encoding="utf-8", errors="ignore") as f:
                content = f.read()
            
            file_functions_data = parse_file_content(filepath, content)
            for func_sig, data in file_functions_data.items():
                if func_sig not in all_functions_data: # Use the generated unique signature
                    all_functions_data[func_sig] = data
                else:
                    # This case should be less frequent if signatures are file-path unique
                    all_functions_data[func_sig]["logs"].extend(data.get("logs", []))
                    all_functions_data[func_sig]["calls"].extend(data.get("calls", []))
        except Exception as e:
            print(f"Error processing file {filepath}: {e}")

    final_structured_data_for_json = []
    for sig, data in all_functions_data.items():
        final_structured_data_for_json.append({
            "signature": sig, # This is the file-path unique signature
            "raw_name": data.get("raw_name", sig.split("::")[-1]), # Store the original parsed name
            "file": data["file"],
            "line": data["line"],
            "end_line": data.get("end_line", -1),
            "logs_in_order": data.get("logs", []),
            "calls_in_order": data.get("calls", [])
        })

    output_json_file = "static_analysis_scheme.json"
    try:
        with open(output_json_file, "w", encoding="utf-8") as f:
            json.dump(final_structured_data_for_json, f, indent=4)
        print(f"✅ Static analysis scheme saved to {output_json_file}")
    except Exception as e:
        print(f"❌ Error saving JSON to {output_json_file}: {e}")

    if all_functions_data:
        # Generate the full call graph
        generate_call_graph_viz(all_functions_data, "call_graph_full")

        # Generate log flow for specific entry points
        # IMPORTANT: You MUST update these signatures to match what your parser actually generates
        # Look into 'static_analysis_scheme.json' to find the correct full signatures.
        # The signature format is roughly: "sanitized_relative_filepath::FullClassName::MethodName" or "sanitized_relative_filepath::FunctionName"
        key_entry_points_to_visualize = [
            "main_application_main_vehicle_controller_cpp::MainVehicleController::simulateDrivingCycle",
            "ecu_powertrain_control_engine_manager_cpp::EngineManager::startEngine",
            "ecu_safety_systems_abs_control_cpp::ABSControl::processBraking",
            "ecu_safety_systems_airbag_control_cpp::AirbagControl::processImpactData",
            "main_application_main_vehicle_controller_cpp::MainVehicleController::handleIgnitionOn"
            # Add more key function signatures as needed
        ]
        
        print("\nGenerating specific log flow visualizations...")
        for entry_point_sig_guess in key_entry_points_to_visualize:
            # Try to find the actual signature that matches the guessed pattern
            actual_entry_point_sig = None
            for parsed_sig in all_functions_data.keys():
                if entry_point_sig_guess.endswith(parsed_sig.split("::",1)[-1]): # Match based on class::method or method part
                    if entry_point_sig_guess.startswith(parsed_sig.split("::")[0]): # And if file part also matches
                        actual_entry_point_sig = parsed_sig
                        break
                # Fallback: if the guess is very precise and IS a key
                if entry_point_sig_guess == parsed_sig:
                    actual_entry_point_sig = parsed_sig
                    break


            if actual_entry_point_sig:
                # Create a filename-safe version of the entry point for the output PDF
                sanitized_filename_part = sanitize_for_graphviz_id(actual_entry_point_sig.replace("::","_"))
                generate_path_log_visualization(actual_entry_point_sig, all_functions_data, 
                                                f"log_flow_{sanitized_filename_part}", max_depth=6)
            else:
                print(f"⚠️ Could not precisely find entry point matching '{entry_point_sig_guess}' for log flow visualization. Check your JSON output for exact signatures.")
    else:
        print("⚠️ No function data extracted, skipping graph generation.")

if __name__ == "__main__":
    main()