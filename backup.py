#!/usr/bin/env python3
"""Back-upscript: zipt een bronmap naar een back-upmap.

- Maakt per run één gecomprimeerd zip-bestand (ZIP_DEFLATED) met tijdstempel.
- Elke run maakt een nieuwe zip (optioneel overslaan bij geen wijzigingen).
- Bewaart alleen de nieuwste MAX_BACKUPS (10) zip-bestanden, de oudste wordt verwijderd om opslag te besparen.
- Schrijft alles naar een logbestand en handelt fouten af.
"""

import logging
import os
import sys
import zipfile
from datetime import datetime
from pathlib import Path

# ---------------- Instellingen ----------------
# Pas deze paden aan, of zet ze als omgevingsvariabele.
SOURCE_DIR = Path(os.environ.get("BACKUP_SOURCE", Path.home() / "Documenten"))
BACKUP_DIR = Path(os.environ.get("BACKUP_DEST", Path.home() / "backup"))
MAX_BACKUPS = int(os.environ.get("BACKUP_KEEP", 10))
# Overslaan als er niets veranderd is? 1 = ja, 0 = nee (elke run maakt een zip)
SKIP_UNCHANGED = os.environ.get("BACKUP_SKIP_UNCHANGED", "0") == "1"
# Bijv. USB-stick: BACKUP_DEST=/media/pi/USB/backup
# ----------------------------------------------

LOG_FILE = BACKUP_DIR / "backup.log"


def setup_logging():
    BACKUP_DIR.mkdir(parents=True, exist_ok=True)
    logging.basicConfig(
        level=logging.INFO,
        format="%(asctime)s [%(levelname)s] %(message)s",
        handlers=[logging.FileHandler(LOG_FILE), logging.StreamHandler()],
    )


def newest_mtime(folder: Path) -> float:
    """Tijdstip van het meest recent gewijzigde bestand in de map."""
    latest = 0.0
    for root, _dirs, files in os.walk(folder):
        for name in files:
            try:
                latest = max(latest, os.path.getmtime(os.path.join(root, name)))
            except OSError:
                pass
    return latest


def existing_backups():
    return sorted(BACKUP_DIR.glob("backup_*.zip"))


def make_backup() -> Path:
    stamp = datetime.now().strftime("%Y-%m-%d_%H-%M-%S")
    target = BACKUP_DIR / f"backup_{stamp}.zip"
    tmp = target.with_suffix(".zip.tmp")
    count = 0
    try:
        with zipfile.ZipFile(tmp, "w", zipfile.ZIP_DEFLATED, compresslevel=9) as zf:
            for root, _dirs, files in os.walk(SOURCE_DIR):
                for name in files:
                    path = Path(root) / name
                    try:
                        zf.write(path, path.relative_to(SOURCE_DIR))
                        count += 1
                    except (PermissionError, FileNotFoundError, OSError) as err:
                        logging.warning("Overgeslagen: %s (%s)", path, err)
        tmp.rename(target)  # pas hernoemen als de zip compleet is
    except Exception:
        tmp.unlink(missing_ok=True)  # geen half bestand laten staan
        raise
    size_kb = target.stat().st_size / 1024
    logging.info("Back-up gemaakt: %s (%d bestanden, %.1f KB)", target.name, count, size_kb)
    return target


def cleanup_old():
    backups = existing_backups()
    for old in backups[:-MAX_BACKUPS] if MAX_BACKUPS > 0 else []:
        try:
            old.unlink()
            logging.info("Oude back-up verwijderd: %s", old.name)
        except OSError as err:
            logging.warning("Kon %s niet verwijderen: %s", old.name, err)


def main() -> int:
    try:
        setup_logging()
    except OSError as err:
        print(f"Back-upmap niet beschikbaar ({BACKUP_DIR}): {err}", file=sys.stderr)
        return 1

    if not SOURCE_DIR.is_dir():
        logging.error("Bronmap bestaat niet: %s", SOURCE_DIR)
        return 1

    backups = existing_backups()
    if SKIP_UNCHANGED and backups and newest_mtime(SOURCE_DIR) <= backups[-1].stat().st_mtime:
        logging.info("Geen wijzigingen sinds de laatste back-up, overgeslagen.")
        return 0

    try:
        make_backup()
        cleanup_old()
    except OSError as err:  # bijv. schijf vol, USB eruit
        logging.error("Back-up mislukt: %s", err)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
