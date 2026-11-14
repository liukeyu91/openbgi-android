/* SPDX-License-Identifier: GPL-2.0-only */
package org.openbgi.port;
import java.io.*;
import java.nio.file.*;
import java.nio.file.attribute.BasicFileAttributes;
import java.util.*;
final class GameImport {
    interface OpenArchive { InputStream open() throws IOException; }
    interface Progress { void copied(int completed, int total); }
    static final class Archive {
        final String name; final long size; final OpenArchive source;
        Archive(String name, long size, OpenArchive source) { this.name=name; this.size=size; this.source=source; }
    }
    static boolean gamePresent(File dir) {
        if(dir==null)return false;
        File[] files=dir.listFiles(); if(files==null)return false;
        for(File file:files)if(file.isFile() && file.length()>0 && (file.getName().equalsIgnoreCase("system.arc") || file.getName().equalsIgnoreCase("ipl._bp")))return true;
        return false;
    }
    static Path safePath(Path root, String name) throws IOException {
        if (name.isEmpty() || name.startsWith("/") || name.contains("\\") || name.indexOf(0)>=0)
            throw new IOException("非法文件路径："+name);
        for (String part : name.split("/", -1)) if(part.isEmpty() || part.equals(".") || part.equals(".."))
            throw new IOException("非法文件路径："+name);
        Path result = root.resolve(name).normalize();
        if (!result.startsWith(root)) throw new IOException("文件路径越界");
        return result;
    }
    static synchronized void install(File target, List<Archive> files, Progress progress) throws IOException {
        Path game=target.toPath(), staging=game.resolveSibling(".import-"+target.getName());
        if (Files.exists(game)) throw new IOException("目标游戏目录已存在");
        remove(staging);
        long bytes=0; Set<String> names=new HashSet<>();
        for(Archive file:files) {
            safePath(staging,file.name);
            if(!names.add(file.name)) throw new IOException("重名文件："+file.name);
            bytes=Math.addExact(bytes, Math.max(0,file.size));
        }
        Files.createDirectories(game.getParent());
        if(bytes>game.getParent().toFile().getUsableSpace()) throw new IOException("手机剩余空间不足");
        Files.createDirectory(staging);
        try {
            int done=0; byte[] buffer=new byte[1024*1024];
            for(Archive file:files) {
                Path destination=safePath(staging,file.name);
                if(file.source==null) Files.createDirectories(destination);
                else {
                    Files.createDirectories(destination.getParent());
                    try(InputStream in=file.source.open()) {
                        if(in==null) throw new IOException("无法打开："+file.name);
                        try(OutputStream out=Files.newOutputStream(destination,StandardOpenOption.CREATE_NEW)) {
                            long copied=0; int n;
                            while((n=in.read(buffer))!=-1) { out.write(buffer,0,n); copied+=n; }
                            if(file.size>=0 && copied!=file.size) throw new IOException("复制不完整："+file.name);
                        }
                    }
                }
                progress.copied(++done,files.size());
            }
            if(!gamePresent(staging.toFile())) throw new IOException("目录根部缺少非空的 system.arc 或 ipl._bp，请选择完整游戏目录");
            Files.move(staging,game,StandardCopyOption.ATOMIC_MOVE);
        } catch(IOException|RuntimeException error) {
            try { remove(staging); } catch(IOException cleanup) { error.addSuppressed(cleanup); }
            throw error;
        }
    }
    static void remove(Path root) throws IOException {
        if(!Files.exists(root,LinkOption.NOFOLLOW_LINKS)) return;
        Files.walkFileTree(root,new SimpleFileVisitor<Path>() {
            public FileVisitResult visitFile(Path path,BasicFileAttributes attrs) throws IOException { Files.delete(path); return FileVisitResult.CONTINUE; }
            public FileVisitResult postVisitDirectory(Path path,IOException error) throws IOException {
                if(error!=null) throw error; Files.delete(path); return FileVisitResult.CONTINUE;
            }
        });
    }
}
