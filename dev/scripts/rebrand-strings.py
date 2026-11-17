#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Rebrand user-visible "Krusader" mentions to "Panero".

Applied once on the identity branch (see dev/DECISIONS.md D-008). It only
touches C++ string literals (never identifiers, comments or copyright lines)
and applies the identical rule to msgid/msgstr in the gettext catalogs so
existing translations keep matching. Strings that are attribution or that
name an upstream behaviour are kept (KEEP).

Usage: python3 -I dev/scripts/rebrand-strings.py <repo-root>
"""
import pathlib, re, sys

root = pathlib.Path(sys.argv[1])
WORD = re.compile(r'\bKrusader\b')
# Literal contents that must keep the upstream name.
KEEP = [
    'Krusader Krew', 'Krusader Mode', 'Krusader sort', 'for Krusader',
    'fork of Krusader', 'Krusader maintainers', 'Krusader project',
    'Based on Krusader', 'Krusader %1',
]
SKIP_FILES = {'app/main.cpp'}
LITERAL = re.compile(r'"((?:[^"\\\n]|\\.)*)"')

def keep(text):
    return any(k in text for k in KEEP)

def rebrand(text):
    return text if keep(text) else WORD.sub('Panero', text)

changed_src = 0
for path in sorted(list(root.glob('app/**/*.cpp')) + list(root.glob('app/**/*.h'))
                   + list(root.glob('plugins/**/*.cpp')) + list(root.glob('plugins/**/*.h'))):
    rel = path.relative_to(root).as_posix()
    if rel in SKIP_FILES:
        continue
    src = path.read_text(encoding='utf-8')
    out_lines = []
    for line in src.split('\n'):
        stripped = line.lstrip()
        if stripped.startswith('//') or stripped.startswith('*') or '#include' in line:
            out_lines.append(line)
            continue
        out_lines.append(LITERAL.sub(lambda m: '"' + rebrand(m.group(1)) + '"', line))
    new = '\n'.join(out_lines)
    if new != src:
        path.write_text(new, encoding='utf-8')
        changed_src += 1

# gettext catalogs: rewrite msgid/msgid_plural/msgstr lines of each entry
changed_po = 0
for po in sorted(root.glob('po/*/panero.po')):
    text = po.read_text(encoding='utf-8')
    entries = text.split('\n\n')
    out = []
    for entry in entries:
        lines = entry.split('\n')
        # collect msgid text to decide on KEEP for the whole entry
        msgid, in_id = [], False
        for l in lines:
            if l.startswith('msgid '):
                in_id = True; msgid.append(l[6:])
            elif l.startswith(('msgstr', 'msgid_plural', 'msgctxt')):
                in_id = False
            elif in_id and l.startswith('"'):
                msgid.append(l)
        joined = ''.join(s.strip('"') for s in msgid)
        if 'Krusader' not in joined or keep(joined) or lines[0].startswith('#~'):
            out.append(entry); continue
        new_lines = []
        for l in lines:
            if l.startswith('#'):
                new_lines.append(l)
            else:
                new_lines.append(WORD.sub('Panero', l) if not l.startswith('msgctxt') else l)
        out.append('\n'.join(new_lines))
    new = '\n\n'.join(out)
    if new != text:
        po.write_text(new, encoding='utf-8'); changed_po += 1

print(f'source files changed: {changed_src}, catalogs changed: {changed_po}')
