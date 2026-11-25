/* SPDX-License-Identifier: GPL-2.0-only */
package org.openbgi.port;

import java.io.*;
import java.nio.charset.Charset;
import java.nio.charset.StandardCharsets;
import java.nio.file.*;
import java.util.*;
import java.util.zip.*;

final class GamePatchTest {
    private static void check(boolean ok) {
        if (!ok) throw new AssertionError();
    }

    private static GameImport.Archive file(String name, byte[] bytes) {
        return new GameImport.Archive(name, bytes.length, () -> new ByteArrayInputStream(bytes));
    }

    public static void main(String[] args) throws IOException {
        Path tmp = Files.createTempDirectory("openbgi-overlay");
        try {
            File game = Files.createDirectory(tmp.resolve("entry")).toFile();
            File original = Files.createDirectory(tmp.resolve("source")).toFile();
            byte[] base = {1}, patch = {2};
            Path system = original.toPath().resolve("SYSTEM.ARC");
            Files.write(system, base);
            GamePatch.install(game, Arrays.asList(file("System.ARC", patch)), false, (done,total)->{});
            check(GamePatch.active(game) != null);
            String initialHash = GamePatch.recordedFingerprint(GamePatch.active(game));
            check(initialHash.matches("[a-f0-9]{64}") && initialHash.equals(GamePatch.fingerprint(GamePatch.active(game))));
            check(Arrays.equals(Files.readAllBytes(GamePatch.active(game).toPath().resolve("system.arc")), patch));
            Map<String,File> merged = ArchiveLanguage.layeredFiles(original, Arrays.asList(system.toFile()),
                GamePatch.active(game), Arrays.asList(new File(GamePatch.active(game), "system.arc")), null);
            check(merged.size() == 1 && merged.get("system.arc").getParentFile().equals(GamePatch.active(game)));
            ByteArrayOutputStream zip = new ByteArrayOutputStream();
            try (ZipOutputStream out = new ZipOutputStream(zip)) {
                out.putNextEntry(new ZipEntry("Wrapper/Title._bp"));
                out.write(patch);
                out.putNextEntry(new ZipEntry("Wrapper/.openbgi/patch.json"));
                out.write("{}".getBytes(StandardCharsets.UTF_8));
            }
            GamePatch.installZip(game, () -> new ByteArrayInputStream(zip.toByteArray()), (done,total)->{});
            File active = GamePatch.active(game);
            String installedHash = GamePatch.recordedFingerprint(active);
            check(!initialHash.equals(installedHash) && installedHash.equals(GamePatch.fingerprint(active)));
            check(!new File(active, "system.arc").exists());
            check(Arrays.equals(Files.readAllBytes(new File(active, "title._bp").toPath()), patch));
            try {
                GamePatch.install(game, Arrays.asList(file("TITLE._bp", base), file("title._bp", patch)), false, (done,total)->{});
                throw new AssertionError("Duplicate patch path accepted");
            } catch (IOException expected) {
                check(active.equals(GamePatch.active(game)));
            }
            ByteArrayOutputStream exported = new ByteArrayOutputStream();
            GamePatch.export(game, exported);
            Path exportFile = tmp.resolve("exported.zip");
            Files.write(exportFile, exported.toByteArray());
            try (ZipFile out = new ZipFile(exportFile.toFile())) {
                check(out.size() == 2 && out.getEntry("title._bp") != null && out.getEntry(".openbgi/patch.json") != null);
            }
            GamePatch.installZip(game, () -> Files.newInputStream(exportFile), (done,total)->{});
            check(installedHash.equals(GamePatch.recordedFingerprint(GamePatch.active(game))));
            ByteArrayOutputStream nestedZip = new ByteArrayOutputStream();
            try (ZipOutputStream out = new ZipOutputStream(nestedZip,Charset.forName("windows-31j"))) {
                out.putNextEntry(new ZipEntry("Archive\\TITLE._BP"));
                out.write(patch);
                out.putNextEntry(new ZipEntry("Archive/日本語.dat"));
                out.write(base);
            }
            GamePatch.installZip(game, () -> new ByteArrayInputStream(nestedZip.toByteArray()), (done,total)->{});
            File nested = GamePatch.active(game);
            check(!new File(nested,"title._bp").exists());
            check(Arrays.equals(Files.readAllBytes(nested.toPath().resolve("archive/title._bp")),patch));
            check(Arrays.equals(Files.readAllBytes(nested.toPath().resolve("archive/日本語.dat")),base));
            GamePatch.remove(game);
            check(GamePatch.active(game) == null);
            check(Arrays.equals(Files.readAllBytes(system), base) && original.list().length == 1);
        } finally {
            GameImport.remove(tmp);
        }
        System.out.println("Game patch overlay tests passed");
    }
}
