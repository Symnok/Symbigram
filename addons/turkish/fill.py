# -*- coding: utf-8 -*-
# Puts the Turkish text from strings_tr.py into symbigram_tr.ts.
#
# A message with no entry in the table is left "unfinished", which lrelease leaves out of the
# .qm - so Qt falls back to the English source for it. That is what makes a partly-translated
# pack safe to ship.
import io, os, sys
import xml.etree.ElementTree as ET

HERE = os.path.dirname(os.path.abspath(__file__))
ns = {}
exec(compile(io.open(os.path.join(HERE, "strings_tr.py"), encoding="utf-8").read(), "strings_tr.py", "exec"), ns)
TR = ns["TR"]

path = os.path.join(HERE, "symbigram_tr.ts")
tree = ET.parse(path)
root = tree.getroot()
root.set("language", "tr")

done = 0
missing = []
for ctx in root.findall("context"):
    for msg in ctx.findall("message"):
        source = msg.find("source").text or ""
        tr = msg.find("translation")
        if tr is None:
            tr = ET.SubElement(msg, "translation")
        if source in TR:
            tr.text = TR[source]
            if "type" in tr.attrib:
                del tr.attrib["type"]
            done += 1
        else:
            tr.text = ""
            tr.set("type", "unfinished")
            missing.append(source)

tree.write(path, encoding="utf-8", xml_declaration=True)
print("tr: %d translated, %d left in English" % (done, len(missing)))
for m in missing:
    print("   " + m)
