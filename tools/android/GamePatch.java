/* SPDX-License-Identifier: GPL-2.0-only */
package org.openbgi.port;

import java.io.*;
import java.nio.charset.Charset;
import java.nio.charset.StandardCharsets;
import java.nio.file.*;
import java.security.*;
import java.util.*;
import java.util.stream.Stream;
import java.util.zip.*;

/** One game-relative overlay, stored separately from the original game. */
final class GamePatch {
    static final String MANIFEST = ".openbgi/patch.json";
    private static final String FINGERPRINT = ".openbgi/content.sha256";
    static final class NameConflict extends IOException {
        final String path;
        NameConflict(String path){super("补丁资源路径冲突："+path);this.path=path;}
    }

    private static Path folder(File game) {
        return game.toPath().resolve(".openbgi/patches");
    }

    static File active(File game) throws IOException {
        Path root = folder(game), pointer = root.resolve("active");
        if (!Files.exists(pointer)) return null;
        if (Files.size(pointer) != 36) throw new IOException("无效的补丁记录");
        String id = new String(Files.readAllBytes(pointer), StandardCharsets.US_ASCII);
        if (!id.matches("[0-9a-f-]{36}")) throw new IOException("无效的补丁记录");
        Path dir = root.resolve(id);
        if (!Files.isDirectory(dir)) throw new IOException("找不到已安装的补丁");
        return dir.toFile();
    }

    static List<File> resourceLayers(File game, File resources) throws IOException {
        List<File> layers = new ArrayList<>();
        layers.add(resources);
        File data = DirectGame.privateLayer(game), patch = active(game);
        if (data != null) layers.add(data);
        if (patch != null) layers.add(patch);
        return layers;
    }

    static void installZip(File game, GameImport.OpenArchive open, GameImport.Progress progress) throws IOException {
        try(Prepared patch=prepareZip(game,open,progress)){patch.attach();}
    }

    static Prepared prepareZip(File game, GameImport.OpenArchive open, GameImport.Progress progress) throws IOException {
        Path root = folder(game);
        Files.createDirectories(root);
        Path temp = Files.createTempFile(root, ".zip-", ".tmp");
        try {
            try (InputStream in = open.open()) {
                if (in == null) throw new IOException("无法打开补丁 ZIP");
                Files.copy(in, temp, StandardCopyOption.REPLACE_EXISTING);
            }
            List<GameImport.Archive> files = new ArrayList<>();
            try (ZipFile zip = openZip(temp.toFile(),files)) {
                return prepare(game, files, true, progress);
            }
        } finally {
            Files.deleteIfExists(temp);
        }
    }

    private static ZipFile openZip(File file,List<GameImport.Archive> files) throws IOException {
        try {
            return readZip(file,StandardCharsets.UTF_8,files);
        } catch (ZipException utf8Error) {
            files.clear();
            try {
                return readZip(file,Charset.forName("windows-31j"),files);
            } catch (IOException legacyError) {
                utf8Error.addSuppressed(legacyError);
                throw utf8Error;
            }
        }
    }

    private static ZipFile readZip(File file,Charset charset,List<GameImport.Archive> files) throws IOException {
        ZipFile zip = new ZipFile(file,charset);
        try {
            Enumeration<? extends ZipEntry> entries = zip.entries();
            while (entries.hasMoreElements()) {
                ZipEntry entry = entries.nextElement();
                String name = entry.getName().replace('\\','/');
                if (name.endsWith("/")) continue;
                files.add(new GameImport.Archive(name,entry.getSize(),() -> zip.getInputStream(entry)));
            }
            return zip;
        } catch (IllegalArgumentException invalidName) {
            ZipException error = new ZipException("无法解码补丁 ZIP 文件名");
            error.initCause(invalidName);
            try { zip.close(); } catch(IOException closeError) { error.addSuppressed(closeError); }
            throw error;
        }
    }

    static void install(File game, List<GameImport.Archive> input, boolean wrappedZip, GameImport.Progress progress) throws IOException {
        try(Prepared patch=prepare(game,input,wrappedZip,progress)){patch.attach();}
    }

    static Prepared prepare(File game, List<GameImport.Archive> input, boolean wrappedZip, GameImport.Progress progress) throws IOException {
        Path root = folder(game);
        List<GameImport.Archive> files = new ArrayList<>();
        String prefix = wrappedZip ? wrapper(input) : "";
        Set<String> names = new HashSet<>();
        boolean hasResources = false;
        for (GameImport.Archive item : input) {
            if (item.source == null) continue;
            GameImport.safePath(root, item.name);
            String name = item.name.substring(prefix.length()).toLowerCase(Locale.ROOT);
            if (name.equals(".openbgi") || (name.startsWith(".openbgi/") && !name.equals(MANIFEST))) continue;
            if (!names.add(name)) throw new NameConflict(item.name);
            files.add(new GameImport.Archive(name, item.size, item.source));
            if (!name.equals(MANIFEST)) hasResources = true;
        }
        for(String name:names)
            for(int slash=name.indexOf('/');slash>=0;slash=name.indexOf('/',slash+1))
                if(names.contains(name.substring(0,slash)))throw new NameConflict(name);
        if (!hasResources) throw new IOException("补丁中没有可加载的文件");
        Files.createDirectories(root);
        try(DirectoryStream<Path> pending=Files.newDirectoryStream(root)){
            for(Path path:pending)
                if(path.getFileName().toString().matches("(?:\\.import-)?\\.prepared-[0-9a-f-]{36}"))
                    GameImport.remove(path);
        }
        String id = UUID.randomUUID().toString();
        Path target = root.resolve(".prepared-"+id);
        GameImport.installFiles(target.toFile(), files, progress, staging->{});
        return new Prepared(game,target,id);
    }

    static final class Prepared implements Closeable {
        final File game, files;
        private final String id;
        private boolean attached;
        private Prepared(File game,Path target,String id){this.game=game;files=target.toFile();this.id=id;}

        void attach() throws IOException {
            if(attached)return;
            Path staged=files.toPath(),root=staged.getParent(),target=root.resolve(id);
            Path hash=staged.resolve(FINGERPRINT);
            Files.createDirectories(hash.getParent());
            Files.write(hash,fingerprint(files).getBytes(StandardCharsets.US_ASCII));
            File previous=active(game);
            Path pending=Files.createTempFile(root,".active-",".tmp");
            boolean moved=false;
            try{
                Files.write(pending, id.getBytes(StandardCharsets.US_ASCII));
                Files.move(staged,target,StandardCopyOption.ATOMIC_MOVE);
                moved=true;
                Files.move(pending,root.resolve("active"),StandardCopyOption.ATOMIC_MOVE,StandardCopyOption.REPLACE_EXISTING);
                attached=true;
            }catch(IOException error){
                if(moved)try{GameImport.remove(target);}catch(IOException cleanup){error.addSuppressed(cleanup);}
                throw error;
            }finally{
                Files.deleteIfExists(pending);
            }
            if(previous!=null)GameImport.remove(previous.toPath());
        }

        public void close() throws IOException {
            if(!attached)GameImport.remove(files.toPath());
        }
    }

    private static String wrapper(List<GameImport.Archive> files) {
        String prefix = null;
        boolean manifestFound = false;
        for (GameImport.Archive item : files) {
            if (item.source == null) continue;
            int slash = item.name.indexOf('/');
            if (slash <= 0) return "";
            String candidate = item.name.substring(0, slash + 1);
            if (prefix == null) prefix = candidate;
            else if (!candidate.equals(prefix)) return "";
            if (item.name.substring(slash+1).equalsIgnoreCase(MANIFEST)) manifestFound = true;
        }
        // A single real resource directory is a valid upstream patch root.
        return manifestFound && !prefix.equalsIgnoreCase(".openbgi/") ? prefix : "";
    }

    static void remove(File game) throws IOException {
        File active = active(game);
        if (active == null) return;
        Files.delete(folder(game).resolve("active"));
        GameImport.remove(active.toPath());
    }

    private static MessageDigest sha256() {
        try { return MessageDigest.getInstance("SHA-256"); }
        catch (NoSuchAlgorithmException impossible) { throw new AssertionError(impossible); }
    }

    private static String hex(byte[] bytes) {
        StringBuilder value = new StringBuilder(64);
        for (byte b : bytes) value.append(String.format(Locale.ROOT,"%02x",b & 255));
        return value.toString();
    }

    static String recordedFingerprint(File patch) throws IOException {
        Path file = patch.toPath().resolve(FINGERPRINT);
        if (!Files.isRegularFile(file) || Files.size(file) != 64) return "";
        String hash = new String(Files.readAllBytes(file),StandardCharsets.US_ASCII);
        return hash.matches("[0-9a-f]{64}") ? hash : "";
    }

    static String fingerprint(File patch) throws IOException {
        MessageDigest filesHash = sha256(), fileHash = sha256();
        byte[] buffer = new byte[64*1024];
        Path root = patch.toPath();
        try (Stream<Path> files = Files.walk(root)) {
            Iterator<Path> entries = files.filter(p -> Files.isRegularFile(p,LinkOption.NOFOLLOW_LINKS))
                .filter(p -> !p.equals(root.resolve(FINGERPRINT))).sorted().iterator();
            while (entries.hasNext()) {
                Path file = entries.next();
                filesHash.update(root.relativize(file).toString().replace(File.separatorChar,'/').getBytes(StandardCharsets.UTF_8));
                filesHash.update((byte)0);
                try (InputStream in = Files.newInputStream(file)) {
                    int n;
                    while ((n=in.read(buffer))!=-1) fileHash.update(buffer,0,n);
                }
                filesHash.update(fileHash.digest());
            }
        } catch (UncheckedIOException error) { throw error.getCause(); }
        return hex(filesHash.digest());
    }

    static String extractionFingerprint(File data,File resources) throws IOException {
        Path report = data.toPath().resolve(".openbgi/translation.properties");
        if(!Files.isRegularFile(report)) report = resources.toPath().resolve(".openbgi/translation.properties");
        if(!Files.isRegularFile(report) || Files.size(report)>4*1024*1024) return "unavailable";
        Properties record = new Properties();
        try (InputStream in=Files.newInputStream(report)) { record.load(in); }
        MessageDigest hash = sha256();
        boolean found = false;
        for (String name : new TreeSet<>(record.stringPropertyNames())) {
            String value = record.getProperty(name);
            if (!name.startsWith("resource.") || !value.matches("[0-9a-f]{64}")) continue;
            hash.update(name.getBytes(StandardCharsets.UTF_8));
            hash.update((byte)0);
            hash.update(value.getBytes(StandardCharsets.US_ASCII));
            found = true;
        }
        return found ? hex(hash.digest()) : "unavailable";
    }

    static void export(File game, OutputStream destination) throws IOException {
        File active = active(game);
        if (active == null) throw new IOException("没有已安装的补丁");
        Path root = active.toPath();
        try (ZipOutputStream zip = new ZipOutputStream(destination);
             Stream<Path> files = Files.walk(root)) {
            Iterator<Path> entries = files.filter(p -> Files.isRegularFile(p, LinkOption.NOFOLLOW_LINKS))
                .filter(p -> !p.equals(root.resolve(FINGERPRINT))).sorted().iterator();
            while (entries.hasNext()) {
                Path path = entries.next();
                String name = root.relativize(path).toString().replace(File.separatorChar, '/');
                ZipEntry entry = new ZipEntry(name);
                if (Files.size(path) > 32L * 1024 * 1024) storeUncompressed(entry, path);
                zip.putNextEntry(entry);
                Files.copy(path, zip);
                zip.closeEntry();
            }
        }
    }

    private static void storeUncompressed(ZipEntry entry, Path path) throws IOException {
        CRC32 crc = new CRC32();
        byte[] buffer = new byte[65536];
        try (InputStream in = Files.newInputStream(path)) {
            int n;
            while ((n = in.read(buffer)) != -1) crc.update(buffer, 0, n);
        }
        entry.setMethod(ZipEntry.STORED);
        entry.setSize(Files.size(path));
        entry.setCrc(crc.getValue());
    }
}
