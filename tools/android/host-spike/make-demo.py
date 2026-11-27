#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-only
"""Build the sample game for the Android host API prototype."""

import argparse
import io
import math
import struct
import wave
from zipfile import ZipFile, ZipInfo


def boot_program(confirm=False, text_encoding=None, pause_test=False, sample="Wi あ中", archive_test=False):
    code = bytearray()
    labels = {}
    references = []

    def emit(*data):
        code.extend(data)

    def push(value):
        emit(2, *struct.pack("<I", value))

    def reference(opcode, label):
        references.append((len(code), label))
        emit(opcode, 0, 0)

    def discard_result():
        label = f"next_{len(code)}"
        reference(6, label)
        emit(0x15, 0)  # Both branches proceed to the next instruction.
        labels[label] = len(code)

    def background(colour):
        push(0)
        push(colour)
        emit(0x90, 0x13)  # Clear bitmap 0.
        if text_encoding:
            for value in (0, 100, 100):
                push(value)
            reference(5, "sample")
            for value in (0, 64, 100, 0, 0, 0xFFFFFF):
                push(value)
            emit(0x92, 0x1C)  # Draw text onto bitmap 0.
            discard_result()
        emit(0, 0, 0x90, 0x40)  # Use bitmap 0 as the background.
        emit(0, 1, 0x90, 0x00)  # Request a full present.

    def wait_button(label, pressed):
        labels[label] = len(code)
        emit(1, 16, 0, 0x80, 0x58, 0x80, 0x5A)  # Yield for 16 ms.
        emit(0, 1, 0x80, 0x11)  # Read VK_LBUTTON.
        reference(6, label)
        emit(0x15, 0 if pressed else 1)

    if archive_test:
        reference(5, "archive_names")
        reference(5, "archive_count")
        reference(5, "archive_path")
        emit(0x81, 0x39)
        discard_result()
    reference(5, "archive_names" if archive_test else "ready")
    if confirm:
        emit(0, 0, 0xB0, 0x81)  # Ask yes/no, default No.
        reference(6, "declined")
        emit(0x15, 1)
    else:
        emit(0xB0, 0x80)  # Report successful boot to the host.
    if text_encoding == "utf8":
        push(1)
        emit(0x81, 0x00)  # Select UTF-8 script text.
        discard_result()
    for value in (0, 800, 600, 1):
        push(value)
    emit(0x90, 0x11)  # Allocate an RGB32 bitmap.
    emit(0, 1, 0, 1, 0x90, 0x4C)  # Show the background.
    emit(0, 1, 0x80, 0x64)  # Show the game window.
    labels["idle"] = len(code)
    background(0x245584)
    wait_button("wait_press", pressed=False)
    background(0x387858)
    reference(5, "tone")
    emit(0xA0, 0xC0)  # Play the bundled WAV.
    discard_result()
    if pause_test:
        for value in (300, 0, 0):
            push(value)
        emit(0x80, 0x5C)  # Wait 300 ms, even when a key is pressed.
        discard_result()
    wait_button("wait_release", pressed=True)
    reference(6, "idle")
    emit(0x14)

    if confirm:
        labels["declined"] = len(code)
        emit(0x80, 0x45)
    labels["ready"] = len(code)
    code.extend(("テストを開始しますか？".encode("cp932") if confirm else b"OpenBGI host spike ready") + b"\0")
    labels["tone"] = len(code)
    code.extend(b"tone.wav\0")
    if text_encoding:
        labels["sample"] = len(code)
        code.extend(sample.encode(text_encoding) + b"\0")
    if archive_test:
        labels["archive_path"] = len(code)
        code.extend(b"names.arc\0")
        labels["archive_count"] = len(code)
        code.extend(bytes(4))
        labels["archive_names"] = len(code)
        code.extend(bytes(97))
    for place, label in references:
        struct.pack_into("<h", code, place + 1, labels[label] - place)
    return struct.pack("<IIII", 16, len(code) + 64, 0, 0) + code + bytes(64)


def tone():
    output = io.BytesIO()
    rate = 44100
    length = rate // 4
    samples = bytearray()
    for t in range(length):
        fade = min(t, length - t, 256) / 256
        value = int(5000 * fade * math.sin(2 * math.pi * 440 * t / rate))
        samples.extend(struct.pack("<h", value))
    with wave.open(output, "wb") as audio:
        audio.setparams((1, 2, rate, 0, "NONE", "not compressed"))
        audio.writeframes(samples)
    return output.getvalue()


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("output")
    parser.add_argument("--confirm", action="store_true")
    parser.add_argument("--text", choices=("cp932", "utf8", "gbk", "big5"))
    parser.add_argument("--sample", default="Wi あ中")
    parser.add_argument("--pause-test", action="store_true")
    parser.add_argument("--archive-test", action="store_true")
    args = parser.parse_args()
    with ZipFile(args.output, "w") as archive:
        archive.writestr(ZipInfo("ipl._bp"), boot_program(args.confirm, args.text, args.pause_test, args.sample, args.archive_test))
        archive.writestr(ZipInfo("tone.wav"), tone())

