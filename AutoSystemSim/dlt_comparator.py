import json
import os
import glob
import re
import datetime
from collections import deque

# --- CONFIGURATION ---
WAD_DIR = "simulated_dlts_json_wad"
BUG_DIR = "simulated_dlts_json_bugs"
REPORTS_DIR = "reports"
os.makedirs(REPORTS_DIR, exist_ok=True)

# REMPLACEZ la classe DLTComparator entière dans dlt_comparator.py par celle-ci

# REMPLACEZ la classe DLTComparator entière dans dlt_comparator.py par celle-ci

class DLTComparator:
    """Compare une trace d'exécution à une trace de référence (WAD)."""

    def __init__(self, wad_records, bug_records):
        self.wad_deque = deque(wad_records)
        self.bug_deque = deque(bug_records)
        self.deviations = []
        self._preprocess_logs()

    def _preprocess_logs(self):
        """Supprime les logs de méta-scénario pour ne comparer que les logs applicatifs."""
        self.wad_deque = deque([r for r in self.wad_deque if r.get("ApID") != "SCENARIO"])
        self.bug_deque = deque([r for r in self.bug_deque if r.get("ApID") != "SCENARIO"])

    # LA FONCTION EST MAINTENANT CORRECTEMENT INDENTÉE DANS LA CLASSE
    def _add_deviation(self, dev_type, expected, found):
        """Formate et ajoute une déviation à la liste avec la nouvelle structure."""
        deviation = {"type": dev_type}
        
        if found:
            deviation["log_details"] = {
                "index": found.get("Index"),
                "LogLevel": found.get("LogLevel"),
                "ApID": found.get("ApID"),
                "CtID": found.get("CtID"),
                "Payload": found.get("Payload")
            }
        
        if expected:
            deviation["expected_behavior"] = {
                "LogLevel": expected.get("LogLevel"),
                "ApID": expected.get("ApID"),
                "CtID": expected.get("CtID"),
                "Payload": expected.get("Payload")
            }
            
        if dev_type == "MISSING_LOG":
            deviation["description"] = f"Un log attendu est manquant. Attendu : [{expected.get('LogLevel')}] {expected.get('Payload')}"
        elif dev_type == "EXTRA_LOG":
            deviation["description"] = f"Log inattendu trouvé à l'index {found.get('Index')}."
        elif dev_type == "LOG_MISMATCH":
            diffs = []
            if expected.get("LogLevel") != found.get("LogLevel"):
                diffs.append(f"LogLevel (attendu: '{expected.get('LogLevel')}', trouvé: '{found.get('LogLevel')}')")
            if expected.get("ApID") != found.get("ApID"):
                diffs.append(f"ApID (attendu: '{expected.get('ApID')}', trouvé: '{found.get('ApID')}')")
            if expected.get("CtID") != found.get("CtID"):
                diffs.append(f"CtID (attendu: '{expected.get('CtID')}', trouvé: '{found.get('CtID')}')")
            if expected.get("Payload") != found.get("Payload"):
                diffs.append("Payload")
            
            if diffs:
                deviation["description"] = f"Incohérence détectée sur le log à l'index {found.get('Index')}. Champ(s) affecté(s): {', '.join(diffs)}."
            else:
                deviation["description"] = f"Incohérence de contenu non spécifiée à l'index {found.get('Index')}."
                
        self.deviations.append(deviation)

    def _logs_are_similar(self, log1, log2):
        """Vérifie si deux logs sont considérés comme identiques pour la comparaison."""
        if log1 is None or log2 is None:
            return False
        
        return (log1.get("LogLevel") == log2.get("LogLevel") and
                log1.get("ApID") == log2.get("ApID") and
                log1.get("CtID") == log2.get("CtID") and
                log1.get("Payload") == log2.get("Payload"))

    def compare(self):
        """Lance la séquence de comparaison et retourne les déviations trouvées."""
        
        while self.wad_deque and self.bug_deque:
            wad_log = self.wad_deque[0]
            bug_log = self.bug_deque[0]

            if bug_log.get("ApID") == "INJECT":
                extra = self.bug_deque.popleft()
                # Appel à la méthode maintenant correctement définie dans la classe
                self._add_deviation("EXTRA_LOG", expected=None, found=extra)
                continue

            if self._logs_are_similar(wad_log, bug_log):
                self.wad_deque.popleft()
                self.bug_deque.popleft()
            else:
                if len(self.wad_deque) > 1 and self._logs_are_similar(self.wad_deque[1], bug_log):
                    missing = self.wad_deque.popleft()
                    self._add_deviation("MISSING_LOG", expected=missing, found=None)
                elif len(self.bug_deque) > 1 and self._logs_are_similar(wad_log, self.bug_deque[1]):
                    extra = self.bug_deque.popleft()
                    self._add_deviation("EXTRA_LOG", expected=None, found=extra)
                else:
                    wad_mismatch = self.wad_deque.popleft()
                    bug_mismatch = self.bug_deque.popleft()
                    self._add_deviation("LOG_MISMATCH", expected=wad_mismatch, found=bug_mismatch)

        while self.wad_deque:
            self._add_deviation("MISSING_LOG", expected=self.wad_deque.popleft(), found=None)
            
        while self.bug_deque:
            self._add_deviation("EXTRA_LOG", expected=None, found=self.bug_deque.popleft())

        return self.deviations
    """Compare un fichier DLT 'bug' à un fichier DLT 'WAD' (référence)."""

    def __init__(self, wad_records, bug_records):
        # Utiliser des deques pour des suppressions efficaces en début de liste (O(1))
        self.wad_deque = deque(wad_records)
        self.bug_deque = deque(bug_records)
        self.deviations = []
        
        # Ignorer les logs de début/fin de scénario injectés par le générateur
        self._preprocess_logs()

    def _preprocess_logs(self):
        """Supprime les logs de méta-scénario pour ne comparer que les logs applicatifs."""
        self.wad_deque = deque([r for r in self.wad_deque if r.get("ApID") != "SCENARIO"])
        self.bug_deque = deque([r for r in self.bug_deque if r.get("ApID") != "SCENARIO"])

    def compare(self):
        """Lance la séquence de comparaison et retourne les déviations trouvées."""
        
        while self.wad_deque and self.bug_deque:
            wad_log = self.wad_deque[0]
            bug_log = self.bug_deque[0]

            # CORRECTION: Avant, le log "EXTRA" n'avait pas tous les champs, maintenant si.
            # Il faut s'assurer que notre log injecté est bien géré.
            if bug_log.get("ApID") == "INJECT":
                extra = self.bug_deque.popleft()
                self._add_deviation("EXTRA_LOG", expected=None, found=extra)
                continue

            if self._logs_are_similar(wad_log, bug_log):
                self.wad_deque.popleft()
                self.bug_deque.popleft()
            else:
                if len(self.wad_deque) > 1 and self._logs_are_similar(self.wad_deque[1], bug_log):
                    missing = self.wad_deque.popleft()
                    self._add_deviation("MISSING_LOG", expected=missing, found=None)
                elif len(self.bug_deque) > 1 and self._logs_are_similar(wad_log, self.bug_deque[1]):
                    extra = self.bug_deque.popleft()
                    self._add_deviation("EXTRA_LOG", expected=None, found=extra)
                else:
                    wad_mismatch = self.wad_deque.popleft()
                    bug_mismatch = self.bug_deque.popleft()
                    self._add_deviation("LOG_MISMATCH", expected=wad_mismatch, found=bug_mismatch)

        while self.wad_deque:
            self._add_deviation("MISSING_LOG", expected=self.wad_deque.popleft(), found=None)
            
        while self.bug_deque:
            self._add_deviation("EXTRA_LOG", expected=None, found=self.bug_deque.popleft())

        return self.deviations

    def _logs_are_similar(self, log1, log2):
        """Vérifie si deux logs sont considérés comme identiques pour la comparaison."""
        if log1 is None or log2 is None:
            return False
        
        return (log1.get("LogLevel") == log2.get("LogLevel") and
                log1.get("ApID") == log2.get("ApID") and
                log1.get("CtID") == log2.get("CtID") and
                log1.get("Payload") == log2.get("Payload"))

    # LA FONCTION MANQUANTE EST MAINTENANT INCLUSE
   # REMPLACEZ cette fonction dans dlt_comparator.py

def _add_deviation(self, dev_type, expected, found):
    """Formate et ajoute une déviation à la liste avec la nouvelle structure."""
    deviation = {"type": dev_type}
    
    # Le log "trouvé" (celui du fichier analysé)
    if found:
        deviation["log_details"] = {
            "index": found.get("Index"),
            "LogLevel": found.get("LogLevel"),
            "ApID": found.get("ApID"),
            "CtID": found.get("CtID"),
            "Payload": found.get("Payload")
        }
    
    # Le comportement "attendu" (celui du WAD de référence)
    if expected:
        deviation["expected_behavior"] = {
            # On ne met pas l'index du WAD pour ne pas y faire référence
            "LogLevel": expected.get("LogLevel"),
            "ApID": expected.get("ApID"),
            "CtID": expected.get("CtID"),
            "Payload": expected.get("Payload")
        }
        
    # Descriptions améliorées
    if dev_type == "MISSING_LOG":
        deviation["description"] = f"Un log attendu est manquant. Attendu : [{expected.get('LogLevel')}] {expected.get('Payload')}"
    elif dev_type == "EXTRA_LOG":
        deviation["description"] = f"Log inattendu trouvé à l'index {found.get('Index')}."
    elif dev_type == "LOG_MISMATCH":
        diffs = []
        if expected.get("LogLevel") != found.get("LogLevel"):
            diffs.append(f"LogLevel (attendu: '{expected.get('LogLevel')}', trouvé: '{found.get('LogLevel')}')")
        if expected.get("ApID") != found.get("ApID"):
            diffs.append(f"ApID (attendu: '{expected.get('ApID')}', trouvé: '{found.get('ApID')}')")
        if expected.get("CtID") != found.get("CtID"):
            diffs.append(f"CtID (attendu: '{expected.get('CtID')}', trouvé: '{found.get('CtID')}')")
        if expected.get("Payload") != found.get("Payload"):
            diffs.append("Payload")
        
        if diffs:
            deviation["description"] = f"Incohérence détectée sur le log à l'index {found.get('Index')}. Champ(s) affecté(s): {', '.join(diffs)}."
        else:
            deviation["description"] = f"Incohérence de contenu non spécifiée à l'index {found.get('Index')}."
            
    self.deviations.append(deviation)
# REMPLACEZ cette fonction dans dlt_comparator.py

# REMPLACEZ cette fonction dans dlt_comparator.py

def generate_report(wad_filepath, trace_to_analyze_filepath):
    """
    Génère un rapport d'analyse pour une trace donnée, en utilisant un WAD comme référence interne.
    """
    try:
        with open(wad_filepath, 'r', encoding='utf-8') as f:
            reference_records = json.load(f)
        with open(trace_to_analyze_filepath, 'r', encoding='utf-8') as f:
            runtime_records = json.load(f)
    except Exception as e:
        print(f"  Erreur de lecture de fichier : {e}")
        return None

    comparator = DLTComparator(reference_records, runtime_records)
    deviations = comparator.compare()

    # Construire le rapport final avec la sémantique "autonome"
    report = {
        "metadata": {
            "report_generated_at": datetime.datetime.now(datetime.timezone.utc).isoformat(),
            "analyzed_trace_file": os.path.basename(trace_to_analyze_filepath) # Ne mentionne que le fichier analysé
        },
        "comparison_summary": {
            "status": "DEVIATION_DETECTED" if deviations else "CLEAN",
            "total_deviations": len(deviations),
            "deviation_types": {dev_type: len([d for d in deviations if d['type'] == dev_type]) for dev_type in set(d['type'] for d in deviations)}
        },
        "deviations": deviations
    }
    return report

def find_corresponding_wad(bug_filename):
    """Reconstitue le chemin du fichier WAD à partir du nom du fichier bug."""
    match = re.search(r'^(.*)_BUG_.*\.json$', bug_filename)
    if match:
        wad_basename = match.group(1) + ".json"
        return os.path.join(WAD_DIR, wad_basename)
    return None
# REMPLACEZ cette fonction dans dlt_comparator.py

# REMPLACEZ cette fonction dans dlt_comparator.py

def main():
    print("--- Démarrage du générateur de rapports d'analyse ---")

    # Créer une liste de tous les fichiers à analyser (WADs et BUGs)
    files_to_analyze = glob.glob(os.path.join(BUG_DIR, "*.json")) + glob.glob(os.path.join(WAD_DIR, "*.json"))

    print(f"\nTraitement de {len(files_to_analyze)} traces...")

    for trace_filepath in files_to_analyze:
        trace_filename = os.path.basename(trace_filepath)
        
        # Le WAD correspondant est soit le fichier lui-même (pour un WAD), soit trouvé par le nom (pour un bug)
        wad_filepath = find_corresponding_wad(trace_filename)
        if wad_filepath is None: # Si ce n'est pas un bug, c'est un WAD
            wad_filepath = trace_filepath
        
        if os.path.exists(wad_filepath):
            print(f"  Analyse de : '{trace_filename}'")
            # Appel de la nouvelle fonction de génération de rapport
            report = generate_report(wad_filepath, trace_filepath)
            
            if report:
                report_filename = f"report_for_{os.path.splitext(trace_filename)[0]}.json"
                report_filepath = os.path.join(REPORTS_DIR, report_filename)
                
                with open(report_filepath, 'w', encoding='utf-8') as f:
                    json.dump(report, f, indent=2, ensure_ascii=False)
        else:
            print(f"  ! Attention: Référence WAD non trouvée pour '{trace_filename}'")

    print("\n--- Processus d'analyse terminé. Rapports générés dans le dossier 'reports'. ---")

if __name__ == "__main__":
    main()