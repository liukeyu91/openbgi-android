/* SPDX-License-Identifier: GPL-2.0-only */
package org.openbgi.port;

import java.io.*;
import java.util.*;

/**
 * The base resources of a game plus its writable overlays, for a game that
 * lives in a directory and for one that lives in a container (a disc image or
 * a ZIP archive). The container is never walked for display: a summary
 * written when it was added answers the library's questions, and the index
 * itself is read only by the tasks that need the files (encoding sampling,
 * the static compatibility check).
 */
final class ResourceLayers implements Closeable {
    final File game;          // the library entry: settings, data layer, patch overlay
    final File base;          // the resources directory (the entry itself for a disc game)
    final DiscImage.Source disc;
    private final List<File> dirs;
    private final List<ArchiveLanguage> languages;   // known facts from the summary
    private final boolean boot, fallback;

    private ResourceLayers(File game, File base, DiscImage.Source disc, List<File> dirs) {
        this.game = game; this.base = base; this.disc = disc; this.dirs = dirs;
        this.languages = null; this.boot = false; this.fallback = false;
    }

    private ResourceLayers(File game, File base, List<File> dirs, List<ArchiveLanguage> languages, boolean boot, boolean fallback) {
        this.game = game; this.base = base; this.disc = null; this.dirs = dirs;
        this.languages = languages; this.boot = boot; this.fallback = fallback;
    }

    /** Directory layers the engine also reads: the private data layer and the patch overlay. */
    static List<File> overlays(File game, File base) throws IOException {
        List<File> dirs = new ArrayList<>();
        if (base != null) dirs.add(base);
        File data = DirectGame.privateLayer(game);
        if (data == null) data = ContainerGame.data(game);
        if (data != null) dirs.add(data);
        File patch = GamePatch.active(game);
        if (patch != null) dirs.add(patch);
        return dirs;
    }

    /** Read the container index. Worker threads only: a large container takes a moment to index. */
    static ResourceLayers of(File game, File resources) throws IOException {
        if (ContainerGame.linked(game)) {
            File container = ContainerGame.source(game);
            if (container == null) throw new IOException("来源记录无效，请移除条目后重新添加");
            DiscImage.Source source = DiscImage.open(ContainerGame.validate(container));
            if (source == null) throw new IOException("无法读取来源索引，请检查镜像或压缩包文件");
            return new ResourceLayers(game, game, source, overlays(game, null));
        }
        return new ResourceLayers(game, resources, null, overlays(game, resources));
    }

    /** A container game whose index is already read (while adding, or in a test). */
    static ResourceLayers ofImage(File game, DiscImage.Source source) throws IOException {
        return new ResourceLayers(game, game, source, overlays(game, null));
    }

    /** The cheap form for the UI thread: the stored summary, or the directory listing. */
    static ResourceLayers summary(File game, File resources) throws IOException {
        if (ContainerGame.linked(game)) {
            ContainerGame.Summary known = ContainerGame.summary(game);
            if (known == null) return of(game, resources);
            return new ResourceLayers(game, game, overlays(game, null), known.languages, known.boot, known.fallback);
        }
        return new ResourceLayers(game, resources, null, overlays(game, resources));
    }

    private boolean known() { return languages != null; }

    boolean hasBootFiles() throws IOException {
        if (known()) return boot;
        if (disc != null) return disc.hasBootFiles();
        return ArchiveLanguage.hasBootFiles(dirs);
    }

    List<ArchiveLanguage> available() {
        if (known()) return languages;
        if (disc != null) return ArchiveLanguage.availableNames(disc.rootNames());
        return ArchiveLanguage.available(dirs);
    }

    boolean hasDefault() {
        if (known()) return fallback;
        if (disc != null) {
            Set<String> names = disc.rootNames();
            return names.contains("system.arc") || names.contains("ipl._bp");
        }
        return ArchiveLanguage.hasDefault(dirs);
    }

    ArchiveLanguage resolve(String stored) {
        if (known()) {
            ArchiveLanguage selected = ArchiveLanguage.fromId(stored);
            if (selected != null && languages.contains(selected)) return selected;
            if (fallback || languages.isEmpty()) return null;
            return languages.get(0);
        }
        if (disc != null) return ArchiveLanguage.resolveNames(disc.rootNames(), stored);
        return ArchiveLanguage.resolve(dirs, stored);
    }

    /** The directory layers only; a container game has no base directory to walk. */
    List<File> directories() { return dirs; }

    @Override public void close() { if (disc != null) disc.close(); }
}
