#!/usr/bin/env python3
"""Black-box maintenance checks; isolated profile, no scheduler or desktop activation."""
import os
from pathlib import Path
import re
import subprocess
import sys
import tempfile
import time


def main():
    binary = Path(sys.argv[1]).resolve()
    with tempfile.TemporaryDirectory(prefix="cbackup-maintenance-") as folder:
        root = Path(folder)
        profile = root / "profile"
        profile.mkdir()
        env = dict(os.environ, HOME=str(profile), USERPROFILE=str(profile))

        def run(*args, fail=False):
            result = subprocess.run([str(binary), *map(str, args)], env=env, encoding="utf-8",
                                    errors="replace", stdout=subprocess.PIPE, stderr=subprocess.STDOUT, timeout=90)
            assert (result.returncode != 0) == fail, (args, result.returncode, result.stdout)
            return result.stdout

        def setting(job, key, value):
            return run("config", job, "set", key, value)

        def statepath(job):
            return profile / ".config/cbackup/state" / job / "health.conf"

        def state(job):
            return dict(re.findall(r'^(\w+)="(.*)"$', statepath(job).read_text(), re.M))

        def age(job, key, seconds):
            path = statepath(job)
            text = re.sub(r'^' + key + r'=".*"$', key + '="' + str(int(time.time()) - seconds) + '"',
                          path.read_text(), flags=re.M)
            path.write_bytes(text.encode())

        def store(job):
            return profile / "backups" / job

        def ids(job):
            return run("ids", job).splitlines()

        source = root / "source"
        source.mkdir()
        (source / "a.txt").write_text("alpha")
        (source / "b.txt").write_text("bravo")
        run("init", "health", source, "--no-schedule")
        assert 'stale' in run("health", "health", fail=True)
        first_alerts = run("alerts", "health")
        run("health", "health", fail=True)
        assert run("alerts", "health") == first_alerts, "repeated notices must be throttled"
        run("run", "health")
        assert int(state("health")["last_test"]) > 0
        assert state("health")["test_cursor"] == "1"
        assert "Health: OK" in run("health", "health")
        before = set(Path(tempfile.gettempdir()).glob("cbackup-test-health-*"))
        run("test-restore", "health")
        assert state("health")["test_cursor"] == "2", "sample selection must rotate"
        assert set(Path(tempfile.gettempdir()).glob("cbackup-test-health-*")) == before
        print("OK automatic sample restore, rotating selection, cleanup and alert throttling", flush=True)

        source.rename(root / "missing")
        assert "ALERT" in run("run", "health", fail=True)
        assert "run_failed" in run("health", "health", fail=True)
        count = run("alerts", "health").count("\trun_failed\t")
        run("run", "health", fail=True)
        assert run("alerts", "health").count("\trun_failed\t") == count
        (root / "missing").rename(source)
        run("run", "health")
        assert state("health")["last_failure"] == "0"
        age("health", "last_success", 25 * 3600)
        age("health", "last_offline", 15 * 86400)
        age("health", "last_test", 8 * 86400)
        report = run("health", "health", fail=True)
        assert "stale:" in report and "offline_due:" in report
        run("run", "health")  # unchanged data still refreshes success and runs overdue test
        assert int(state("health")["last_test"]) >= int(time.time()) - 10
        run("offline-confirm", "health", "unknown", fail=True)
        snapshot = ids("health")[-1]
        run("offline-confirm", "health", snapshot)
        assert state("health")["offline_id"] == snapshot
        assert "Health: OK" in run("health", "health")
        print("OK persistent failures, recovery, stale runs and offline-copy reminders/confirmation", flush=True)

        archive = next(store("health").glob("snap_*.tar.zst"))
        original = archive.read_bytes()
        archive.write_bytes(original + b"damage")
        run("test-restore", "health", fail=True)
        assert state("health")["test_failure"] != "0"
        assert set(Path(tempfile.gettempdir()).glob("cbackup-test-health-*")) == before
        archive.write_bytes(original)
        assert "test_failed:" in run("health", "health", fail=True)
        run("test-restore", "health")
        assert "Health: OK" in run("health", "health")
        setting("health", "notifications", "false")
        alerts = run("alerts", "health")
        source.rename(root / "missing")
        assert "ALERT" not in run("run", "health", fail=True)
        assert run("alerts", "health") == alerts
        (root / "missing").rename(source)
        bad_destination = root / "not-a-directory"
        bad_destination.write_text("occupied")
        # Config was saved before logging discovered the unavailable destination.
        run("config", "health", "set", "dest", bad_destination, fail=True)
        run("run", "health", fail=True)
        assert "run_failed:" in run("health", "health", fail=True)
        setting("health", "dest", profile / "backups")
        run("run", "health")
        print("OK damaged archives fail sample restore, state recovers, notifications can be disabled", flush=True)

        quota_source = root / "quota-source"
        quota_source.mkdir()
        (quota_source / "payload.bin").write_bytes(os.urandom(600 * 1024))
        run("init", "quota", quota_source, "--no-schedule")
        setting("quota", "compression", "none")
        setting("quota", "storage_quota_mb", "1")
        run("run", "quota")
        initial = ids("quota")
        (quota_source / "payload.bin").write_bytes(os.urandom(600 * 1024))
        assert "estimated new archive" in run("run", "quota", "--dry-run")
        assert "Storage quota exceeded" in run("run", "quota", fail=True)
        assert ids("quota") == initial
        assert not list(store("quota").glob("*.partial*"))
        run("restore", "quota", initial[0], "--to", root / "quota-old")
        setting("quota", "storage_quota_mb", "2")
        run("run", "quota")
        assert len(ids("quota")) == 2
        setting("quota", "storage_quota_mb", "1")
        assert "storage:" in run("health", "quota", fail=True)
        for value in ("-1", "2147483648", "bad"):
            setting_args = ("config", "quota", "set", "storage_quota_mb", value)
            run(*setting_args, fail=True)
        print("OK quota estimates and exact limits preserve restore chains and remove failed staging files", flush=True)

        run("init", "mirror", quota_source, "--no-schedule")
        setting("mirror", "mode", "mirror")
        setting("mirror", "storage_quota_mb", "1")
        assert "estimated new archive" in run("run", "mirror", "--dry-run")
        assert not (store("mirror") / "mirror").exists()
        run("run", "mirror")
        (quota_source / "payload.bin").write_bytes(os.urandom(600 * 1024))
        run("run", "mirror")
        assert (store("mirror") / "mirror/files/source_0/payload.bin").read_bytes() == (quota_source / "payload.bin").read_bytes()
        print("OK mirror replacement accounts for the replaced tree within the quota", flush=True)

        large_source = root / "large-source"
        large_source.mkdir()
        (large_source / "large.bin").write_bytes(b"x" * (2 * 1024 * 1024))
        run("init", "sample-limit", large_source, "--no-schedule")
        setting("sample-limit", "test_restore_max_mb", "1")
        assert "No test sample fits" in run("run", "sample-limit", fail=True)
        assert len(ids("sample-limit")) == 1, "a failed test must keep the valid local snapshot"
        setting("sample-limit", "test_restore_max_mb", "3")
        run("run", "sample-limit")
        assert "Health: OK" in run("health", "sample-limit")
        setting("sample-limit", "test_restore_days", "0")
        cursor = state("sample-limit")["test_cursor"]
        (large_source / "large.bin").write_bytes(b"y" * (2 * 1024 * 1024))
        run("run", "sample-limit")
        assert state("sample-limit")["test_cursor"] == cursor
        print("OK sample limits report untested data and automatic test restores can be disabled", flush=True)
        print("PASS: 6 maintenance integration groups", flush=True)


if __name__ == "__main__":
    main()
