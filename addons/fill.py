# -*- coding: utf-8 -*-
# Puts a pack's translations into its .ts.  Usage: fill.py <lang> <pack folder>
#
# A message with no entry in the table is left "unfinished", which lrelease leaves out of the .qm,
# so Qt falls back to the English source for it. That is what makes a partly-translated pack safe
# to ship: it can lag behind the application's strings without ever breaking anything.
import io, os, sys
import xml.etree.ElementTree as ET

lang, folder = sys.argv[1], sys.argv[2]

ns = {}
path = os.path.join(folder, "strings_%s.py" % lang)
exec(compile(io.open(path, encoding="utf-8").read(), path, "exec"), ns)
table = ns.get("TABLE") or ns["TR"]          # TR is the name the first pack (Turkish) used

ts = os.path.join(folder, "symbigram_%s.ts" % lang)
tree = ET.parse(ts)
root = tree.getroot()
root.set("language", lang)

done, missing = 0, []
for ctx in root.findall("context"):
    for msg in ctx.findall("message"):
        source = msg.find("source").text or ""
        tr = msg.find("translation")
        if tr is None:
            tr = ET.SubElement(msg, "translation")
        if source in table:
            tr.text = table[source]
            if "type" in tr.attrib:
                del tr.attrib["type"]
            done += 1
        else:
            tr.text = ""
            tr.set("type", "unfinished")
            missing.append(source)

tree.write(ts, encoding="utf-8", xml_declaration=True)
print("%s: %d translated, %d left in English" % (lang, done, len(missing)))
