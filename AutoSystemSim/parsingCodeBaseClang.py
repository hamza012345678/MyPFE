import os
import re
import json
import clang.cindex
import subprocess

# --- LIBCLANG PATH CONFIGURATION ---
# (Pas de changement ici, je le garde pour la complétude du fichier)
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

# Regex pour LOG_IMPL pourraient être moins nécessaires si on se base sur les arguments de la macro
# mais gardons-les pour l'instant pour la structure générale
LOG_IMPL_ARGS_PATTERN = re.compile(r"LOG_IMPL\s*\((.*)\)", re.DOTALL | re.IGNORECASE)
# Moins utile maintenant, mais peut servir de fallback
ORIGINAL_LOG_CALL_ARGS_PATTERN = re.compile(r"LOG_(?:FATAL|ERROR|WARNING|INFO|DEBUG)\s*\((.*)\)\s*;?", re.DOTALL | re.IGNORECASE)
PRINTF_ARGS_PATTERN = re.compile(r"printf\s*\((.*)\)\s*;?", re.DOTALL | re.IGNORECASE) # Pour fallback

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
    # (Pas de changement ici)
    source_file_extensions = ('.cpp', '.h', '.hpp', '.c', '.cc')
    excluded_dirs = ['build', 'tests', '.git', '.vscode', 'venv', '__pycache__', 'docs', 'examples']

    if target_file_rel_path:
        full_target_path = os.path.normpath(os.path.join(root_dir, target_file_rel_path))
        if os.path.isfile(full_target_path):
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


# Variable globale ou passée en argument pour contrôler la profondeur de débogage récursif
# afin d'éviter une sortie de log trop massive.
RECURSION_DEPTH = 0

def get_cursor_source_code(cursor):
    global RECURSION_DEPTH
    RECURSION_DEPTH += 1
    
    # --- DÉBOGAGE COMMENTÉ ---
    # should_print_debug = (RECURSION_DEPTH <= 4) 
    # if should_print_debug:
    #     print(f"{'  ' * (RECURSION_DEPTH-1)}[GCS_DEBUG In] Depth: {RECURSION_DEPTH}, Kind: {cursor.kind}, Spelling: '{cursor.spelling}', Extent: L{cursor.extent.start.line}:{cursor.extent.start.column}-L{cursor.extent.end.line}:{cursor.extent.end.column}")
    # --- FIN DÉBOGAGE COMMENTÉ ---

    result_src = None 

    if cursor.kind == clang.cindex.CursorKind.UNEXPOSED_EXPR or \
       cursor.kind == clang.cindex.CursorKind.PAREN_EXPR:
        children = list(cursor.get_children())
        if children:
            inner_src = get_cursor_source_code(children[0]) 
            if cursor.kind == clang.cindex.CursorKind.PAREN_EXPR:
                result_src = f"({inner_src})"
            else: 
                result_src = inner_src
    
    if result_src is None:
        if cursor.kind == clang.cindex.CursorKind.STRING_LITERAL:
            tokens = list(cursor.get_tokens())
            if tokens: result_src = tokens[0].spelling 
        elif cursor.kind == clang.cindex.CursorKind.INTEGER_LITERAL or \
             cursor.kind == clang.cindex.CursorKind.FLOATING_LITERAL or \
             cursor.kind == clang.cindex.CursorKind.CHARACTER_LITERAL:
            tokens = list(cursor.get_tokens())
            if tokens: result_src = tokens[0].spelling

    if result_src is None:
        if (cursor and cursor.extent and cursor.extent.start.file and
                hasattr(cursor.extent.start.file, 'name') and cursor.extent.end.file and
                hasattr(cursor.extent.end.file, 'name') and
                cursor.extent.start.file.name == cursor.extent.end.file.name):
            try:
                file_path = cursor.extent.start.file.name
                if os.path.exists(file_path):
                    source_bytes = open(file_path, 'rb').read()
                    start_offset, end_offset = cursor.extent.start.offset, cursor.extent.end.offset

                    if 0 <= start_offset < end_offset <= len(source_bytes):
                        raw_text = source_bytes[start_offset:end_offset].decode('utf-8', errors='replace').strip()
                        if raw_text.strip():
                            result_src = ' '.join(raw_text.splitlines())
                        elif cursor.spelling and cursor.kind not in [clang.cindex.CursorKind.COMPOUND_STMT, clang.cindex.CursorKind.NULL_STMT]:
                            result_src = cursor.spelling
                    elif cursor.spelling and result_src is None: 
                         result_src = cursor.spelling
                elif cursor.spelling: # Fichier non trouvé, fallback sur spelling si disponible
                    if cursor.kind in [clang.cindex.CursorKind.STRING_LITERAL, clang.cindex.CursorKind.CHARACTER_LITERAL,
                                       clang.cindex.CursorKind.INTEGER_LITERAL, clang.cindex.CursorKind.FLOATING_LITERAL,
                                       clang.cindex.CursorKind.DECL_REF_EXPR, clang.cindex.CursorKind.MEMBER_REF_EXPR,
                                       clang.cindex.CursorKind.TYPE_REF, clang.cindex.CursorKind.TEMPLATE_REF,
                                       clang.cindex.CursorKind.NAMESPACE_REF]:
                        result_src = cursor.spelling

            except FileNotFoundError:
                if cursor.spelling and result_src is None: result_src = cursor.spelling
            except Exception:
                if cursor.spelling and result_src is None: result_src = cursor.spelling
    
    if result_src is None: 
        if cursor.kind == clang.cindex.CursorKind.CONDITIONAL_OPERATOR: 
            children = list(cursor.get_children())
            if len(children) == 3:
                cond_src = get_cursor_source_code(children[0]) or "_cond_"
                true_src = get_cursor_source_code(children[1]) or "_true_" 
                false_src = get_cursor_source_code(children[2]) or "_false_" 
                result_src = f"{cond_src} ? {true_src} : {false_src}"
            elif cursor.spelling: result_src = cursor.spelling
            else: result_src = "[ConditionalOpError]"

        elif cursor.kind == clang.cindex.CursorKind.CALL_EXPR:
            func_name_cursor = cursor.referenced
            func_name = cursor.spelling 
            if func_name_cursor: 
                if func_name_cursor.kind in [clang.cindex.CursorKind.FUNCTION_DECL, clang.cindex.CursorKind.CXX_METHOD, clang.cindex.CursorKind.CONSTRUCTOR, clang.cindex.CursorKind.DESTRUCTOR]:
                    func_name = func_name_cursor.spelling
                else: 
                    ref_src = get_cursor_source_code(func_name_cursor) 
                    if ref_src and not ref_src.startswith("["): func_name = ref_src
            if not func_name : func_name = "_func_" # S'assurer que func_name n'est pas vide
            
            args_sources = []
            try:
                for arg_node in cursor.get_arguments():
                    arg_src = get_cursor_source_code(arg_node) or "_arg_"
                    args_sources.append(arg_src)
                result_src = f"{func_name}({', '.join(args_sources)})"
            except Exception: 
                 result_src = f"{func_name}(...)" 

        elif cursor.kind in [clang.cindex.CursorKind.CXX_STATIC_CAST_EXPR,
                             clang.cindex.CursorKind.CXX_DYNAMIC_CAST_EXPR,
                             clang.cindex.CursorKind.CXX_REINTERPRET_CAST_EXPR,
                             clang.cindex.CursorKind.CXX_CONST_CAST_EXPR,
                             clang.cindex.CursorKind.CSTYLE_CAST_EXPR]:
            target_type_name = cursor.type.spelling
            children = list(cursor.get_children())
            expr_to_cast_src = "_expr_"
            if children: 
                node_for_expr = children[0]
                if cursor.kind == clang.cindex.CursorKind.CSTYLE_CAST_EXPR and len(children) > 1:
                    # Pour CStyle, le premier enfant peut être TypeRef, le second l'expression si c'est `(TYPE)EXPR`
                    # Si c'est `TYPE(EXPR)`, alors le premier enfant est l'expression.
                    # On peut vérifier si le premier enfant a un type (type de l'expression) ou est un type lui-même (TypeRef)
                    if children[0].kind == clang.cindex.CursorKind.TYPE_REF:
                         node_for_expr = children[1]
                         target_type_name = get_cursor_source_code(children[0]) # Obtenir le type du C-style cast
                    # else: l'expression est bien children[0] et cursor.type.spelling est bon
                
                expr_to_cast_src = get_cursor_source_code(node_for_expr) or "_expr_"

            if cursor.kind == clang.cindex.CursorKind.CSTYLE_CAST_EXPR:
                result_src = f"({target_type_name}){expr_to_cast_src}"
            else:
                cast_name = cursor.kind.name.replace("CXX_", "").replace("_EXPR", "").lower()
                result_src = f"{cast_name}<{target_type_name}>({expr_to_cast_src})"
        
        elif cursor.kind == clang.cindex.CursorKind.BINARY_OPERATOR:
            children = list(cursor.get_children())
            if len(children) == 2:
                lhs_src = get_cursor_source_code(children[0]) or "_lhs_"
                rhs_src = get_cursor_source_code(children[1]) or "_rhs_"
                op_token = ""
                
                # Heuristique pour l'opérateur
                if children[0].extent.end.file and hasattr(children[0].extent.end.file, 'name') and \
                   children[1].extent.start.file and hasattr(children[1].extent.start.file, 'name') and \
                   children[0].extent.end.file.name == children[1].extent.start.file.name and \
                   os.path.exists(children[0].extent.end.file.name) and \
                   children[0].extent.end.offset < children[1].extent.start.offset:
                    try:
                        file_path = children[0].extent.end.file.name
                        source_bytes = open(file_path, 'rb').read()
                        op_text_candidate = source_bytes[children[0].extent.end.offset:children[1].extent.start.offset].decode('utf-8', errors='replace').strip()
                        if op_text_candidate and op_text_candidate in ["+", "-", "*", "/", "%", "==", "!=", "<", ">", "<=", ">=", "&&", "||", "&", "|", "^", "<<", ">>", "+=", "-=", "*=", "/=", "%=", "&=", "|=", "^=", "<<=", ">>=", "->*", "."]:
                            op_token = op_text_candidate
                    except Exception:
                        pass
                if not op_token:
                     op_token = cursor.spelling if cursor.spelling and cursor.spelling in ["+", "-", "*", "/", "%", "==", "!=", "<", ">", "<=", ">=", "&&", "||", "&", "|", "^", "<<", ">>", "+=", "-=", "*=", "/=", "%=", "&=", "|=", "^=", "<<=", ">>=", "->*", "."] else f" {cursor.spelling or '_op_'} "
                result_src = f"{lhs_src} {op_token} {rhs_src}"
            else: result_src = "[BinaryOpError]"

        elif cursor.kind == clang.cindex.CursorKind.UNARY_OPERATOR:
            children = list(cursor.get_children())
            op_spelling = cursor.spelling or "_un_op_" 
            if children:
                operand_src = get_cursor_source_code(children[0]) or "_operand_"
                is_postfix = (op_spelling in ["++", "--"] and cursor.extent.start.offset > children[0].extent.start.offset)
                if is_postfix:
                     result_src = f"{operand_src}{op_spelling}"
                else: result_src = f"{op_spelling}{operand_src}"
            elif op_spelling.startswith("sizeof") and cursor.type.spelling : 
                 result_src = f"sizeof({cursor.type.spelling})"
            else: result_src = f"[{cursor.kind.name}_Error]"
        
        elif cursor.kind == clang.cindex.CursorKind.MEMBER_REF_EXPR:
            children = list(cursor.get_children())
            base_expr_src = ""
            if children:
                base_expr_src = get_cursor_source_code(children[0]) 
            
            op = "."
            if children and children[0].type.kind == clang.cindex.TypeKind.POINTER:
                op = "->"
            elif children and children[0].type.get_pointee().kind != clang.cindex.TypeKind.INVALID: 
                 base_type_name = children[0].type.spelling.lower()
                 if "ptr" in base_type_name or "*" in base_type_name : op = "->"

            member_name = cursor.spelling
            if base_expr_src and member_name:
                result_src = f"{base_expr_src}{op}{member_name}"
            elif member_name: 
                result_src = member_name
            else:
                result_src = "[MemberRefError]"
        
        elif cursor.spelling: 
            result_src = cursor.spelling

    if result_src is None: 
        result_src = f"[{cursor.kind.name}_L{cursor.location.line}_NoSrc]"

    # --- DÉBOGAGE COMMENTÉ ---
    # if should_print_debug:
    #     print(f"{'  ' * (RECURSION_DEPTH-1)}[GCS_DEBUG Out] Depth: {RECURSION_DEPTH}, Kind: {cursor.kind}, Returning: '{result_src}'")
    # --- FIN DÉBOGAGE COMMENTÉ ---

    RECURSION_DEPTH -= 1
    return result_src


def get_full_qualified_name(cursor):
    # (Pas de changement ici)
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
    # (Pas de changement ici)
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


# MODIFIED: parse_log_arguments_from_string is now less critical for argument splitting
# if we get arguments directly from AST nodes. It can still be a fallback or helper.
# We'll simplify its role here to mostly extract the format string if the first arg is a string literal.
def get_log_format_string_from_first_arg_text(first_arg_text):
    first_arg_text = first_arg_text.strip()
    # Check for standard C/C++ string literal prefixes
    if (first_arg_text.startswith('"') and first_arg_text.endswith('"')) or \
       (first_arg_text.startswith('L"') and first_arg_text.endswith('"')) or \
       (first_arg_text.startswith('u"') and first_arg_text.endswith('"')) or \
       (first_arg_text.startswith('U"') and first_arg_text.endswith('"')) or \
       (first_arg_text.startswith('u8"') and first_arg_text.endswith('"')):
        # Remove quotes and prefixes like L, u, U, u8
        if first_arg_text.startswith('L') or first_arg_text.startswith('u') or first_arg_text.startswith('U'):
            if first_arg_text.startswith('u8'):
                return first_arg_text[3:-1] # u8"..."
            return first_arg_text[2:-1]   # L"...", u"...", U"..."
        return first_arg_text[1:-1]       # "..."
    return None # Not a simple string literal


def extract_macro_arguments_as_source(macro_cursor):
    """
    NEW: Helper to extract arguments of a macro instantiation as source code strings.
    This relies on the macro expansion containing a CALL_EXPR or similar,
    or by directly tokenizing the macro arguments if it's a simple variadic macro.
    """
    arg_sources = []
    # Attempt 1: Look for a CALL_EXPR within the macro expansion
    # This is common for macros that wrap function calls.
    # Note: This might be too simplistic if macros are very complex.
    # A deeper dive into macro expansion children might be needed.
    call_expr_node = None
    for child in macro_cursor.get_children(): # Children of MACRO_INSTANTIATION are its expanded form
        if child.kind == clang.cindex.CursorKind.CALL_EXPR:
            call_expr_node = child
            break
        # Sometimes the call expr is wrapped in UNEXPOSED_EXPR or other intermediate nodes
        elif child.kind in [clang.cindex.CursorKind.UNEXPOSED_EXPR, clang.cindex.CursorKind.PAREN_EXPR]:
            for sub_child in child.get_children():
                if sub_child.kind == clang.cindex.CursorKind.CALL_EXPR:
                    call_expr_node = sub_child
                    break
            if call_expr_node: break

    if call_expr_node:
        for arg_node in call_expr_node.get_arguments():
            arg_sources.append(get_cursor_source_code(arg_node))
        return arg_sources

    # Attempt 2: Tokenize the macro definition if it's not a clear call_expr expansion
    # This is more fragile and relies on the macro arguments appearing directly.
    # It's hard to robustly get macro arguments directly from MACRO_INSTANTIATION tokens
    # without parsing the macro definition itself, which libclang doesn't fully expose easily for this.
    # We will fall back to the regex-based parsing of the full macro source code for now if this fails.
    # print(f"    [MACRO_ARGS_AST_DEBUG] No direct CALL_EXPR found in expansion of '{macro_cursor.spelling}'. Trying token-based or regex fallback.")

    # Fallback for now: use existing regex logic on the full source of the macro call
    # This is what the original code was doing more or less.
    raw_macro_call_text = get_cursor_source_code(macro_cursor)
    if macro_cursor.spelling in LOG_MACRO_NAMES_ORIGINAL:
        match = ORIGINAL_LOG_CALL_ARGS_PATTERN.match(raw_macro_call_text)
        if match:
            combined_args_str = match.group(1).strip()
            # Use the old parser for this combined string
            _ , parsed_args = parse_log_arguments_from_string_fallback(combined_args_str)
            return [parsed_args[0] if parsed_args else ""] + parsed_args[1:] # first is fmt string
    elif macro_cursor.spelling == "LOG_IMPL":
        match = LOG_IMPL_ARGS_PATTERN.match(raw_macro_call_text)
        if match:
            combined_args_str = match.group(1).strip()
            _ , parsed_args = parse_log_arguments_from_string_fallback(combined_args_str)
            return parsed_args # LOG_IMPL args structure: level, file, line, fmt, ...

    return [] # Return empty if no arguments found or strategy fails

def parse_log_arguments_from_string_fallback(args_str_combined):
    # RENAMED & KEPT: This is the original parse_log_arguments_from_string,
    # used as a fallback if AST-based argument extraction fails.
    args_str_combined = args_str_combined.strip()
    format_string = args_str_combined
    argument_expressions = []
    # Regex to find the first string literal (handles L"", u"", U"", u8"" prefixes)
    fmt_str_match = re.match(r'\s*([LuU8]?L?"(?:\\.|[^"\\])*")', args_str_combined)

    if fmt_str_match:
        format_string_candidate = fmt_str_match.group(1).strip()
        # Verify it's a proper string literal
        if (format_string_candidate.startswith('"') and format_string_candidate.endswith('"')) or \
           (format_string_candidate.startswith('L"') and format_string_candidate.endswith('"')) or \
           (format_string_candidate.startswith('u"') and format_string_candidate.endswith('"')) or \
           (format_string_candidate.startswith('U"') and format_string_candidate.endswith('"')) or \
           (format_string_candidate.startswith('u8"') and format_string_candidate.endswith('"')):
            format_string = format_string_candidate # Keep the quotes for now
            remaining_args_str = args_str_combined[fmt_str_match.end():].strip()
            if remaining_args_str.startswith(','):
                remaining_args_str = remaining_args_str[1:].strip()

            if remaining_args_str:
                # Simple split by comma, aware of parentheses, brackets, braces, and string/char literals
                current_arg = ""
                paren_level = 0
                angle_bracket_level = 0
                square_bracket_level = 0
                brace_level = 0
                in_string_literal = False
                in_char_literal = False
                # Ensure we only split top-level commas
                for char_idx, char in enumerate(remaining_args_str):
                    # Handle escape characters for string/char literals
                    is_escaped = (char_idx > 0 and remaining_args_str[char_idx-1] == '\\' and \
                                  (char_idx > 1 and remaining_args_str[char_idx-2] != '\\')) # handle \\"

                    if char == '"' and not is_escaped : in_string_literal = not in_string_literal
                    elif char == "'" and not is_escaped: in_char_literal = not in_char_literal

                    if not in_string_literal and not in_char_literal:
                        if char == '(': paren_level += 1
                        elif char == ')': paren_level = max(0, paren_level - 1)
                        elif char == '<': # Could be template or operator
                            # Heuristic: if not followed by space or another <, might be operator
                            if not (char_idx + 1 < len(remaining_args_str) and \
                                    (remaining_args_str[char_idx+1].isspace() or remaining_args_str[char_idx+1] == '<')):
                                angle_bracket_level +=1
                        elif char == '>':  # Could be template or operator
                             if not (char_idx + 1 < len(remaining_args_str) and \
                                     remaining_args_str[char_idx+1] == '>'): # Avoid >> operator issues
                                angle_bracket_level = max(0, angle_bracket_level -1)
                        elif char == '[': square_bracket_level +=1
                        elif char == ']': square_bracket_level = max(0, square_bracket_level -1)
                        elif char == '{': brace_level +=1
                        elif char == '}': brace_level = max(0, brace_level -1)
                        elif char == ',' and paren_level == 0 and angle_bracket_level == 0 and \
                             square_bracket_level == 0 and brace_level == 0:
                            if current_arg.strip(): argument_expressions.append(current_arg.strip())
                            current_arg = ""
                            continue
                    current_arg += char
                if current_arg.strip(): argument_expressions.append(current_arg.strip())
        else: # First part wasn't a clear string literal.
            format_string = "[NoValidLeadingStringLiteral_Fallback]"
            if args_str_combined: # Treat the whole thing as a single (complex) argument for now
                argument_expressions.append(args_str_combined)
    else: # No leading string literal found by regex.
        format_string = "[NoLeadingStringLiteral_Fallback]"
        if args_str_combined: # Treat the whole thing as a single (complex) argument
            argument_expressions.append(args_str_combined)

    # Remove quotes from the extracted format_string at the very end
    if format_string.startswith('"') and format_string.endswith('"'):
        format_string = format_string[1:-1]
    elif (format_string.startswith('L"') or format_string.startswith('u"') or format_string.startswith('U"')) and format_string.endswith('"'):
        format_string = format_string[2:-1]
    elif format_string.startswith('u8"') and format_string.endswith('"'):
        format_string = format_string[3:-1]

    return format_string, argument_expressions


def extract_execution_elements_recursive(parent_cursor_node, filepath, func_display_name_for_debug="<toplevel_or_unknown>"):
    elements = []
    if not parent_cursor_node: return elements

    for cursor in parent_cursor_node.get_children():
        if not cursor.location.file or not hasattr(cursor.location.file, 'name') or cursor.location.file.name != filepath:
            continue

        current_node_source_for_debug = get_cursor_source_code(cursor)
        element_data = None

        if cursor.kind == clang.cindex.CursorKind.MACRO_INSTANTIATION:
            macro_name = cursor.spelling
            identified_log_level = None
            parsed_format_string = "[FmtStrNotExtracted]"
            parsed_log_args_sources = [] 

            expanded_call_expr = None
            for child_of_macro in cursor.get_children(): 
                if child_of_macro.kind == clang.cindex.CursorKind.CALL_EXPR:
                    expanded_call_expr = child_of_macro
                    break
                elif child_of_macro.kind in [clang.cindex.CursorKind.UNEXPOSED_EXPR, clang.cindex.CursorKind.PAREN_EXPR]:
                    for sub_child in child_of_macro.get_children():
                        if sub_child.kind == clang.cindex.CursorKind.CALL_EXPR:
                            expanded_call_expr = sub_child
                            break
                    if expanded_call_expr: break
            
            if macro_name in LOG_MACRO_NAMES_ORIGINAL:
                identified_log_level = macro_name.replace("LOG_", "")
                if identified_log_level == "VERBOSE": continue

                if expanded_call_expr:
                    args_from_expansion = list(expanded_call_expr.get_arguments())
                    if args_from_expansion:
                        fmt_str_candidate_src = get_cursor_source_code(args_from_expansion[0])
                        fmt_str_candidate_val = get_log_format_string_from_first_arg_text(fmt_str_candidate_src)
                        if fmt_str_candidate_val is not None:
                            parsed_format_string = fmt_str_candidate_val
                            for arg_node in args_from_expansion[1:]:
                                parsed_log_args_sources.append(get_cursor_source_code(arg_node))
                        else: 
                            parsed_format_string = f"[NoFmtStrInExpansion:{fmt_str_candidate_src}]"
                            for arg_node in args_from_expansion: 
                                parsed_log_args_sources.append(get_cursor_source_code(arg_node))
                    else: 
                        raw_macro_text = get_cursor_source_code(cursor)
                        match_orig = ORIGINAL_LOG_CALL_ARGS_PATTERN.match(raw_macro_text)
                        if match_orig:
                            combined_args_str = match_orig.group(1).strip()
                            parsed_format_string, parsed_log_args_sources = parse_log_arguments_from_string_fallback(combined_args_str)
                        else:
                             parsed_format_string = "[ErrorParsingArgsFromDirectMacro_Fallback]"
                else: 
                    raw_macro_text = get_cursor_source_code(cursor)
                    match_orig = ORIGINAL_LOG_CALL_ARGS_PATTERN.match(raw_macro_text)
                    if match_orig:
                        combined_args_str = match_orig.group(1).strip()
                        parsed_format_string, parsed_log_args_sources = parse_log_arguments_from_string_fallback(combined_args_str)
                    else:
                        parsed_format_string = f"[ErrorParsingArgsFromDirectMacro_Fallback:{raw_macro_text[:50]}]"

            elif macro_name == "LOG_IMPL":
                if expanded_call_expr: 
                    all_impl_args_nodes = list(expanded_call_expr.get_arguments())
                    idx_level = 0
                    idx_fmt_str = 3 
                    idx_first_user_arg = 4

                    if len(all_impl_args_nodes) > idx_level:
                        level_arg_node = all_impl_args_nodes[idx_level]
                        level_src = get_cursor_source_code(level_arg_node)
                        level_val = get_log_format_string_from_first_arg_text(level_src) 
                        if not level_val: 
                            level_val = level_src 
                        level_val = level_val.strip('"')

                        if level_val == "VERBOSE": continue
                        if level_val in EXPECTED_LOG_LEVELS:
                            identified_log_level = level_val

                    if identified_log_level and len(all_impl_args_nodes) > idx_fmt_str:
                        fmt_str_node_impl = all_impl_args_nodes[idx_fmt_str] 
                        fmt_str_src_impl = get_cursor_source_code(fmt_str_node_impl) 
                        fmt_val_impl = get_log_format_string_from_first_arg_text(fmt_str_src_impl) 
                        if fmt_val_impl is not None:
                            parsed_format_string = fmt_val_impl
                        else:
                            parsed_format_string = f"[FmtStrNotLiteralInLOG_IMPL:{fmt_str_src_impl}]"

                        if len(all_impl_args_nodes) > idx_first_user_arg:
                            for user_arg_node in all_impl_args_nodes[idx_first_user_arg:]:
                                parsed_log_args_sources.append(get_cursor_source_code(user_arg_node))
                else: 
                    match_impl_re = LOG_IMPL_ARGS_PATTERN.match(current_node_source_for_debug)
                    if match_impl_re:
                        log_impl_all_args_str_re = match_impl_re.group(1).strip()
                        temp_fmt_re, temp_args_list_re = parse_log_arguments_from_string_fallback(log_impl_all_args_str_re)
                        
                        if len(temp_args_list_re) >= 3: 
                            level_str_from_impl_re = temp_fmt_re 
                            level_str_from_impl_re = get_log_format_string_from_first_arg_text(level_str_from_impl_re) or level_str_from_impl_re.strip('"')

                            if level_str_from_impl_re == "VERBOSE": continue
                            if level_str_from_impl_re in EXPECTED_LOG_LEVELS:
                                identified_log_level = level_str_from_impl_re
                                if len(temp_args_list_re) > 2: 
                                     parsed_format_string_candidate_re = temp_args_list_re[2] 
                                     parsed_format_string = get_log_format_string_from_first_arg_text(parsed_format_string_candidate_re) or parsed_format_string_candidate_re.strip('"')
                                     parsed_log_args_sources = temp_args_list_re[3:] 
                                else:
                                     parsed_format_string = "[FmtStrMissingInLOG_IMPL_RegexFallback]"
                    else: 
                        identified_log_level = "UNKNOWN_LVL_LOG_IMPL_PARSE_FAILED"
                        parsed_format_string = "[LOG_IMPL_ArgParseFailed]"

            if identified_log_level:
                if (parsed_format_string.startswith('"') and parsed_format_string.endswith('"')) or \
                   (parsed_format_string.startswith('L"') and parsed_format_string.endswith('"')):
                    parsed_format_string = parsed_format_string[1:-1] if not parsed_format_string.startswith('L') else parsed_format_string[2:-1]

                element_data = {
                    "type": "LOG", "level": identified_log_level,
                    "log_format_string": parsed_format_string,
                    "log_arguments": parsed_log_args_sources, 
                    "message_args_str_combined": ", ".join(parsed_log_args_sources), 
                    "line": cursor.location.line,
                    "raw_log_statement": current_node_source_for_debug
                }
                elements.append(element_data)
                print(f"    [LOG_EXTRACTED] SUCCESS (MACRO VIA AST/Fallback): Level '{identified_log_level}', Line {cursor.location.line}, Format: '{parsed_format_string}', Args: {len(parsed_log_args_sources)}")
                continue 

        if element_data is None and cursor.kind == clang.cindex.CursorKind.DO_STMT:
            printf_call_node = None 
            log_level_from_heuristic = "UNKNOWN_LVL" 

            do_body_children = list(cursor.get_children())
            if do_body_children and do_body_children[0].kind == clang.cindex.CursorKind.COMPOUND_STMT:
                do_body_compound_stmt = do_body_children[0]

                def find_level_in_ostream_op_recursive(op_call_expr_node_local): 
                    nonlocal log_level_from_heuristic
                    if log_level_from_heuristic != "UNKNOWN_LVL": return

                    callee_name_node = op_call_expr_node_local.referenced
                    callee_name = get_full_qualified_name(callee_name_node) if callee_name_node else op_call_expr_node_local.spelling

                    if not (callee_name.startswith("std::") and callee_name.endswith("operator<<")):
                        return

                    call_args = list(op_call_expr_node_local.get_arguments())
                    if len(call_args) == 2:
                        rhs_arg_cursor = call_args[1]
                        for node_in_rhs in rhs_arg_cursor.walk_preorder():
                            if node_in_rhs.kind == clang.cindex.CursorKind.STRING_LITERAL:
                                try:
                                    tokens = list(node_in_rhs.get_tokens())
                                    str_literal_val = tokens[0].spelling.strip('"') if tokens else node_in_rhs.spelling.strip('"')
                                except Exception: str_literal_val = node_in_rhs.spelling.strip('"')

                                if str_literal_val in EXPECTED_LOG_LEVELS:
                                    log_level_from_heuristic = str_literal_val
                                    return
                        if log_level_from_heuristic == "UNKNOWN_LVL":
                            lhs_arg_cursor = call_args[0]
                            if lhs_arg_cursor.kind == clang.cindex.CursorKind.CALL_EXPR:
                                find_level_in_ostream_op_recursive(lhs_arg_cursor)

                nodes_to_examine_in_do_body = []
                compound_stmt_children = list(do_body_compound_stmt.get_children())
                for direct_child_of_compound in compound_stmt_children:
                    if direct_child_of_compound.kind == clang.cindex.CursorKind.CALL_EXPR:
                        nodes_to_examine_in_do_body.append(direct_child_of_compound)
                    elif direct_child_of_compound.kind == clang.cindex.CursorKind.UNEXPOSED_EXPR:
                        for sub_child in direct_child_of_compound.get_children():
                            if sub_child.kind == clang.cindex.CursorKind.CALL_EXPR:
                                nodes_to_examine_in_do_body.append(sub_child)

                for node_to_examine in nodes_to_examine_in_do_body:
                    if log_level_from_heuristic != "UNKNOWN_LVL": break
                    if node_to_examine.kind == clang.cindex.CursorKind.CALL_EXPR:
                        find_level_in_ostream_op_recursive(node_to_examine)

                if log_level_from_heuristic != "UNKNOWN_LVL" and log_level_from_heuristic != "VERBOSE": 
                    
                    # --- DÉBOGAGE COMMENTÉ ---
                    # print(f"    [DEBUG_DO_STMT_BODY_CHILDREN] Line {cursor.location.line}, Level: {log_level_from_heuristic}. Children of DO_STMT's CompoundStmt:")
                    # for child_idx, child_node_debug in enumerate(do_body_compound_stmt.get_children()):
                    #     child_source_for_debug_print = get_cursor_source_code(child_node_debug)
                    #     print(f"      Child {child_idx}: Kind={child_node_debug.kind}, Spelling='{child_node_debug.spelling}', DisplayName='{child_node_debug.displayname}', Source='{child_source_for_debug_print[:100]}'")
                    # print(f"    --- Fin Children of DO_STMT's CompoundStmt ---")
                    # --- FIN DÉBOGAGE COMMENTÉ ---

                    for node_to_examine_for_printf in nodes_to_examine_in_do_body: 
                        if node_to_examine_for_printf.kind == clang.cindex.CursorKind.CALL_EXPR:
                            callee_name_node_p = node_to_examine_for_printf.referenced
                            callee_name_p = get_full_qualified_name(callee_name_node_p) if callee_name_node_p else node_to_examine_for_printf.spelling
                            if callee_name_p.endswith("printf"): 
                                printf_call_node = node_to_examine_for_printf
                                break

                    if printf_call_node:
                        parsed_format_string = "[ErrorParsingPrintfFormatString_AST]"
                        parsed_log_args_sources = [] 

                        printf_args_cursors = list(printf_call_node.get_arguments())
                        if printf_args_cursors:
                            actual_fmt_node = printf_args_cursors[0] 

                            if actual_fmt_node.kind == clang.cindex.CursorKind.UNEXPOSED_EXPR:
                                string_literal_found_in_unexposed = None
                                for child_of_unexposed in actual_fmt_node.get_children():
                                    if child_of_unexposed.kind == clang.cindex.CursorKind.STRING_LITERAL:
                                        string_literal_found_in_unexposed = child_of_unexposed
                                        break
                                    elif child_of_unexposed.kind in [clang.cindex.CursorKind.UNEXPOSED_EXPR, clang.cindex.CursorKind.PAREN_EXPR]:
                                        for sub_child in child_of_unexposed.get_children():
                                            if sub_child.kind == clang.cindex.CursorKind.STRING_LITERAL:
                                                string_literal_found_in_unexposed = sub_child
                                                break
                                        if string_literal_found_in_unexposed: break
                                
                                if string_literal_found_in_unexposed:
                                    actual_fmt_node = string_literal_found_in_unexposed

                            fmt_str_src = get_cursor_source_code(actual_fmt_node)
                            
                            # --- DÉBOGAGE COMMENTÉ ---
                            # print(f"    [DEBUG_DO_STMT_PRINTF_FMT_SRC] Line {cursor.location.line}: actual_fmt_node kind: {actual_fmt_node.kind}, spelling: '{actual_fmt_node.spelling}', fmt_str_src: '{fmt_str_src}'")
                            # --- FIN DÉBOGAGE COMMENTÉ ---

                            fmt_val = get_log_format_string_from_first_arg_text(fmt_str_src) 
                            if fmt_val is not None:
                                parsed_format_string = fmt_val
                            else: 
                                parsed_format_string = f"[FmtStrNotLiteralInPrintf:{fmt_str_src}]"

                            for arg_idx, arg_node_orig in enumerate(printf_args_cursors[1:]):
                                arg_src_text = get_cursor_source_code(arg_node_orig)
                                
                                # --- DÉBOGAGE COMMENTÉ ---
                                # print(f"    [DEBUG_DO_STMT_ARG] Line {cursor.location.line}, Arg {arg_idx+1}:")
                                # print(f"        Original Node Kind: {arg_node_orig.kind}, Spelling: '{arg_node_orig.spelling}'")
                                # print(f"        Source from get_cursor_source_code(arg_node_orig): '{arg_src_text}'")
                                # --- FIN DÉBOGAGE COMMENTÉ ---
                                
                                parsed_log_args_sources.append(arg_src_text if arg_src_text.strip() else "[ArgSrcNotExtracted_OrEmpty]")
                        else: 
                            raw_printf_text = get_cursor_source_code(printf_call_node)
                            printf_match_re = PRINTF_ARGS_PATTERN.match(raw_printf_text)
                            if printf_match_re:
                                combined_args_str_re = printf_match_re.group(1).strip()
                                parsed_format_string, parsed_log_args_sources = parse_log_arguments_from_string_fallback(combined_args_str_re)
                            else:
                                parsed_format_string = f"[ErrorRegexParsingPrintfArgsHeuristic:{raw_printf_text[:50]}]"
                        
                        element_data = {
                            "type": "LOG", 
                            "level": log_level_from_heuristic,
                            "log_format_string": parsed_format_string, 
                            "log_arguments": parsed_log_args_sources,
                            "line": cursor.location.line, 
                            "raw_log_statement": current_node_source_for_debug 
                        }
                        elements.append(element_data)
                        print(f"    [LOG_EXTRACTED] SUCCESS (DO_STMT_HEURISTIC VIA AST/Fallback): Level '{log_level_from_heuristic}', Line {cursor.location.line}, Format: '{parsed_format_string}', Args: {len(parsed_log_args_sources)}")
                        continue 
            
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
                if cursor.kind == clang.cindex.CursorKind.DO_STMT: # Déjà géré si c'est un log, ici c'est pour une boucle générique
                    if children_of_construct and children_of_construct[0].kind != clang.cindex.CursorKind.INVALID_FILE :
                        body_node_of_construct = children_of_construct[0]
                elif cursor.kind == clang.cindex.CursorKind.SWITCH_STMT:
                    # Le corps d'un switch est généralement un COMPOUND_STMT
                    for child_node in children_of_construct: # Le dernier est souvent le corps ou un CompoundStmt
                        if child_node.kind == clang.cindex.CursorKind.COMPOUND_STMT:
                            body_node_of_construct = child_node; break
                    if not body_node_of_construct and children_of_construct : # Fallback si pas de compound, prendre le dernier non invalide
                         last_child_sw = children_of_construct[-1]
                         if last_child_sw.kind != clang.cindex.CursorKind.INVALID_FILE: body_node_of_construct = last_child_sw

                else: # FOR, WHILE, CXX_FOR_RANGE
                    if children_of_construct:
                        last_child = children_of_construct[-1] # Le corps est typiquement le dernier enfant
                        if last_child.kind != clang.cindex.CursorKind.INVALID_FILE:
                             body_node_of_construct = last_child

                if body_node_of_construct:
                     body_elements_list = extract_execution_elements_recursive(body_node_of_construct, filepath, func_display_name_for_debug + "::loop_switch_body")

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
                # Le premier enfant d'un CASE_STMT est l'expression constante.
                # Les suivants sont les statements DANS ce case (avant le prochain case/default/break).
                if case_children and case_children[0].kind != clang.cindex.CursorKind.INVALID_FILE :
                    case_expr_text = get_cursor_source_code(case_children[0])
                
                # Pour les éléments du corps du case, il faut itérer sur les frères (siblings)
                # ou gérer cela au niveau du SWITCH_BLOCK qui contient tous les statements.
                # Pour l'instant, on ne peuple pas body_elements ici directement pour CASE/DEFAULT.
                # La structure de l'AST place les statements comme enfants du COMPOUND_STMT du SWITCH.
                element_data = {"type": "CASE_LABEL", "case_expression_text": case_expr_text, "line": cursor.location.line}


            elif cursor.kind == clang.cindex.CursorKind.DEFAULT_STMT:
                element_data = { "type": "DEFAULT_LABEL", "line": cursor.location.line }

            elif cursor.kind == clang.cindex.CursorKind.COMPOUND_STMT: # Bloc { ... }
                # Si c'est un bloc vide ou si ses éléments sont déjà capturés par une structure parente (IF, LOOP),
                # on ne veut pas le dupliquer. Mais s'il contient des éléments non capturés, on les prend.
                # Cette logique est délicate car extract_execution_elements_recursive est déjà appelé sur les corps des IF/LOOP.
                # On pourrait ajouter un flag pour éviter la double récursion ou simplement ne pas traiter COMPOUND_STMT ici
                # si ce n'est pas le corps direct d'une fonction.
                # Pour l'instant, laissons-le, mais soyons conscients du potentiel de redondance.
                inner_elements = extract_execution_elements_recursive(cursor, filepath, func_display_name_for_debug + "::compound_stmt_L" + str(cursor.location.line))
                if inner_elements: elements.extend(inner_elements) # Étendre la liste parente directement


        if element_data: elements.append(element_data)

    return elements


def parse_cpp_with_clang(filepath, include_paths=None):
    # (Pas de changement majeur ici, mais s'assure que les bonnes options sont passées à Clang)
    if include_paths is None: include_paths = []

    args = []
    for inc_path in include_paths: args.append(f'-I{inc_path}')

    # Add flags to help clang resolve macros and types better
    # -fparse-all-comments can sometimes help with macro locations but not primary here
    # -Xclang -detailed-preprocessing-record might be too verbose / slow
    args.extend(['-ferror-limit=0']) # Try to parse as much as possible despite errors

    if filepath.lower().endswith(('.cpp', '.hpp', '.cc', '.cxx')): args.extend(['-std=c++17', '-x', 'c++'])
    elif filepath.lower().endswith('.c'): args.extend(['-std=c11', '-x', 'c'])
    elif filepath.lower().endswith('.h'): args.extend(['-std=c++17', '-x', 'c++-header']) # -xc++-header might be better for .h
    else: args.extend(['-std=c++17']) # Default for unknown extensions

    # print(f"    [CLANG_PARSE_SETUP] Attempting to parse '{filepath}' with args: {args}")
    idx = clang.cindex.Index.create()
    tu_object = None

    try:
        # PARSE_DETAILED_PROCESSING_RECORD is important for macro expansions
        tu_options = (clang.cindex.TranslationUnit.PARSE_DETAILED_PROCESSING_RECORD |
                      clang.cindex.TranslationUnit.PARSE_INCOMPLETE | # Allow parsing even with errors
                      # clang.cindex.TranslationUnit.PARSE_PRECOMPILED_PREAMBLE | # Can speed up parsing of headers
                      # clang.cindex.TranslationUnit.PARSE_CACHE_COMPLETION_RESULTS |
                      clang.cindex.TranslationUnit.PARSE_SKIP_FUNCTION_BODIES if not True else 0 # Set to True to skip bodies for faster header parsing if needed, but we need bodies
                      )
        tu_object = idx.parse(filepath, args=args, options=tu_options)
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
    # (Diagnostic handling - pas de changement)
    if tu_object.diagnostics:
        for diag in tu_object.diagnostics:
            diag_file_name = diag.location.file.name if diag.location.file else "NO_FILE_FOR_DIAG"
            if diag_file_name == filepath and diag.severity >= clang.cindex.Diagnostic.Warning: # Capture warnings and above
                diag_severity_str = "UNKNOWN_SEVERITY"
                if diag.severity == clang.cindex.Diagnostic.Note: diag_severity_str = "NOTE" # Should be filtered by >= Warning
                elif diag.severity == clang.cindex.Diagnostic.Warning: diag_severity_str = "WARNING"
                elif diag.severity == clang.cindex.Diagnostic.Error: diag_severity_str = "ERROR"; has_critical_errors = True
                elif diag.severity == clang.cindex.Diagnostic.Fatal: diag_severity_str = "FATAL"; has_critical_errors = True
                diagnostics_for_file.append({ "severity": diag_severity_str, "line": diag.location.line,
                                              "column": diag.location.column, "message": diag.spelling })
    if has_critical_errors:
        print(f"  ⚠️ Clang reported critical errors for {filepath}. AST parsing might be incomplete or fail.")


    functions_data = {}
    if tu_object.cursor:
        for cursor in tu_object.cursor.walk_preorder():
            # Ensure we only process definitions from the current file being parsed
            # (not from included headers, unless that's desired and handled explicitly)
            if not cursor.location.file or cursor.location.file.name != filepath:
                continue

            if cursor.kind in [clang.cindex.CursorKind.FUNCTION_DECL,
                               clang.cindex.CursorKind.CXX_METHOD,
                               clang.cindex.CursorKind.CONSTRUCTOR,
                               clang.cindex.CursorKind.DESTRUCTOR] and cursor.is_definition():
                func_key = get_reliable_signature_key(cursor, filepath)
                display_name = get_full_qualified_name(cursor) or cursor.spelling or f"func_L{cursor.location.line}"

                body_cursor = None
                for child in cursor.get_children(): # Find the compound statement representing the body
                    if child.kind == clang.cindex.CursorKind.COMPOUND_STMT:
                        body_cursor = child
                        break
                execution_elements_list = []
                if body_cursor:
                    execution_elements_list = extract_execution_elements_recursive(body_cursor, filepath, display_name)
                # else: print(f"    INFO: No CompoundStmt body for func '{display_name}' in {filepath} (walk_preorder).")

                if func_key not in functions_data:
                    functions_data[func_key] = {
                        "signature_display": display_name,
                        "file": filepath, # Store full path initially
                        "line": cursor.location.line,
                        "end_line": cursor.extent.end.line,
                        "execution_elements": execution_elements_list
                    }
                else: # Should ideally not happen if keys are truly unique
                    print(f"    WARN: Duplicate function key '{func_key}' (walk_preorder). Merging.")
                    functions_data[func_key]["execution_elements"].extend(execution_elements_list)
                    # Simple sort by line, might need more sophisticated merging if order is critical across partial defs
                    functions_data[func_key]["execution_elements"].sort(key=lambda x: x.get("line", 0))
    return functions_data, diagnostics_for_file


def main():
    # (Pas de changement majeur dans main(), sauf peut-être la gestion des include paths si nécessaire)
    global CODEBASE_PATH_FOR_KEYS

   #DEBUG_TARGET_FILE_REL_PATH = "ecu_safety_systems/abs_control.cpp" #debugging a specific file
    DEBUG_TARGET_FILE_REL_PATH = None # Process all files

    script_dir = os.path.dirname(os.path.abspath(__file__))
    if os.path.basename(script_dir).lower() == "autosystemsim": codebase_root_path = script_dir
    elif os.path.isdir(os.path.join(script_dir, "AutoSystemSim")): codebase_root_path = os.path.join(script_dir, "AutoSystemSim")
    else: codebase_root_path = script_dir # Fallback

    CODEBASE_PATH_FOR_KEYS = os.path.abspath(codebase_root_path)
    print(f"ℹ️ Using codebase_path: {CODEBASE_PATH_FOR_KEYS}")

    project_subfolders_for_include = ["common", "ecu_powertrain_control", "ecu_body_control_module",
                                      "ecu_infotainment", "ecu_safety_systems", "ecu_power_management",
                                      "main_application"]
    abs_project_include_paths = [CODEBASE_PATH_FOR_KEYS] # Include the root of the codebase
    for folder in project_subfolders_for_include:
        path = os.path.join(CODEBASE_PATH_FOR_KEYS, folder)
        if os.path.isdir(path): abs_project_include_paths.append(os.path.abspath(path))

    cpp_std_includes_heuristic = []
    try:
        # Try to get system includes from clang++
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
        if cpp_std_includes_heuristic: print(f"ℹ️ Auto-detected potential system include paths via 'clang++ -v': {len(cpp_std_includes_heuristic)} paths.")
        else: print("ℹ️ 'clang++ -v' did not yield std include paths, will rely on fallbacks and libclang internal search.")
    except Exception as e_clang_v: print(f"ℹ️ Error auto-detecting clang system include paths via 'clang++ -v': {e_clang_v}. Relying on fallbacks.")

    # Common fallback standard include paths
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
            try: # Try to derive clang resource include directory
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

    # Combine and unique include paths
    for std_inc_path in cpp_std_includes_heuristic:
        abs_std_inc = os.path.abspath(std_inc_path)
        if abs_std_inc not in abs_project_include_paths: abs_project_include_paths.append(abs_std_inc)
    abs_project_include_paths = sorted(list(set(abs_project_include_paths)))
    print(f"ℹ️ Using effective include paths for Clang ({len(abs_project_include_paths)} paths): {abs_project_include_paths if len(abs_project_include_paths) < 5 else abs_project_include_paths[:4] + ['...'] }")


    all_functions_data_clang = {}
    all_diagnostics_data = {}
    files_to_process_generator = find_source_files(CODEBASE_PATH_FOR_KEYS, DEBUG_TARGET_FILE_REL_PATH)

    for filepath in files_to_process_generator:
        print(f"Scanning: {filepath}")
        try:
            file_functions_data, file_diagnostics = parse_cpp_with_clang(filepath, include_paths=abs_project_include_paths)
            try:
                rel_filepath_key = os.path.relpath(filepath, CODEBASE_PATH_FOR_KEYS)
            except ValueError: # Happens if paths are on different drives (Windows)
                rel_filepath_key = os.path.normpath(filepath)

            if file_diagnostics: all_diagnostics_data[rel_filepath_key] = file_diagnostics

            for func_key, data in file_functions_data.items():
                data["file"] = rel_filepath_key # Use relative path in final JSON
                if func_key not in all_functions_data_clang:
                    all_functions_data_clang[func_key] = data
                else:
                    # This case should be rare if get_reliable_signature_key is robust
                    print(f"    WARN: Merging data for potentially duplicate function key '{func_key}'. Original file: {all_functions_data_clang[func_key]['file']}, New file: {data['file']}")
                    all_functions_data_clang[func_key]["execution_elements"].extend(data.get("execution_elements", []))
                    all_functions_data_clang[func_key]["execution_elements"].sort(key=lambda x: x.get("line",0)) # Re-sort after merge
        except Exception as e_file_proc:
            print(f"❌ Error processing file {filepath} in main loop: {e_file_proc.__class__.__name__} - {e_file_proc}")
            import traceback; traceback.print_exc()


    final_json_output_functions = []
    for reliable_key, data in all_functions_data_clang.items():
        final_json_output_functions.append({
            "function_id_key": reliable_key,
            "display_signature": data.get("signature_display", reliable_key),
            "file": data["file"],
            "line": data["line"],
            "end_line": data.get("end_line", -1),
            "execution_elements": data.get("execution_elements", [])
        })
    final_json_output_functions.sort(key=lambda f: (f["file"], f["line"])) # Sort functions by file then line

    final_json_output = {
        "functions": final_json_output_functions,
        "clang_diagnostics": all_diagnostics_data
    }

    if DEBUG_TARGET_FILE_REL_PATH:
        target_basename = os.path.basename(DEBUG_TARGET_FILE_REL_PATH).split('.')[0]
        output_json_file_name = f"static_analysis_DEBUG_{target_basename}.json"
    else:
        output_json_file_name = "static_analysis_scheme_clang.json"

    output_json_file_path = os.path.join(CODEBASE_PATH_FOR_KEYS, output_json_file_name) # Save in codebase root
    try:
        with open(output_json_file_path, "w", encoding="utf-8") as f:
            json.dump(final_json_output, f, indent=2)
        print(f"✅ Clang-based static analysis scheme saved to {output_json_file_path}")
    except Exception as e:
        print(f"❌ Error saving JSON to {output_json_file_path}: {e}")

    print("\nℹ️ Phase 1 (Analyse Statique et Génération JSON) terminée.")
    print(f"   Le fichier '{output_json_file_path}' a été généré.")
    if DEBUG_TARGET_FILE_REL_PATH:
        print(f"   NOTE: C'était un cycle de débogage pour '{DEBUG_TARGET_FILE_REL_PATH}'.")
        print(f"   Pour analyser tous les fichiers, mettez DEBUG_TARGET_FILE_REL_PATH = None.")

if __name__ == "__main__":
    main()