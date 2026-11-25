/* SPDX-License-Identifier: GPL-2.0-only */
package org.openbgi.port;

import java.io.*;

/**
 * A readable byte range with the same semantics either way: a file on disk,
 * or one entry of a disc image. The samplers and the compatibility check are
 * written against this so a directory game and an image game take one path.
 */
interface Blob {
    long size();

    /** `length` bytes from `offset`; throws EOFException when the range is short. */
    byte[] read(long offset, int length) throws IOException;

    static Blob file(File file) {
        return new Blob() {
            long size = -1;
            public long size() { if (size < 0) size = file.length(); return size; }
            public byte[] read(long offset, int length) throws IOException {
                byte[] out = new byte[length];
                try (RandomAccessFile in = new RandomAccessFile(file, "r")) {
                    in.seek(offset);
                    in.readFully(out);
                }
                return out;
            }
        };
    }

    static Blob image(DiscImage.Source source, DiscImage.Entry entry) {
        return new Blob() {
            public long size() { return entry.size; }
            public byte[] read(long offset, int length) throws IOException { return source.readFully(entry, offset, length); }
        };
    }
}
