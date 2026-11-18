#!/usr/bin/env python3
"""Smoke test for Price Game GTK4 Desktop Client.
Verifies binary existence, --help output, display startup, and multi-instance readiness.
"""
import os
import signal
import subprocess
import sys
import time
from pathlib import Path

def test_gui_smoke():
    repo_root = Path(__file__).resolve().parent.parent.parent
    build_dir = repo_root / "build"
    gui_bin = build_dir / "pr_gui"

    print("==> Checking GTK4 binary existence...")
    if not gui_bin.exists():
        print(f"FAIL: {gui_bin} does not exist. Run 'make gui' first.")
        sys.exit(1)
    if not os.access(gui_bin, os.X_OK):
        print(f"FAIL: {gui_bin} is not executable.")
        sys.exit(1)
    print("PASS: Binary exists and is executable.")

    print("\n==> Checking --help options...")
    res = subprocess.run([str(gui_bin), "--help"], capture_output=True, text=True)
    if res.returncode != 0:
        print(f"FAIL: --help returned {res.returncode}")
        print("stderr:", res.stderr)
        sys.exit(1)
    if "help" not in res.stdout.lower() and "options" not in res.stdout.lower():
        print("FAIL: --help output does not contain help/options text.")
        print(res.stdout)
        sys.exit(1)
    print("PASS: --help returned valid GTK/GApplication help text.")

    print("\n==> Checking GUI startup and clean shutdown...")
    proc = subprocess.Popen([str(gui_bin)], stdout=subprocess.PIPE, stderr=subprocess.PIPE)
    time.sleep(1.0)
    # Check if process crashed immediately
    poll_res = proc.poll()
    if poll_res is not None and poll_res != 0:
        stderr_txt = proc.stderr.read().decode('utf-8', errors='ignore')
        print(f"FAIL: GUI crashed on startup with exit code {poll_res}: {stderr_txt}")
        sys.exit(1)

    # Terminate gracefully
    proc.send_signal(signal.SIGTERM)
    try:
        proc.wait(timeout=3.0)
    except subprocess.TimeoutExpired:
        proc.kill()
        proc.wait()
    print("PASS: GUI process initialized successfully and handled termination.")

    print("\n==> Checking multi-instance support (G_APPLICATION_NON_UNIQUE)...")
    p1 = subprocess.Popen([str(gui_bin)], stdout=subprocess.PIPE, stderr=subprocess.PIPE)
    time.sleep(0.5)
    p2 = subprocess.Popen([str(gui_bin)], stdout=subprocess.PIPE, stderr=subprocess.PIPE)
    time.sleep(1.0)

    p1_alive = (p1.poll() is None)
    p2_alive = (p2.poll() is None)

    p1.send_signal(signal.SIGTERM)
    p2.send_signal(signal.SIGTERM)
    p1.wait(timeout=3.0)
    p2.wait(timeout=3.0)

    if not (p1_alive and p2_alive):
        print(f"FAIL: Multi-instance check failed. P1 alive: {p1_alive}, P2 alive: {p2_alive}")
        sys.exit(1)
    print("PASS: Multiple GUI instances run concurrently without bus conflicts.")

    print("\n=======================================================")
    print("ALL GUI SMOKE TESTS PASSED!")
    print("=======================================================")

if __name__ == "__main__":
    test_gui_smoke()
