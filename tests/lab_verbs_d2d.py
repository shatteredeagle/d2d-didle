#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Without --lab-verbs the setup/cheat `debug` verbs answer `err lab verbs off`
and the read-only ones, ping, state and info still answer; with the flag the
gated verb is handled. Usage: lab_verbs_d2d.py <path-to-d2d>. Skips without MPQs."""
import os, socket, subprocess, sys, tempfile, time

d2d = sys.argv[1]
data = os.environ.get("D2_MPQ_DIR") or os.path.expanduser("~/Workspace/private/diablo2")
if not os.path.exists(os.path.join(data, "d2data.mpq")):
    print(f"SKIP: {data}/d2data.mpq not found")
    sys.exit(77)

GATED = ["debug clearinv", "debug stat 12 5", "debug give hp1 pack", "debug goto 2", "debug warp 1 1",
         "debug spawn fallen", "debug points 36 1", "debug belt clear", "debug durability all 1",
         "debug wp all", "debug states set 1", "debug quest 1", "debug hit 1", "debug kill", "debug die"]
OPEN = ["debug players", "debug level", "debug warps", "debug presets", "debug itemat 1", "debug quest 2 show",
        "debug states", "debug help", "ping", "state", "info", "clock"]

def run(extra):
    home = tempfile.mkdtemp(prefix="d2d_labverbs_")
    os.makedirs(os.path.join(home, ".config/d2d/save"))
    sock = os.path.join(tempfile.gettempdir(), f"d2d_labverbs_{os.getpid()}.sock")
    env = dict(os.environ, HOME=home)
    proc = subprocess.Popen([d2d, "--headless", "--no-save", "--seed", "3", "--data", data, "--devctl", sock,
                             "--start-screen", "charselect"] + extra, env=env)
    def cmd(line):
        with socket.socket(socket.AF_UNIX) as s:
            s.settimeout(5)
            s.connect(sock)
            s.sendall((line + "\n").encode())
            out = ""
            while not out.endswith("\n") or not out.rsplit("\n", 2)[-2].startswith(("ok", "err")):
                chunk = s.recv(4096)
                assert chunk, f"{line!r}: closed after {out!r}"
                out += chunk.decode()
        return out
    try:
        for _ in range(300):
            if os.path.exists(sock):
                break
            assert proc.poll() is None, "d2d exited during startup"
            time.sleep(0.1)
        return {line: cmd(line) for line in GATED + OPEN}
    finally:
        proc.terminate()
        proc.wait(10)
        if os.path.exists(sock):
            os.unlink(sock)

off = run([])
for line in GATED:
    assert off[line].startswith("err lab verbs off"), (line, off[line])
for line in OPEN:
    assert "lab verbs off" not in off[line], (line, off[line])
on = run(["--lab-verbs"])
for line in GATED:
    assert "lab verbs off" not in on[line], (line, on[line])
print("ok: --lab-verbs gates the setup debug verbs")
