#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""d2d starts with and without --overlay-dir; `info` says overlay=off or
overlay=<hash8>; a malformed fragment stops startup (non-zero exit).
Usage: overlay_d2d.py <path-to-d2d>. Skips without MPQs."""
import os, socket, subprocess, sys, tempfile, time

d2d = sys.argv[1]
data = os.environ.get("D2_MPQ_DIR") or os.path.expanduser("~/Workspace/private/diablo2")
if not os.path.exists(os.path.join(data, "d2data.mpq")):
    print(f"SKIP: {data}/d2data.mpq not found")
    sys.exit(77)

def start(extra):
    home = tempfile.mkdtemp(prefix="d2d_overlay_")
    os.makedirs(os.path.join(home, ".config/d2d/save"))
    sock = os.path.join(tempfile.gettempdir(), f"d2d_overlay_{os.getpid()}.sock")
    env = dict(os.environ, HOME=home)
    patch = os.environ.get("D2_PATCH_INSTALLER")
    if patch and os.path.exists(patch):
        with open(os.path.join(home, ".config/d2d/d2d.cfg"), "w") as f:
            f.write(f"patch = {patch}\n")
    proc = subprocess.Popen([d2d, "--headless", "--no-save", "--seed", "3", "--data", data, "--devctl", sock,
                             "--start-screen", "charselect"] + extra, env=env)
    return proc, sock

def info(extra):
    proc, sock = start(extra)
    try:
        for _ in range(300):
            if os.path.exists(sock):
                break
            assert proc.poll() is None, "d2d exited during startup"
            time.sleep(0.1)
        with socket.socket(socket.AF_UNIX) as s:
            s.settimeout(5)
            s.connect(sock)
            s.sendall(b"info\n")
            out = ""
            while not out.endswith("ok\n"):
                chunk = s.recv(4096)
                assert chunk, f"closed after {out!r}"
                out += chunk.decode()
        return out
    finally:
        proc.terminate()
        proc.wait(10)
        if os.path.exists(sock):
            os.unlink(sock)

def overlay(text):
    d = tempfile.mkdtemp(prefix="d2d_ovl_")
    os.makedirs(os.path.join(d, "excel"))
    with open(os.path.join(d, "excel", "UniqueItems.txt"), "w", newline="") as f:
        f.write(text)
    return d

assert "overlay=off" in info([])
good = overlay("index\tcode\tlvl\tenabled\r\nzz_test_fake_row\tcap\t1\t1\r\n")
out = info(["--overlay-dir", good])
assert "overlay=" in out and "overlay=off" not in out, out
bad = overlay("index\tcode\r\nzz_bad_row\tcap\textra\r\n")
proc, sock = start(["--overlay-dir", bad])
try:
    assert proc.wait(60) != 0, "a malformed fragment must stop startup"
finally:
    if proc.poll() is None:
        proc.terminate()
        proc.wait(10)
print("ok: --overlay-dir off/on/malformed")
