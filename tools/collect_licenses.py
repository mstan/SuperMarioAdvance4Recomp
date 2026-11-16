"""Stage dependency notices next to a desktop release (no private build data)."""
import argparse
import hashlib
from pathlib import Path
import shutil
import subprocess
import tarfile
import urllib.request

p = argparse.ArgumentParser(description=__doc__)
p.add_argument('engine', type=Path)
p.add_argument('ui', type=Path)
p.add_argument('output', type=Path)
p.add_argument('--mingw', type=Path)
p.add_argument('--archive-root', type=Path)
a = p.parse_args()
a.output.mkdir(parents=True, exist_ok=True)
sources = {
    'gbarecomp': a.engine / 'LICENSE',
    'arm-recomp-core': a.engine / 'external/arm-recomp-core/LICENSE',
    'recomp-net': a.engine / 'external/recomp-net/LICENSE',
    'rbengine': a.engine / 'external/rbengine/LICENSE',
    'recomp-ui': a.ui / 'LICENSE',
    'Dear-ImGui': a.ui / 'src/third_party/imgui/LICENSE.txt',
}
for name, source in sources.items():
    shutil.copyfile(source, a.output / f'{name}.txt')
if a.mingw:
    for package in ('gcc-libs', 'libwinpthread', 'SDL2'):
        shutil.copytree(a.mingw / 'share/licenses' / package,
                        a.output / package, dirs_exist_ok=True)
else:
    shutil.copyfile(a.engine / 'platform/android/third_party/SDL/LICENSE.txt',
                    a.output / 'SDL2.txt')
    for package in ('libfreetype6', 'libharfbuzz0b', 'libpng16-16',
                    'libbrotli1', 'libgraphite2-3'):
        shutil.copyfile(Path('/usr/share/doc') / package / 'copyright',
                        a.output / f'{package}.txt')

# The optional archive reader is dynamically linked. Include its complete
# upstream source/notices, the codec notices, and libiconv's exact upstream
# source plus MSYS2 build/patch inputs so the replaceable LGPL DLL is covered.
if a.archive_root:
    header = (a.archive_root / 'include/archive.h').read_text(encoding='utf-8')
    if not any(line.split() == ['#define', 'ARCHIVE_VERSION_NUMBER', '3008011'] for line in header.splitlines()):
        raise RuntimeError('The Windows archive notices recipe is pinned to libarchive 3.8.11')
    if not a.mingw:
        raise RuntimeError('--archive-root currently requires the Windows release codec set')
    for package in ('libb2', 'bzip2', 'libiconv', 'xz', 'zlib', 'zstd'):
        shutil.copytree(a.mingw / 'share/licenses' / package,
                        a.output / package, dirs_exist_ok=True)
    archive_out = a.output / 'libarchive'
    archive_out.mkdir(exist_ok=True)
    shutil.copyfile(a.archive_root / 'share/licenses/libarchive/LICENSE', archive_out / 'LICENSE')
    source_cache = a.engine / 'tools/_toolchain_cache/rom-archives'
    source_cache.mkdir(parents=True, exist_ok=True)
    archive_sources = [
        ('libarchive-3.8.11.tar.xz', 'https://github.com/libarchive/libarchive/releases/download/v3.8.11/libarchive-3.8.11.tar.xz',
         'a3dc0872b1b39da34dc18ef1b944f0f11ffd37425d70ae04be679279f1cf43d2'),
        ('libiconv-1.18.tar.gz', 'https://ftp.gnu.org/pub/gnu/libiconv/libiconv-1.18.tar.gz',
         '3b08f5f4f9b4eb82f151a7040bfd6fe6c6fb922efe4b1659c66ea933276965e8'),
        ('lz4-LICENSE', 'https://raw.githubusercontent.com/lz4/lz4/v1.10.0/lib/LICENSE',
         '8b58c446121a109ccf32edc094bba3010a3d85e4ee3702950db55e4d3e87736c'),
    ]
    recipe_url = 'https://raw.githubusercontent.com/msys2/MINGW-packages/f89ccd3564cceff9ed46d778c91f278381393c75/mingw-w64-libiconv/'
    for filename, digest in (
        ('0003-add-cp65001-as-utf8-alias.patch', 'cb2b1cca44c9b9bd11c3bd33997c10d22dff263542275324c88f34b59910ef87'),
        ('0002-fix-cr-for-awk-in-configure.all.patch', '89d5c0f666e50a0186cfb142ba7b77e8fe1ac4d65bdfd9ae14ae8d2f0045a87c'),
        ('fix-pointer-buf.patch', '6b6e2393840f4dc6067587165777b1a07978f4c05247d7c1010a45ad251bbeeb'),
        ('iconv.pc', '56e7ec406bf42eb66b1d972f20d229f7a76ba00c38c7b4403bf348875704baae'),
        ('PKGBUILD', '4d9f62cdcd26dc0e2c1abe9c558239a273dc9668a00948cf3585fae86f3389d2'),
    ):
        archive_sources.append((filename, recipe_url + filename, digest))
    lines = ['# Archive reader and codec source', '',
             'libarchive and its RAR readers use BSD licenses, not proprietary UnRAR.',
             'libiconv is a separate replaceable LGPL DLL; its source and build inputs are included.', '']
    for filename, url, digest in archive_sources:
        cached = source_cache / filename
        if not cached.exists():
            with urllib.request.urlopen(url, timeout=60) as response:
                cached.write_bytes(response.read())
        if hashlib.sha256(cached.read_bytes()).hexdigest() != digest:
            raise RuntimeError(f'Archive dependency source checksum differs: {filename}')
        shutil.copyfile(cached, archive_out / filename)
        lines.append(f'- {filename}: {url} (SHA-256 {digest})')
    (archive_out / 'SOURCES.md').write_text('\n'.join(lines) + '\n', encoding='utf-8')

# The shipped overlay compiler is the fixed TinyCC 0.9.27 binary. Include its
# matching license and complete upstream source alongside it.
tcc_url = 'https://download.savannah.gnu.org/releases/tinycc/tcc-0.9.27.tar.bz2'
tcc_sha256 = 'de23af78fca90ce32dff2dd45b3432b2334740bb9bb7b05bf60fdbfc396ceb9c'
tcc_cache = a.engine / 'tools/_toolchain_cache/tcc-0.9.27.tar.bz2'
if not tcc_cache.exists():
    tcc_cache.parent.mkdir(parents=True, exist_ok=True)
    with urllib.request.urlopen(tcc_url, timeout=60) as response:
        data = response.read()
    if hashlib.sha256(data).hexdigest() != tcc_sha256:
        raise RuntimeError('TinyCC source archive checksum differs')
    tcc_cache.write_bytes(data)
if hashlib.sha256(tcc_cache.read_bytes()).hexdigest() != tcc_sha256:
    raise RuntimeError('Cached TinyCC source archive checksum differs')
tcc_output = a.output / 'TinyCC'
tcc_output.mkdir(exist_ok=True)
shutil.copyfile(tcc_cache, tcc_output / tcc_cache.name)
with tarfile.open(tcc_cache) as archive:
    (tcc_output / 'COPYING').write_bytes(archive.extractfile('tcc-0.9.27/COPYING').read())

git = 'C:/Program Files/Git/cmd/git.exe' if Path('C:/Program Files/Git/cmd/git.exe').exists() else 'git'
title = Path(__file__).resolve().parents[1]
source_lines = ['# Source identities', '',
                'The title and pinned dependencies include their build scripts. Supply your own ROM/BIOS to regenerate the game corpus.', '']
for label, root, url in (
    ('Title', title, None),
    ('Engine', a.engine, 'https://github.com/mstan/gbarecomp'),
    ('Launcher', a.ui, 'https://github.com/RetroPortingToolKit/recomp-ui'),
):
    head = subprocess.check_output([git, '-C', str(root), 'rev-parse', 'HEAD'], text=True).strip()
    if url is None:
        for remote in ('origin', 'mstan'):
            candidate = subprocess.run([git, '-C', str(root), 'config', '--get', f'remote.{remote}.url'], text=True, capture_output=True)
            if candidate.returncode == 0:
                url = candidate.stdout.strip().removesuffix('.git').replace('git@github.com:', 'https://github.com/')
                break
        if url is None:
            raise RuntimeError('Title source remote is missing')
    source_lines.append(f'- {label}: {url}/tree/{head}')
source_lines += ['', 'Additional engine dependencies are pinned by the engine source tree above.',
                 f'TinyCC 0.9.27 source is included in TinyCC/{tcc_cache.name}; upstream: {tcc_url}',
                 f'TinyCC source SHA256: {tcc_sha256}', '']
(a.output / 'SOURCES.md').write_text('\n'.join(source_lines), encoding='utf-8')
