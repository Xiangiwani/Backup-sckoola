
from __future__ import annotations

import json
import hashlib
import os
from datetime import datetime, timedelta, timezone
from pathlib import Path
import re
import subprocess
import sys
import tempfile
import time


def main() -> None:
    binary = Path(sys.argv[1]).resolve()
    if not binary.is_file():
        raise SystemExit(f"Executable not found: {binary}")
    with tempfile.TemporaryDirectory(prefix="cbackup-test-") as tmp:
        root = Path(tmp).resolve()
        home = root / "home"
        home.mkdir()
        env = dict(os.environ, HOME=str(home), USERPROFILE=str(home),
                   CBACKUP_PASSWORD="test-only password with spaces 12345")
        env.pop("CBACKUP_LANG", None)
        checks = 0

        def run(*args: object, fail: bool = False, overrides=None) -> str:
            proc_env = dict(env)
            if overrides:
                proc_env.update(overrides)
            proc = subprocess.run([str(binary), *map(str, args)], env=proc_env,
                                  text=True, encoding="utf-8", errors="replace",
                                  stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                                  timeout=90)
            if (proc.returncode == 0) == fail:
                raise AssertionError(f"Unexpected exit {proc.returncode}: {args}\n{proc.stdout}")
            return proc.stdout

        def ok(label: str) -> None:
            nonlocal checks
            checks += 1
            print(f"OK {checks:02d} {label}", flush=True)

        def write(path: Path, content: str) -> None:
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_text(content, encoding="utf-8")

        def jobdir(name: str) -> Path:
            return home / "backups" / name

        def snapshots(name: str):
            result = []
            for p in sorted(jobdir(name).glob("snap_*.manifest")):
                headers = {}
                files = {}
                for line in p.read_text(encoding="utf-8").splitlines()[1:]:
                    parts = line.split("\t")
                    if parts[0] == "file":
                        files[bytes.fromhex(parts[1]).decode("utf-8")] = parts[2:]
                    elif len(parts) == 2:
                        headers[parts[0]] = parts[1]
                headers["files"] = files
                headers["path"] = p
                result.append(headers)
            return result

        def init(name: str, source: Path) -> None:
            run("init", name, source, "--no-schedule")

        source = root / "source with spaces"
        write(source / "a.txt", "alpha\n")
        write(source / "folder" / "b.txt", "bravo\n")
        write(source / "café-資料.txt", "unicode filename content\n")
        write(source / "package.json", json.dumps({"dependencies": {"discord.js": "13.17.1"}}))
        lock = '{"lockfileVersion":2,"packages":{"node_modules/discord.js":{"version":"13.17.1"}}}'
        write(source / "package-lock.json", lock)
        write(source / "node_modules" / "discord.js" / "index.js", "regenerate me")
        write(source / "nested" / "node_modules" / "lib" / "index.js", "also regenerate")
        write(source / ".venv" / "lib" / "fake.py", "environment")
        write(source / "requirements.txt", "requests==2.32.3\n")
        run("help")
        run("settings")
        run("init", "defaults")  # no sources => no OS startup registration
        defaults_conf = (home / ".config/cbackup/jobs/defaults.conf").read_text()
        assert 'schedule="every 2h"' in defaults_conf, defaults_conf
        assert 'autostart="true"' in defaults_conf, defaults_conf
        assert 'exclude_dependencies="true"' in defaults_conf, defaults_conf
        init("main", source)
        conf = (home / ".config" / "cbackup" / "jobs" / "main.conf").read_text()
        assert "keep_days=90" in conf and "full_every=12" in conf, conf
        assert 'schedule=""' in conf, conf
        ok("safe isolated init, 90-day retention and daily full checkpoint defaults")

        run("run", "main")
        first = snapshots("main")[-1]
        assert first["type"] == "full"
        assert "source_0/package-lock.json" in first["files"]
        assert "source_0/requirements.txt" in first["files"]
        assert not any("node_modules/" in f or ".venv/" in f for f in first["files"])
        ids_output = run("ids", "main")
        assert first["id"] in ids_output
        run("verify", "main", first["id"])
        ok("first full snapshot, queryable ID, dependency exclusions and preserved lockfiles")

        run("run", "main")
        assert len(snapshots("main")) == 1
        ok("unchanged incremental run does not add a snapshot")

        write(source / "a.txt", "alpha modified and longer\n")
        (source / "folder" / "b.txt").unlink()
        write(source / "folder" / "new.txt", "new file\n")
        run("run", "main")
        second = snapshots("main")[-1]
        assert second["type"] == "incremental"
        assert "source_0/folder/b.txt" not in second["files"]
        changes = run("changes", "main", first["id"], second["id"])
        for name in ("a.txt", "b.txt", "new.txt"):
            assert name in changes, changes
        restored = root / "restored latest"
        run("restore", "main", second["id"], "--to", restored)
        assert (restored / "files/source_0/a.txt").read_text() == "alpha modified and longer\n"
        assert not (restored / "files/source_0/folder/b.txt").exists()
        assert (restored / "files/source_0/package-lock.json").read_text() == lock
        assert (restored / "files/source_0/café-資料.txt").read_text() == "unicode filename content\n"
        ok("incremental additions, modifications and deletions reconstruct exactly")

        one = root / "one"
        run("restore", "main", first["id"], "--only", "a.txt", "--to", one)
        assert sorted(p.name for p in (one / "files/source_0").rglob("*") if p.is_file()) == ["a.txt"]
        folder = root / "folder-only"
        run("restore", "main", first["id"], "--only", "folder", "--to", folder)
        assert (folder / "files/source_0/folder/b.txt").read_text() == "bravo\n"
        assert not (folder / "files/source_0/a.txt").exists()
        run("restore", "main", first["id"], "--to", folder, fail=True)
        ok("file/folder selection and refusal to overwrite an existing recovery directory")

        dep_report = root / "dependency-report"
        run("dependencies", "main", first["id"], "--to", dep_report)
        assert (dep_report / "DEPENDENCIES.txt").is_file()
        assert any(p.name == "package-lock.json" for p in dep_report.rglob("*"))
        assert not any(p.name == "a.txt" for p in dep_report.rglob("*"))
        ok("dependency recovery keeps lockfiles and writes guidance without executing installers")

        count = len(snapshots("main"))
        run("run", "main", "--type", "full", "--dry-run")
        assert len(snapshots("main")) == count
        run("config", "main", "set", "watch_seconds", "0", fail=True)
        run("config", "main", "set", "profile", "invalid", fail=True)
        run("config", "main", "set", "encryption", "invalid", fail=True)
        run("config", "main", "set", "password_env", "1FOO", fail=True)
        run("config", "main", "set", "password_env", "FOO-BAR", fail=True)
        run("config", "main", "set", "mode", "invalid", fail=True)
        run("config", "main", "set", "keep_days", "-1", fail=True)
        run("schedule", "main", "--daily", "25:00", fail=True)
        run("restore", "main", "nonexistent-id", "--to", root / "bad", fail=True)
        ok("dry runs and invalid input do not mutate snapshots")

        recursive_source = jobdir("selfstore")
        write(recursive_source / "must-not-back-up-itself.txt", "protected source\n")
        init("selfstore", recursive_source)
        run("run", "selfstore", fail=True)
        assert not snapshots("selfstore")
        assert (recursive_source / "must-not-back-up-itself.txt").read_text() == "protected source\n"
        ok("actual job storage inside the source is rejected before archiving itself")

        # Restoration to original locations has a separate undo ID.
        run("restore", "main", first["id"], "--original", "--yes")
        assert (source / "a.txt").read_text() == "alpha\n"
        assert (source / "folder/b.txt").read_text() == "bravo\n"
        assert (source / "folder/new.txt").exists()  # extra current files stay safe
        undo_listing = run("undo-list", "main")
        undo_ids = re.findall(r"\d{8}_\d{6}_\d{6}_\d+", undo_listing)
        assert undo_ids, undo_listing
        undo_id = undo_ids[-1]
        run("undo", "main", undo_id, "--dry-run")
        assert (source / "a.txt").read_text() == "alpha\n"
        write(source / "a.txt", "edited after restoration\n")
        run("undo", "main", undo_id, "--yes", fail=True)
        assert (source / "a.txt").read_text() == "edited after restoration\n"
        write(source / "a.txt", "alpha\n")
        run("undo", "main", undo_id, "--yes")
        assert (source / "a.txt").read_text() == "alpha modified and longer\n"
        assert not (source / "folder/b.txt").exists()
        run("undo", "main", undo_id, "--yes", fail=True)
        ok("original restore, undo ID, dry-run, conflict protection and removal of newly restored files")

        write(source / "differential.txt", "new delta before explicit differential run\n")
        run("run", "main", "--type", "differential")
        differential = snapshots("main")[-1]
        assert differential["type"] == "differential"
        run("restore", "main", differential["id"], "--to", root / "diff")
        assert (root / "diff/files/source_0/a.txt").read_text() == "alpha modified and longer\n"
        run("run", "main", "--type", "mirror")
        assert (jobdir("main") / "mirror/files/source_0/a.txt").is_file()
        ok("differential reconstruction and mirror mode")

        revert_source = root / "reverted-source"
        write(revert_source / "state.txt", "original state\n")
        init("reverted", revert_source)
        run("run", "reverted", "--type", "full")
        write(revert_source / "state.txt", "changed state with longer content\n")
        run("run", "reverted", "--type", "incremental")
        write(revert_source / "state.txt", "original state\n")
        run("run", "reverted", "--type", "differential")
        reverted = snapshots("reverted")
        assert len(reverted) == 3, "reverting to full content still needs a differential snapshot"
        assert reverted[-1]["type"] == "differential"
        run("restore", "reverted", reverted[-1]["id"], "--to", root / "reverted-out")
        assert (root / "reverted-out/files/source_0/state.txt").read_text() == "original state\n"
        run("run", "reverted", "--type", "differential")
        assert len(snapshots("reverted")) == 3
        ok("differential records reverts to the full baseline and skips unchanged repeats")

        run("config", "main", "set", "exclude_dependencies", "false")
        run("run", "main")
        assert any("node_modules/" in f for f in snapshots("main")[-1]["files"])
        run("config", "main", "add-exclude", "folder", "nested")
        run("run", "main")
        selected = snapshots("main")[-1]["files"]
        assert not any(f.startswith("source_0/folder/") or f.startswith("source_0/nested/") for f in selected)
        ok("dependency exclusion can be disabled; custom directory exclusions prune descendants")

        init("encrypted", source)
        run("config", "encrypted", "set", "encryption", "aes-256-gcm")
        run("run", "encrypted", overrides={"CBACKUP_PASSWORD": ""}, fail=True)
        assert not snapshots("encrypted")
        run("run", "encrypted")
        enc = snapshots("encrypted")[-1]
        archive = jobdir("encrypted") / enc["archive"]
        assert b"alpha modified" not in archive.read_bytes()
        run("restore", "encrypted", enc["id"], "--to", root / "wrong-key",
            overrides={"CBACKUP_PASSWORD": "wrong key at least twelve bytes"}, fail=True)
        assert not (root / "wrong-key").exists()
        run("restore", "encrypted", enc["id"], "--to", root / "decrypted")
        assert (root / "decrypted/files/source_0/a.txt").read_text() == "alpha modified and longer\n"
        run("run", "encrypted", "--type", "mirror", fail=True)
        original_bytes = archive.read_bytes()
        tampered = bytearray(original_bytes)
        tampered[len(tampered) // 2] ^= 1
        archive.write_bytes(tampered)
        run("verify", "encrypted", enc["id"], fail=True)
        run("restore", "encrypted", enc["id"], "--to", root / "tampered", fail=True)
        assert not (root / "tampered").exists()
        # Even a modified plaintext manifest cannot bypass GCM authentication.
        manifest_before = enc["path"].read_text(encoding="utf-8")
        enc["path"].write_text(manifest_before.replace(
            enc["archive_hash"], hashlib.sha256(tampered).hexdigest()), encoding="utf-8")
        run("restore", "encrypted", enc["id"], "--to", root / "bad-tag", fail=True)
        assert not (root / "bad-tag").exists()
        enc["path"].write_text(manifest_before, encoding="utf-8")
        archive.write_bytes(original_bytes)
        run("verify", "encrypted", enc["id"])
        ok("authenticated encryption, missing/wrong secret, tamper detection and mirror refusal")

        profiles = root / "profile-source"
        write(profiles / "config.yaml", "setting: true\n")
        write(profiles / "photo.txt", "user file\n")
        write(profiles / "setup.msi", "pretend installer\n")
        init("profiles", profiles)
        run("config", "profiles", "set", "profile", "settings")
        run("run", "profiles")
        assert "source_0/config.yaml" in snapshots("profiles")[-1]["files"]
        assert "source_0/setup.msi" not in snapshots("profiles")[-1]["files"]
        run("config", "profiles", "set", "profile", "installers")
        run("run", "profiles")
        assert set(snapshots("profiles")[-1]["files"]) == {"source_0/setup.msi"}
        ok("settings-only and installers-only selection profiles")

        # Age fixtures edit IDs consistently, leaving archive bytes intact.
        retention_source = root / "retention-source"
        write(retention_source / "v.txt", "version one\n")
        init("retention", retention_source)
        run("run", "retention", "--type", "full")
        write(retention_source / "v.txt", "version two\n")
        run("run", "retention", "--type", "full")
        old = snapshots("retention")
        renamed = []
        for snap, age in zip(old, (120, 100)):
            dated = (datetime.now(timezone.utc) - timedelta(days=age)).strftime("%Y%m%d_%H%M%S")
            new_id = dated + "_000000_1"
            previous_id = snap["id"]
            for manifest in jobdir("retention").glob("snap_*.manifest"):
                manifest.write_text(manifest.read_text().replace(previous_id, new_id), encoding="utf-8")
            for item in list(jobdir("retention").glob("snap_" + previous_id + "*")):
                item.rename(item.with_name(item.name.replace(previous_id, new_id)))
            renamed.append(new_id)
        write(retention_source / "v.txt", "version three\n")
        run("run", "retention", "--type", "full")
        retained = snapshots("retention")
        assert renamed[0] not in {s["id"] for s in retained}
        assert renamed[1] in {s["id"] for s in retained}
        assert not list(jobdir("retention").glob("snap_" + renamed[0] + "*"))
        run("restore", "retention", renamed[1], "--to", root / "retention-restore")
        assert (root / "retention-restore/files/source_0/v.txt").read_text() == "version two\n"
        ok("90-day pruning removes expired chains and preserves the boundary recovery chain")

        multi_a, multi_b = root / "multi-a", root / "multi-b"
        write(multi_a / "same.txt", "first source\n")
        write(multi_b / "same.txt", "second source\n")
        run("init", "multi", multi_a, multi_b, "--no-schedule")
        run("run", "multi")
        multi_id = snapshots("multi")[-1]["id"]
        run("restore", "multi", multi_id, "--to", root / "multi-out")
        assert (root / "multi-out/files/source_0/same.txt").read_text() == "first source\n"
        assert (root / "multi-out/files/source_1/same.txt").read_text() == "second source\n"
        ok("multiple source folders keep identical filenames in separate namespaces")

        for codec in ("none", "gzip:1", "zip:6"):
            run("run", "multi", "--type", "full", "--compression", codec)
            compressed = snapshots("multi")[-1]
            target = root / ("codec-" + codec.split(":")[0])
            run("verify", "multi", compressed["id"])
            run("restore", "multi", compressed["id"], "--to", target)
            assert (target / "files/source_0/same.txt").read_text() == "first source\n"
        ok("mixed tar/gzip/zip archive formats round-trip with their recorded codecs")

        watched = root / "watched"
        write(watched / "saved.txt", "before save\n")
        init("watched", watched)
        run("config", "watched", "set", "watch_seconds", "5")
        run("config", "watched", "set", "watch", "true")
        watcher = subprocess.Popen([str(binary), "watch", "watched"], env=env,
                                   stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
        try:
            deadline = time.monotonic() + 20
            while not snapshots("watched") and time.monotonic() < deadline:
                if watcher.poll() is not None:
                    raise AssertionError(watcher.stdout.read().decode("utf-8", errors="replace"))
                time.sleep(0.1)
            assert snapshots("watched"), "watcher did not create its first backup"
            run("watch", "watched", fail=True)  # exclusive watch worker lock
            write(watched / "saved.txt", "after save with changed length\n")
            while len(snapshots("watched")) < 2 and time.monotonic() < deadline:
                if watcher.poll() is not None:
                    raise AssertionError(watcher.stdout.read().decode("utf-8", errors="replace"))
                time.sleep(0.1)
            assert len(snapshots("watched")) >= 2, "watcher did not capture the saved change"
        finally:
            watcher.terminate()
            try:
                watcher.communicate(timeout=5)
            except subprocess.TimeoutExpired:
                watcher.kill()
                watcher.communicate(timeout=5)
        run("restore", "watched", snapshots("watched")[-1]["id"], "--to", root / "watch-restored")
        assert (root / "watch-restored/files/source_0/saved.txt").read_text() == "after save with changed length\n"
        ok("manual polling watcher captures saves and enforces a single watcher per job")

        run("log", "main")
        run("log", "main", "--tail", "3")
        for code in ("en", "nl", "es", "de", "uk", "ru", "pl"):
            run("--lang", code, "help")
        run("--lang", "nonexistent", "help", fail=True)
        ok("log reading, bounded tail and requested interface languages")

        print(f"PASS: {checks} portable integration groups", flush=True)


if __name__ == "__main__":
    main()
