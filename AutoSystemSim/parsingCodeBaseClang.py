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
        '/usr/lib/llvm-14/lib', '/usr/lib/llvm-13/lib', '/usr/lib/llvm-12/lib', 
        '/usr/lib/llvm-11/lib', '/usr/lib/llvm-10/lib', 
        '/usr/lib/x86_64-linux-gnu', '/usr/lib64', '/usr/local/lib', 
        '/opt/homebrew/opt/llvm/lib', '/usr/local/opt/llvm/lib',
        'C:/Program Files/LLVM/bin'
    ]
    lib_names = [
        'libclang.so', 'libclang.so.1', 'libclang.so.14', 'libclang-14.so', 
        'libclang.so.13', 'libclang-13.so', 'libclang.so.12', 'libclang-12.so',
        'libclang.so.11', 'libclang-11.so', 'libclang.so.10', 'libclang-10.so', 
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
                except Exception: 
                    pass 
        if libclang_found_and_set: break

if not libclang_found_and_set:
    print("❌ CRITICAL: libclang could not be found or configured. "
          "Please set LIBCLANG_LIBRARY_PATH environment variable or "
          "ensure libclang is in a standard system location. Exiting.")
    exit(1)
# --- END LIBCLANG PATH CONFIGURATION ---

EXPECTED_LOG_LEVELS = ["FATAL", "ERROR", "WARNING", "INFO", "DEBUG"] 
LOG_MACRO_NAMES_ORIGINAL = ["LOG_FATAL", "LOG_ERROR", "LOG_WARNING", "LOG_INFO", "LOG_DEBUG"]

PRINTF_ARGS_PATTERN = re.compile(r"printf\s*\((.*)\)\s*;?", re.DOTALL | re.IGNORECASE)
LOG_IMPL_ARGS_PATTERN = re.compile(r"LOG_IMPL\s*\((.*)\)", re.DOTALL | re.IGNORECASE)
ORIGINAL_LOG_CALL_ARGS_PATTERN = re.compile(r"LOG_(?:FATAL|ERROR|WARNING|INFO|DEBUG)\s*\((.*)\)\s*;?", re.DOTALL | re.IGNORECASE)

IGNORE_CALL_DISPLAY_PREFIXES = (
    "std::", "__gnu_cxx::", "printf", "rand", "srand", "exit", "abort", "malloc", "free",
    "getCurrentTimestamp", 
    "std::chrono", "std::this_thread", "std::vector", "std::map", "std::list", "std::set",
    "std::basic_string", "std::basic_ostream", "std::basic_istream", "std::cout", "std::cerr",
    "std::uniform_int_distribution", "std::uniform_real_distribution",
    "std::mersenne_twister_engine", "std::random_device", "std::to_string","std::min", "std::max",
    "std::fabs", "std::sqrt", "std::cos", "std::sin", "sleep_for",
    "os::", "re::", "json::", 
    "clang::"
)

CODEBASE_PATH_FOR_KEYS = "" 

def find_source_files(root_dir, target_file_rel_path=None):
    source_file_extensions = ('.cpp', '.h', '.hpp', '.c', '.cc')
    excluded_dirs = ['build', 'tests', '.git', '.vscode', 'venv', '__pycache__', 'docs', 'examples']
    
    if target_file_rel_path:
        full_target_path = os.path.normpath(os.path.join(root_dir, target_file_rel_path))
        if os.path.isfile(full_target_path):
            # print(f"ℹ️ Debug mode: Processing only target file: {full_target_path}") # DEBUG
            yield full_target_path
        else:
            print(f"⚠️ Debug mode: Target file '{full_target_path}' not found. Processing all files.")
            for subdir, dirs, files in os.walk(root_dir):
                dirs[:] = [d for d in dirs if d not in excluded_dirs and not d.startswith('.')]
                for file_name in files:
                    if os.path.basename(file_name) == os.path.basename(__file__): continue
                    if file_name.lower().endswith(source_file_extensions): yield os.path.join(subdir, file_name)
        return

    for subdir, dirs, files in os.walk(root_dir):
        dirs[:] = [d for d in dirs if d not in excluded_dirs and not d.startswith('.')]
        for file_name in files:
            if os.path.basename(file_name) == os.path.basename(__file__): continue
            if file_name.lower().endswith(source_file_extensions): yield os.path.join(subdir, file_name)

def get_cursor_source_code(cursor):
    if not (cursor and cursor.extent and cursor.extent.start.file and 
            hasattr(cursor.extent.start.file, 'name') and cursor.extent.end.file and 
            hasattr(cursor.extent.end.file, 'name') and
            cursor.extent.start.file.name == cursor.extent.end.file.name):
        # if cursor and cursor.extent and cursor.extent.start.file: # DEBUG
        #     print(f"    [GET_SRC_DEBUG] Invalid extent or file mismatch for '{cursor.spelling}' L{cursor.location.line}. File: {cursor.location.file.name if cursor.location.file else 'N/A'}") # DEBUG
        # elif not cursor: # DEBUG
        #      print(f"    [GET_SRC_DEBUG] Invalid extent: cursor is None") # DEBUG
        # else: # DEBUG
        #      print(f"    [GET_SRC_DEBUG] Invalid extent for '{cursor.spelling}' L{cursor.location.line}. Extent start file: {cursor.extent.start.file.name if cursor.extent.start.file else 'N/A'}") # DEBUG
        return f"[Invalid Extent ForSrcCode: {cursor.kind} '{cursor.spelling}']"
    try:
        file_path = cursor.extent.start.file.name
        if not os.path.exists(file_path):
            return f"[File Not Found: {file_path}]"
            
        with open(file_path, 'rb') as f_bytes: 
            source_bytes = f_bytes.read()
        start_offset, end_offset = cursor.extent.start.offset, cursor.extent.end.offset
        
        # print(f"    [GET_SRC_DEBUG_OFFSETS] For '{cursor.spelling}' L{cursor.location.line} (Kind: {cursor.kind}): File='{file_path}', StartOffset={start_offset}, EndOffset={end_offset}, FileLen={len(source_bytes)}") # DEBUG

        if not (0 <= start_offset <= end_offset <= len(source_bytes)):
            if cursor.spelling and cursor.kind in [clang.cindex.CursorKind.DECL_REF_EXPR, clang.cindex.CursorKind.MEMBER_REF_EXPR]:
                # print(f"    [GET_SRC_DEBUG_OFFSETS] OffsetIssue but using spelling for '{cursor.spelling}' (Kind: {cursor.kind})") # DEBUG
                return cursor.spelling
            return f"[OffsetIssue {start_offset}-{end_offset} in {len(source_bytes)} for {file_path}]"
        
        if start_offset == end_offset and cursor.kind not in [clang.cindex.CursorKind.STRING_LITERAL, clang.cindex.CursorKind.INTEGER_LITERAL, clang.cindex.CursorKind.FLOATING_LITERAL, clang.cindex.CursorKind.CHARACTER_LITERAL]:
            # print(f"    [GET_SRC_DEBUG_OFFSETS] Zero-length extent for non-literal '{cursor.spelling}' L{cursor.location.line} (Kind: {cursor.kind}). Returning spelling.") # DEBUG
             if cursor.spelling:
                 return cursor.spelling 
        
        raw_text = source_bytes[start_offset:end_offset].decode('utf-8', errors='replace').strip()
        
        if not raw_text.strip() and cursor.spelling and cursor.kind not in [clang.cindex.CursorKind.COMPOUND_STMT, clang.cindex.CursorKind.NULL_STMT]:
            # print(f"    [GET_SRC_DEBUG_SPELLING_FALLBACK] Raw text empty for '{cursor.spelling}' (Kind: {cursor.kind}), using spelling.") # DEBUG
            return cursor.spelling

        return ' '.join(raw_text.splitlines())
    except FileNotFoundError:
        return f"[FileNotFoundDuringSourceExtraction: {file_path}]"
    except Exception as e_src: 
        return f"[Error Extracting Source: {type(e_src).__name__} - {e_src} for {file_path}]"

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
    global CODEBASE_PATH_FOR_KEYS
    if hasattr(cursor, 'usr') and cursor.usr: return cursor.usr
    if hasattr(cursor, 'mangled_name') and cursor.mangled_name: return cursor.mangled_name
    fqn = get_full_qualified_name(cursor) or cursor.spelling or f"unnamed_L{cursor.location.line}"
    norm_filepath = "unknown_file"
    if filepath_context:
        try:
            abs_codebase_path = os.path.abspath(CODEBASE_PATH_FOR_KEYS) if CODEBASE_PATH_FOR_KEYS else None
            abs_filepath_context = os.path.abspath(filepath_context)
            if abs_codebase_path and abs_filepath_context.startswith(abs_codebase_path):
                norm_filepath = os.path.relpath(abs_filepath_context, abs_codebase_path)
            else: norm_filepath = os.path.normpath(filepath_context)
        except (ValueError, TypeError): norm_filepath = os.path.normpath(filepath_context)
    return f"{norm_filepath}::{fqn}@L{cursor.location.line}"

def parse_log_arguments_from_string(args_str_combined):
    args_str_combined = args_str_combined.strip()
    format_string = args_str_combined 
    argument_expressions = []
    fmt_str_match = re.match(r'\s*([LuU8]?L?"(?:\\.|[^"\\])*")', args_str_combined) 
    
    if fmt_str_match:
        format_string_candidate = fmt_str_match.group(1).strip()
        if (format_string_candidate.startswith('"') and format_string_candidate.endswith('"')) or \
           (format_string_candidate.startswith('L"') and format_string_candidate.endswith('"')) or \
           (format_string_candidate.startswith('u"') and format_string_candidate.endswith('"')) or \
           (format_string_candidate.startswith('U"') and format_string_candidate.endswith('"')) or \
           (format_string_candidate.startswith('u8"') and format_string_candidate.endswith('"')):
            format_string = format_string_candidate
            remaining_args_str = args_str_combined[fmt_str_match.end():].strip()
            if remaining_args_str.startswith(','): remaining_args_str = remaining_args_str[1:].strip()
            
            if remaining_args_str:
                current_arg = ""; paren_level = 0; angle_bracket_level = 0; square_bracket_level = 0; brace_level = 0;
                in_string_literal = False; in_char_literal = False
                for char_idx, char in enumerate(remaining_args_str):
                    if char == '"' and (char_idx == 0 or remaining_args_str[char_idx-1] != '\\'): in_string_literal = not in_string_literal
                    elif char == "'" and (char_idx == 0 or remaining_args_str[char_idx-1] != '\\'): in_char_literal = not in_char_literal
                    if not in_string_literal and not in_char_literal:
                        if char == '(': paren_level += 1
                        elif char == ')': paren_level = max(0, paren_level - 1)
                        elif char == '<': angle_bracket_level +=1
                        elif char == '>': angle_bracket_level = max(0, angle_bracket_level -1)
                        elif char == '[': square_bracket_level +=1
                        elif char == ']': square_bracket_level = max(0, square_bracket_level -1)
                        elif char == '{': brace_level +=1
                        elif char == '}': brace_level = max(0, brace_level -1)
                        elif char == ',' and paren_level == 0 and angle_bracket_level == 0 and square_bracket_level == 0 and brace_level == 0:
                            if current_arg.strip(): argument_expressions.append(current_arg.strip())
                            current_arg = ""; continue
                    current_arg += char
                if current_arg.strip(): argument_expressions.append(current_arg.strip())
        else: 
            format_string = "[NoValidLeadingStringLiteral]"
            if args_str_combined: 
                argument_expressions.append(args_str_combined)
    else: 
        format_string = "[NoLeadingStringLiteral_Or_ComplexArgs]"
        if args_str_combined:
            argument_expressions.append(args_str_combined) 

    if not argument_expressions and "," in format_string and not (format_string.startswith('"') and format_string.endswith('"')):
        if format_string != "[NoLeadingStringLiteral_Or_ComplexArgs]" and format_string != "[NoValidLeadingStringLiteral]":
             # print(f"    [PARSE_LOG_ARGS_WARN] format_string ('{format_string}') contains comma but no args found. Re-evaluating.") # DEBUG
             pass
    return format_string, argument_expressions


def extract_execution_elements_recursive(parent_cursor_node, filepath, func_display_name_for_debug="<toplevel_or_unknown>"):
    elements = []
    if not parent_cursor_node: return elements

    for cursor in parent_cursor_node.get_children():
        if not cursor.location.file or not hasattr(cursor.location.file, 'name') or cursor.location.file.name != filepath:
            continue
        
        current_node_source_for_debug = get_cursor_source_code(cursor)
        # print(f"[AST_TRAVERSAL] Visiting {cursor.kind} ('{cursor.spelling}') at L{cursor.location.line} in '{func_display_name_for_debug}'. Source: '{current_node_source_for_debug[:100]}...'") # DEBUG
        element_data = None

        if cursor.kind == clang.cindex.CursorKind.MACRO_INSTANTIATION:
            macro_name = cursor.spelling
            # print(f"  [MACRO_DEBUG] Found MACRO_INSTANTIATION: '{macro_name}' at L{cursor.location.line}. Raw text: '{current_node_source_for_debug}'") # DEBUG
            identified_log_level = None
            user_args_for_log_parsing = "" 

            if macro_name in LOG_MACRO_NAMES_ORIGINAL: 
                identified_log_level = macro_name.replace("LOG_", "")
                if identified_log_level == "VERBOSE": 
                    # print(f"    [MACRO_DEBUG] IGNORING LOG_VERBOSE macro '{macro_name}' (direct).") # DEBUG
                    continue 
                
                match = ORIGINAL_LOG_CALL_ARGS_PATTERN.match(current_node_source_for_debug)
                if match: user_args_for_log_parsing = match.group(1).strip()
                else: user_args_for_log_parsing = f"[ErrorParsingArgsFromDirectMacro:{current_node_source_for_debug}]"
                # print(f"    [MACRO_DEBUG] DIRECT LOG MACRO: '{macro_name}', Level: '{identified_log_level}', UserArgs: '{user_args_for_log_parsing}'") # DEBUG

            elif macro_name == "LOG_IMPL":
                # print(f"    [MACRO_DEBUG] LOG_IMPL macro found.") # DEBUG
                match_impl = LOG_IMPL_ARGS_PATTERN.match(current_node_source_for_debug)
                if match_impl:
                    log_impl_all_args_str = match_impl.group(1).strip() 
                    # print(f"      [MACRO_DEBUG] LOG_IMPL raw args string: '{log_impl_all_args_str}'") # DEBUG
                    
                    temp_parsed_level_str_lit, temp_parsed_impl_args_list = parse_log_arguments_from_string(log_impl_all_args_str)
                    level_str_from_impl = temp_parsed_level_str_lit.strip().strip('"')
                    # print(f"      [MACRO_DEBUG] LOG_IMPL parsed level string: '{level_str_from_impl}', other args: {temp_parsed_impl_args_list}") # DEBUG

                    if level_str_from_impl in EXPECTED_LOG_LEVELS: 
                        identified_log_level = level_str_from_impl
                        
                        if len(temp_parsed_impl_args_list) >= 3: 
                            user_args_for_log_parsing = ", ".join(temp_parsed_impl_args_list[2:]) 
                            # print(f"        [MACRO_DEBUG] LOG_IMPL UserArgs for parsing: '{user_args_for_log_parsing}'") # DEBUG
                        else:
                            user_args_for_log_parsing = "[ErrorExtractingUserArgsFromLOG_IMPL_ParsedList]"
                            # print(f"        [MACRO_DEBUG] WARN: Not enough structured args in LOG_IMPL parsed list for user args: {temp_parsed_impl_args_list}") # DEBUG
                    else: 
                        if level_str_from_impl == "VERBOSE": 
                            # print(f"    [MACRO_DEBUG] IGNORING VERBOSE level from LOG_IMPL expansion (parsed level string was 'VERBOSE').") # DEBUG
                            continue 
                        else:
                            pass # print(f"        [MACRO_DEBUG] WARN: LOG_IMPL level '{level_str_from_impl}' not in EXPECTED_LOG_LEVELS (and not VERBOSE).") # DEBUG
                else: 
                    pass # print(f"        [MACRO_DEBUG] WARN: Could not regex parse LOG_IMPL args string: {current_node_source_for_debug}") # DEBUG
            
            if identified_log_level : 
                parsed_format_string, parsed_log_args = parse_log_arguments_from_string(user_args_for_log_parsing)
                
                if parsed_format_string.startswith('"') and parsed_format_string.endswith('"'):
                    parsed_format_string = parsed_format_string[1:-1]
                elif parsed_format_string.startswith('L"') and parsed_format_string.endswith('"'): 
                    parsed_format_string = parsed_format_string[2:-1] 

                element_data = { "type": "LOG", "level": identified_log_level,
                                 "log_format_string": parsed_format_string, "log_arguments": parsed_log_args,
                                 "message_args_str_combined": user_args_for_log_parsing, 
                                 "line": cursor.location.line, "raw_log_statement": current_node_source_for_debug }
                elements.append(element_data)
                print(f"    [LOG_EXTRACTED] SUCCESS (MACRO): Level '{identified_log_level}', Line {cursor.location.line}, Format: '{parsed_format_string}'")
                continue 
        
        if element_data is None and cursor.kind == clang.cindex.CursorKind.DO_STMT:
            # print(f"  [DO_STMT_LOG_HEURISTIC] Examining DO_STMT at L{cursor.location.line}. Raw: '{current_node_source_for_debug[:120]}...'") # DEBUG
            printf_call_node = None
            log_level_from_heuristic = "UNKNOWN_LVL" 

            do_body_children = list(cursor.get_children())
            if do_body_children and do_body_children[0].kind == clang.cindex.CursorKind.COMPOUND_STMT:
                do_body_compound_stmt = do_body_children[0]
                # print(f"    [DO_STMT_LOG_HEURISTIC] Body of DO_STMT is COMPOUND_STMT (L{do_body_compound_stmt.location.line}).") # DEBUG

                def find_level_in_ostream_op_recursive(op_call_expr_node):
                    nonlocal log_level_from_heuristic 
                    if log_level_from_heuristic != "UNKNOWN_LVL": return 

                    callee_name_node = op_call_expr_node.referenced
                    callee_name = get_full_qualified_name(callee_name_node) if callee_name_node else op_call_expr_node.spelling
                    
                    # print(f"        [FIND_LEVEL_RECURSIVE] Examining node: {op_call_expr_node.kind} '{op_call_expr_node.spelling}', Callee: '{callee_name}'") # DEBUG

                    if not (callee_name.startswith("std::") and callee_name.endswith("operator<<")):
                        # print(f"          [FIND_LEVEL_RECURSIVE] Not a std::ostream operator<< (callee_name: '{callee_name}'). Returning.") # DEBUG
                        return 
                    
                    call_args = list(op_call_expr_node.get_arguments()) 
                    if len(call_args) == 2:
                        rhs_arg_cursor = call_args[1]
                        # print(f"          [FIND_LEVEL_RECURSIVE] RHS arg kind: {rhs_arg_cursor.kind}, spelling: '{rhs_arg_cursor.spelling}'") # DEBUG
                        
                        for node_in_rhs in rhs_arg_cursor.walk_preorder(): 
                            if node_in_rhs.kind == clang.cindex.CursorKind.STRING_LITERAL:
                                try:
                                    tokens = list(node_in_rhs.get_tokens())
                                    str_literal_val = tokens[0].spelling.strip('"') if tokens else node_in_rhs.spelling.strip('"')
                                except Exception: str_literal_val = node_in_rhs.spelling.strip('"')
                                
                                # print(f"            [FIND_LEVEL_RECURSIVE] Checking string literal from RHS: '{str_literal_val}'") # DEBUG
                                if str_literal_val in EXPECTED_LOG_LEVELS: 
                                    log_level_from_heuristic = str_literal_val
                                    print(f"              [DO_STMT_LOG_HEURISTIC_LEVEL_FOUND] LEVEL FOUND: '{log_level_from_heuristic}'")
                                    return 

                        if log_level_from_heuristic == "UNKNOWN_LVL":
                            lhs_arg_cursor = call_args[0]
                            if lhs_arg_cursor.kind == clang.cindex.CursorKind.CALL_EXPR:
                                # print(f"          [FIND_LEVEL_RECURSIVE] Level not in RHS. LHS is CALL_EXPR ('{lhs_arg_cursor.spelling}'), recursing...") # DEBUG
                                find_level_in_ostream_op_recursive(lhs_arg_cursor) 
                            # else: # DEBUG
                                # print(f"          [FIND_LEVEL_RECURSIVE] Level not in RHS. LHS is {lhs_arg_cursor.kind} ('{lhs_arg_cursor.spelling}'), not recursing further on LHS.") # DEBUG
                    # else: # DEBUG
                        # print(f"          [FIND_LEVEL_RECURSIVE] operator<< call does not have 2 arguments. Args: {len(call_args)}") # DEBUG
                
                nodes_to_examine_in_do_body = []
                compound_stmt_children = list(do_body_compound_stmt.get_children())
                # print(f"    [DO_STMT_LOG_HEURISTIC] COMPOUND_STMT (L{do_body_compound_stmt.location.line}) has {len(compound_stmt_children)} direct children.") # DEBUG
                
                for child_idx, direct_child_of_compound in enumerate(compound_stmt_children):
                    # print(f"      [DO_STMT_LOG_HEURISTIC_COMPOUND_CHILD_{child_idx}] Kind: {direct_child_of_compound.kind}, Spelling: '{direct_child_of_compound.spelling}'") # DEBUG
                    if direct_child_of_compound.kind == clang.cindex.CursorKind.CALL_EXPR:
                        nodes_to_examine_in_do_body.append(direct_child_of_compound)
                    elif direct_child_of_compound.kind == clang.cindex.CursorKind.UNEXPOSED_EXPR: 
                        # print(f"        [DO_STMT_LOG_HEURISTIC_UNEXPOSED] Found UNEXPOSED_EXPR. Examining its children...") # DEBUG
                        for sub_child in direct_child_of_compound.get_children():
                            # print(f"          [DO_STMT_LOG_HEURISTIC_UNEXPOSED_SUB_CHILD] Kind: {sub_child.kind}, Spelling: '{sub_child.spelling}'") # DEBUG
                            if sub_child.kind == clang.cindex.CursorKind.CALL_EXPR:
                                nodes_to_examine_in_do_body.append(sub_child)
                                # print(f"            [DO_STMT_LOG_HEURISTIC_UNEXPOSED_SUB_CHILD] Added CALL_EXPR from UNEXPOSED_EXPR.") # DEBUG
                
                # print(f"    [DO_STMT_LOG_HEURISTIC] Total nodes to examine for log parts: {len(nodes_to_examine_in_do_body)}") # DEBUG
                # for idx, n_exam in enumerate(nodes_to_examine_in_do_body):  # DEBUG
                #      print(f"      NodeToExamine {idx}: {n_exam.kind} '{n_exam.spelling}' Source: '{get_cursor_source_code(n_exam)[:80]}...'") # DEBUG

                for node_to_examine in nodes_to_examine_in_do_body:
                    if log_level_from_heuristic != "UNKNOWN_LVL": break 
                    if node_to_examine.kind == clang.cindex.CursorKind.CALL_EXPR:
                        find_level_in_ostream_op_recursive(node_to_examine)
                
                if log_level_from_heuristic == "UNKNOWN_LVL":
                     print(f"    [DO_STMT_LOG_HEURISTIC] FINAL: Level not identified from any cout calls in DO_STMT.")
                
                if log_level_from_heuristic != "UNKNOWN_LVL": 
                    print(f"    [DO_STMT_LOG_HEURISTIC] FINAL: Level '{log_level_from_heuristic}' identified. Now searching for printf call...")
                    for node_to_examine_for_printf in nodes_to_examine_in_do_body: 
                        if node_to_examine_for_printf.kind == clang.cindex.CursorKind.CALL_EXPR:
                            callee_name_node_p = node_to_examine_for_printf.referenced
                            callee_name_p = get_full_qualified_name(callee_name_node_p) if callee_name_node_p else node_to_examine_for_printf.spelling
                            if callee_name_p.endswith("printf"): 
                                printf_call_node = node_to_examine_for_printf
                                # print(f"      [DO_STMT_LOG_HEURISTIC_PRINTF_FOUND] Found printf call: '{callee_name_p}' at L{printf_call_node.location.line}") # DEBUG
                                break 
                    
                    if printf_call_node:
                        message_args_str_combined = "" 
                        parsed_format_string = "[ErrorParsingPrintfFormatString]"
                        parsed_log_args = [] 
                        
                        printf_args_cursors = list(printf_call_node.get_arguments())
                        if printf_args_cursors:
                            arg_strings_for_printf = [] 
                            # print(f"        [DO_STMT_LOG_HEURISTIC_PRINTF_ARGS_DETAIL] Found {len(printf_args_cursors)} arguments for printf.") # DEBUG
                            for arg_c_idx, arg_c_original in enumerate(printf_args_cursors):
                                actual_arg_node_for_source = arg_c_original
                                # print(f"          [PRINTF_ARG_RAW_{arg_c_idx}] Kind: {arg_c_original.kind}, Spelling: '{arg_c_original.spelling}', Extent: L{arg_c_original.extent.start.line}C{arg_c_original.extent.start.column}-L{arg_c_original.extent.end.line}C{arg_c_original.extent.end.column}") # DEBUG
                                
                                if arg_c_original.kind == clang.cindex.CursorKind.UNEXPOSED_EXPR:
                                    unexposed_children = list(arg_c_original.get_children())
                                    if unexposed_children:
                                        actual_arg_node_for_source = unexposed_children[0]
                                        # print(f"            [PRINTF_ARG_UNEXPOSED_CHILD] For arg {arg_c_idx}, UNEXPOSED_EXPR child is {actual_arg_node_for_source.kind} '{actual_arg_node_for_source.spelling}'") # DEBUG
                                    # else: # DEBUG
                                        # print(f"            [PRINTF_ARG_UNEXPOSED_CHILD] WARN: Arg {arg_c_idx} is UNEXPOSED_EXPR but has no children.") # DEBUG
                                
                                arg_src = ""
                                if actual_arg_node_for_source.kind in [clang.cindex.CursorKind.STRING_LITERAL, clang.cindex.CursorKind.CHARACTER_LITERAL]:
                                    try:
                                        tokens = list(actual_arg_node_for_source.get_tokens())
                                        if tokens:
                                            arg_src = "".join(t.spelling for t in tokens) 
                                            # print(f"          [PRINTF_ARG_FROM_TOKENS_{arg_c_idx}] Literal from tokens: {arg_src}") # DEBUG
                                        else: 
                                            arg_src_candidate = get_cursor_source_code(actual_arg_node_for_source)
                                            if arg_src_candidate and not arg_src_candidate.startswith("["): arg_src = arg_src_candidate
                                            elif actual_arg_node_for_source.spelling:
                                                arg_src = f'"{actual_arg_node_for_source.spelling}"' if actual_arg_node_for_source.kind == clang.cindex.CursorKind.STRING_LITERAL else actual_arg_node_for_source.spelling
                                    except Exception as e_tok:
                                        # print(f"          [PRINTF_ARG_TOKEN_EXC_{arg_c_idx}] Exc getting tokens: {e_tok}") # DEBUG
                                        arg_src_candidate = get_cursor_source_code(actual_arg_node_for_source)
                                        if arg_src_candidate and not arg_src_candidate.startswith("["): arg_src = arg_src_candidate
                                        elif actual_arg_node_for_source.spelling and actual_arg_node_for_source.kind == clang.cindex.CursorKind.STRING_LITERAL:
                                            arg_src = f'"{actual_arg_node_for_source.spelling}"'
                                        elif actual_arg_node_for_source.spelling: arg_src = actual_arg_node_for_source.spelling
                                else: 
                                    arg_src_candidate = get_cursor_source_code(actual_arg_node_for_source)
                                    if arg_src_candidate and not arg_src_candidate.startswith("["):
                                        arg_src = arg_src_candidate
                                    elif actual_arg_node_for_source.spelling: 
                                        # print(f"          [PRINTF_ARG_SPELLING_FALLBACK_{arg_c_idx}] get_cursor_source_code was '{arg_src_candidate}', using spelling: '{actual_arg_node_for_source.spelling}'") # DEBUG
                                        arg_src = actual_arg_node_for_source.spelling
                                
                                if not arg_src.strip() or arg_src.startswith("["):
                                    if actual_arg_node_for_source.spelling and actual_arg_node_for_source.kind not in [clang.cindex.CursorKind.UNEXPOSED_EXPR, clang.cindex.CursorKind.BINARY_OPERATOR, clang.cindex.CursorKind.PAREN_EXPR]:
                                        arg_src = actual_arg_node_for_source.spelling
                                    else:
                                        arg_src = "[complex_arg]" 

                                # print(f"        [DO_STMT_LOG_HEURISTIC_PRINTF_ARG] Arg {arg_c_idx}: '{arg_src}' (OriginalKind: {arg_c_original.kind}, ProcessedKind: {actual_arg_node_for_source.kind})") # DEBUG
                                arg_strings_for_printf.append(arg_src if arg_src is not None else "[NoneArgSrcEncountered]") 
                            
                            if arg_strings_for_printf:
                                parsed_format_string = arg_strings_for_printf[0].strip('"') 
                                parsed_log_args = arg_strings_for_printf[1:]     
                                message_args_str_combined = ", ".join(arg_strings_for_printf) 
                                # print(f"        [DO_STMT_LOG_HEURISTIC] Printf args extracted: fmt='{parsed_format_string}', args_list={parsed_log_args}") # DEBUG
                            else:
                                raw_printf_call_text = get_cursor_source_code(printf_call_node)
                                print(f"        [DO_STMT_LOG_HEURISTIC] WARN: Could not get structured arguments from printf node. Fallback to regex on: '{raw_printf_call_text}'")
                                printf_match = PRINTF_ARGS_PATTERN.match(raw_printf_call_text)
                                if printf_match:
                                    message_args_str_combined = printf_match.group(1).strip()
                                    temp_fmt, temp_args = parse_log_arguments_from_string(message_args_str_combined) 
                                    parsed_format_string = temp_fmt.strip('"')
                                    parsed_log_args = temp_args
                                else: 
                                    message_args_str_combined = f"[ErrorRegexParsingPrintfArgs: {raw_printf_call_text}]"
                        else: 
                            raw_printf_call_text = get_cursor_source_code(printf_call_node)
                            print(f"        [DO_STMT_LOG_HEURISTIC] WARN: printf_call_node.get_arguments() returned empty list. Fallback to regex on: '{raw_printf_call_text}'")
                            printf_match = PRINTF_ARGS_PATTERN.match(raw_printf_call_text)
                            if printf_match:
                                message_args_str_combined = printf_match.group(1).strip()
                                temp_fmt, temp_args = parse_log_arguments_from_string(message_args_str_combined)
                                parsed_format_string = temp_fmt.strip('"')
                                parsed_log_args = temp_args
                            else: 
                                message_args_str_combined = f"[ErrorRegexParsingPrintfArgsOnEmptyGetArguments: {raw_printf_call_text}]"


                        element_data = { "type": "LOG", "level": log_level_from_heuristic,
                                         "log_format_string": parsed_format_string, "log_arguments": parsed_log_args,
                                         "message_args_str_combined": message_args_str_combined, "line": cursor.location.line,
                                         "raw_log_statement": f"[HeuristicLog:{log_level_from_heuristic}] " + current_node_source_for_debug }
                        elements.append(element_data)
                        print(f"    [LOG_EXTRACTED] SUCCESS (DO_STMT_HEURISTIC): Level '{log_level_from_heuristic}', Line {cursor.location.line}, Format: '{parsed_format_string}'")
                        continue 
                    else:
                        print(f"    [DO_STMT_LOG_HEURISTIC] WARN: Level '{log_level_from_heuristic}' found, but no corresponding printf call in the same DO_STMT body.")
            
            if not element_data: 
                print(f"  [DO_STMT_DEBUG] DO_STMT at L{cursor.location.line} not a log. Treating as user loop if body exists.")
        # --- FIN HEURISTIQUE DO_STMT ---

        # --- GENERIC STRUCTURE HANDLING (IF NOT A LOG) ---
        if element_data is None: 
            if cursor.kind == clang.cindex.CursorKind.CALL_EXPR:
                callee_cursor = cursor.referenced
                callee_name_at_call_site = cursor.spelling
                resolved_callee_display_name = callee_name_at_call_site or "(unknown_callee_expr)"
                if callee_cursor and callee_cursor.kind != clang.cindex.CursorKind.NO_DECL_FOUND:
                    display_name_from_ref = get_full_qualified_name(callee_cursor) or callee_cursor.spelling
                    if display_name_from_ref: resolved_callee_display_name = display_name_from_ref
                is_operator_call = "operator" in resolved_callee_display_name.lower()
                should_keep_call = True
                if any(resolved_callee_display_name.startswith(prefix) for prefix in IGNORE_CALL_DISPLAY_PREFIXES): should_keep_call = False
                if is_operator_call and any(std_ns in resolved_callee_display_name for std_ns in ["std::", "__gnu_cxx::"]): should_keep_call = False
                if should_keep_call:
                    resolved_callee_key = None
                    if callee_cursor and callee_cursor.kind != clang.cindex.CursorKind.NO_DECL_FOUND:
                         callee_file_ctx = callee_cursor.location.file.name if callee_cursor.location.file else filepath
                         resolved_callee_key = get_reliable_signature_key(callee_cursor, callee_file_ctx)
                    element_data = { "type": "CALL", "callee_expression": current_node_source_for_debug,
                                     "callee_name_at_call_site": callee_name_at_call_site or "(anonymous_call)",
                                     "callee_resolved_key": resolved_callee_key, "callee_resolved_display_name": resolved_callee_display_name,
                                     "line": cursor.location.line }
            elif cursor.kind == clang.cindex.CursorKind.IF_STMT:
                condition_expr_text, then_elements, else_elements = "[ConditionNonExtraite]", [], []
                if_stmt_children = list(cursor.get_children())
                if len(if_stmt_children) > 0:
                    condition_node = if_stmt_children[0]
                    condition_expr_text = get_cursor_source_code(condition_node) if condition_node else "[NoConditionNode]"
                if len(if_stmt_children) > 1:
                    then_node = if_stmt_children[1]
                    if then_node and then_node.kind != clang.cindex.CursorKind.INVALID_FILE:
                        then_elements = extract_execution_elements_recursive(then_node, filepath, func_display_name_for_debug + "::if_then")
                if len(if_stmt_children) > 2:
                    else_node = if_stmt_children[2]
                    if else_node and else_node.kind != clang.cindex.CursorKind.INVALID_FILE:
                        else_elements = extract_execution_elements_recursive(else_node, filepath, func_display_name_for_debug + "::if_else")
                element_data = { "type": "IF_STMT", "condition_expression_text": condition_expr_text, 
                                 "line": cursor.location.line, "then_branch_elements": then_elements, "else_branch_elements": else_elements }
            
            elif cursor.kind in [clang.cindex.CursorKind.FOR_STMT, clang.cindex.CursorKind.WHILE_STMT, 
                                 clang.cindex.CursorKind.DO_STMT, 
                                 clang.cindex.CursorKind.SWITCH_STMT,
                                 clang.cindex.CursorKind.CXX_FOR_RANGE_STMT]:
                body_elements_list = []
                body_node_of_construct = None
                children_of_construct = list(cursor.get_children())
                if cursor.kind == clang.cindex.CursorKind.DO_STMT: 
                    if children_of_construct and children_of_construct[0].kind != clang.cindex.CursorKind.INVALID_FILE : 
                        body_node_of_construct = children_of_construct[0] 
                elif cursor.kind == clang.cindex.CursorKind.SWITCH_STMT:
                    for child_node in children_of_construct: 
                        if child_node.kind == clang.cindex.CursorKind.COMPOUND_STMT: 
                            body_node_of_construct = child_node; break
                else: 
                    if children_of_construct:
                        last_child = children_of_construct[-1]
                        if last_child.kind != clang.cindex.CursorKind.INVALID_FILE: 
                             body_node_of_construct = last_child
                if body_node_of_construct:
                     body_elements_list = extract_execution_elements_recursive(body_node_of_construct, filepath, func_display_name_for_debug + "::loop_switch_body")
                # else: print(f"    WARN: Could not find body for {cursor.kind} at L{cursor.location.line}. Source: '{current_node_source_for_debug[:80]}...'") # DEBUG
                loop_type_str = "STRUCTURED_BLOCK"
                if cursor.kind == clang.cindex.CursorKind.FOR_STMT: loop_type_str = "FOR_LOOP"
                elif cursor.kind == clang.cindex.CursorKind.WHILE_STMT: loop_type_str = "WHILE_LOOP"
                elif cursor.kind == clang.cindex.CursorKind.DO_STMT: loop_type_str = "DO_WHILE_LOOP" 
                elif cursor.kind == clang.cindex.CursorKind.SWITCH_STMT: loop_type_str = "SWITCH_BLOCK"
                elif cursor.kind == clang.cindex.CursorKind.CXX_FOR_RANGE_STMT: loop_type_str = "FOR_RANGE_LOOP"
                element_data = {"type": loop_type_str, "line": cursor.location.line, "body_elements": body_elements_list}

            elif cursor.kind == clang.cindex.CursorKind.CASE_STMT:
                case_children = list(cursor.get_children())
                case_expr_text = "[CaseExprNonExtraite]"
                if case_children and case_children[0].kind != clang.cindex.CursorKind.INVALID_FILE : 
                    case_expr_text = get_cursor_source_code(case_children[0])
                element_data = {"type": "CASE_LABEL", "case_expression_text": case_expr_text, "line": cursor.location.line, "body_elements": []} 
            
            elif cursor.kind == clang.cindex.CursorKind.DEFAULT_STMT:
                element_data = { "type": "DEFAULT_LABEL", "line": cursor.location.line, "body_elements": [] }
            
            elif cursor.kind == clang.cindex.CursorKind.COMPOUND_STMT:
                # print(f"    [COMPOUND_STMT_DEBUG] Recursing into direct COMPOUND_STMT at L{cursor.location.line} (likely a nested block)") # DEBUG
                inner_elements = extract_execution_elements_recursive(cursor, filepath, func_display_name_for_debug + "::compound_stmt_L" + str(cursor.location.line))
                if inner_elements: elements.extend(inner_elements) 

        if element_data: elements.append(element_data)
            
    return elements

def parse_cpp_with_clang(filepath, include_paths=None):
    if include_paths is None: include_paths = []
    
    args = []
    for inc_path in include_paths: args.append(f'-I{inc_path}')

    if filepath.lower().endswith(('.cpp', '.hpp', '.cc', '.cxx')): args.extend(['-std=c++17', '-x', 'c++'])
    elif filepath.lower().endswith('.c'): args.extend(['-std=c11', '-x', 'c'])
    elif filepath.lower().endswith('.h'): args.extend(['-std=c++17', '-x', 'c++-header'])
    else: args.extend(['-std=c++17']) 
    
    # print(f"    [CLANG_PARSE_SETUP] Attempting to parse '{filepath}' with args: {args}") # DEBUG
    idx = clang.cindex.Index.create()
    tu_object = None 
    
    try:
        tu_options = (clang.cindex.TranslationUnit.PARSE_DETAILED_PROCESSING_RECORD |
                      clang.cindex.TranslationUnit.PARSE_INCOMPLETE | 
                      clang.cindex.TranslationUnit.PARSE_PRECOMPILED_PREAMBLE | 
                      clang.cindex.TranslationUnit.PARSE_CACHE_COMPLETION_RESULTS)
        tu_object = idx.parse(filepath, args=args, options=tu_options)
        # print(f"    [CLANG_PARSE_RESULT_RAW] idx.parse result type: {type(tu_object)}")  # DEBUG
        if tu_object is None: 
            print(f"  ❌ Clang: idx.parse returned None for {filepath}. Treating as load failure.")
            return {}, []
    except clang.cindex.TranslationUnitLoadError as e:
        print(f"  ❌ Clang TU Load Error for {filepath}: {e}")
        return {}, [] 
    except Exception as e_parse: 
        print(f"  ❌ Clang: Unexpected error during idx.parse for {filepath}: {type(e_parse).__name__} - {e_parse}")
        import traceback
        traceback.print_exc()
        return {}, []
    
    if not isinstance(tu_object, clang.cindex.TranslationUnit):
        print(f"  ❌ Clang: idx.parse did not return a valid TranslationUnit object for {filepath}. Type was: {type(tu_object)}")
        return {},[]

    diagnostics_for_file = []
    has_critical_errors = False
    if tu_object.diagnostics:
        # print(f"    [CLANG_DIAG] Diagnostics for {filepath}:") # DEBUG
        for diag in tu_object.diagnostics:
            diag_file_name = diag.location.file.name if diag.location.file else "NO_FILE_FOR_DIAG"
            if diag_file_name == filepath and diag.severity >= clang.cindex.Diagnostic.Warning:
                diag_severity_str = "UNKNOWN_SEVERITY"
                if diag.severity == clang.cindex.Diagnostic.Warning: diag_severity_str = "WARNING"
                elif diag.severity == clang.cindex.Diagnostic.Error: diag_severity_str = "ERROR"; has_critical_errors = True
                elif diag.severity == clang.cindex.Diagnostic.Fatal: diag_severity_str = "FATAL"; has_critical_errors = True
                # print(f"      [CLANG_DIAG_KEPT] {diag_severity_str} at L{diag.location.line}: {diag.spelling}") # DEBUG
                diagnostics_for_file.append({ "severity": diag_severity_str, "line": diag.location.line,
                                              "column": diag.location.column, "message": diag.spelling })
    # else: # DEBUG
        # print(f"    [CLANG_DIAG] No diagnostics reported by Clang for {filepath}.") # DEBUG

    if has_critical_errors: 
        print(f"  ⚠️ Clang reported critical errors for {filepath}. AST parsing might be incomplete or fail.")

    functions_data = {} 
    # print(f"    [CLANG_AST_ROOT_CHILDREN_RAW] Iterating ALL top-level children of TU for {filepath}:") # DEBUG
    
    has_any_tu_children = False
    if tu_object.cursor and hasattr(tu_object.cursor, 'get_children'): 
        for child_of_tu in tu_object.cursor.get_children(): # DEBUG
            has_any_tu_children = True # DEBUG
            # child_file_name = child_of_tu.location.file.name if child_of_tu.location.file else "CHILD_NO_FILE" # DEBUG
            # print(f"      [CLANG_AST_TU_CHILD_RAW] NodeKind: {child_of_tu.kind}, Spelling: '{child_of_tu.spelling}', LocationFile: '{child_file_name}', Line: {child_of_tu.location.line}") # DEBUG
    # else: # DEBUG
        # print(f"    [CLANG_AST_ROOT_CHILDREN_RAW] !!! tu_object.cursor is None or has no get_children method for {filepath} !!!") # DEBUG


    # if not has_any_tu_children: # DEBUG
        # print(f"    [CLANG_AST_ROOT_CHILDREN_RAW] !!! No children found for tu_object.cursor OR cursor was invalid !!! AST might be empty for {filepath}") # DEBUG

    # print(f"    [CLANG_AST_TARGET_FILE_PROCESSING] Processing nodes for target file {filepath} using walk_preorder:") # DEBUG
    if tu_object.cursor: 
        for cursor in tu_object.cursor.walk_preorder():
            cursor_file_name = cursor.location.file.name if cursor.location.file else "CURSOR_NO_FILE"
            if cursor_file_name == filepath:
                if cursor.kind in [clang.cindex.CursorKind.FUNCTION_DECL, 
                                   clang.cindex.CursorKind.CXX_METHOD,
                                   clang.cindex.CursorKind.CONSTRUCTOR, 
                                   clang.cindex.CursorKind.DESTRUCTOR] and cursor.is_definition():
                    # print(f"            [CLANG_AST_FUNC_DEFINITION] PROCESSING DEFINITION: '{get_full_qualified_name(cursor) or cursor.spelling}' at L{cursor.location.line} (via walk_preorder)") # DEBUG
                    func_key = get_reliable_signature_key(cursor, filepath)
                    display_name = get_full_qualified_name(cursor) or cursor.spelling or f"func_L{cursor.location.line}"
                    body_cursor = None
                    for child in cursor.get_children(): 
                        if child.kind == clang.cindex.CursorKind.COMPOUND_STMT: body_cursor = child; break
                    execution_elements_list = []
                    if body_cursor:
                        execution_elements_list = extract_execution_elements_recursive(body_cursor, filepath, display_name)
                    # else: print(f"    INFO: No CompoundStmt body for func '{display_name}' in {filepath} (walk_preorder).") # DEBUG

                    if func_key not in functions_data:
                        functions_data[func_key] = { "signature_display": display_name, "file": filepath, 
                                                     "line": cursor.location.line, "end_line": cursor.extent.end.line,
                                                     "execution_elements": execution_elements_list }
                    else: 
                        print(f"    WARN: Duplicate function key '{func_key}' (walk_preorder). Merging.")
                        functions_data[func_key]["execution_elements"].extend(execution_elements_list)
                        functions_data[func_key]["execution_elements"].sort(key=lambda x: x.get("line", 0))
    # else: # DEBUG
        # print(f"    [CLANG_AST_TARGET_FILE_PROCESSING] !!! tu_object.cursor was None, cannot walk_preorder for {filepath} !!!") # DEBUG
    
    # if not functions_data: print(f"    [CLANG_PARSE_RESULT_FINAL] No functions extracted from {filepath} after walk_preorder.") # DEBUG
    # else: print(f"    [CLANG_PARSE_RESULT_FINAL] Extracted {len(functions_data)} functions from {filepath} after walk_preorder.") # DEBUG

    return functions_data, diagnostics_for_file

def main():
    global CODEBASE_PATH_FOR_KEYS
    #DEBUG_TARGET_FILE_REL_PATH = "ecu_safety_systems/airbag_control.cpp" 
    DEBUG_TARGET_FILE_REL_PATH = None 

    script_dir = os.path.dirname(os.path.abspath(__file__))
    if os.path.basename(script_dir).lower() == "autosystemsim": codebase_root_path = script_dir
    elif os.path.isdir(os.path.join(script_dir, "AutoSystemSim")): codebase_root_path = os.path.join(script_dir, "AutoSystemSim")
    else: codebase_root_path = script_dir 
    
    CODEBASE_PATH_FOR_KEYS = os.path.abspath(codebase_root_path)
    print(f"ℹ️ Using codebase_path: {CODEBASE_PATH_FOR_KEYS}")

    project_subfolders_for_include = ["common", "ecu_powertrain_control", "ecu_body_control_module",
                                      "ecu_infotainment", "ecu_safety_systems", "ecu_power_management",
                                      "main_application"]
    abs_project_include_paths = [CODEBASE_PATH_FOR_KEYS] 
    for folder in project_subfolders_for_include:
        path = os.path.join(CODEBASE_PATH_FOR_KEYS, folder)
        if os.path.isdir(path): abs_project_include_paths.append(os.path.abspath(path))
    
    cpp_std_includes_heuristic = []
    try:
        process = subprocess.run(['clang++', '-E', '-P', '-x', 'c++', '-', '-v'],
                                 input='#include <vector>\nint main(){return 0;}', 
                                 capture_output=True, text=True, check=False, timeout=15)
        in_search_list = False
        for line in process.stderr.splitlines(): 
            line_strip = line.strip()
            if line_strip.startswith("#include <...> search starts here:"): in_search_list = True; continue
            if line_strip.startswith("End of search list."): in_search_list = False; continue
            if in_search_list:
                path_candidate = line_strip
                if os.path.isdir(path_candidate) and path_candidate not in cpp_std_includes_heuristic:
                    cpp_std_includes_heuristic.append(path_candidate)
            elif ("/" in line_strip and ("include" in line_strip or "inc" in line_strip.lower()) and 
                  os.path.isdir(line_strip) and 
                  any(kw in line_strip.lower() for kw in ["clang", "llvm", "gcc", "g++", "crosstool", "sysroot", "/usr/local", "/opt/homebrew"])):
                path_candidate = line_strip
                if os.path.isdir(path_candidate) and path_candidate not in cpp_std_includes_heuristic:
                     cpp_std_includes_heuristic.append(path_candidate)
        if cpp_std_includes_heuristic: print(f"ℹ️ Auto-detected potential system include paths via 'clang++ -v': {cpp_std_includes_heuristic}")
        else: print("ℹ️ 'clang++ -v' did not yield std include paths, will rely on fallbacks and libclang internal search.")
    except Exception as e_clang_v: print(f"ℹ️ Error auto-detecting clang system include paths via 'clang++ -v': {e_clang_v}. Relying on fallbacks.")

    fallback_std_includes = [ '/usr/include/c++/11', '/usr/include/x86_64-linux-gnu/c++/11', 
                              '/usr/include/c++/10', '/usr/include/x86_64-linux-gnu/c++/10',
                              '/usr/include/c++/9', '/usr/include/x86_64-linux-gnu/c++/9',
                              '/Applications/Xcode.app/Contents/Developer/Toolchains/XcodeDefault.xctoolchain/usr/include/c++/v1',
                              '/Applications/Xcode.app/Contents/Developer/Platforms/MacOSX.platform/Developer/SDKs/MacOSX.sdk/usr/include',
                              '/opt/homebrew/opt/llvm/include/c++/v1', '/usr/local/opt/llvm/include/c++/v1',
                              '/usr/include', '/usr/local/include' ]
    if libclang_found_and_set : 
        libclang_path_str = getattr(clang.cindex.Config, 'library_file', None) or \
                            getattr(clang.cindex.Config, 'library_path', None) or \
                            os.environ.get('LIBCLANG_LIBRARY_PATH', '')
        if libclang_path_str:
            try:
                path_parts = os.path.normpath(libclang_path_str).split(os.sep)
                possible_llvm_root_idx = -1
                for i, part in enumerate(reversed(path_parts)):
                    if "llvm" in part.lower(): possible_llvm_root_idx = len(path_parts) - 1 - i; break
                if possible_llvm_root_idx != -1:
                    llvm_root_guess = os.path.join(*(path_parts[:possible_llvm_root_idx+1]))
                    potential_clang_lib_include = os.path.join(llvm_root_guess, "lib", "clang")
                    if os.path.isdir(potential_clang_lib_include):
                        versions = sorted([d for d in os.listdir(potential_clang_lib_include) 
                                           if os.path.isdir(os.path.join(potential_clang_lib_include, d)) and 
                                           re.match(r'\d+(\.\d+)*', d)], reverse=True)
                        if versions:
                            clang_resource_dir = os.path.join(potential_clang_lib_include, versions[0], 'include')
                            if os.path.isdir(clang_resource_dir) and clang_resource_dir not in fallback_std_includes:
                                fallback_std_includes.append(clang_resource_dir)
                                print(f"ℹ️ Added Clang resource include path: {clang_resource_dir}")
            except Exception as e_res_dir: print(f"ℹ️ Could not derive Clang resource include path: {e_res_dir}")

    for fi_path in fallback_std_includes:
        if os.path.isdir(fi_path) and fi_path not in cpp_std_includes_heuristic: cpp_std_includes_heuristic.append(fi_path)
    for std_inc_path in cpp_std_includes_heuristic:
        abs_std_inc = os.path.abspath(std_inc_path)
        if abs_std_inc not in abs_project_include_paths: abs_project_include_paths.append(abs_std_inc)
    abs_project_include_paths = sorted(list(set(abs_project_include_paths)))
    print(f"ℹ️ Using effective include paths for Clang: {abs_project_include_paths}")

    all_functions_data_clang = {} 
    all_diagnostics_data = {} 
    files_to_process_generator = find_source_files(CODEBASE_PATH_FOR_KEYS, DEBUG_TARGET_FILE_REL_PATH)

    for filepath in files_to_process_generator:
        print(f"Scanning: {filepath}")
        try:
            file_functions_data, file_diagnostics = parse_cpp_with_clang(filepath, include_paths=abs_project_include_paths)
            try: rel_filepath_key = os.path.relpath(filepath, CODEBASE_PATH_FOR_KEYS)
            except ValueError: rel_filepath_key = os.path.normpath(filepath)
            if file_diagnostics: all_diagnostics_data[rel_filepath_key] = file_diagnostics
            for func_key, data in file_functions_data.items():
                data["file"] = rel_filepath_key 
                if func_key not in all_functions_data_clang: all_functions_data_clang[func_key] = data
                else: 
                    print(f"    WARN: Merging data for potentially duplicate function key '{func_key}'. Original file: {all_functions_data_clang[func_key]['file']}, New file: {data['file']}")
                    all_functions_data_clang[func_key]["execution_elements"].extend(data.get("execution_elements", []))
                    all_functions_data_clang[func_key]["execution_elements"].sort(key=lambda x: x.get("line",0))
        except Exception as e_file_proc:
            print(f"❌ Error processing file {filepath} in main loop: {e_file_proc.__class__.__name__} - {e_file_proc}")
            import traceback; traceback.print_exc()

    final_json_output_functions = []
    for reliable_key, data in all_functions_data_clang.items():
        final_json_output_functions.append({ "function_id_key": reliable_key, "display_signature": data.get("signature_display", reliable_key), 
                                             "file": data["file"], "line": data["line"], "end_line": data.get("end_line", -1),
                                             "execution_elements": data.get("execution_elements", []) })
    final_json_output_functions.sort(key=lambda f: (f["file"], f["line"]))
    final_json_output = { "functions": final_json_output_functions, "clang_diagnostics": all_diagnostics_data }
    
    if DEBUG_TARGET_FILE_REL_PATH:
        target_basename = os.path.basename(DEBUG_TARGET_FILE_REL_PATH).split('.')[0]
        output_json_file_name = f"static_analysis_DEBUG_{target_basename}.json"
    else: output_json_file_name = "static_analysis_scheme_clang.json"
    output_json_file_path = os.path.join(CODEBASE_PATH_FOR_KEYS, output_json_file_name)
    try:
        with open(output_json_file_path, "w", encoding="utf-8") as f: json.dump(final_json_output, f, indent=2)
        print(f"✅ Clang-based static analysis scheme saved to {output_json_file_path}")
    except Exception as e: print(f"❌ Error saving JSON to {output_json_file_path}: {e}")

    print("\nℹ️ Phase 1 (Analyse Statique et Génération JSON) terminée.")
    print(f"   Le fichier '{output_json_file_path}' a été généré.")
    if DEBUG_TARGET_FILE_REL_PATH:
        print(f"   NOTE: C'était un cycle de débogage pour '{DEBUG_TARGET_FILE_REL_PATH}'.")
        print(f"   Pour analyser tous les fichiers, mettez DEBUG_TARGET_FILE_REL_PATH = None.")

if __name__ == "__main__":
    main()