# -*- coding: utf-8 -*-
# Refuses to let a malformed .qm ship.
#
# One did: translations/symbigram_ru.qm went out with 20 junk bytes after its last block, and
# QTranslator::load() rejects a file whose block table runs past the end - the WHOLE file, not
# just the damaged part. The app then shows English with no error anywhere, so "switching to
# Russian does nothing" was the only symptom, and finding it took a hex dump. It is cheap to
# notice at build time instead, for the built-in languages and for every add-on pack.
#
# Checks the same things QTranslator does, in the same order (qtranslator.cpp, do_load):
# the magic, then a walk of the tag/length block table, which must land exactly on the end.
#
#   python tools/checkqm.py            everything under translations/ and addons/
#   python tools/checkqm.py a.qm b.qm  just those files
import glob
import os
import struct
import sys

MAGIC = b'\x3c\xb8\x64\x18\xca\xef\x9c\x95\xcd\x21\x1c\xbf\x60\xa1\xbd\xdd'

# The block tags Qt 4 writes. An unknown one means we are no longer reading block headers at all,
# which is exactly what junk at the end of the file looks like.
TAGS = {0x2f: 'contexts', 0x42: 'hashes', 0x69: 'messages', 0x88: 'numerus rules',
        0x96: 'dependencies'}


def problem(path):
    """The reason this .qm would not load, or None when it is sound."""
    data = open(path, 'rb').read()
    if len(data) < 16 or data[:16] != MAGIC:
        return 'not a Qt translation file (bad magic)'
    at = 16
    while at < len(data) - 4:
        tag = data[at] if sys.version_info[0] >= 3 else ord(data[at])
        length = struct.unpack('>I', data[at + 1:at + 5])[0]
        if tag == 0 or length == 0:
            return 'empty block at offset %d' % at
        if tag not in TAGS:
            return ('unknown block tag 0x%02x at offset %d - %d stray bytes at the end of the '
                    'file' % (tag, at, len(data) - at))
        at += 5
        if len(data) - at < length:
            return ('the %s block at offset %d claims %d bytes but only %d are left'
                    % (TAGS[tag], at - 5, length, len(data) - at))
        at += length
    if at != len(data):
        return '%d bytes left over after the last block' % (len(data) - at)
    return None


def main(argv):
    files = argv[1:]
    if not files:
        files = sorted(glob.glob(os.path.join('translations', '*.qm'))
                       + glob.glob(os.path.join('addons', '*', '*.qm')))
    if not files:
        print('checkqm: no .qm files found - run this from the project directory')
        return 1
    bad = 0
    for path in files:
        reason = problem(path)
        if reason:
            bad += 1
            print('checkqm: %s WOULD NOT LOAD: %s' % (path, reason))
        else:
            print('checkqm: %s ok (%d bytes)' % (path, os.path.getsize(path)))
    if bad:
        print('checkqm: %d translation(s) are unusable - rebuild them with lrelease' % bad)
    return 1 if bad else 0


if __name__ == '__main__':
    sys.exit(main(sys.argv))
