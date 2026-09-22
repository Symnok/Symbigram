#!/usr/bin/env python3
# Turns credentials.cfg (KEY=VALUE lines, see credentials.cfg.template) into
# src/core/tgcredentials.h, which the protocol core includes. qmake runs this (core.pri), so
# the header is regenerated whenever the project is configured; it is gitignored.
import os, sys

root = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..")
cfg = os.path.join(root, "credentials.cfg")
out = os.path.join(root, "src", "core", "tgcredentials.h")

if not os.path.exists(cfg):
    print("credentials.cfg is missing - copy credentials.cfg.template to credentials.cfg and fill in the values",
          file=sys.stderr)
    sys.exit(1)

values = {}
for line in open(cfg, encoding="utf-8"):
    line = line.strip()
    if not line or line.startswith("#") or "=" not in line:
        continue
    key, val = line.split("=", 1)
    values[key.strip()] = val.strip()

missing = [k for k in ("TG_API_ID", "TG_API_HASH") if not values.get(k)]
if missing:
    print("credentials.cfg has no " + ", ".join(missing), file=sys.stderr)
    sys.exit(1)
if not values["TG_API_ID"].isdigit() or values["TG_API_ID"] == "0":
    print("credentials.cfg: TG_API_ID must be your numeric api_id from https://my.telegram.org", file=sys.stderr)
    sys.exit(1)

def cstr(s):
    return '"' + s.replace("\\", "\\\\").replace('"', '\\"') + '"'

text = ("// Generated from credentials.cfg by tools/gen-credentials.py - do not edit, do not commit.\n"
        "#ifndef TGCREDENTIALS_H\n#define TGCREDENTIALS_H\n"
        "#define TG_API_ID %s\n"
        "#define TG_API_HASH %s\n"
        "#endif\n") % (values["TG_API_ID"], cstr(values["TG_API_HASH"]))

# Only touch the file when it changes, so a plain re-run of qmake does not rebuild the core.
if not (os.path.exists(out) and open(out, encoding="utf-8").read() == text):
    open(out, "w", encoding="utf-8", newline="\n").write(text)
print("tgcredentials.h: api id " + values["TG_API_ID"])
