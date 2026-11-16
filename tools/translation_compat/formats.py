# This file is part of OpenBGI (https://openbgi.net).
# SPDX-License-Identifier: GPL-2.0-only

"""Bounded readers for shared translation container formats.

Adapters expose the same entries/read interface. Stored BGI compressed streams
remain compressed: OpenBGI already decodes these after reading loose resources.
"""
from dataclasses import dataclass, field
from pathlib import Path
import mmap
import struct
import zlib

MAX_ENTRIES = 100000
MAX_RESOURCE = 256 * 1024 * 1024
MAX_INDEX = 32 * 1024 * 1024


class FormatError(ValueError):
    pass


def exact(stream, size):
    data = stream.read(size)
    if len(data) != size:
        raise FormatError("Truncated container")
    return data


def safe_name(name):
    # Every output is one loose resource, with portable Windows/POSIX naming.
    if (not name or name in (".", "..") or len(name) > 255
            or any(ord(c) < 32 or c in '/\\:<>"|?*' for c in name)
            or name.endswith((" ", "."))
            or name.split('.')[0].upper() in {
                'CON', 'PRN', 'AUX', 'NUL',
                *(f'COM{i}' for i in range(1, 10)),
                *(f'LPT{i}' for i in range(1, 10))}):
        raise FormatError(f"Unsafe resource name: {name!r}")
    return name


@dataclass
class Entry:
    name: str
    offset: int
    stored: int
    size: int
    codec: str = "stored"
    private: dict = field(default_factory=dict, repr=False)

    def public(self):
        return {k: getattr(self, k) for k in ('name', 'offset', 'stored', 'size', 'codec')}


class Container:
    def __init__(self, path, max_size=MAX_RESOURCE):
        self.path = Path(path)
        self.limit = max_size
        self.file_size = self.path.stat().st_size
        self.entries = []

    def validate(self):
        names = set()
        ranges = []
        for e in self.entries:
            safe_name(e.name)
            if e.name.casefold() in names:
                raise FormatError(f"Duplicate resource: {e.name}")
            names.add(e.name.casefold())
            if (e.offset < 0 or min(e.size, e.stored) < 0
                    or max(e.size, e.stored) > self.limit
                    or e.offset + e.stored > self.file_size):
                raise FormatError(f"Invalid resource range/size: {e.name}")
            if e.stored:
                ranges.append((e.offset, e.offset + e.stored))
        ranges.sort()
        if any(a[1] > b[0] for a, b in zip(ranges, ranges[1:])):
            raise FormatError("Overlapping container payloads")

    def stored_bytes(self, e):
        with self.path.open('rb') as stream:
            stream.seek(e.offset)
            return exact(stream, e.stored)

    def read(self, e):
        return self.stored_bytes(e)


class BgiArc(Container):
    """Both PackFile and ARC20, regardless of archive filename/extension."""
    format_id = 'bgi-arc'

    def __init__(self, path, max_size=MAX_RESOURCE):
        super().__init__(path, max_size)
        with self.path.open('rb') as stream:
            header = exact(stream, 16)
            if header[:12] not in (b'PackFile    ', b'BURIKO ARC20'):
                raise FormatError("Not a BGI archive")
            count = struct.unpack_from('<I', header, 12)[0]
            record_size, name_size = (128, 96) if header[:12] == b'BURIKO ARC20' else (32, 16)
            if count > MAX_ENTRIES or count * record_size > MAX_INDEX:
                raise FormatError("BGI index too large")
            base = 16 + count * record_size
            index = exact(stream, count * record_size)
        for at in range(0, len(index), record_size):
            record = index[at:at + record_size]
            try:
                name = record[:name_size].split(b'\0')[0].decode('cp932')
            except UnicodeError as error:
                raise FormatError("Invalid BGI resource name") from error
            offset, size = struct.unpack_from('<II', record, name_size)
            self.entries.append(Entry(name, base + offset, size, size))
        self.validate()


class CpkV2(Container):
    """The Camellia/EAX + zlib loader family, with caller-supplied sidecar key."""
    format_id = 'cpk-v2'

    def __init__(self, path, key, max_size=MAX_RESOURCE):
        super().__init__(path, max_size)
        key_path = Path(key)
        if key_path.stat().st_size > 40 + MAX_ENTRIES * 72:
            raise FormatError("CPK key too large")
        raw = key_path.read_bytes()
        if len(raw) < 40:
            raise FormatError("Truncated CPK key header")
        header = bytes(v ^ 0x13 for v in raw[:40])
        magic, count = struct.unpack_from('<4sI', header)
        if magic != b'CPK\0' or count > MAX_ENTRIES or len(raw) != 40 + count * 72:
            raise FormatError("Invalid CPK key header/count")
        with self.path.open('rb') as stream:
            archive_header = exact(stream, 140)
            index = exact(stream, count * 44)
        archive_header = bytes(v ^ header[8 + i % 32] for i, v in enumerate(archive_header))
        if (archive_header[:8] != b'CPK\0\x02\0\0\0'
                or struct.unpack_from('<I', archive_header, 136)[0] != count):
            raise FormatError("CPK/key mismatch")
        base = 140 + count * 44
        for i in range(count):
            record = bytes(v ^ 0x13 for v in raw[40 + i * 72:40 + (i + 1) * 72])
            data = bytes(v ^ record[j % 32] for j, v in enumerate(index[i * 44:(i + 1) * 44]))
            try:
                name = data[:32].split(b'\0')[0].decode('ascii')
            except UnicodeError as error:
                raise FormatError("Invalid CPK resource name") from error
            size, packed, offset = struct.unpack_from('<III', data, 32)
            if struct.unpack_from('<II', record, 32) != (16, 16):
                raise FormatError("Unsupported CPK key/nonce size")
            self.entries.append(Entry(name, base + offset, packed, size, 'camellia-eax-zlib',
                                      {'key': record[40:56], 'nonce': record[56:72]}))
        self.validate()

    def read(self, e):
        try:
            from cryptography.hazmat.primitives.ciphers import Cipher, modes
            from cryptography.hazmat.primitives.cmac import CMAC
            try:
                from cryptography.hazmat.decrepit.ciphers.algorithms import Camellia
            except ImportError:
                from cryptography.hazmat.primitives.ciphers.algorithms import Camellia
        except ImportError as error:
            raise FormatError("CPK conversion requires the optional cryptography package") from error
        mac = CMAC(Camellia(e.private['key']))
        mac.update(bytes(16) + e.private['nonce'])
        counter = int.from_bytes(mac.finalize(), 'big')
        cipher = Cipher(Camellia(e.private['key']), modes.ECB()).encryptor()
        decoder = zlib.decompressobj()
        out = bytearray()
        with self.path.open('rb') as stream:
            stream.seek(e.offset)
            remaining = e.stored
            while remaining:
                data = exact(stream, min(65536, remaining))
                blocks = (len(data) + 15) // 16
                mask = cipher.update(b''.join(((counter + j) % (1 << 128)).to_bytes(16, 'big')
                                             for j in range(blocks)))
                counter = (counter + blocks) % (1 << 128)
                plain = bytes(a ^ b for a, b in zip(data, mask))
                try:
                    out += decoder.decompress(plain, e.size + 1 - len(out))
                except zlib.error as error:
                    raise FormatError("Invalid CPK compressed payload") from error
                if len(out) > e.size or decoder.unconsumed_tail:
                    raise FormatError("CPK output exceeds declared size")
                remaining -= len(data)
        cipher.finalize()
        if not decoder.eof or decoder.unused_data or len(out) != e.size:
            raise FormatError("Invalid CPK stream/declared size")
        return bytes(out)


class EvbExternal(Container):
    """External EVB file table; compressed blocks use an explicit external decoder.

    The decoder is a caller-provided function(bytes, max_size)->bytes, keeping
    optional decoder dependencies separate from the engine and this reader.
    """
    format_id = 'evb-external'

    def __init__(self, path, decoder=None, max_size=MAX_RESOURCE, base_offset=0):
        super().__init__(path, max_size)
        self.decoder = decoder
        self.base_offset = base_offset
        with self.path.open('rb') as stream:
            if not 0 <= base_offset <= self.file_size - 80:
                raise FormatError("Invalid EVB base offset")
            stream.seek(base_offset)
            if exact(stream, 64)[:4] != b'EVB\0':
                raise FormatError("Not an external EVB container")
            main = exact(stream, 16)
            table_size = struct.unpack_from('<I', main)[0]
            table_end = payload = base_offset + table_size + 68
            if not 80 <= table_size + 68 <= min(self.file_size - base_offset, MAX_INDEX):
                raise FormatError("Invalid EVB table size")
            stream.seek(base_offset + 79)
            pending = 1
            nodes = 0
            while pending:
                if stream.tell() + 16 > table_end or nodes >= MAX_ENTRIES:
                    raise FormatError("Truncated/oversized EVB table")
                header = exact(stream, 16)
                children = struct.unpack_from('<I', header, 12)[0]
                name = bytearray()
                while stream.tell() + 2 <= table_end and len(name) <= 8192:
                    char = exact(stream, 2)
                    if char == b'\0\0':
                        break
                    name += char
                else:
                    raise FormatError("Unterminated EVB name")
                try:
                    name = name.decode('utf-16-le')
                except UnicodeError as error:
                    raise FormatError("Invalid EVB name") from error
                kind = exact(stream, 1)[0]
                pending -= 1
                nodes += 1
                if kind == 3:
                    # Flatten only the virtual default root. Nested paths need an
                    # explicit adapter so a directory cannot silently shadow root.
                    if nodes != 1 or name not in ('%DEFAULT FOLDER%', ''):
                        raise FormatError("EVB nested folders require a separate layout adapter")
                    exact(stream, 25)
                    pending += children
                elif kind == 2:
                    meta = exact(stream, 53)
                    size = struct.unpack_from('<I', meta, 2)[0]
                    packed = struct.unpack_from('<I', meta, 49)[0]
                    self.entries.append(Entry(name, payload, packed, size,
                                              'aplib-blocks' if size != packed else 'stored'))
                    payload += packed
                else:
                    raise FormatError(f"Unsupported EVB node type {kind}")
                if pending > MAX_ENTRIES or stream.tell() > table_end:
                    raise FormatError("Invalid EVB node count/table boundary")
            padding = table_end - stream.tell()
            if padding not in (0, 4) or exact(stream, padding) != bytes(padding):
                raise FormatError("EVB directory count does not consume the complete table")
        self.validate()

    def read(self, e):
        data = self.stored_bytes(e)
        if e.codec == 'stored':
            return data
        if not self.decoder:
            raise FormatError("Compressed EVB requires an explicitly selected aPLib decoder")
        if len(data) < 12:
            raise FormatError("Truncated EVB block table")
        table = struct.unpack_from('<I', data)[0]
        if not 12 <= table <= len(data) or (table - 8) % 4:
            raise FormatError("Invalid EVB block table")
        sizes = [struct.unpack_from('<I', data, p)[0] for p in range(8, table, 12)]
        if not sizes or any(not n for n in sizes) or sum(sizes) != len(data) - table:
            raise FormatError("EVB block sizes mismatch")
        at = table
        if hasattr(self.decoder, 'decode_blocks'):
            blocks = []
            for size in sizes:
                blocks.append(data[at:at + size])
                at += size
            decoded = self.decoder.decode_blocks(blocks, e.size)
            if not isinstance(decoded, bytes) or len(decoded) != e.size:
                raise FormatError("EVB decoded length mismatch")
            return decoded
        out = bytearray()
        for size in sizes:
            decoded = self.decoder(data[at:at + size], min(65536, e.size - len(out)))
            if not isinstance(decoded, bytes) or not 0 < len(decoded) <= min(65536, e.size - len(out)):
                raise FormatError("Invalid EVB decoded block size")
            out += decoded
            at += size
        if len(out) != e.size:
            raise FormatError("EVB decoded length mismatch")
        return bytes(out)


class EvbPe(EvbExternal):
    """Find a uniquely valid external-layout EVB inside a PE, without loading it."""
    format_id = 'evb-pe'

    def __init__(self, path, decoder=None, max_size=MAX_RESOURCE, base_offset=None):
        path = Path(path)
        size = path.stat().st_size
        if not 64 <= size <= 256 * 1024 * 1024:
            raise FormatError('PE carrier outside embedded scan bounds')
        with path.open('rb') as stream:
            header = exact(stream, 64)
            pe_offset = struct.unpack_from('<I', header, 60)[0]
            if header[:2] != b'MZ' or pe_offset + 4 > size:
                raise FormatError('Not a PE carrier')
            stream.seek(pe_offset)
            if exact(stream, 4) != b'PE\0\0':
                raise FormatError('Not a PE carrier')
            if base_offset is not None:
                chosen = EvbExternal(path, decoder, max_size, base_offset)
            else:
                valid = []
                with mmap.mmap(stream.fileno(), 0, access=mmap.ACCESS_READ) as view:
                    at, candidates = 0, 0
                    while True:
                        at = view.find(b'EVB\0', at)
                        if at < 0:
                            break
                        candidates += 1
                        if candidates > 128:
                            raise FormatError('Too many embedded EVB candidates; specify base_offset')
                        try:
                            candidate = EvbExternal(path, decoder, max_size, at)
                            if candidate.entries:
                                valid.append(candidate)
                        except (FormatError, UnicodeError, struct.error):
                            pass
                        at += 4
                if len(valid) != 1:
                    raise FormatError(f'Expected one valid embedded EVB, found {len(valid)}; specify base_offset')
                chosen = valid[0]
        self.__dict__.update(chosen.__dict__)


READERS = {cls.format_id: cls for cls in (BgiArc, CpkV2, EvbExternal, EvbPe)}
