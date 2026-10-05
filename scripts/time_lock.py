"""Rewrite a Cargo.lock so every crates.io package is the newest version published at or before CUTOFF."""

import json
import re
import subprocess
import sys
import time
import tomllib
import urllib.request

CUTOFF = sys.argv[1]
CRATE_DIR = sys.argv[2]
CACHE: dict = {}


def published(name):
    if name not in CACHE:
        out, url = [], f"https://crates.io/api/v1/crates/{name}/versions?per_page=100"
        while url:
            req = urllib.request.Request(url, headers={"User-Agent": "repro-build-check"})
            d = json.load(urllib.request.urlopen(req))
            out += [(v["num"], v["created_at"]) for v in d["versions"]]
            nxt = d.get("meta", {}).get("next_page")
            url = f"https://crates.io/api/v1/crates/{name}/versions{nxt}" if nxt else None
            time.sleep(0.5)
        CACHE[name] = out
    return CACHE[name]


def key(v):
    return tuple(int(x) for x in v.split("."))


def compat(v):
    p = key(v)
    return p[:1] if p[0] else p[:2] if p[1] else p[:3]


def lock_pkgs():
    with open(f"{CRATE_DIR}/Cargo.lock", "rb") as f:
        lock = tomllib.load(f)
    return [(p["name"], p["version"]) for p in lock["package"] if "registry" in p.get("source", "")]


for rnd in range(10):
    changed = 0
    for name, cur in lock_pkgs():
        vers = dict(published(name))
        if vers[cur] <= CUTOFF:
            continue
        cands = [v for v, t in vers.items()
                 if t <= CUTOFF and re.fullmatch(r"\d+\.\d+\.\d+", v) and compat(v) == compat(cur)]
        if not cands:
            print(f"round {rnd}: {name} {cur}: no compatible version before cutoff", flush=True)
            continue
        new = max(cands, key=key)
        r = subprocess.run(["cargo", "update", "-p", f"{name}@{cur}", "--precise", new],
                           cwd=CRATE_DIR, capture_output=True, text=True)
        if r.returncode == 0:
            changed += 1
            print(f"round {rnd}: {name} {cur} -> {new}", flush=True)
        else:
            print(f"round {rnd}: {name} {cur} -> {new} FAILED: {r.stderr.strip().splitlines()[-1]}", flush=True)
    if not changed:
        break

late = [(n, v) for n, v in lock_pkgs() if dict(published(n))[v] > CUTOFF]
print("packages still newer than cutoff:", late)
