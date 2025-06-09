import os
import sys

def count_files_in_directory(directory_path):
    """
    Compte le nombre de fichiers dans un répertoire spécifié.
    Ignore les sous-dossiers.
    """
    # Vérifie si le chemin fourni est bien un dossier existant
    if not os.path.isdir(directory_path):
        print(f"Erreur : Le dossier '{directory_path}' n'a pas été trouvé.")
        return

    try:
        # Liste tous les noms dans le dossier
        all_items = os.listdir(directory_path)
        
        # Construit le chemin complet pour chaque nom et vérifie si c'est un fichier
        file_count = sum(1 for item in all_items if os.path.isfile(os.path.join(directory_path, item)))
        
        print(f"Le dossier '{directory_path}' contient : {file_count} fichier(s).")

    except OSError as e:
        print(f"Erreur lors de la lecture du dossier '{directory_path}': {e}")

def main():
    """
    Fonction principale pour exécuter le script.
    """
    # Vérifie si un chemin de dossier a été fourni en argument de la ligne de commande
    if len(sys.argv) > 1:
        # Utilise le premier argument comme chemin de dossier
        directory_to_count = sys.argv[1]
    else:
        # Si aucun argument n'est fourni, demande à l'utilisateur de saisir un chemin
        directory_to_count = "reports"

    count_files_in_directory(directory_to_count)

if __name__ == "__main__":
    main()