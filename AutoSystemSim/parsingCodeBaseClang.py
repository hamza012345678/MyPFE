import os
import re
import json
import clang.cindex
import subprocess

# --- LIBCLANG PATH CONFIGURATION ---
libclang_found_and_set = False
# (Votre code de configuration LibClang existant ici - je le garde concis pour l'exemple)
if 'LIBCLANG_LIBRARY_PATH' in os.environ:
    path_from_env = os.environ['LIBCLANG_LIBRARY_PATH']
    if os.path.isfile(path_from_env):
        try:
            clang.cindex.Config.set_library_file(path_from_env)
            print(f"✅ Using libclang file from LIBCLANG_LIBRARY_PATH: {path_from_env}")
            libclang_found_and_set = True
        except Exception as e:
            print(f"⚠️ Tried LIBCLANG_LIBRARY_PATH (as file) '{path_from_env}', but failed: {e}")
    elif os.path.isdir(path_from_env):
        try:
            clang.cindex.Config.set_library_path(path_from_env)
            idx_test = clang.cindex.Index.create(excludeDecls=True)
            del idx_test
            print(f"✅ Using libclang directory from LIBCLANG_LIBRARY_PATH: {path_from_env}")
            libclang_found_and_set = True
        except Exception as e:
            print(f"⚠️ Tried LIBCLANG_LIBRARY_PATH (as dir) '{path_from_env}', but failed: {e}")
    else:
        print(f"⚠️ LIBCLANG_LIBRARY_PATH ('{path_from_env}') is not a valid file or directory.")

if not libclang_found_and_set:
    print("ℹ️ LIBCLANG_LIBRARY_PATH not set or failed. Trying common locations...")
    common_libclang_dirs = [
        '/usr/lib/llvm-10/lib', 
        '/usr/lib/x86_64-linux-gnu', '/usr/lib64', '/usr/local/lib', 
        '/opt/homebrew/opt/llvm/lib', '/usr/local/opt/llvm/lib'
    ]
    lib_names = ['libclang.so', 'libclang.so.10', 'libclang-10.so', 'libclang.dylib']
    
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
                except Exception as e_auto:
                    print(f"ℹ️ Tried auto-detected libclang at {potential_path}, but failed: {e_auto}")
        if libclang_found_and_set: break

if not libclang_found_and_set:
    print("❌ CRITICAL: libclang could not be found or configured. Exiting.")
    exit(1)
# --- END LIBCLANG PATH CONFIGURATION ---


LOG_MACRO_NAMES = ["LOG_FATAL", "LOG_ERROR", "LOG_WARNING", "LOG_INFO", "LOG_DEBUG"]
LOG_ARG_EXTRACT_PATTERNS = {
    name: re.compile(r"{}\s*\((.*)\)\s*(;)?".format(re.escape(name)), re.DOTALL)
    for name in LOG_MACRO_NAMES
}

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
IGNORE_AS_CALLEE_IF_PART_OF_LOG_IMPL = { "getCurrentTimestamp", "printf" }


def find_source_files(root_dir):
    source_file_extensions = ('.cpp', '.h', '.hpp', '.c', '.cc')
    excluded_dirs = ['build', 'tests', '.git', '.vscode', 'venv', '__pycache__', 'docs', 'examples']
    for subdir, dirs, files in os.walk(root_dir):
        dirs[:] = [d for d in dirs if d not in excluded_dirs and not d.startswith('.')]
        for file in files:
            if os.path.basename(file) == os.path.basename(__file__):
                continue
            if file.lower().endswith(source_file_extensions):
                yield os.path.join(subdir, file)

def get_cursor_source_code(cursor):
    if not (cursor and cursor.extent and
            cursor.extent.start.file and cursor.extent.end.file and
            cursor.extent.start.file.name == cursor.extent.end.file.name):
        return "[Invalid Extent]"
    try:
        with open(cursor.extent.start.file.name, 'rb') as f_bytes:
            source_bytes = f_bytes.read()
        start_offset = cursor.extent.start.offset
        end_offset = cursor.extent.end.offset
        if 0 <= start_offset <= end_offset <= len(source_bytes):
            return source_bytes[start_offset:end_offset].decode('utf-8', errors='replace').strip().replace('\n', ' ')
        return "[Offset Extent Issue]"
    except Exception:
        return "[Error Extracting Source]"

def get_full_qualified_name(cursor):
    if not cursor: return ""
    name_parts = []
    curr = cursor
    while curr and curr.kind != clang.cindex.CursorKind.TRANSLATION_UNIT:
        spelling = curr.spelling
        if not spelling and curr.kind == clang.cindex.CursorKind.NAMESPACE: spelling = "(anonymous_namespace)"
        if spelling: name_parts.append(spelling)
        curr = curr.lexical_parent if curr.lexical_parent else curr.semantic_parent
    return "::".join(reversed(name_parts))

def get_reliable_signature_key(cursor, filepath_context):
    if hasattr(cursor, 'usr') and cursor.usr: return cursor.usr
    if hasattr(cursor, 'mangled_name') and cursor.mangled_name: return cursor.mangled_name
    fqn = get_full_qualified_name(cursor) or cursor.spelling or f"unnamed_L{cursor.location.line}"
    return f"{os.path.normpath(filepath_context)}::{fqn}@L{cursor.location.line}"

def parse_log_arguments_from_string(args_str_combined):
    args_str_combined = args_str_combined.strip()
    format_string = args_str_combined 
    argument_expressions = []
    fmt_str_match = re.match(r'\s*L?(" (?: \\. | [^"\\] )* ") \s* (?: , \s* (.*) )? $', args_str_combined, re.DOTALL | re.UNICODE)
    if fmt_str_match:
        format_string = fmt_str_match.group(1).strip()
        remaining_args_part = fmt_str_match.group(2)
        if remaining_args_part:
            remaining_args_str = remaining_args_part.strip()
            current_arg = ""
            paren_level = 0
            angle_bracket_level = 0 
            in_string = False
            string_char = ''
            for char in remaining_args_str:
                if char == '"' and (not current_arg or current_arg[-1] != '\\'):
                    in_string = not in_string
                    string_char = '"' if in_string else ''
                elif char == "'" and (not current_arg or current_arg[-1] != '\\'):
                    in_string = not in_string
                    string_char = "'" if in_string else ''
                if not in_string:
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

def macro_name_from_raw(raw_statement):
    if not raw_statement: return None
    match = re.match(r"(\w+)\s*\(", raw_statement)
    return match.group(1) if match else None

def parse_cpp_with_clang(filepath, include_paths=None):
    if include_paths is None: include_paths = []
    args = [f'-I{os.path.dirname(os.path.abspath(filepath))}'] 
    for inc_path in include_paths:
        if os.path.abspath(os.path.dirname(os.path.abspath(filepath))) != os.path.abspath(inc_path):
             args.append(f'-I{inc_path}')

    if filepath.lower().endswith(('.cpp', '.hpp', '.cc')): args.extend(['-std=c++17', '-x', 'c++'])
    elif filepath.lower().endswith('.c'): args.extend(['-std=c11', '-x', 'c'])
    elif filepath.lower().endswith('.h'): args.extend(['-std=c++17', '-x', 'c++-header'])
    
    idx = clang.cindex.Index.create()
    tu = None
    try:
        tu_options = (clang.cindex.TranslationUnit.PARSE_DETAILED_PROCESSING_RECORD |
                      clang.cindex.TranslationUnit.PARSE_INCOMPLETE |
                      clang.cindex.TranslationUnit.PARSE_PRECOMPILED_PREAMBLE | 
                      clang.cindex.TranslationUnit.PARSE_CACHE_COMPLETION_RESULTS)
        if hasattr(clang.cindex.TranslationUnit, 'PARSE_SKIP_FUNCTION_BODIES_IF_MAIN_FILE_IS_HEADER'):
            tu_options |= clang.cindex.TranslationUnit.PARSE_SKIP_FUNCTION_BODIES_IF_MAIN_FILE_IS_HEADER
        tu = idx.parse(filepath, args=args, options=tu_options)
    except clang.cindex.TranslationUnitLoadError as e:
        print(f"  ❌ Clang TU Load Error for {filepath}: {e}")
        return {}, [] 
    if not tu:
        print(f"  ❌ Clang: Failed to create TranslationUnit for {filepath}")
        return {}, []
        
    diagnostics_for_file = []
    for diag in tu.diagnostics:
        if diag.location.file and diag.location.file.name == filepath and diag.severity >= clang.cindex.Diagnostic.Warning:
            diag_severity_str = "UNKNOWN"
            if diag.severity == clang.cindex.Diagnostic.Warning: diag_severity_str = "WARNING"
            elif diag.severity == clang.cindex.Diagnostic.Error: diag_severity_str = "ERROR"
            elif diag.severity == clang.cindex.Diagnostic.Fatal: diag_severity_str = "FATAL"
            
            diagnostics_for_file.append({
                "severity": diag_severity_str, "line": diag.location.line,
                "column": diag.location.column, "message": diag.spelling
            })
            # print(f"  ⚠️ Clang {diag_severity_str} in {diag.location.file.name}:{diag.location.line}:{diag.location.column}: {diag.spelling}")

    functions_data = {} 
    defined_function_map = {} 
    
    for cursor in tu.cursor.walk_preorder():
        if cursor.location.file and cursor.location.file.name == filepath:
            if cursor.kind in [clang.cindex.CursorKind.FUNCTION_DECL, 
                               clang.cindex.CursorKind.CXX_METHOD,
                               clang.cindex.CursorKind.CONSTRUCTOR, 
                               clang.cindex.CursorKind.DESTRUCTOR] and cursor.is_definition():
                func_key = get_reliable_signature_key(cursor, filepath)
                display_name = get_full_qualified_name(cursor) or cursor.spelling or f"func_L{cursor.location.line}"
                if func_key not in functions_data:
                    functions_data[func_key] = {
                        "signature_display": display_name, "file": filepath,
                        "line": cursor.location.line, "end_line": cursor.extent.end.line,
                        "logs_in_order": [], "calls_in_order": []
                    }
                    defined_function_map[func_key] = cursor 

    for cursor in tu.cursor.walk_preorder():
        if not cursor.location.file or cursor.location.file.name != filepath:
            continue

        containing_function_key = None
        for func_key_iter, func_def_cursor_iter in defined_function_map.items():
            if (func_def_cursor_iter.extent.start.file.name == cursor.location.file.name and
                func_def_cursor_iter.extent.start.offset <= cursor.location.offset < func_def_cursor_iter.extent.end.offset):
                containing_function_key = func_key_iter
                break 
        
        if not containing_function_key: continue

        if cursor.kind == clang.cindex.CursorKind.MACRO_INSTANTIATION:
            macro_name = cursor.spelling
            if macro_name in LOG_MACRO_NAMES:
                log_statement_text = get_cursor_source_code(cursor)
                
                parsed_format_string = f"[RawMacroText: {log_statement_text}]"
                parsed_log_args = []
                message_args_str_combined = log_statement_text 

                arg_pattern = LOG_ARG_EXTRACT_PATTERNS.get(macro_name)
                if arg_pattern:
                    log_match = arg_pattern.search(log_statement_text) 
                    if log_match: 
                        captured_args_content = log_match.group(1) 
                        if captured_args_content is not None:
                            message_args_str_combined = captured_args_content.strip()
                            parsed_format_string, parsed_log_args = parse_log_arguments_from_string(message_args_str_combined)
                
                functions_data[containing_function_key]["logs_in_order"].append({
                    "level": macro_name.replace("LOG_", ""), 
                    "log_format_string": parsed_format_string,
                    "log_arguments": parsed_log_args,
                    "message_args_str_combined": message_args_str_combined,
                    "line": cursor.location.line, 
                    "raw_log_statement": log_statement_text
                })

        elif cursor.kind == clang.cindex.CursorKind.CALL_EXPR:
            if not (functions_data[containing_function_key]['line'] <= cursor.location.line <= functions_data[containing_function_key]['end_line']):
                 continue

            callee_cursor = cursor.referenced 
            callee_name_at_call_site = cursor.spelling 
            resolved_callee_key = None
            resolved_callee_display_name = callee_name_at_call_site or "(unknown_callee_expr)"
            is_operator_call = "operator" in resolved_callee_display_name.lower()

            if callee_cursor and callee_cursor.kind != clang.cindex.CursorKind.NO_DECL_FOUND :
                callee_file_ctx = callee_cursor.location.file.name if callee_cursor.location.file else filepath
                resolved_callee_key = get_reliable_signature_key(callee_cursor, callee_file_ctx)
                display_name = get_full_qualified_name(callee_cursor)
                resolved_callee_display_name = display_name or callee_cursor.spelling or resolved_callee_display_name
                is_operator_call = "operator" in resolved_callee_display_name.lower()

            should_keep_call = True
            if any(resolved_callee_display_name.startswith(prefix) for prefix in IGNORE_CALL_DISPLAY_PREFIXES):
                should_keep_call = False
            if is_operator_call and any(std_ns in resolved_callee_display_name for std_ns in ["std::", "__gnu_cxx::"]):
                should_keep_call = False
            if callee_name_at_call_site in IGNORE_AS_CALLEE_IF_PART_OF_LOG_IMPL:
                if functions_data[containing_function_key]["logs_in_order"]:
                    last_log = functions_data[containing_function_key]["logs_in_order"][-1]
                    if cursor.location.line == last_log["line"] and \
                       macro_name_from_raw(last_log["raw_log_statement"]) in LOG_MACRO_NAMES :
                        should_keep_call = False
            if callee_name_at_call_site in LOG_MACRO_NAMES:
                should_keep_call = False

            if should_keep_call:
                functions_data[containing_function_key]["calls_in_order"].append({
                    "callee_expression": get_cursor_source_code(cursor),
                    "callee_name_at_call_site": callee_name_at_call_site or "(anonymous_call)",
                    "callee_resolved_key": resolved_callee_key,
                    "callee_resolved_display_name": resolved_callee_display_name,
                    "line": cursor.location.line
                })

    for func_key in functions_data:
        functions_data[func_key]["logs_in_order"].sort(key=lambda x: x.get("line", 0))
        functions_data[func_key]["calls_in_order"].sort(key=lambda x: x.get("line", 0))
        
    return functions_data, diagnostics_for_file

# --- Main Execution ---
def main():
    script_dir = os.path.dirname(os.path.abspath(__file__))
    if os.path.basename(script_dir) == "AutoSystemSim": codebase_path = script_dir
    elif os.path.isdir(os.path.join(script_dir, "AutoSystemSim")): codebase_path = os.path.join(script_dir, "AutoSystemSim")
    else: codebase_path = "."; print(f"⚠️ Could not auto-detect 'AutoSystemSim'. Using current directory: {os.path.abspath(codebase_path)}")
    print(f"ℹ️ Using codebase_path: {os.path.abspath(codebase_path)}")

    project_subfolders = ["common", "ecu_powertrain_control", "ecu_body_control_module",
                          "ecu_infotainment", "ecu_safety_systems", "ecu_power_management",
                          "main_application"]
    abs_project_include_paths = [os.path.abspath(codebase_path)]
    for folder in project_subfolders:
        path = os.path.join(codebase_path, folder)
        if os.path.isdir(path): abs_project_include_paths.append(os.path.abspath(path))
    
    cpp_std_includes_heuristic = []
    try:
        process = subprocess.run(['clang++', '-E', '-P', '-x', 'c++', '-', '-v'],
                                 input='#include <vector>\nint main(){return 0;}', 
                                 capture_output=True, text=True, check=False, timeout=10)
        in_search_list = False
        for line in process.stderr.splitlines():
            if line.strip().startswith("#include <...> search starts here:"): in_search_list = True; continue
            if line.strip().startswith("End of search list."): in_search_list = False; continue
            if in_search_list:
                path_candidate = line.strip()
                if os.path.isdir(path_candidate) and path_candidate not in cpp_std_includes_heuristic: cpp_std_includes_heuristic.append(path_candidate)
            elif ("/" in line and ("include" in line or "inc" in line.lower()) and os.path.isdir(line.strip()) and any(kw in line.lower() for kw in ["clang", "llvm", "gcc", "g++", "crosstool", "sysroot", "/usr/local"])):
                path_candidate = line.strip()
                if os.path.isdir(path_candidate) and path_candidate not in cpp_std_includes_heuristic: cpp_std_includes_heuristic.append(path_candidate)
        if cpp_std_includes_heuristic: print(f"ℹ️ Auto-detected potential system include paths: {cpp_std_includes_heuristic}")
    except Exception as e_clang_v: print(f"ℹ️ Could not auto-detect clang system include paths via 'clang++ -v': {e_clang_v}")

    fallback_std_includes = ["/usr/include/c++/11", "/usr/include/x86_64-linux-gnu/c++/11", 
                             "/usr/include/c++/10", "/usr/include/x86_64-linux-gnu/c++/10",
                             "/usr/include", "/usr/local/include"]
    if libclang_found_and_set : 
        libclang_path_str = os.environ.get('LIBCLANG_LIBRARY_PATH', '')
        if not libclang_path_str: 
            for lib_dir_cand in common_libclang_dirs:
                if os.path.isdir(lib_dir_cand) and any(os.path.isfile(os.path.join(lib_dir_cand, name)) for name in lib_names):
                    libclang_path_str = lib_dir_cand 
                    break
        
        if "llvm-10/lib/libclang" in libclang_path_str: 
            llvm_base = libclang_path_str.split('llvm-10/lib/libclang')[0]
            clang_resource_dir = os.path.join(llvm_base, 'llvm-10/lib/clang/10.0.0/include') 
            if os.path.isdir(clang_resource_dir) and clang_resource_dir not in fallback_std_includes: 
                fallback_std_includes.append(clang_resource_dir)
        elif "llvm" in libclang_path_str.lower() and "lib/libclang" in libclang_path_str:
             llvm_base_match = re.search(r"(.*llvm[^/]*)/lib/libclang", libclang_path_str, re.IGNORECASE)
             if llvm_base_match:
                 llvm_base = llvm_base_match.group(1)
                 clang_lib_dir_cand = os.path.join(llvm_base, 'lib/clang')
                 if os.path.isdir(clang_lib_dir_cand):
                    versions = sorted([d for d in os.listdir(clang_lib_dir_cand) if os.path.isdir(os.path.join(clang_lib_dir_cand, d))], reverse=True)
                    if versions:
                        clang_resource_dir = os.path.join(clang_lib_dir_cand, versions[0], 'include')
                        if os.path.isdir(clang_resource_dir) and clang_resource_dir not in fallback_std_includes:
                            fallback_std_includes.append(clang_resource_dir)

    for fi in fallback_std_includes:
        if os.path.isdir(fi) and fi not in cpp_std_includes_heuristic:
            cpp_std_includes_heuristic.append(fi)
    
    for std_inc in cpp_std_includes_heuristic:
        abs_std_inc = os.path.abspath(std_inc)
        if abs_std_inc not in abs_project_include_paths: abs_project_include_paths.append(abs_std_inc)
    
    abs_project_include_paths = sorted(list(set(abs_project_include_paths)))
    print(f"ℹ️ Using effective include paths for Clang: {abs_project_include_paths}")

    all_functions_data_clang = {} 
    all_diagnostics_data = {} 

    for filepath in find_source_files(codebase_path):
        print(f"Scanning: {filepath}")
        try:
            file_functions_data, file_diagnostics = parse_cpp_with_clang(filepath, include_paths=abs_project_include_paths)
            if file_diagnostics: 
                all_diagnostics_data[filepath] = file_diagnostics

            for func_key, data in file_functions_data.items():
                if func_key not in all_functions_data_clang:
                    all_functions_data_clang[func_key] = data
                else: 
                    all_functions_data_clang[func_key]["logs_in_order"].extend(data.get("logs_in_order", []))
                    all_functions_data_clang[func_key]["calls_in_order"].extend(data.get("calls_in_order", []))
                    all_functions_data_clang[func_key]["logs_in_order"].sort(key=lambda x: x.get("line",0))
                    all_functions_data_clang[func_key]["calls_in_order"].sort(key=lambda x: x.get("line",0))
        except Exception as e_file_proc:
            print(f"❌ Error processing file {filepath} in main loop: {e_file_proc.__class__.__name__} - {e_file_proc}")

    final_json_output_functions = []
    for reliable_key, data in all_functions_data_clang.items():
        final_json_output_functions.append({
            "function_id_key": reliable_key,
            "display_signature": data.get("signature_display", reliable_key), 
            "file": data["file"],
            "line": data["line"],
            "end_line": data.get("end_line", -1),
            "logs_in_order": data.get("logs_in_order", []),
            "calls_in_order": data.get("calls_in_order", [])
        })
    
    final_json_output = {
        "functions": final_json_output_functions,
        "clang_diagnostics": all_diagnostics_data
    }

    output_json_file = "static_analysis_scheme_clang.json"
    try:
        with open(output_json_file, "w", encoding="utf-8") as f:
            json.dump(final_json_output, f, indent=4)
        print(f"✅ Clang-based static analysis scheme saved to {output_json_file}")
    except Exception as e:
        print(f"❌ Error saving JSON to {output_json_file}: {e}")

    # --- LA GÉNÉRATION DES TRACES TEXTUELLES EST MAINTENANT DANS generateLogTraces.py ---
    # --- Vous pouvez supprimer les appels à find_matching_usr_keys et generate_text_log_sequence d'ici ---
    
    # if all_functions_data_clang:
    #     entry_point_patterns_to_trace = [
    #         "MainVehicleController::simulateDrivingCycle", 
    #         # ... autres patterns ...
    #     ]
    #     actual_entry_point_usr_keys = find_matching_usr_keys(all_functions_data_clang, entry_point_patterns_to_trace)
        
    #     if not actual_entry_point_usr_keys:
    #         print("\n⚠️ No key entry points matched your patterns for text log trace generation.")
    #     else:
    #         # ... (print des points d'entrée trouvés) ...

    #     print("\nGenerating text-based log sequence traces...")
        
    #     target_output_dir_name = "logsText" 
    #     output_logs_text_dir = os.path.join(codebase_path, target_output_dir_name) 
    #     # ... (création du dossier) ...

    #     for actual_entry_point_usr in actual_entry_point_usr_keys:
    #         # ... (appel à generate_text_log_sequence) ...
    # else:
    #     print("⚠️ No function data extracted with Clang, skipping text log sequence generation.")


if __name__ == "__main__":
    main()