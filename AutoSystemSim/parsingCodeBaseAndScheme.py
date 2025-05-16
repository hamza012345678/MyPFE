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
    # Optional namespace/class qualifiers, then function name
    # Catches: void MyNs::MyClass::MyMethod(...), void MyClass::MyMethod(...), void myFunc(...)
    r"([\w:<>,\s\*&]+?)\s+"                                 # Return type
    r"((?:[\w_]+::)*[\w_~]+)"                               # Optional namespaces/class, then function/destructor name
    r"\s*\((.*?)\)\s*(const)?\s*(noexcept)?\s*(override)?\s*([=;{])", # Parameters, qualifiers, and opening brace, semicolon or equals for deleted/defaulted
    re.MULTILINE
)

CALL_PATTERNS = [
    re.compile(r"\b([\w.-]+(?:->|\.)[\w_]+)\s*\((.*?)\);"),
    re.compile(r"\b((?:[\w_]+::)*[\w_]+)\s*\((.*?)\);")
]

def sanitize_for_graphviz_id(text):
    """Replaces characters problematic for Graphviz IDs."""
    text = re.sub(r"::", "_NS_", text) # Replace :: with _NS_ (Namespace Separator)
    text = re.sub(r"[^a-zA-Z0-9_]", "_", text) # Replace other non-alphanumeric with _
    # Ensure it doesn't start with a number if Graphviz has issues with that
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
    functions_data = {}
    lines = content.splitlines()
    
    # Try to find all function definitions first
    # Note: This regex approach is still limited for complex C++ syntax
    for func_match in FUNCTION_DEF_PATTERN.finditer(content):
        full_name_with_class = func_match.group(2).strip() # e.g., MyNs::MyClass::MyMethod, MyClass::MyMethod, myFunc
        start_char_index = func_match.start()
        start_line_num = content.count('\n', 0, start_char_index) + 1
        
        # Construct a more robust signature using filepath to help uniqueness
        # This still needs refinement for proper namespace handling from includes
        relative_path = os.path.relpath(filepath, ".").replace(os.sep, "_") # e.g. ecu_powertrain_control_engine_manager_cpp
        signature = f"{relative_path}::{full_name_with_class}"
        
        if signature not in functions_data:
            functions_data[signature] = {
                "logs": [],
                "calls": [],
                "file": filepath,
                "line": start_line_num,
                "raw_name": full_name_with_class # Store the parsed name
            }
        # We'll populate logs and calls by iterating lines and checking if they fall within function body later
        # For now, this just identifies function blocks. A proper AST parser is needed for accurate scoping.

    # Iterate again for logs and calls, trying to associate with functions (very basic scope detection)
    # This part is highly challenging with regex and is a major simplification.
    current_function_signature_active = None
    current_function_brace_level = 0

    for line_num, line_text in enumerate(lines):
        # Try to detect if we entered a function body found by FUNCTION_DEF_PATTERN
        for sig, func_data_val in functions_data.items():
            if func_data_val["line"] == line_num + 1 and '{' in line_text: # Simplistic check
                current_function_signature_active = sig
                current_function_brace_level = line_text.count('{') - line_text.count('}')
                break # Found a potential function start

        if current_function_signature_active:
            # Extract logs
            for level, pattern in LOG_PATTERNS.items():
                for log_match in pattern.finditer(line_text):
                    log_entry = {
                        "level": level,
                        "message_args_str": log_match.group(1).strip(),
                        "line": line_num + 1,
                        "raw_log_statement": log_match.group(0).strip()
                    }
                    functions_data[current_function_signature_active]["logs"].append(log_entry)
            
            # Extract calls
            for call_pattern in CALL_PATTERNS:
                for call_match in call_pattern.finditer(line_text):
                    if not any(call_match.group(1).startswith(log_macro) for log_macro in LOG_PATTERNS.keys()):
                        call_entry = {
                            "callee_expression": call_match.group(1).strip(),
                            "callee_args_str": call_match.group(2).strip(),
                            "line": line_num + 1
                        }
                        functions_data[current_function_signature_active]["calls"].append(call_entry)

            # Update brace level for current function
            current_function_brace_level += line_text.count('{')
            current_function_brace_level -= line_text.count('}')
            if current_function_brace_level <= 0:
                current_function_signature_active = None # Exited current function scope

    return functions_data

def main():
    codebase_path = "."
    all_functions_data = {}

    for filepath in find_source_files(codebase_path):
        if os.path.basename(filepath) == os.path.basename(__file__): # Compare basenames
            continue
        print(f"Scanning: {filepath}")
        try:
            with open(filepath, "r", encoding="utf-8", errors="ignore") as f:
                content = f.read()
            
            file_functions_data = parse_file_content(filepath, content)
            for func_sig, data in file_functions_data.items():
                if func_sig not in all_functions_data:
                    all_functions_data[func_sig] = data
                else:
                    all_functions_data[func_sig]["logs"].extend(data["logs"])
                    all_functions_data[func_sig]["calls"].extend(data["calls"])
        except Exception as e:
            print(f"Error processing file {filepath}: {e}")

    final_structured_data_for_json = []
    for sig, data in all_functions_data.items():
        final_structured_data_for_json.append({
            "signature": sig,
            "file": data["file"],
            "line": data["line"],
            "logs_in_order": data["logs"],
            "calls_in_order": data["calls"]
        })

    output_json_file = "static_analysis_scheme.json"
    try:
        with open(output_json_file, "w", encoding="utf-8") as f:
            json.dump(final_structured_data_for_json, f, indent=4)
        print(f"✅ Static analysis scheme saved to {output_json_file}")
    except Exception as e:
        print(f"❌ Error saving JSON to {output_json_file}: {e}")

    output_graph_file = 'call_graph' # Filename without extension
    dot = Digraph(comment='AutoSystemSim Call Graph', format='pdf')
    dot.attr(rankdir='LR', size='10,7', overlap='false', splines='true') # Added some attributes

    added_nodes = set()
    
    # Add defined functions as nodes
    for func_data in final_structured_data_for_json:
        node_id = sanitize_for_graphviz_id(func_data["signature"])
        if node_id not in added_nodes:
            label_parts = func_data["signature"].split("::") # Original signature for label
            short_label = "::".join(label_parts[-2:]) if len(label_parts) >=2 else label_parts[-1]
            short_label += f"\\n({os.path.basename(func_data['file'])}:{func_data['line']})"
            dot.node(node_id, label=short_label)
            added_nodes.add(node_id)

    # Add edges and potentially new nodes for unresolved callees
    for func_data in final_structured_data_for_json:
        caller_node_id = sanitize_for_graphviz_id(func_data["signature"])
        
        for call in func_data["calls_in_order"]:
            callee_expr = call["callee_expression"]
            # Try to find if callee_expr (or part of it) matches a defined function's raw_name
            # This is still a heuristic for regex-based parsing.
            potential_callee_sig_found = None
            # Clean the expression: remove obj-> or obj. parts for matching
            simple_callee_name = callee_expr.split('->')[-1].split('.')[-1]

            for defined_sig, defined_data in all_functions_data.items():
                if defined_data.get("raw_name", "").endswith(simple_callee_name):
                    potential_callee_sig_found = defined_sig
                    break
            
            if potential_callee_sig_found:
                callee_node_id = sanitize_for_graphviz_id(potential_callee_sig_found)
                if callee_node_id not in added_nodes: # Should be there if defined_data existed
                    # This case might indicate an issue or a call to an external/library function
                    # For now, if we found it in all_functions_data, it should have been added as a node.
                    # If it wasn't, there's a discrepancy.
                    # Let's assume if potential_callee_sig_found, its node exists or will be made
                    pass
                dot.edge(caller_node_id, callee_node_id)
            else:
                # Callee not found in our defined functions, treat as external or unresolved
                unresolved_callee_node_id = sanitize_for_graphviz_id(callee_expr)
                if unresolved_callee_node_id not in added_nodes:
                    dot.node(unresolved_callee_node_id, label=callee_expr, shape="box", style="dashed", color="grey")
                    added_nodes.add(unresolved_callee_node_id)
                dot.edge(caller_node_id, unresolved_callee_node_id, style="dashed", color="grey")
    try:
        dot.render(output_graph_file, view=False, cleanup=True) # Renders to call_graph.pdf
        print(f"✅ Call graph saved to {output_graph_file}.pdf")
    except Exception as e:
        if "make sure the Graphviz executables are on your systems' PATH" in str(e).lower() or isinstance(e, FileNotFoundError):
            print(f"❌ Error rendering graph: {e}")
            print("Ensure Graphviz is installed and its 'bin' directory is in your system's PATH.")
            print("You can download Graphviz from: https://graphviz.org/download/")
        else:
            print(f"❌ Error rendering graph: {e}")

if __name__ == "__main__":
    main()