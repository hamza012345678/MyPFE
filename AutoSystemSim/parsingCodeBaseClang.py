import os
import re
import json
import clang.cindex
import subprocess

# --- LIBCLANG PATH CONFIGURATION ---
libclang_found_and_set = False
if 'LIBCLANG_LIBRARY_PATH' in os.environ:
    path_from_env = os.environ['LIBCLANG_LIBRARY_PATH']
    if os.path.isfile(path_from_env):
        try:
            clang.cindex.Config.set_library_file(path_from_env)
            libclang_found_and_set = True
        except Exception: pass
    elif os.path.isdir(path_from_env):
        try:
            clang.cindex.Config.set_library_path(path_from_env)
            idx_test = clang.cindex.Index.create(excludeDecls=True)
            del idx_test
            libclang_found_and_set = True
        except Exception: pass

if not libclang_found_and_set:
    common_libclang_dirs = [
        '/usr/lib/llvm-16/lib', '/usr/lib/llvm-15/lib', '/usr/lib/llvm-14/lib', 
        '/usr/lib/llvm-13/lib', '/usr/lib/llvm-12/lib', '/usr/lib/llvm-11/lib', 
        '/usr/lib/llvm-10/lib',
        '/usr/lib/x86_64-linux-gnu', '/usr/lib64', '/usr/local/lib',
        '/opt/homebrew/opt/llvm/lib', '/usr/local/opt/llvm/lib', 
        'C:/Program Files/LLVM/bin' 
    ]
    lib_names = [
        'libclang.so', 'libclang.so.1', 'libclang-16.so', 'libclang-15.so', 
        'libclang-14.so', 'libclang-13.so', 'libclang-12.so', 'libclang-11.so', 
        'libclang-10.so',
        'libclang.dylib', 'libclang.dll'
    ]
    for lib_dir in common_libclang_dirs:
        if not os.path.isdir(lib_dir): continue
        for lib_name in lib_names:
            potential_path = os.path.join(lib_dir, lib_name)
            if os.path.isfile(potential_path):
                try:
                    clang.cindex.Config.set_library_file(potential_path)
                    idx_test = clang.cindex.Index.create(excludeDecls=True)
                    del idx_test
                    print(f"✅ Automatically found and using libclang: {potential_path}")
                    libclang_found_and_set = True
                    break
                except Exception: pass
        if libclang_found_and_set: break

if not libclang_found_and_set:
    print("❌ CRITICAL: libclang could not be found or configured. Exiting.")
    exit(1)
# --- END LIBCLANG PATH CONFIGURATION ---

LOG_MACRO_PREFIX = "ECU_LOG_"
EXPECTED_LOG_LEVELS = ["FATAL", "ERROR", "WARN", "INFO", "DEBUG"]

IGNORE_CALL_PREFIXES = (
    "std::", "__gnu_cxx::", "printf", "rand", "srand", "exit", "abort", "malloc", "free", "memset", "memcpy", "memmove",
    "clang::" 
)

CODEBASE_ABS_PATH = "" 

def find_source_files(root_dir):
    source_file_extensions = ('.cpp', '.h')
    excluded_dirs = ['build', '.git', '.vscode', 'venv', '__pycache__']
    for subdir, dirs, files in os.walk(root_dir):
        dirs[:] = [d for d in dirs if d not in excluded_dirs and not d.startswith('.')]
        for file_name in files:
            if file_name.lower().endswith(source_file_extensions):
                yield os.path.join(subdir, file_name)

def get_literal_value_from_node(node):
    if not node: return None
    if node.kind == clang.cindex.CursorKind.STRING_LITERAL:
        tokens = list(node.get_tokens())
        if tokens:
            val_str = tokens[0].spelling
            if val_str.startswith('L"'): return val_str[2:-1]
            if val_str.startswith('u"'): return val_str[2:-1]
            if val_str.startswith('U"'): return val_str[2:-1]
            if val_str.startswith('u8"'): return val_str[3:-1]
            if val_str.startswith('"'): return val_str[1:-1]
            return val_str
        return node.spelling
    elif node.kind == clang.cindex.CursorKind.INTEGER_LITERAL:
        try: return int(list(node.get_tokens())[0].spelling)
        except: return list(node.get_tokens())[0].spelling if node.get_tokens() else node.spelling 
    elif node.kind == clang.cindex.CursorKind.FLOATING_LITERAL:
        try: return float(list(node.get_tokens())[0].spelling)
        except: return list(node.get_tokens())[0].spelling if node.get_tokens() else node.spelling
    elif node.kind == clang.cindex.CursorKind.CHARACTER_LITERAL:
        val_str = list(node.get_tokens())[0].spelling
        return val_str[1:-1] if len(val_str) > 1 and val_str.startswith("'") and val_str.endswith("'") else val_str
    elif node.kind == clang.cindex.CursorKind.CXX_BOOL_LITERAL_EXPR:
        return True if node.spelling == "true" else False
    elif node.kind == clang.cindex.CursorKind.DECL_REF_EXPR:
        return node.spelling 
    elif node.kind == clang.cindex.CursorKind.UNEXPOSED_EXPR or \
         node.kind == clang.cindex.CursorKind.PAREN_EXPR or \
         node.kind == clang.cindex.CursorKind.CSTYLE_CAST_EXPR :
        children = list(node.get_children())
        node_to_eval = children[-1] if children else None
        if node.kind == clang.cindex.CursorKind.CSTYLE_CAST_EXPR and children:
            if children[0].kind == clang.cindex.CursorKind.TYPE_REF and len(children) > 1:
                node_to_eval = children[1]
            else: 
                node_to_eval = children[0] 
                while node_to_eval and node_to_eval.kind in [clang.cindex.CursorKind.UNEXPOSED_EXPR, clang.cindex.CursorKind.PAREN_EXPR]:
                    sub_children = list(node_to_eval.get_children())
                    if sub_children: node_to_eval = sub_children[0]
                    else: break
        if node_to_eval: return get_literal_value_from_node(node_to_eval)
    raw_text_fallback = node.spelling
    if not raw_text_fallback: 
        tokens = list(node.get_tokens())
        if tokens: raw_text_fallback = " ".join([t.spelling for t in tokens])
    return raw_text_fallback if raw_text_fallback else f"[NonLiteral:{node.kind.name}]"

def get_full_qualified_name(cursor):
    if not cursor: return ""
    name_parts = []
    current_spelling_at_cursor = cursor.spelling 
    curr = cursor
    if curr.spelling:
        name_parts.append(curr.spelling)
    parent = curr.semantic_parent
    while parent and parent.kind not in [clang.cindex.CursorKind.TRANSLATION_UNIT, clang.cindex.CursorKind.INVALID_FILE]:
        parent_spelling = parent.spelling
        if not parent_spelling and parent.kind == clang.cindex.CursorKind.NAMESPACE:
            parent_spelling = "(anonymous_namespace)"
        if parent_spelling:
            if not (cursor.kind in [clang.cindex.CursorKind.CONSTRUCTOR, clang.cindex.CursorKind.DESTRUCTOR] and \
                    len(name_parts) == 1 and name_parts[0].endswith(parent_spelling)): 
                if not name_parts or name_parts[-1] != parent_spelling : 
                     if cursor.kind in [clang.cindex.CursorKind.CONSTRUCTOR, clang.cindex.CursorKind.DESTRUCTOR] and \
                        current_spelling_at_cursor.endswith(parent_spelling) and len(name_parts)==1 :
                        pass 
                     else: name_parts.append(parent_spelling)
        if parent == parent.semantic_parent : break 
        parent = parent.semantic_parent
    qualified_name = "::".join(reversed(name_parts))
    if cursor.kind == clang.cindex.CursorKind.CONSTRUCTOR and len(qualified_name.split("::")) > 1 and \
       qualified_name.split("::")[-1] == qualified_name.split("::")[-2] and \
       qualified_name.split("::")[-1] == current_spelling_at_cursor :
        qualified_name = "::".join(qualified_name.split("::")[:-1])
    return qualified_name

# Dans extract_execution_elements_recursive:

# Assurez-vous que CODEBASE_ABS_PATH est une variable globale accessible
# ou passez-la en argument si elle est définie dans main() uniquement.
# REMPLACEZ la fonction extract_execution_elements_recursive existante par celle-ci

def extract_execution_elements_recursive(parent_cursor_node, filepath, current_function_fqn, counters):
    # Votre logique de debug est conservée
    is_parent_a_traced_IF = False
    is_parent_a_traced_COMPOUND = False
    # ... etc.

    elements = []
    if not parent_cursor_node: return elements

    for child_idx, cursor in enumerate(parent_cursor_node.get_children()):
        # Votre logique de filtrage est conservée
        if not cursor.location.file or not hasattr(cursor.location.file, 'name') or \
           (cursor.location.file.name != filepath and parent_cursor_node.kind != clang.cindex.CursorKind.TRANSLATION_UNIT):
            continue

        element_data = None
        
        # VOTRE LOGIQUE POUR DO_STMT
        if cursor.kind == clang.cindex.CursorKind.DO_STMT:
            do_stmt_children = list(cursor.get_children())
            is_log_macro_structure = False
            if len(do_stmt_children) >= 1 and do_stmt_children[0].kind == clang.cindex.CursorKind.COMPOUND_STMT:
                body_compound_stmt = do_stmt_children[0]
                potential_printf_calls = [node for node in body_compound_stmt.walk_preorder() if node.kind == clang.cindex.CursorKind.CALL_EXPR and node.spelling == "printf"]
                
                if len(potential_printf_calls) == 3:
                    is_log_macro_structure = True
                    # ... votre logique d'extraction de log détaillée ...
                    printf1_args_nodes = list(potential_printf_calls[0].get_arguments())
                    printf2_args_nodes = list(potential_printf_calls[1].get_arguments())
                    prefix_format_val_cand = get_literal_value_from_node(printf1_args_nodes[0])
                    match_level = re.search(r"\[(FATAL|ERROR|WARN|INFO|DEBUG)\s*\]", str(prefix_format_val_cand))
                    log_level_str_from_printf = match_level.group(1) if match_level else "[Level?]"
                    apid_val = get_literal_value_from_node(printf1_args_nodes[1])
                    ctid_val = get_literal_value_from_node(printf1_args_nodes[2])
                    template_val = get_literal_value_from_node(printf2_args_nodes[0])
                    arg_values_list = [get_literal_value_from_node(arg) for arg in printf2_args_nodes[1:]]
                    
                    element_data = {
                        "type": "LOG", "level": log_level_str_from_printf,
                        "apId": apid_val, "ctId": ctid_val,
                        "message_template": template_val, "values": arg_values_list,
                        "file": os.path.relpath(filepath, CODEBASE_ABS_PATH),
                        "line": cursor.location.line, "function": current_function_fqn
                    }
            
            if not is_log_macro_structure:
                # AJOUT DE L'ID POUR DO_WHILE
                stable_id = f"loop_{counters['loop']}"
                counters['loop'] += 1
                body_elements_loop = extract_execution_elements_recursive(do_stmt_children[0], filepath, current_function_fqn + "::dowhile_body", counters)
                element_data = {
                    "type": "DO_WHILE_LOOP", 
                    "id_in_function": stable_id,
                    "loop_condition": " ".join([t.spelling for t in do_stmt_children[-1].get_tokens()]),
                    "line": cursor.location.line, "body_elements": body_elements_loop
                }

        # VOTRE LOGIQUE POUR CALL_EXPR
        elif element_data is None and cursor.kind == clang.cindex.CursorKind.CALL_EXPR:
            # ... votre logique inchangée ...
            callee_cursor_node = cursor.referenced
            callee_name_at_call_site = cursor.spelling
            resolved_callee_fqn = ""
            if callee_cursor_node and callee_cursor_node.kind != clang.cindex.CursorKind.NO_DECL_FOUND:
                resolved_callee_fqn = get_full_qualified_name(callee_cursor_node)
            if not any(callee_name_at_call_site.startswith(p) for p in IGNORE_CALL_PREFIXES) and callee_name_at_call_site != "printf":
                 element_data = {
                    "type": "CALL", "caller": current_function_fqn,
                    "callee": resolved_callee_fqn or callee_name_at_call_site or "[UnknownCallee]",
                    "file": os.path.relpath(filepath, CODEBASE_ABS_PATH), "line": cursor.location.line
                }
        
        # VOTRE LOGIQUE POUR IF_STMT (AVEC L'AJOUT)
        elif element_data is None and cursor.kind == clang.cindex.CursorKind.IF_STMT:
            # AJOUT DE L'ID
            stable_id = f"if_{counters['if']}"
            counters['if'] += 1

            if_stmt_children = list(cursor.get_children())
            cond_node = if_stmt_children[0]
            then_node = if_stmt_children[1] if len(if_stmt_children) > 1 else None
            else_node = if_stmt_children[2] if len(if_stmt_children) > 2 else None

            condition_expr_text = " ".join([t.spelling for t in cond_node.get_tokens()])
            
            then_elements = extract_execution_elements_recursive(then_node, filepath, current_function_fqn + "::if_then", counters) if then_node else []
            else_elements = extract_execution_elements_recursive(else_node, filepath, current_function_fqn + "::if_else", counters) if else_node else []

            element_data = {
                "type": "IF_STMT",
                "id_in_function": stable_id, # <<<< LA LIGNE AJOUTÉE
                "condition_expression": condition_expr_text,
                "line": cursor.location.line,
                "then_branch_elements": then_elements, "else_branch_elements": else_elements
            }

        # VOTRE LOGIQUE POUR LES AUTRES BOUCLES (AVEC L'AJOUT)
        elif element_data is None and cursor.kind in [clang.cindex.CursorKind.FOR_STMT, clang.cindex.CursorKind.WHILE_STMT, clang.cindex.CursorKind.CXX_FOR_RANGE_STMT]:
            # AJOUT DE L'ID
            stable_id = f"loop_{counters['loop']}"
            counters['loop'] += 1
            
            loop_type_str = cursor.kind.name.replace("_STMT", "") + "_LOOP"
            body_node = list(cursor.get_children())[-1]
            body_elements_loop = extract_execution_elements_recursive(body_node, filepath, current_function_fqn + "::loop_body", counters)
            
            element_data = {
                "type": loop_type_str, 
                "id_in_function": stable_id, # <<<< LA LIGNE AJOUTÉE
                "line": cursor.location.line, "body_elements": body_elements_loop
            }
        
        # VOTRE LOGIQUE POUR SWITCH/CASE/DEFAULT
        elif element_data is None and cursor.kind == clang.cindex.CursorKind.SWITCH_STMT:
            # AJOUT DE L'ID
            stable_id = f"switch_{counters['switch']}"
            counters['switch'] += 1
            
            body_node = next((c for c in cursor.get_children() if c.kind == clang.cindex.CursorKind.COMPOUND_STMT), None)
            switch_body_elements = extract_execution_elements_recursive(body_node, filepath, current_function_fqn + "::switch_body", counters) if body_node else []
            
            element_data = {
                "type": "SWITCH_STMT",
                "id_in_function": stable_id, # <<<< LA LIGNE AJOUTÉE
                "line": cursor.location.line, "body_elements": switch_body_elements
            }
        # ... (le reste de votre logique pour CASE, DEFAULT, etc. est conservé)

        if element_data:
            elements.append(element_data)
        # Gérer les blocs anonymes, en propageant les compteurs
        elif cursor.kind == clang.cindex.CursorKind.COMPOUND_STMT and parent_cursor_node.kind not in [
                clang.cindex.CursorKind.IF_STMT, clang.cindex.CursorKind.FOR_STMT,
                clang.cindex.CursorKind.WHILE_STMT, clang.cindex.CursorKind.DO_STMT,
                clang.cindex.CursorKind.SWITCH_STMT, clang.cindex.CursorKind.FUNCTION_DECL,
                clang.cindex.CursorKind.CXX_METHOD, clang.cindex.CursorKind.CONSTRUCTOR,
                clang.cindex.CursorKind.DESTRUCTOR, clang.cindex.CursorKind.CXX_FOR_RANGE_STMT,
                clang.cindex.CursorKind.CASE_STMT, clang.cindex.CursorKind.DEFAULT_STMT
            ]:
            elements.extend(extract_execution_elements_recursive(cursor, filepath, current_function_fqn + "::block", counters))

    return elements
def parse_cpp_file_with_clang(filepath, include_paths_for_clang):
    args = ['-ferror-limit=0'] 
    for inc_path in include_paths_for_clang:
        args.append(f'-I{inc_path}')
    if filepath.lower().endswith(('.cpp', '.hpp', '.cxx')):
        args.extend(['-std=c++17', '-x', 'c++'])
    elif filepath.lower().endswith('.c'):
        args.extend(['-std=c11', '-x', 'c'])
    elif filepath.lower().endswith('.h'): 
        args.extend(['-std=c++17', '-x', 'c++-header'])
    else: args.extend(['-std=c++17'])
    idx = clang.cindex.Index.create()
    tu = None
    try:
        tu_options = (clang.cindex.TranslationUnit.PARSE_DETAILED_PROCESSING_RECORD |
                      clang.cindex.TranslationUnit.PARSE_INCOMPLETE)
        tu = idx.parse(filepath, args=args, options=tu_options)
    except clang.cindex.TranslationUnitLoadError as e:
        print(f"  ❌ Clang TU Load Error for {filepath}: {e}")
        return None
    if not tu:
        print(f"  ❌ Clang: idx.parse returned None for {filepath}.")
        return None
    has_critical_errors = any(d.severity >= clang.cindex.Diagnostic.Error for d in tu.diagnostics if d.location.file and d.location.file.name == filepath)
    if has_critical_errors:
        print(f"  ⚠️ Clang reported critical errors for {filepath}. AST parsing might be incomplete.")
    return tu.cursor


def main():
    global CODEBASE_ABS_PATH
    CODEBASE_ROOT_DIR_NAME = "dummy_automotive_project" 
    script_dir = os.path.dirname(os.path.abspath(__file__))
    codebase_root_path = ""
    potential_paths = [
        script_dir, # Script dans le dossier parent de dummy_automotive_project
        os.path.join(script_dir, CODEBASE_ROOT_DIR_NAME), # Script à côté de dummy_automotive_project
        os.path.abspath(os.path.join(script_dir, "..", CODEBASE_ROOT_DIR_NAME)), # Script dans un sous-dossier de dummy_automotive_project
        os.path.abspath(os.path.join(script_dir, "..")) # Script dans dummy_automotive_project, codebase_root est parent
    ]
    # Priorité si le nom du dossier correspond
    for p_path in potential_paths:
        if os.path.isdir(p_path) and os.path.basename(p_path) == CODEBASE_ROOT_DIR_NAME:
            codebase_root_path = p_path
            break
    # Fallback si le nom ne correspond pas mais que src/include existent
    if not codebase_root_path:
        for p_path in potential_paths: 
            if os.path.isdir(p_path) and \
               os.path.exists(os.path.join(p_path, "src")) and \
               os.path.exists(os.path.join(p_path, "include")):
                codebase_root_path = p_path
                # print(f"⚠️  WARN: Using fallback codebase path strategy: '{codebase_root_path}'")
                break
    if not codebase_root_path or not os.path.isdir(codebase_root_path):
        print(f"❌ CRITICAL: Could not find/validate the codebase root directory '{CODEBASE_ROOT_DIR_NAME}'.")
        print(f"   Tried combinations around script dir: '{script_dir}'")
        print(f"   Please ensure the script is placed correctly relative to '{CODEBASE_ROOT_DIR_NAME}' or update CODEBASE_ROOT_DIR_NAME.")
        return

    CODEBASE_ABS_PATH = os.path.abspath(codebase_root_path)
    print(f"ℹ️ Using codebase path: {CODEBASE_ABS_PATH}")

    project_include_paths = [os.path.join(CODEBASE_ABS_PATH, "include")]
    common_include_path = os.path.join(CODEBASE_ABS_PATH, "include", "common")
    if os.path.isdir(common_include_path) and common_include_path not in project_include_paths:
        project_include_paths.append(common_include_path)

    cpp_std_includes_heuristic = []
    try:
        process = subprocess.run(['clang++', '-E', '-P', '-x', 'c++', '-', '-v'],
                                 input='#include <vector>\nint main(){return 0;}',
                                 capture_output=True, text=True, check=False, timeout=5)
        in_search_list = False
        for line in process.stderr.splitlines():
            line_strip = line.strip()
            if line_strip.startswith("#include <...> search starts here:"): in_search_list = True; continue
            if line_strip.startswith("End of search list."): in_search_list = False; continue
            if in_search_list and os.path.isdir(line_strip) and line_strip not in cpp_std_includes_heuristic: 
                cpp_std_includes_heuristic.append(line_strip)
        # if cpp_std_includes_heuristic: print(f"ℹ️ Auto-detected system include paths via 'clang++ -v': {len(cpp_std_includes_heuristic)} paths.")
    except Exception: pass 

    fallback_std_includes = ['/usr/include', '/usr/local/include']
    effective_include_paths = list(set(project_include_paths + cpp_std_includes_heuristic + fallback_std_includes))
    effective_include_paths = [p for p in effective_include_paths if os.path.isdir(p)]
    # print(f"ℹ️ Using effective include paths for Clang ({len(effective_include_paths)} paths):")
    # for p_idx, p in enumerate(effective_include_paths):
    #     if p_idx < 5: print(f"    {p}")
    # if len(effective_include_paths) > 5: print("    ...")

    all_functions_data = {} 
    all_calls_for_graph = [] 

    def collect_calls_recursively(element_list, graph_list_collector):
        for item in element_list:
            if item["type"] == "CALL":
                if item.get("caller") and item.get("callee"):
                    if item["callee"] and not item["callee"].startswith("[Unknown"):
                        graph_list_collector.append((item["caller"], item["callee"]))
            if "then_branch_elements" in item:
                collect_calls_recursively(item["then_branch_elements"], graph_list_collector)
            if "else_branch_elements" in item:
                collect_calls_recursively(item["else_branch_elements"], graph_list_collector)
            if "body_elements" in item: 
                collect_calls_recursively(item["body_elements"], graph_list_collector)

    for filepath in find_source_files(CODEBASE_ABS_PATH):
        print(f"🔍 Parsing: {os.path.relpath(filepath, CODEBASE_ABS_PATH)}")
        tu_root_cursor = parse_cpp_file_with_clang(filepath, effective_include_paths)
        if not tu_root_cursor: continue
        for cursor in tu_root_cursor.walk_preorder():
            if not cursor.location.file or cursor.location.file.name != filepath: continue
            if cursor.kind in [clang.cindex.CursorKind.FUNCTION_DECL,
                               clang.cindex.CursorKind.CXX_METHOD,
                               clang.cindex.CursorKind.CONSTRUCTOR,
                               clang.cindex.CursorKind.DESTRUCTOR] and cursor.is_definition():
                func_fqn = get_full_qualified_name(cursor)
                if not func_fqn: 
                    func_fqn = f"unknown_func_at_{os.path.relpath(filepath,CODEBASE_ABS_PATH)}_{cursor.location.line}"
                if func_fqn not in all_functions_data:
                    all_functions_data[func_fqn] = {
                        "file": os.path.relpath(filepath, CODEBASE_ABS_PATH),
                        "line_start": cursor.location.line,
                        "line_end": cursor.extent.end.line,
                        "execution_elements_structured": []
                    }
                func_body_cursor = None
                for child in cursor.get_children():
                    if child.kind == clang.cindex.CursorKind.COMPOUND_STMT:
                        func_body_cursor = child; break
                if func_body_cursor:
                    counters = {'if': 0, 'loop': 0, 'switch': 0}
                    structured_elements = extract_execution_elements_recursive(func_body_cursor, filepath, func_fqn,counters)
                    all_functions_data[func_fqn]["execution_elements_structured"] = structured_elements
                    collect_calls_recursively(structured_elements, all_calls_for_graph)
                
    call_graph_output = {}
    for caller, callee in all_calls_for_graph:
        if caller not in call_graph_output: call_graph_output[caller] = []
        if callee not in call_graph_output[caller]: call_graph_output[caller].append(callee)

    final_output = { "functions": all_functions_data, "call_graph": call_graph_output }
    output_json_filename = "static_analysis_output.json"
    output_json_filepath = os.path.join(CODEBASE_ABS_PATH, output_json_filename)
    try:
        with open(output_json_filepath, "w", encoding="utf-8") as f:
            json.dump(final_output, f, indent=2, ensure_ascii=False)
        print(f"✅ Static analysis results saved to {output_json_filepath}")
    except Exception as e_save: print(f"❌ Error saving JSON output: {e_save}")
    print("\nℹ️ Static analysis phase completed.")

if __name__ == "__main__":
    LOG_MACRO_PREFIX = "ECU_LOG_"
    EXPECTED_LOG_LEVELS = ["FATAL", "ERROR", "WARN", "INFO", "DEBUG"]
    IGNORE_CALL_PREFIXES = (
        "std::", "__gnu_cxx::", "printf", "rand", "srand", "exit", "abort", "malloc", "free", "memset", "memcpy", "memmove",
        "clang::" 
    )
    CODEBASE_ABS_PATH = "" 
    main()
    