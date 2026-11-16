/* SPDX-License-Identifier: GPL-2.0-only */
package org.openbgi.port;

import java.io.IOException;
import java.util.Arrays;

/** Bounded raw aPLib block reader for translation resources.
 *
 * This is a new Java implementation of the published token grammar, not a
 * translation of the optional GPLv3 Python decoder. Format/provenance notes:
 * tools/android/PatchAplib-format.md. No Windows library is loaded.
 */
public final class PatchAplib {
    /** EVB stores independently compressed blocks of at most 64 KiB output. */
    public static final int MAX_OUTPUT_BYTES = 65536;
    /** Generous bound above the literal-only representation of one EVB block. */
    public static final int MAX_INPUT_BYTES = 1024 * 1024;

    private PatchAplib() {}

    /** Decode one raw block, requiring its end marker and complete byte input.
     * The returned size may be less than maxOutput; the container checks totals.
     * Unused bits in the final tag byte are permitted, trailing bytes are not.
     */
    public static byte[] decompress(byte[] packed, int maxOutput) throws IOException {
        if (packed == null || packed.length < 3 || packed.length > MAX_INPUT_BYTES
                || maxOutput < 1 || maxOutput > MAX_OUTPUT_BYTES) {
            throw new IOException("aPLib block size outside limits");
        }
        return new Block(packed, maxOutput).decode();
    }

    private static final class Block {
        private final byte[] input;
        private final byte[] output;
        private int cursor;
        private int produced;
        private int tag;
        private int bits;
        private int previousDistance;
        private boolean previousWasMatch;

        Block(byte[] input, int maximum) {
            this.input = input;
            output = new byte[maximum];
        }

        private int readByte() throws IOException {
            if (cursor == input.length) throw new IOException("Truncated aPLib block");
            return input[cursor++] & 255;
        }

        private int readBit() throws IOException {
            if (bits == 0) {
                tag = readByte();
                bits = 8;
            }
            int value = tag >>> 7;
            tag = (tag << 1) & 255;
            bits--;
            return value;
        }

        // Interlaced gamma: implicit leading 1, then (value bit, continue bit).
        private int gamma() throws IOException {
            int value = 1;
            for (int pairs = 0; pairs < 17; pairs++) {
                value = (value << 1) | readBit();
                if (value > MAX_OUTPUT_BYTES + 3) {
                    throw new IOException("aPLib gamma exceeds block limits");
                }
                if (readBit() == 0) return value;
            }
            throw new IOException("Overlong aPLib gamma");
        }

        private void literal(int value) throws IOException {
            if (produced == output.length) throw new IOException("aPLib output exceeds limit");
            output[produced++] = (byte) value;
        }

        private void match(int distance, int length) throws IOException {
            if (distance < 1 || distance > produced || length < 1
                    || length > output.length - produced) {
                throw new IOException("Invalid aPLib back-reference or output length");
            }
            // Forward copying deliberately allows an overlapping LZ match.
            for (int end = produced + length; produced < end; produced++) {
                output[produced] = output[produced - distance];
            }
        }

        byte[] decode() throws IOException {
            literal(readByte());
            while (true) {
                if (readBit() == 0) {
                    literal(readByte());
                    previousWasMatch = false;
                } else if (readBit() == 0) {
                    int high = gamma();
                    if (!previousWasMatch && high == 2) {
                        match(previousDistance, gamma());
                    } else {
                        high -= previousWasMatch ? 2 : 3;
                        if (high < 0 || high > MAX_OUTPUT_BYTES / 256) {
                            throw new IOException("aPLib distance exceeds block limits");
                        }
                        int distance = (high << 8) | readByte();
                        int length = gamma();
                        if (distance < 128 || distance >= 32000) length += 2;
                        else if (distance >= 1280) length++;
                        match(distance, length);
                        previousDistance = distance;
                    }
                    previousWasMatch = true;
                } else if (readBit() == 0) {
                    int value = readByte();
                    int distance = value >>> 1;
                    if (distance == 0) {
                        if (cursor != input.length) throw new IOException("Trailing aPLib bytes");
                        return Arrays.copyOf(output, produced);
                    }
                    match(distance, 2 + (value & 1));
                    previousDistance = distance;
                    previousWasMatch = true;
                } else {
                    int distance = 0;
                    for (int i = 0; i < 4; i++) distance = (distance << 1) | readBit();
                    if (distance == 0) literal(0);
                    else match(distance, 1);
                    previousWasMatch = false;
                }
            }
        }
    }
}
