import json
import os

INPUT_JSON_FILE = "static_analysis_scheme_clang.json"
OUTPUT_DIR = "logsText" 
OUTPUT_FILENAME = "all_display_signatures_for_list.txt"

def extract_and_save_signatures_for_list():
    if not os.path.exists(INPUT_JSON_FILE):
        print(f"❌ ERREUR : Le fichier d'entrée '{INPUT_JSON_FILE}' n'a pas été trouvé.")
        return

    all_display_signatures = set() 
    try:
        with open(INPUT_JSON_FILE, "r", encoding="utf-8") as f:
            data = json.load(f)
    except Exception as e:
        print(f"❌ ERREUR lors de la lecture de '{INPUT_JSON_FILE}': {e}")
        return

    if "functions" not in data or not isinstance(data["functions"], list):
        print(f"❌ ERREUR : '{INPUT_JSON_FILE}' ne contient pas de clé 'functions' attendue.")
        return
        
    functions_list = data["functions"]
    for func_data in functions_list:
        if isinstance(func_data, dict) and "display_signature" in func_data:
            all_display_signatures.add(func_data["display_signature"])
            
    if not all_display_signatures:
        print("ℹ️ INFO : Aucune 'display_signature' trouvée.")
        return

    sorted_signatures = sorted(list(all_display_signatures))
    os.makedirs(OUTPUT_DIR, exist_ok=True)
    output_filepath = os.path.join(OUTPUT_DIR, OUTPUT_FILENAME)

    try:
        with open(output_filepath, "w", encoding="utf-8") as f_out:
            f_out.write("[\n")
            for i, signature in enumerate(sorted_signatures):
                python_string_signature = json.dumps(signature) 
                f_out.write(f"    {python_string_signature}")
                if i < len(sorted_signatures) - 1:
                    f_out.write(",\n")
                else:
                    f_out.write("\n")
            f_out.write("]\n")
        print(f"✅ Succès ! {len(sorted_signatures)} 'display_signatures' sauvegardées dans '{output_filepath}'.")
    except Exception as e:
        print(f"❌ ERREUR lors de l'écriture dans '{output_filepath}': {e}")

if __name__ == "__main__":
    extract_and_save_signatures_for_list()