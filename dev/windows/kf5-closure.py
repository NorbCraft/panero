import re, os, sys, subprocess, urllib.request, urllib.parse
W = os.path.dirname(os.path.abspath(__file__))
html = open(f'{W}/ucrt64.html').read()
files = [urllib.parse.unquote(f) for f in re.findall(r'href="(mingw-w64-ucrt-x86_64-[^"]+\.pkg\.tar\.zst)"', html)]
db = {}; prov = {}
for d in os.listdir(f'{W}/db'):
    desc = open(f'{W}/db/{d}/desc').read()
    sec = {}
    for blk in desc.strip().split('\n\n'):
        lines = blk.split('\n'); sec[lines[0]] = lines[1:]
    n = sec['%NAME%'][0]; db[n] = sec['%VERSION%'][0]
    for p in sec.get('%PROVIDES%', []): prov[re.split('[<>=]', p)[0]] = n

def key(f):
    return [int(x) if x.isdigit() else x for x in re.split(r'[.+-]', f)]

def latest_file(name):
    c = [f for f in files if re.fullmatch(re.escape(name) + r'-[^-]+-[0-9.]+-any\.pkg\.tar\.zst', f)]
    return sorted(c, key=key)[-1] if c else None

os.makedirs(f'{W}/pkgs', exist_ok=True)
def pkginfo(f):
    p = f'{W}/pkgs/' + f
    if not os.path.exists(p):
        urllib.request.urlretrieve('https://repo.msys2.org/mingw/ucrt64/' + urllib.parse.quote(f), p)
    out = subprocess.run(['tar', '-xOf', p, '.PKGINFO'], capture_output=True, text=True).stdout
    return [l.split(' = ', 1)[1] for l in out.splitlines() if l.startswith('depend = ')]

P = 'mingw-w64-ucrt-x86_64-'
todo = [P + x for x in sys.argv[1:]]
seen = {}; indb = set(); missing = set()
while todo:
    n = todo.pop()
    base = re.split('[<>=]', n)[0]
    if base in seen or base in indb: continue
    if base in db or base in prov: indb.add(base); continue
    f = latest_file(base)
    if not f: missing.add(n); continue
    seen[base] = f
    todo += pkginfo(f)
print("PINNED:"); [print(' ', f) for f in sorted(seen.values())]
print("FROM DB:", ' '.join(sorted(x.replace(P, '') for x in indb)))
print("MISSING:", missing)
