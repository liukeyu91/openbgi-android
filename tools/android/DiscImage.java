/* SPDX-License-Identifier: GPL-2.0-only */
package org.openbgi.port;

import java.io.*;
import java.util.*;

/**
 * Read-only index of a container the engine mounts read in place: an ISO/UDF
 * disc image or a ZIP archive. The launcher never extracts it: it reads the
 * same byte ranges the file services read at run time (inflating a deflated
 * ZIP entry when it is first wanted), so an entry costs no second copy.
 */
final class DiscImage {
    /** The largest entry the launcher will hold decompressed, as in zip.c. */
    private static final long MAX_INFLATE = 256L * 1024 * 1024;
    private static final int CACHE_MAX = 32 * 1024 * 1024;
    private static boolean library;
    private static native String[] nativeEntries(String image);
    private static native void nativeRelease();

    static final class Entry {
        final String path;              // relative to the game root, '/'
        final long data, size;          // byte range the engine reads at run time
        final long csize;               // bytes kept in the container (compressed size)
        final int method;               // 0 stored, 8 deflated
        final boolean directory, readable, stored;

        Entry(String path, long data, long size, long csize, int method,
              boolean directory, boolean readable, boolean stored) {
            this.path = path; this.data = data; this.size = size; this.csize = csize; this.method = method;
            this.directory = directory; this.readable = readable; this.stored = stored;
        }

        String name() { int at = path.lastIndexOf('/'); return at < 0 ? path : path.substring(at + 1); }
        /** A file the engine can open and read: not a directory, non-empty. */
        boolean file() { return !directory && readable && size > 0; }
    }

    /** The container plus its index; reads are positioned, never buffered whole. */
    static final class Source implements Closeable {
        final File image;
        final long size;
        final List<Entry> entries;
        private final Map<String,Entry> index = new HashMap<>();
        private RandomAccessFile stream;
        private Entry inflated;
        private byte[] inflatedBytes;

        Source(File image, long size, List<Entry> entries) {
            this.image = image; this.size = size; this.entries = entries;
            for (Entry entry : entries) index.put(entry.path.toLowerCase(Locale.ROOT), entry);
        }

        Entry find(String path) {
            if (path == null || path.isEmpty() || path.equals(".")) return null;
            return index.get(path.replace('\\', '/').toLowerCase(Locale.ROOT));
        }

        /** Non-empty files at the game root, lower-cased like the directory layers. */
        Set<String> rootNames() {
            Set<String> names = new TreeSet<>();
            for (Entry entry : entries)
                if (entry.path.indexOf('/') < 0 && entry.file())
                    names.add(entry.path.toLowerCase(Locale.ROOT));
            return names;
        }

        boolean hasBootFiles() { return ArchiveLanguage.hasBootFiles(rootNames()); }

        synchronized void seek(Entry entry, long offset) throws IOException {
            if (stream == null) stream = new RandomAccessFile(image, "r");
            stream.seek(entry.data + offset);
        }

        /** Fill `buffer` from `offset` inside `entry`; the count read, or -1 at its end. */
        synchronized int read(Entry entry, long offset, byte[] buffer, int length) throws IOException {
            if (entry == null || offset < 0 || offset >= entry.size) return -1;
            int wanted = (int)Math.min(length, entry.size - offset);
            if (entry.method != 0) {
                byte[] all = inflate(entry);
                System.arraycopy(all, (int)offset, buffer, 0, wanted);
                return wanted;
            }
            seek(entry, offset);
            stream.readFully(buffer, 0, wanted);
            return wanted;
        }

        /** The whole entry decompressed; ZIP entries are cached (one at a time). */
        private byte[] inflate(Entry entry) throws IOException {
            if (entry == inflated && inflatedBytes != null) return inflatedBytes;
            if (entry.size > MAX_INFLATE || entry.csize > Integer.MAX_VALUE || entry.size > Integer.MAX_VALUE)
                throw new IOException("压缩条目过大：" + entry.path);
            byte[] raw = new byte[(int)entry.csize];
            seek(entry, 0);
            stream.readFully(raw, 0, raw.length);
            byte[] out = new byte[(int)entry.size];
            java.util.zip.Inflater inflater = new java.util.zip.Inflater(true);
            try {
                inflater.setInput(raw);
                int got = 0;
                while (got < out.length) {
                    int n = inflater.inflate(out, got, out.length - got);
                    if (n == 0 && (inflater.finished() || inflater.needsInput())) break;
                    got += n;
                }
                if (got != out.length) throw new IOException("压缩条目解压不完整：" + entry.path);
            } catch (java.util.zip.DataFormatException bad) {
                throw new IOException("压缩条目已损坏：" + entry.path, bad);
            } finally { inflater.end(); }
            if (out.length <= CACHE_MAX) { inflated = entry; inflatedBytes = out; }
            return out;
        }

        synchronized byte[] read(Entry entry, long offset, int length) throws IOException {
            long available = entry == null ? 0 : Math.max(0, Math.min(entry.size - offset, length));
            byte[] buffer = new byte[(int)Math.min(available, Integer.MAX_VALUE)];
            if (buffer.length == 0) return buffer;
            int got = read(entry, offset, buffer, buffer.length);
            return got == buffer.length ? buffer : Arrays.copyOf(buffer, Math.max(0, got));
        }

        /** `length` bytes from `offset`; EOFException when the entry is shorter. */
        synchronized byte[] readFully(Entry entry, long offset, int length) throws IOException {
            byte[] buffer = new byte[length];
            if (length == 0) return buffer;
            if (entry == null || offset < 0 || length > entry.size - offset) throw new EOFException("来源条目越界：" + entry.path);
            int got = read(entry, offset, buffer, length);
            if (got != length) throw new EOFException("来源条目越界：" + entry.path);
            return buffer;
        }

        /** The whole entry; callers bound it before reading (the sampler caps at 4 MB). */
        synchronized byte[] bytes(Entry entry) throws IOException {
            if (entry == null || entry.size > Integer.MAX_VALUE) throw new IOException("来源条目过大");
            return read(entry, 0, (int)entry.size);
        }

        @Override public synchronized void close() {
            if (stream != null) { try { stream.close(); } catch (IOException ignored) {} stream = null; }
            inflated = null; inflatedBytes = null;
        }
    }

    /** Load and parse the native index. Only this call needs the packaged library. */
    static Source open(File image) throws IOException {
        if (!library) {
            try { System.loadLibrary("openbgi"); }
            catch (UnsatisfiedLinkError missing) { throw new IOException("无法读取来源索引：" + missing.getMessage()); }
            library = true;
        }
        String[] lines = nativeEntries(image.getAbsolutePath());
        if (lines == null) return null;
        try { return parse(image, lines); }
        finally { nativeRelease(); }
    }

    /** Build a source from the index lines ("path<TAB>data<TAB>size<TAB>csize<TAB>method<TAB>flags";
     * the four-field form of the first disc-image build is still read). */
    static Source parse(File image, String[] lines) throws IOException {
        List<Entry> entries = new ArrayList<>(lines.length);
        long size = image.isFile() ? image.length() : 0;
        for (String line : lines) {
            if (line == null || line.isEmpty()) continue;
            String[] field = line.split("\t", -1);
            if (field.length != 4 && field.length != 6) throw new IOException("来源索引格式错误");
            String path = field[0];
            long data, length, csize;
            int method, flags;
            try {
                data = Long.parseLong(field[1]); length = Long.parseLong(field[2]);
                csize = field.length == 6 ? Long.parseLong(field[3]) : length;
                method = field.length == 6 ? Integer.parseInt(field[4]) : 0;
                flags = Integer.parseInt(field[field.length - 1]);
            } catch (NumberFormatException bad) { throw new IOException("来源索引格式错误", bad); }
            // The mount spells directories with a trailing separator; the index keeps the bare path.
            if (path.length() > 1 && path.endsWith("/")) path = path.substring(0, path.length() - 1);
            if (!validPath(path)) throw new IOException("来源索引包含非法路径：" + path);
            if (data < 0 || length < 0 || csize < 0 || method != 0 && method != 8)
                throw new IOException("来源索引格式错误：" + path);
            // For a deflated entry the range is its compressed bytes; its size is the uncompressed length.
            if (data > size || csize > size - data) throw new IOException("来源索引越界：" + path);
            if (method == 0 && csize != length) throw new IOException("来源索引长度不符：" + path);
            entries.add(new Entry(path, data, length, csize, method,
                (flags & 1) != 0, (flags & 2) != 0, (flags & 4) != 0));
        }
        if (entries.isEmpty()) return null;
        entries.sort(Comparator.comparing(entry -> entry.path, String.CASE_INSENSITIVE_ORDER));
        return new Source(image, size, entries);
    }

    private static boolean validPath(String path) {
        if (path.isEmpty() || path.startsWith("/") || path.contains("\\") || path.indexOf('\0') >= 0) return false;
        for (String part : path.split("/", -1))
            if (part.isEmpty() || part.equals(".") || part.equals("..")) return false;
        return true;
    }
}
