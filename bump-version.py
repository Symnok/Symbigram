#!/usr/bin/env python3
# Increments the patch component of VERSION in Symbigram.pro. build-symbian.cmd runs this
# before each phone build, so Symbigram.pro always names the newest package and every build
# is higher than the last - Symbian refuses to replace an installed app with an equal or
# lower version, and a new version also nudges the launcher to refresh a cached icon.
import re, sys, os
pro = os.path.join(os.path.dirname(os.path.abspath(__file__)), "Symbigram.pro")
s = open(pro, encoding="utf-8").read()
m = re.search(r'^VERSION = (\d+)\.(\d+)\.(\d+)', s, re.M)
if not m:
    print("VERSION line not found"); sys.exit(1)
major, minor, patch = int(m.group(1)), int(m.group(2)), int(m.group(3))
# --print just echoes the current version (for the .sis filename), no bump.
if "--print" in sys.argv:
    print("%d.%d.%d" % (major, minor, patch)); sys.exit(0)
new = "%d.%d.%d" % (major, minor, patch + 1)
s = s[:m.start()] + "VERSION = " + new + s[m.end():]
open(pro, "w", encoding="utf-8", newline="\n").write(s)
print("version %d.%d.%d -> %s" % (major, minor, patch, new))
