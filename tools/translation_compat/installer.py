# SPDX-License-Identifier: GPL-2.0-only
"""Turn an installer's embedded files into a game-relative overlay ZIP."""

import fnmatch
import json
import os
from pathlib import Path
import shutil
import subprocess
import tempfile
import zipfile

from .formats import FormatError
from .pipeline import PROGRESS_DIRS, excluded

MANIFEST = ".openbgi/patch.json"
MAX_MANIFEST = 128 * 1024
WINDOWS_FILES = {".bat", ".cmd", ".com", ".msi", ".msp", ".ps1", ".scr", ".vbs"}
DELTA_FILES = {".xdelta", ".xdelta3", ".vcdiff", ".bps", ".ips", ".bsdiff"}


def relative_parts(name):
    parts = name.replace("\\", "/").split("/")
    if any(not part or part in (".", "..") or part.endswith((" ", "."))
           or ":" in part or any(ord(c) < 32 for c in part) for part in parts):
        raise FormatError(f"Invalid relative path: {name!r}")
    return parts


def extractor(format_name, override):
    names = ("7z", "7zz", "7za") if format_name == "nsis" else ("innoextract",)
    if override:
        candidates = (override,)
    else:
        candidates = names
        if os.name == "nt" and format_name == "nsis":
            candidates += (str(Path(os.environ.get("ProgramFiles", r"C:\Program Files")) / "7-Zip" / "7z.exe"),)
    for candidate in candidates:
        program = shutil.which(candidate)
        if program:
            return str(Path(program).resolve())
    raise FormatError(f"找不到 {names[0]}，请安装后加入 PATH 或用 --extractor 指定路径")


def run(args, cwd):
    return subprocess.run(args, cwd=cwd, stdin=subprocess.DEVNULL, stdout=subprocess.PIPE,
                          stderr=subprocess.STDOUT, encoding="utf-8", errors="replace")


def extraction_error(result, format_name):
    lines = [line.strip() for line in result.stdout.splitlines() if line.strip()]
    detail = lines[-1] if lines else f"exit {result.returncode}"
    return FormatError(f"{format_name.upper()} 解包失败：{detail}")


def extract_nsis(program, source, output):
    options = ["-tNSIS", "-sccUTF-8", "--", str(source)]
    listing = run([program, "l", "-slt", *options], output)
    if listing.returncode:
        raise extraction_error(listing, "nsis")
    _, separator, entries = listing.stdout.partition("\n----------\n")
    if not separator:
        raise FormatError("无法读取 NSIS 文件清单")
    seen = set()
    for line in entries.splitlines():
        if not line.startswith("Path = "):
            continue
        name = "/".join(relative_parts(line[len("Path = "):])).lower()
        if name in seen:
            raise FormatError(f"NSIS 含同名文件：{name}")
        seen.add(name)
    result = run([program, "x", "-y", f"-o{output}", *options], output)
    if result.returncode:
        raise extraction_error(result, "nsis")


def extract_inno(program, source, output):
    result = run([program, "--extract", "--exclude-temp", "--collisions", "error",
                  "--no-extract-unknown", "--output-dir", str(output), "--", str(source)], output)
    if result.returncode:
        raise extraction_error(result, "inno")


def extract(source, work, format_name, override):
    if override and format_name == "auto":
        raise FormatError("--extractor 需要同时指定 --format")
    errors = []
    for name in ("nsis", "inno") if format_name == "auto" else (format_name,):
        output = work / name
        output.mkdir()
        try:
            program = extractor(name, override)
            extractor_fn = extract_nsis if name == "nsis" else extract_inno
            extractor_fn(program, source, output)
            return name, output
        except FormatError as error:
            errors.append(str(error))
    raise FormatError("\n".join(errors))


def overlay_root(extracted, kind, root):
    if root == ".":
        return extracted
    if root is None:
        implicit = extracted / ("$INSTDIR" if kind == "nsis" else "app")
        if implicit.is_dir():
            return implicit
        if kind == "nsis":
            return extracted
        raise FormatError("Inno 安装器没有 {app} 目录，请用 --root 指定资源目录")
    path = extracted.joinpath(*relative_parts(root)).resolve(strict=True)
    if not path.is_relative_to(extracted) or not path.is_dir():
        raise FormatError("--root 必须是解包目录中的子目录")
    return path


def select_files(root, include, exclude):
    files = {}
    include = [pattern.lower() for pattern in include]
    exclude = [pattern.lower() for pattern in exclude]
    ignored = PROGRESS_DIRS | {".openbgi", "__macosx"}
    for folder, dirs, names in os.walk(root):
        folder = Path(folder)
        dirs[:] = [name for name in dirs if name.lower() not in ignored and not name.startswith("$")]
        for name in names:
            path = folder / name
            if path.is_symlink() or not path.is_file():
                continue
            rel = path.relative_to(root).as_posix()
            key = rel.lower()
            suffix = path.suffix.lower()
            if (name.startswith("$") or excluded(name) or suffix in WINDOWS_FILES
                    or any(fnmatch.fnmatchcase(key, pattern) for pattern in exclude)
                    or include and not any(fnmatch.fnmatchcase(key, pattern) for pattern in include)):
                continue
            if suffix in DELTA_FILES:
                raise FormatError(f"{rel} 是差分文件，请改用已生成的完整资源，或用 --exclude 跳过该文件")
            relative_parts(rel)
            if key in files:
                raise FormatError(f"覆盖包文件名冲突：{rel}")
            files[key] = path
    if not files:
        raise FormatError("没有可打包的资源，请检查 --root 和 --select")
    for key in files:
        parent = key.rpartition("/")[0]
        while parent:
            if parent in files:
                raise FormatError(f"覆盖包文件和目录同名：{parent}")
            parent = parent.rpartition("/")[0]
    return files


def read_manifest(path):
    if not path:
        return None
    if path.stat().st_size > MAX_MANIFEST:
        raise FormatError("补丁清单超过 128 KiB")
    manifest = json.loads(path.read_text(encoding="utf-8-sig"))
    if not isinstance(manifest, dict):
        raise FormatError("补丁清单必须是 JSON 对象")
    return manifest


def write_overlay(staged, files, manifest):
    with zipfile.ZipFile(staged, "x") as archive:
        for name, path in sorted(files.items()):
            info = zipfile.ZipInfo(name)
            info.file_size = path.stat().st_size
            info.compress_type = zipfile.ZIP_STORED if info.file_size > 32 * 1024 ** 2 else zipfile.ZIP_DEFLATED
            with path.open("rb") as source, archive.open(info, "w", force_zip64=info.file_size >= zipfile.ZIP64_LIMIT) as output:
                shutil.copyfileobj(source, output, length=256 * 1024)
        if manifest is not None:
            data = (json.dumps(manifest, ensure_ascii=False) + "\n").encode("utf-8")
            if len(data) > MAX_MANIFEST:
                raise FormatError("生成的补丁清单超过 128 KiB")
            archive.writestr(zipfile.ZipInfo(MANIFEST), data, compress_type=zipfile.ZIP_DEFLATED)


def convert_installer(source, destination, format_name="auto", unpacker=None, root=None,
                      include=(), exclude=(), manifest=None):
    source = source.resolve(strict=True)
    if not source.is_file():
        raise FormatError("请选择安装器文件")
    destination = destination.absolute()
    if destination.suffix.lower() != ".zip" or destination.exists() or destination.is_symlink():
        raise FormatError("输出必须是尚不存在的 .zip 文件")
    destination = destination.parent.resolve(strict=True) / destination.name
    metadata = read_manifest(manifest)
    with tempfile.TemporaryDirectory(prefix=".openbgi-installer-", dir=destination.parent) as directory:
        work = Path(directory)
        kind, unpacked = extract(source, work, format_name, unpacker)
        files = select_files(overlay_root(unpacked, kind, root), include, exclude)
        staged = work / "overlay.zip"
        write_overlay(staged, files, metadata)
        if os.name == "nt":
            os.rename(staged, destination)
        else:
            os.link(staged, destination)
    return kind, len(files)
