# This file is part of OpenBGI (https://openbgi.net).
# SPDX-License-Identifier: GPL-2.0-only

"""Bounded, standard-library-only PE static inspection; never executes a target.

Static imports and string candidates are clues, not proof of compatibility or
hook behavior. Invalid file-backed ranges and unterminated tables are rejected.
"""
import hashlib
import re
import struct
from pathlib import Path

MAX_FILE_SIZE = 256 * 1024 * 1024
MAX_SECTIONS = 96
MAX_IMPORTS = 512
MAX_THUNKS = 8192
MAX_EXPORTS = 65536
MAX_STRING = 4096


class PEFormatError(ValueError):
    pass


def inspect_pe(path) -> dict:
    path = Path(path)
    size = path.stat().st_size
    if size < 64 or size > MAX_FILE_SIZE:
        raise PEFormatError('PE size outside supported bounds')
    data = path.read_bytes()
    if len(data) != size:
        raise PEFormatError('PE changed while reading')

    def span(offset, length):
        if offset < 0 or length < 0 or offset + length > len(data):
            raise PEFormatError('Truncated or out-of-bounds PE range')
        return data[offset:offset + length]

    def unpack(fmt, offset):
        return struct.unpack(fmt, span(offset, struct.calcsize(fmt)))

    if span(0, 2) != b'MZ':
        raise PEFormatError('Missing DOS signature')
    pe_offset = unpack('<I', 60)[0]
    if span(pe_offset, 4) != b'PE\0\0':
        raise PEFormatError('Missing PE signature')
    machine, count, timestamp, _, _, optional_size, characteristics = unpack('<HHIIIHH', pe_offset + 4)
    if not 1 <= count <= MAX_SECTIONS:
        raise PEFormatError('Unsupported section count')
    optional = pe_offset + 24
    span(optional, optional_size)
    magic = unpack('<H', optional)[0]
    if magic == 0x10b:
        bits, width, dir_count_offset, dir_start = 32, 4, 92, 96
        imagebase = unpack('<I', optional + 28)[0]
    elif magic == 0x20b:
        bits, width, dir_count_offset, dir_start = 64, 8, 108, 112
        imagebase = unpack('<Q', optional + 24)[0]
    else:
        raise PEFormatError('Unsupported optional-header magic')
    if optional_size < dir_start:
        raise PEFormatError('Truncated optional header')
    directory_count = unpack('<I', optional + dir_count_offset)[0]
    if directory_count > 128 or dir_start + directory_count * 8 > optional_size:
        raise PEFormatError('Invalid data-directory count')
    header_size = unpack('<I', optional + 60)[0]
    if header_size > size or header_size < optional + optional_size + 40 * count:
        raise PEFormatError('Invalid header size')
    directories = [unpack('<II', optional + dir_start + i * 8) for i in range(directory_count)]
    sections = []
    for i in range(count):
        off = optional + optional_size + i * 40
        name, virtual_size, rva, raw_size, raw_offset, _, _, _, _, flags = unpack('<8sIIIIIIHHI', off)
        if raw_size:
            span(raw_offset, raw_size)
        sections.append({'name': name.rstrip(b'\0').decode('ascii', 'replace'),
                         'rva': rva, 'virtual_size': virtual_size,
                         'raw_offset': raw_offset, 'raw_size': raw_size,
                         'characteristics': flags})
    backed = sorted((s['rva'], s['rva'] + s['raw_size']) for s in sections if s['raw_size'])
    if any(a[1] > b[0] for a, b in zip(backed, backed[1:])):
        raise PEFormatError('Ambiguous overlapping section RVAs')

    def mapping(rva, length=1):
        if rva < header_size and rva + length <= header_size:
            return rva, header_size - rva
        for section in sections:
            delta = rva - section['rva']
            if 0 <= delta and delta + length <= section['raw_size']:
                return section['raw_offset'] + delta, section['raw_size'] - delta
        raise PEFormatError('RVA has no complete file-backed range')

    def at(rva, length):
        return span(mapping(rva, length)[0], length)

    def cstring(rva):
        offset, available = mapping(rva)
        raw = span(offset, min(available, MAX_STRING + 1))
        end = raw.find(b'\0')
        if end < 0 or end > MAX_STRING:
            raise PEFormatError('Unterminated or oversized PE string')
        try:
            return raw[:end].decode('ascii')
        except UnicodeDecodeError as error:
            raise PEFormatError('Non-ASCII PE symbol/module name') from error

    def directory(index):
        if index >= len(directories):
            return (0, 0)
        rva, length = directories[index]
        if bool(rva) != bool(length):
            raise PEFormatError('Incomplete PE data directory')
        return rva, length

    imports = []
    rva, length = directory(1)
    if rva:
        if length < 20:
            raise PEFormatError('Truncated import directory')
        terminated = False
        for i in range(min(length // 20, MAX_IMPORTS + 1)):
            original, stamp, forward, module, first = struct.unpack('<IIIII', at(rva + i * 20, 20))
            if not any((original, stamp, forward, module, first)):
                terminated = True
                break
            if i >= MAX_IMPORTS or not module or not (original or first):
                raise PEFormatError('Invalid or excessive import descriptors')
            symbols = []
            thunk = original or first
            for j in range(MAX_THUNKS + 1):
                value = int.from_bytes(at(thunk + j * width, width), 'little')
                if value == 0:
                    break
                if j == MAX_THUNKS:
                    raise PEFormatError('Excessive import thunk count')
                if value & (1 << (bits - 1)):
                    symbols.append({'ordinal': value & 0xffff})
                else:
                    hint = struct.unpack('<H', at(value, 2))[0]
                    symbols.append({'name': cstring(value + 2), 'hint': hint})
            imports.append({'module': cstring(module), 'symbols': symbols})
        if not terminated:
            raise PEFormatError('Unterminated or excessive import directory')

    exports = []
    export_module = None
    rva, length = directory(0)
    if rva:
        if length < 40:
            raise PEFormatError('Truncated export directory')
        fields = struct.unpack('<IIHHIIIIIII', at(rva, 40))
        _, _, _, _, module, base, functions, names, function_table, name_table, ordinal_table = fields
        if functions > MAX_EXPORTS or names > functions:
            raise PEFormatError('Invalid or excessive export count')
        export_module = cstring(module) if module else None
        function_values = struct.unpack('<' + str(functions) + 'I', at(function_table, functions * 4)) if functions else ()
        names_by_slot = {}
        for i in range(names):
            ptr = struct.unpack('<I', at(name_table + i * 4, 4))[0]
            slot = struct.unpack('<H', at(ordinal_table + i * 2, 2))[0]
            if slot >= functions:
                raise PEFormatError('Export name ordinal exceeds function table')
            names_by_slot.setdefault(slot, []).append(cstring(ptr))
        for slot, address in enumerate(function_values):
            if address == 0:
                continue
            row = {'ordinal': base + slot, 'rva': address, 'names': names_by_slot.get(slot, [])}
            if rva <= address < rva + length:
                row['forwarder'] = cstring(address)
            exports.append(row)

    candidates = []
    pattern = re.compile(r'(?i)(?:[\w. ~+*()/-]{1,160}\.(?:arc|pck|cpk|key|klt|chs|dll|ttf|otf|dat|txt)\b|(?:CreateFont|GetGlyphOutline|TextOut|MultiByteToWideChar|WideCharToMultiByte|Detour|MinHook|GB2312|SHIFT_JIS|CP932|CP936)\w{0,128})')
    for match in re.finditer(rb'[\x20-\x7e]{4,512}', data):
        value = match.group().decode('ascii')
        for found in pattern.finditer(value):
            candidates.append({'file_offset': match.start() + found.start(), 'encoding': 'ascii', 'value': found.group()[:512], 'evidence_kind': 'static_string_candidate'})
            if len(candidates) >= 4096:
                break
        if len(candidates) >= 4096:
            break
    for font in ('微软雅黑', '黑体', '宋体', 'ＭＳ ゴシック', 'MS Gothic', 'SimHei', 'SimSun', 'Microsoft YaHei', 'GalHei'):
        for encoding in ('utf-16-le', 'utf-8', 'gbk', 'cp932'):
            try:
                needle = font.encode(encoding)
            except UnicodeEncodeError:
                continue
            start = 0
            for _ in range(16):
                offset = data.find(needle, start)
                if offset < 0:
                    break
                candidates.append({'file_offset': offset, 'encoding': encoding, 'value': font, 'evidence_kind': 'static_font_bytes_candidate'})
                start = offset + len(needle)
    interesting = {'createfonta', 'createfontw', 'createfontindirecta', 'createfontindirectw', 'textouta', 'textoutw', 'getglyphoutlinea', 'getglyphoutlinew', 'multibytetowidechar', 'widechartomultibyte', 'loadlibrarya', 'loadlibraryw', 'getprocaddress', 'createfilea', 'createfilew', 'readfile', 'setfilepointer', 'virtualprotect', 'writeprocessmemory', 'createremotethread', 'createprocessa', 'createprocessw'}
    clues = [{'module': row['module'], 'name': symbol['name'], 'evidence_kind': 'static_import'} for row in imports for symbol in row['symbols'] if symbol.get('name', '').lower() in interesting]
    return {'path': str(path.resolve()), 'size': size, 'sha256': hashlib.sha256(data).hexdigest(),
            'evidence_kind': 'offline_pe_static_parse', 'parser': 'stdlib-pe-probe-v1',
            'bits': bits, 'machine': machine, 'imagebase': imagebase, 'timestamp': timestamp,
            'characteristics': characteristics, 'entrypoint_rva': unpack('<I', optional + 16)[0],
            'sections': sections, 'imports': imports, 'export_module': export_module, 'exports': exports,
            'api_clues': clues, 'string_candidates': candidates,
            'limitations': ['No target execution or compatibility verdict.', 'Imports and strings do not establish reachable behavior.', 'Packed/encrypted/dynamic imports may remain unknown.', 'Delay imports, bound imports and resources are not decoded.', 'String candidates are capped; 512-byte windows can split names at boundaries.']}


if __name__ == '__main__':
    import argparse
    import json
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('path')
    args = parser.parse_args()
    print(json.dumps(inspect_pe(args.path), ensure_ascii=True, indent=2))
