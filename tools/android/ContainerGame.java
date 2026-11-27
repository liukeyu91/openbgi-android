/* SPDX-License-Identifier: GPL-2.0-only */
package org.openbgi.port;

import java.io.*;
import java.nio.charset.StandardCharsets;
import java.nio.file.*;
import java.nio.file.attribute.BasicFileAttributes;
import java.util.*;

/**
 * A library entry whose resources stay in one container file: a disc image
 * (ISO9660/UDF) or a ZIP archive. The container is read in place by the
 * mounted file services; the entry directory holds the settings, the private
 * data layer and a small summary so the library can show and validate the
 * game without walking the container on the UI thread.
 *
 * Both kinds keep the same files: ".openbgi/source.txt" names the container
 * and ".openbgi/source.properties" records what was found in it. The names
 * of the first disc-image build ("source-image.txt", "image.properties") are
 * still read.
 */
final class ContainerGame {
    static final String SOURCE = ".openbgi/source.txt";
    static final String LEGACY_SOURCE = ".openbgi/source-image.txt";
    static final String SUMMARY = ".openbgi/source.properties";
    static final String LEGACY_SUMMARY = ".openbgi/image.properties";
    static final String DATA = ".openbgi/data";
    static final String FORMAT = "openbgi-source";
    static final String LEGACY_FORMAT = "openbgi-image";

    enum Kind {
        IMAGE("image"), ZIP("zip");
        final String id;
        Kind(String id) { this.id = id; }
        static Kind fromId(String id) { return "zip".equals(id) ? ZIP : IMAGE; }
    }

    static boolean linked(File entry) {
        return new File(entry, SOURCE).isFile() || new File(entry, LEGACY_SOURCE).isFile();
    }

    /** The reference name this entry uses, new or legacy. */
    private static File reference(File entry) {
        File file = new File(entry, SOURCE);
        return file.isFile() ? file : new File(entry, LEGACY_SOURCE);
    }

    static Kind kind(File entry) {
        Summary known = summary(entry);
        if (known != null) return known.kind;
        try {
            File source = source(entry);
            if (source != null && source.getName().toLowerCase(Locale.ROOT).endsWith(".zip")) return Kind.ZIP;
        } catch (IOException ignored) {}
        return Kind.IMAGE;
    }

    static File source(File entry) throws IOException {
        File reference = reference(entry);
        if (!reference.exists()) return null;
        if (!reference.isFile() || reference.length() > 3000) throw new IOException("来源记录无效，请移除条目后重新添加");
        String path = new String(Files.readAllBytes(reference.toPath()), StandardCharsets.UTF_8);
        if (!new File(path).isAbsolute() || path.indexOf('\n') >= 0 || path.indexOf('\r') >= 0 || path.indexOf(0) >= 0)
            throw new IOException("来源记录无效，请移除条目后重新添加");
        return new File(path);
    }

    static File validate(File source) throws IOException {
        File file = source.getCanonicalFile();
        if (file.getPath().getBytes(StandardCharsets.UTF_8).length > 3000 || file.getPath().indexOf('\n') >= 0)
            throw new IOException("来源路径过长或包含换行");
        if (!file.isFile() || !file.canRead())
            throw new IOException("无法读取来源文件，来源可能已移动、重命名或被移除；游戏条目和存档会保留，恢复后即可启动");
        return file;
    }

    static File requireSource(File entry) throws IOException {
        File file = source(entry);
        if (file == null) throw new IOException("来源记录无效，请移除条目后重新添加");
        return validate(file);
    }

    static final class SourceStamp {
        final long bytes, modified;
        final String path, fileKey;
        SourceStamp(long bytes, long modified, String path, String fileKey) {
            this.bytes = bytes;
            this.modified = modified;
            this.path = path;
            this.fileKey = fileKey;
        }
        boolean same(SourceStamp other) {
            return other != null && bytes == other.bytes && modified == other.modified
                && path.equals(other.path) && fileKey.equals(other.fileKey);
        }
    }

    static SourceStamp stamp(File source) throws IOException {
        BasicFileAttributes attributes = Files.readAttributes(source.toPath(), BasicFileAttributes.class);
        return new SourceStamp(attributes.size(), attributes.lastModifiedTime().toMillis(),
            source.getPath(), Objects.toString(attributes.fileKey(), ""));
    }

    static final class Summary {
        final Kind kind;
        final boolean boot, fallback;
        final List<ArchiveLanguage> languages;
        final SourceStamp stamp;
        Summary(Kind kind, boolean boot, boolean fallback, List<ArchiveLanguage> languages, SourceStamp stamp) {
            this.kind = kind; this.boot = boot; this.fallback = fallback; this.languages = languages; this.stamp = stamp;
        }
    }

    static Summary summary(File entry) {
        File file = new File(entry, SUMMARY);
        if (!file.isFile()) file = new File(entry, LEGACY_SUMMARY);
        if (!file.isFile() || file.length() > 64 * 1024) return null;
        try (InputStream in = new FileInputStream(file)) {
            Properties p = new Properties(); p.load(in);
            String format = p.getProperty("format");
            if (!FORMAT.equals(format) && !LEGACY_FORMAT.equals(format)) return null;
            SourceStamp stamp = null;
            if (p.getProperty("path") != null && p.getProperty("fileKey") != null) {
                stamp = new SourceStamp(Long.parseLong(p.getProperty("bytes", "-1")),
                    Long.parseLong(p.getProperty("mtime", "-1")), p.getProperty("path"), p.getProperty("fileKey"));
            }
            List<ArchiveLanguage> languages = new ArrayList<>();
            for (String id : p.getProperty("languages", "").split(","))
                if (ArchiveLanguage.fromId(id.trim()) != null) languages.add(ArchiveLanguage.fromId(id.trim()));
            return new Summary(Kind.fromId(p.getProperty("kind", "image")), "1".equals(p.getProperty("boot")),
                "1".equals(p.getProperty("default")), languages, stamp);
        } catch (IOException | IllegalArgumentException bad) { return null; }
    }

    static void writeSummary(File entry, Kind kind, DiscImage.Source source) throws IOException {
        SourceStamp stamp = source.stamp;
        Set<String> names = source.rootNames();
        StringBuilder languages = new StringBuilder();
        for (ArchiveLanguage language : ArchiveLanguage.availableNames(names))
            languages.append(languages.length() == 0 ? "" : ",").append(language.id);
        Properties p = new Properties();
        p.setProperty("format", FORMAT);
        p.setProperty("kind", kind.id);
        p.setProperty("bytes", Long.toString(stamp.bytes));
        p.setProperty("mtime", Long.toString(stamp.modified));
        p.setProperty("path", stamp.path);
        p.setProperty("fileKey", stamp.fileKey);
        p.setProperty("boot", source.hasBootFiles() ? "1" : "0");
        p.setProperty("default", names.contains("system.arc") || names.contains("ipl._bp") ? "1" : "0");
        p.setProperty("languages", languages.toString());
        int unreadable = source.unreadable().size();
        if (unreadable > 0) p.setProperty("unreadable", Integer.toString(unreadable));
        File file = new File(entry, SUMMARY);
        Path temp = file.toPath().resolveSibling("source.properties.tmp");
        try {
            try (OutputStream out = Files.newOutputStream(temp)) { p.store(out, "OpenBGI container summary"); }
            Files.move(temp, file.toPath(), StandardCopyOption.ATOMIC_MOVE, StandardCopyOption.REPLACE_EXISTING);
        } finally { Files.deleteIfExists(temp); }
    }

    /** Why a container cannot boot: the resources are missing, or they are
     *  listed but the engine cannot open them (encrypted, or compressed with a
     *  method the reader does not implement). */
    static String unbootable(Kind kind, DiscImage.Source source) {
        String why = source.describeUnreadable(3);
        boolean listed = ArchiveLanguage.hasBootFiles(source.allRootNames());
        if (kind == Kind.ZIP) {
            if (listed) {
                boolean encrypted = source.unreadable().stream().anyMatch(entry -> entry.encrypted);
                return "压缩包里的启动资源无法读取：" + why + "。请用存储或 Deflate 方式重新打包"
                    + (encrypted ? "，并关闭加密" : "");
            }
            if (!why.isEmpty()) return "压缩包内没有可启动的游戏资源，其中 " + why
                + " 无法读取，请确认打包完整或改用其他压缩包";
            return "压缩包内没有可启动的游戏资源，请选择完整的游戏压缩包";
        }
        if (!why.isEmpty()) return "光盘镜像里有 " + why + " 无法读取，请重新制作镜像";
        return "镜像根目录缺少可启动的游戏资源，请选择完整的游戏光盘";
    }

    /** Create the entry for an existing container; `source` is the index already read. */
    static void register(File entry, File container, Kind kind, DiscImage.Source source) throws IOException {
        register(entry, container, kind, source, staging -> {});
    }

    static void register(File entry, File container, Kind kind, DiscImage.Source source, GameImport.Prepare prepare) throws IOException {
        File file = validate(container);
        if (source == null) throw new IOException(kind == Kind.ZIP
            ? "无法识别压缩包格式，请选择 ZIP 文件" : "无法识别镜像格式，请选择 ISO 或 UDF 光盘镜像");
        source.requireUnchanged(file);
        if (!source.hasBootFiles()) throw new IOException(unbootable(kind, source));
        Path target = entry.toPath(), staging = target.resolveSibling(".import-" + entry.getName());
        if (Files.exists(target)) throw new IOException("游戏条目已存在");
        Files.createDirectories(target.getParent());
        Files.createDirectory(staging);
        try {
            Files.createDirectory(staging.resolve(".openbgi"));
            Files.write(staging.resolve(SOURCE), file.getPath().getBytes(StandardCharsets.UTF_8), StandardOpenOption.CREATE_NEW);
            writeSummary(staging.toFile(), kind, source);
            prepare.run(staging.toFile());
            source.requireUnchanged(file);
            Files.move(staging, target, StandardCopyOption.ATOMIC_MOVE);
        } catch (IOException | RuntimeException failure) {
            try { GameImport.remove(staging); } catch (IOException cleanup) { failure.addSuppressed(cleanup); }
            throw failure;
        }
    }

    /** The writable layer: seeded once with the saves the medium ships. */
    static File data(File entry, DiscImage.Source source) throws IOException {
        Path data = entry.toPath().resolve(DATA);
        if (Files.isDirectory(data, LinkOption.NOFOLLOW_LINKS)) return data.toFile();
        Path staging = Files.createTempDirectory(data.getParent(), "data-");
        try {
            if (source != null) SaveBackup.seedImage(source, staging);
            else SaveBackup.seedEmpty(staging);
            Files.move(staging, data, StandardCopyOption.ATOMIC_MOVE);
        } finally { GameImport.remove(staging); }
        return data.toFile();
    }

    static File data(File entry) {
        File directory = new File(entry, DATA);
        return directory.isDirectory() ? directory : null;
    }

    static File find(File library, File container) throws IOException {
        File canonical = container.getCanonicalFile();
        File[] entries = library.listFiles();
        if (entries != null) for (File entry : entries) if (linked(entry)) try {
            if (source(entry).getCanonicalFile().equals(canonical)) return entry;
        } catch (IOException ignored) {}
        return null;
    }
}
