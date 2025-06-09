# File: count_generated_files.py

import os

# --- CONFIGURATION ---
# Assurez-vous que ces noms de dossiers correspondent exactement
# à ceux utilisés dans le script de génération.
WAD_DIR = "simulated_dlts_json_wad"
BUG_DIR = "simulated_dlts_json_bugs"
def count_files_in_dir(directory_path):
    """Compte le nombre de fichiers (en ignorant les sous-dossiers) dans un répertoire donné."""
    if not os.path.isdir(directory_path):
        return f"Erreur: Le dossier '{directory_path}' n'existe pas.", 0
    
    try:
        # Liste tous les éléments dans le dossier et ne garde que les fichiers
        file_count = sum(1 for item in os.listdir(directory_path) if os.path.isfile(os.path.join(directory_path, item)))
        return f"Dossier '{directory_path}'", file_count
    except Exception as e:
        return f"Erreur lors de la lecture du dossier '{directory_path}': {e}", 0

def main():
    """Fonction principale pour compter et afficher les résultats."""
    print("--- Comptage des fichiers DLT générés ---")
    
    # Compter les fichiers WAD
    wad_message, wad_count = count_files_in_dir(WAD_DIR)
    print(f"{wad_message}: {wad_count} fichiers")
    
    # Compter les fichiers BUG
    bug_message, bug_count = count_files_in_dir(BUG_DIR)
    print(f"{bug_message}: {bug_count} fichiers")
    
    print("-" * 40)
    
    total_files = wad_count + bug_count
    print(f"Total des fichiers générés: {total_files}")
    
    if wad_count > 0:
        ratio = bug_count / wad_count if wad_count > 0 else float('inf')
        print(f"Ratio de bugs par WAD: {ratio:.2f}")

if __name__ == "__main__":
    main()