# This file is part of OpenBGI (https://openbgi.net).
# SPDX-License-Identifier: GPL-2.0-only

"""Profile-driven conversion into a new, save-free OpenBGI loose overlay."""
import fnmatch
import hashlib
import json
from pathlib import Path
import re
import shutil
import struct
import subprocess
import tempfile

from .formats import READERS, BgiArc, EvbExternal, EvbPe, FormatError, safe_name
from .pe_probe import inspect_pe

MANIFEST = 'openbgi-translation-manifest.json'
PROGRESS_NAMES = {'bgi.chs', 'bgi.gdb', 'bgi.hvl', 'bgi.hvc', 'bgi.dat', 'bgierror.txt'}
PROGRESS_DIRS = {'userdata', 'savedata', 'save', 'saves', 'font-cache', '.git'}


def excluded(name):
    value = name.casefold()
    return (value in PROGRESS_NAMES or value == MANIFEST
            or re.fullmatch(r'bgi[0-9]{4,}\.cad', value) is not None
            or value.endswith(('.sav', '.save', '.sud', '.bak', '.dll', '.exe', '.key')))


def inside(root, relative):
    """Resolve profile inputs beneath the selected game, including symlinks."""
    if not isinstance(relative, str) or not relative or Path(relative).is_absolute():
        raise FormatError('Profile paths must be relative to the game')
    path = (root / relative).resolve(strict=True)
    if not path.is_relative_to(root):
        raise FormatError(f'Profile path escapes game directory: {relative}')
    if any(part.casefold() in PROGRESS_DIRS
           for part in (*Path(relative).parts, *path.relative_to(root).parts)):
        raise FormatError('Progress directories cannot be conversion sources')
    return path


def read_profile(path):
    path = Path(path)
    if path.stat().st_size > 1024 * 1024:
        raise FormatError('Profile too large')
    profile = json.loads(path.read_text(encoding='utf-8'))
    if not isinstance(profile, dict) or type(profile.get('schema')) is not int or profile['schema'] != 1:
        raise FormatError('Unsupported profile schema')
    if set(profile) - {'schema', 'id', 'description', 'runtime', 'sources'}:
        raise FormatError('Unknown profile fields')
    if not isinstance(profile.get('id'), str) or not re.fullmatch(r'[A-Za-z0-9._-]{1,80}', profile['id']):
        raise FormatError('Invalid profile id')
    runtime = profile.get('runtime', {})
    if not isinstance(runtime, dict) or set(runtime) - {'engine', 'text_encoding', 'font_family'}:
        raise FormatError('Unknown runtime fields')
    if 'engine' in runtime and not re.fullmatch(r'[0-9]+(?:\.[0-9]+){1,3}', str(runtime['engine'])):
        raise FormatError('Invalid runtime engine')
    if 'text_encoding' in runtime and runtime['text_encoding'] not in ('sjis', 'gbk', 'utf8'):
        raise FormatError('Invalid runtime text encoding')
    if 'font_family' in runtime and (not isinstance(runtime['font_family'], str)
                                    or not 0 < len(runtime['font_family']) <= 128
                                    or any(ord(c) < 32 for c in runtime['font_family'])):
        raise FormatError('Invalid font family')
    sources = profile.get('sources')
    if not isinstance(sources, list) or not 1 <= len(sources) <= 128:
        raise FormatError('Profile requires 1..128 sources')
    for source in sources:
        if not isinstance(source, dict) or set(source) - {'format', 'path', 'key', 'select', 'exclude', 'base_offset'}:
            raise FormatError('Unknown source fields')
        if not isinstance(source.get('format'), str) or source['format'] not in READERS:
            raise FormatError('Unknown source format')
        if not isinstance(source.get('path'), str):
            raise FormatError('Missing source path')
        if source['format'] == 'cpk-v2' and not isinstance(source.get('key'), str):
            raise FormatError('CPK source requires a key path')
        if 'key' in source and source['format'] != 'cpk-v2':
            raise FormatError('Only CPK sources accept a key path')
        if 'base_offset' in source and (source['format'] != 'evb-pe'
                                      or type(source['base_offset']) is not int
                                      or not 0 <= source['base_offset'] < 256 * 1024 * 1024):
            raise FormatError('Invalid embedded EVB base_offset')
        for field in ('select', 'exclude'):
            patterns = source.get(field, [])
            if (not isinstance(patterns, list) or len(patterns) > 256
                    or any(not isinstance(p, str) or not 0 < len(p) <= 255 for p in patterns)):
                raise FormatError(f'Invalid {field} patterns')
        if not source.get('select'):
            raise FormatError('Every source requires explicit select patterns')
    return profile


def open_source(root, source, decoder=None):
    path = inside(root, source['path'])
    reader = READERS[source['format']]
    kwargs = {}
    if source['format'] == 'cpk-v2':
        kwargs['key'] = inside(root, source['key'])
    elif source['format'] in ('evb-external', 'evb-pe'):
        kwargs['decoder'] = decoder
        if 'base_offset' in source:
            kwargs['base_offset'] = source['base_offset']
    return reader(path, **kwargs)


def build_plan(root, profile, decoder=None):
    root = Path(root).resolve(strict=True)
    resources = []
    skipped = []
    names = set()
    for source in profile['sources']:
        container = open_source(root, source, decoder)
        selected = 0
        for entry in container.entries:
            lower = entry.name.casefold()
            if not any(fnmatch.fnmatchcase(lower, p.casefold()) for p in source['select']):
                continue
            if excluded(entry.name) or any(fnmatch.fnmatchcase(lower, p.casefold())
                                           for p in source.get('exclude', [])):
                skipped.append({'source': source['path'], 'name': entry.name, 'reason': 'excluded'})
                continue
            safe_name(entry.name)
            if lower in names:
                raise FormatError(f'Conflicting output name: {entry.name}; select one source explicitly')
            names.add(lower)
            selected += 1
            resources.append((container, entry, source))
        if not selected:
            raise FormatError(f'Source selects no deployable resources: {source["path"]}')
    return root, resources, skipped


def plan_report(root, profile):
    root, resources, skipped = build_plan(root, profile)
    return {'schema': 1, 'profile': profile['id'], 'operation': 'plan',
            'runtime_recommendations': profile.get('runtime', {}),
            'resources': [{'source': source['path'], 'format': source['format'], **entry.public()}
                          for _, entry, source in resources], 'skipped': skipped,
            'limitations': ['Plan validates indexes, not payload decryption or game behavior.',
                            'Runtime recommendations require manual settings; no importer integration yet.']}


def convert(root, profile, destination, decoder=None):
    root, resources, skipped = build_plan(root, profile, decoder)
    destination = Path(destination).absolute()
    if destination.exists() or destination.is_symlink():
        raise FormatError('Output must be a new directory')
    parent = destination.parent.resolve(strict=True)
    destination = parent / destination.name
    if destination.is_relative_to(root):
        raise FormatError('Output must be outside the source game directory')
    # Build out of sight, and publish only after every selected payload validates.
    staging = Path(tempfile.mkdtemp(prefix='.translation-', dir=parent))
    records = []
    try:
        total = 0
        for container, entry, source in resources:
            data = container.read(entry)
            total += len(data)
            if total > 4 * 1024 * 1024 * 1024:
                raise FormatError('Overlay exceeds the 4 GiB conversion limit')
            # The native loader also accepts expanded BGI raw images. Preserve
            # them and identify their layout instead of recompressing pixels.
            pixel_width = int.from_bytes(data[:2], 'little') if len(data) >= 16 else 0
            pixel_height = int.from_bytes(data[2:4], 'little') if len(data) >= 16 else 0
            pixel_bpp = int.from_bytes(data[4:8], 'little') if len(data) >= 16 else 0
            raw_image = (0 < pixel_width <= 32768
                    and 0 < pixel_height <= 32768
                    and pixel_bpp in (8, 24, 32)
                    and len(data) == 16 + pixel_width * pixel_height * (pixel_bpp // 8))
            with (staging / entry.name).open('xb') as stream:
                stream.write(data)
            records.append({'name': entry.name, 'source': source['path'], 'format': source['format'],
                            'size': len(data), 'sha256': hashlib.sha256(data).hexdigest(),
                            'content_kind': 'bgi-raw-image' if raw_image else 'resource'})
        sources = []
        for spec in profile['sources']:
            path = inside(root, spec['path'])
            with path.open('rb') as stream:
                digest = hashlib.file_digest(stream, 'sha256').hexdigest()
            sources.append({'path': spec['path'], 'sha256': digest, 'size': path.stat().st_size})
        manifest = {'schema': 1, 'profile': profile['id'], 'operation': 'converted-overlay',
                    'runtime_recommendations': profile.get('runtime', {}), 'sources': sources,
                    'resources': records, 'skipped': skipped,
                    'limitations': ['Resource conversion is not a whole-game compatibility test.',
                                    'DLL hooks and mixed-encoding text are not automatically rewritten.',
                                    'Apply runtime recommendations manually.']}
        (staging / MANIFEST).write_text(json.dumps(manifest, ensure_ascii=False, indent=2) + '\n', encoding='utf-8')
        # mkdir is an exclusive reservation, unlike POSIX rename which may
        # replace an empty directory created between the check and rename.
        destination.mkdir()
        created = []
        try:
            files = sorted(staging.iterdir(), key=lambda p: p.name == MANIFEST)
            for path in files:
                target = destination / path.name
                with path.open('rb') as source_stream, target.open('xb') as target_stream:
                    created.append(target)
                    shutil.copyfileobj(source_stream, target_stream)
        except BaseException:
            for target in created:
                target.unlink(missing_ok=True)
            try:
                destination.rmdir()
            except OSError:
                pass  # A concurrently added file is never removed by cleanup.
            raise
        return manifest
    finally:
        if staging.exists():
            shutil.rmtree(staging)


def aplib_subprocess(python):
    """Use an explicitly selected Python + aPLib installation via stdio IPC.

    No decoder command can come from a game or profile. -I ignores the game's
    working directory, PYTHONPATH and user site imports.
    """
    executable = str(Path(python).resolve(strict=True))
    script = '''import sys,struct,aplib
inp=sys.stdin.buffer;out=sys.stdout.buffer;remaining=int(sys.argv[1])
while True:
    head=inp.read(4)
    if not head:break
    if len(head)!=4:raise ValueError('short frame')
    n=struct.unpack('<I',head)[0];data=inp.read(n)
    if len(data)!=n:raise ValueError('short block')
    decoded=aplib.decompress(data,max_size=min(65536,remaining))
    if not 0<len(decoded)<=min(65536,remaining):raise ValueError('block size')
    remaining-=len(decoded);out.write(decoded)
if remaining:raise ValueError('output size')
'''

    class Decoder:
        def decode_blocks(self, blocks, maximum):
            framed = b''.join(struct.pack('<I', len(block)) + block for block in blocks)
            run = subprocess.run([executable, '-I', '-c', script, str(maximum)], input=framed,
                                 stdout=subprocess.PIPE, stderr=subprocess.PIPE, timeout=30)
            if run.returncode or len(run.stdout) != maximum:
                raise FormatError('External aPLib decoder failed or exceeded its output limit')
            return run.stdout

        def __call__(self, data, maximum):
            return self.decode_blocks([data], maximum)
    return Decoder()


def inspect_game(root, depth=2):
    root = Path(root).resolve(strict=True)
    files, errors = [], []
    pending = [(root, 0)]
    while pending:
        folder, level = pending.pop()
        for path in sorted(folder.iterdir(), key=lambda p: p.name.casefold()):
            resolved = path.resolve()
            if (path.is_symlink() or not resolved.is_relative_to(root)
                    or any(part.casefold() in PROGRESS_DIRS for part in resolved.relative_to(root).parts)):
                continue
            if path.is_dir():
                if level < depth:
                    pending.append((path, level + 1))
                continue
            if len(files) >= 10000:
                raise FormatError('Inspection file limit exceeded; choose a smaller game directory')
            relative = path.relative_to(root).as_posix()
            if excluded(path.name) and path.suffix.lower() not in ('.dll', '.exe'):
                continue
            row = {'path': relative, 'size': path.stat().st_size}
            try:
                with path.open('rb') as stream:
                    magic = stream.read(64)
                if magic.startswith((b'PackFile    ', b'BURIKO ARC20')):
                    archive = BgiArc(path)
                    row.update(format=archive.format_id, entries=len(archive.entries),
                               sample=[e.name for e in archive.entries[:12]], evidence='validated-index')
                elif magic.startswith(b'EVB\0'):
                    archive = EvbExternal(path)
                    row.update(format=archive.format_id, entries=len(archive.entries),
                               sample=[e.name for e in archive.entries[:12]], evidence='validated-index')
                elif magic.startswith(b'MZ'):
                    try:
                        pe = inspect_pe(path)
                        row.update(format='pe', sha256=pe['sha256'], bits=pe['bits'],
                                   sections=pe['sections'], api_clues=pe['api_clues'],
                                   string_candidates=pe['string_candidates'],
                                   evidence=pe['evidence_kind'], limitations=pe['limitations'])
                    except ValueError as error:
                        row.update(format='pe-unresolved', pe_parse_error=str(error))
                        errors.append(relative)
                    try:
                        embedded = EvbPe(path)
                        row['embedded_evb'] = {'base_offset': embedded.base_offset,
                                               'entries': len(embedded.entries), 'evidence': 'validated-index'}
                    except FormatError as error:
                        row['embedded_evb_scan'] = str(error)
                elif path.suffix.lower() == '.cpk' and path.with_suffix('.key').is_file():
                    archive = READERS['cpk-v2'](path, path.with_suffix('.key'))
                    row.update(format=archive.format_id, entries=len(archive.entries),
                               sample=[e.name for e in archive.entries[:12]], evidence='validated-index-key-pair')
                else:
                    row.update(format='unclassified', magic_hex=magic[:16].hex())
            except (ValueError, OSError, UnicodeError, struct.error) as error:
                row.update(format='unresolved', error=str(error))
                errors.append(relative)
            files.append(row)
    return {'schema': 1, 'operation': 'offline-inspection', 'files': files, 'unresolved': errors,
            'limitations': ['No executable or DLL was run.',
                            'Recognized resources do not prove the DLL is fully replaced.',
                            f'Inspection depth is {depth}; symlinks and progress directories are skipped.']}
