#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-only
"""Build the sample game for the Android host API prototype."""

import io
import math
import struct
import sys
import wave
from zipfile import ZipFile


def boot_program():
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

    def background(colour):
        push(0)
        push(colour)
        emit(0x90, 0x13)  # Clear bitmap 0.
        emit(0, 0, 0x90, 0x40)  # Use bitmap 0 as the background.
        emit(0, 1, 0x90, 0x00)  # Request a full present.

    def wait_button(label, pressed):
        labels[label] = len(code)
        emit(1, 16, 0, 0x80, 0x58, 0x80, 0x5A)  # Yield for 16 ms.
        emit(0, 1, 0x80, 0x11)  # Read VK_LBUTTON.
        reference(6, label)
        emit(0x15, 0 if pressed else 1)

    reference(5, "ready")
    emit(0xB0, 0x80)  # Report successful boot to the host.
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
    reference(6, "wait_release")  # Discard the result; both branches continue here.
    emit(0x15, 0)
    wait_button("wait_release", pressed=True)
    reference(6, "idle")
    emit(0x14)

    labels["ready"] = len(code)
    code.extend(b"OpenBGI host spike ready\0")
    labels["tone"] = len(code)
    code.extend(b"tone.wav\0")
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
    with ZipFile(sys.argv[1], "w") as archive:
        archive.writestr("ipl._bp", boot_program())
        archive.writestr("tone.wav", tone())

