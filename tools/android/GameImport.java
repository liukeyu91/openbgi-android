/*
 * This file is part of OpenBGI (https://openbgi.net).
 * SPDX-License-Identifier: GPL-2.0-only
 */
package org.openbgi.port;

import java.io.DataInputStream;
import java.io.File;
import java.io.IOException;
import java.io.InputStream;
import java.io.OutputStream;
import java.nio.charset.StandardCharsets;
import java.nio.file.Files;
import java.nio.file.Path;
import java.nio.file.SimpleFileVisitor;
import java.nio.file.FileVisitResult;
import java.nio.file.StandardCopyOption;
import java.nio.file.attribute.BasicFileAttributes;
import java.util.List;

final class GameImport {
    interface OpenArchive {
        InputStream open() throws IOException;
    }

    interface Progress {
        void copied(int completed, int total);
    }

    static final class Archive {
        final String name;
        final long size;
        final OpenArchive source;

        Archive(String name, long size, OpenArchive source) {
            this.name = name;
            this.size = size;
            this.source = source;
        }
    }

    private GameImport() {}

    static boolean gamePresent(File folder) {
        return folder != null && (new File(folder, "system.arc").isFile()
                || new File(folder, "ipl._bp").isFile());
    }

    static synchronized void install(File filesDir, List<Archive> archives, Progress progress)
            throws IOException {
        Path game = new File(filesDir, "game").toPath();
        Path staging = new File(filesDir, ".game-import").toPath();
        if (gamePresent(game.toFile())) {
            return;
        }
        // An earlier engine build may have created an empty game directory.
        if (Files.exists(game) && (!Files.isDirectory(game) || !game.toFile().delete())) {
            throw new IOException("游戏目录已有文件，无法导入到该目录。");
        }
        removeStaging(staging);
        long required = 0;
        for (Archive archive : archives) {
            required = Math.addExact(required, Math.max(archive.size, 0));
        }
        if (required > filesDir.getUsableSpace()) {
            throw new IOException("手机剩余空间不足。");
        }
        Files.createDirectory(staging);
        try {
            Files.createDirectory(staging.resolve("Archive"));
            byte[] buffer = new byte[1024 * 1024];
            int completed = 0;
            for (Archive archive : archives) {
                copyArchive(staging.resolve(archive.name), archive, buffer);
                progress.copied(++completed, archives.size());
            }
            // Both directories are under filesDir. The engine only sees the complete copy.
            Files.move(staging, game, StandardCopyOption.ATOMIC_MOVE);
        } catch (IOException | RuntimeException error) {
            try {
                removeStaging(staging);
            } catch (IOException cleanupError) {
                error.addSuppressed(cleanupError);
            }
            throw error;
        }
    }

    private static void copyArchive(Path destination, Archive archive, byte[] buffer)
            throws IOException {
        InputStream source = archive.source.open();
        if (source == null) {
            throw new IOException("无法打开 " + archive.name);
        }
        try (DataInputStream input = new DataInputStream(source)) {
            byte[] header = new byte[16];
            input.readFully(header);
            String signature = new String(header, 0, 12, StandardCharsets.US_ASCII);
            if (!signature.equals("PackFile    ") && !signature.equals("BURIKO ARC20")) {
                throw new IOException("无法识别的游戏档案 " + archive.name);
            }
            try (OutputStream output = Files.newOutputStream(destination,
                    java.nio.file.StandardOpenOption.CREATE_NEW,
                    java.nio.file.StandardOpenOption.WRITE)) {
                output.write(header);
                long copied = header.length;
                int count;
                while ((count = input.read(buffer)) != -1) {
                    output.write(buffer, 0, count);
                    copied += count;
                }
                if (archive.size > 0 && copied != archive.size) {
                    throw new IOException("文件复制不完整 " + archive.name);
                }
            }
        }
    }

    private static void removeStaging(Path folder) throws IOException {
        if (!Files.exists(folder)) {
            return;
        }
        Files.walkFileTree(folder, new SimpleFileVisitor<Path>() {
            @Override
            public FileVisitResult visitFile(Path path, BasicFileAttributes attributes)
                    throws IOException {
                Files.delete(path);
                return FileVisitResult.CONTINUE;
            }

            @Override
            public FileVisitResult postVisitDirectory(Path path, IOException error)
                    throws IOException {
                if (error != null) {
                    throw error;
                }
                Files.delete(path);
                return FileVisitResult.CONTINUE;
            }
        });
    }
}
