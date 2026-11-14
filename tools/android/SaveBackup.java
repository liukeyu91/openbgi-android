/* SPDX-License-Identifier: GPL-2.0-only */
package org.openbgi.port;

import java.io.*;
import java.nio.charset.StandardCharsets;
import java.nio.file.*;
import java.nio.file.attribute.*;
import java.security.*;
import java.time.Instant;
import java.util.*;
import java.util.zip.*;

/** Read-only save snapshots. Archive paths are relative to the game directory. */
final class SaveBackup {
    static final int MAX_FILES=10000, MAX_METADATA=4*1024*1024;
    static final long MAX_BYTES=512L*1024*1024;
    static final String MANIFEST="OpenBGI-save-backup.json",README="OpenBGI-save-backup.txt";
    interface Progress { void update(int done,int total); }
    static final class Backup {
        final File file;final int count;final long bytes;
        Backup(File file,int count,long bytes){this.file=file;this.count=count;this.bytes=bytes;}
    }
    private static final class Entry {
        final Path file;final String name;final long size;final FileTime modified;final Object key;
        Entry(Path root,Path file,BasicFileAttributes a)throws IOException {
            this.file=file;name=root.relativize(file).toString().replace(File.separatorChar,'/');
            if(name.startsWith("/") || name.contains("\\") || name.indexOf('\0')>=0)throw new IOException("无效的存档路径");
            for(String part:name.split("/",-1))if(part.isEmpty() || part.equals(".") || part.equals(".."))throw new IOException("无效的存档路径");
            size=a.size();modified=a.lastModifiedTime();key=a.fileKey();
        }
        boolean same(Entry other){return name.equals(other.name) && size==other.size && modified.equals(other.modified) && Objects.equals(key,other.key);}
    }
    private static boolean excluded(String name){
        String n=name.toLowerCase(Locale.ROOT);
        return n.startsWith(".") || Arrays.asList("archive","extras","backup","backups","patch-backups").contains(n);
    }
    private static boolean saveFolder(String name){
        return Arrays.asList("userdata","savedata","saves","save").contains(name.toLowerCase(Locale.ROOT));
    }
    private static boolean namedSave(String name){
        String n=name.toLowerCase(Locale.ROOT);
        return n.equals("bgi.gdb") || n.endsWith(".sud") || n.matches("bgi[0-9]{4,}\\.cad");
    }
    private static boolean resource(String name){
        String n=name.toLowerCase(Locale.ROOT);int dot=n.lastIndexOf('.');
        return dot>=0 && Arrays.asList(".arc",".pck","._bp",".exe",".dll",".mpg",".wmv",".mp4",".ttf",".otf").contains(n.substring(dot));
    }
    static boolean allowedSavePath(String name){
        if(name.isEmpty() || name.startsWith("/") || name.contains("\\") || name.indexOf('\0')>=0 || name.matches("^[A-Za-z]:.*"))return false;
        String[] parts=name.split("/",-1);boolean folder=false;
        for(int i=0;i<parts.length;i++){
            if(parts[i].isEmpty() || parts[i].equals(".") || parts[i].equals(".."))return false;
            if(i<parts.length-1){if(excluded(parts[i]))return false;if(saveFolder(parts[i]))folder=true;}
        }
        return !resource(parts[parts.length-1]) && (folder || namedSave(parts[parts.length-1]));
    }
    private static List<Entry> inventory(File game)throws IOException {
        Path root=game.toPath().toAbsolutePath().normalize();
        if(!Files.isDirectory(root,LinkOption.NOFOLLOW_LINKS))throw new IOException("游戏目录不存在");
        List<Entry> files=new ArrayList<>();long[] total={0};int[] visited={0};
        Files.walkFileTree(root,EnumSet.noneOf(FileVisitOption.class),32,new SimpleFileVisitor<Path>(){
            public FileVisitResult preVisitDirectory(Path dir,BasicFileAttributes a)throws IOException {
                if(!dir.equals(root) && excluded(dir.getFileName().toString()))return FileVisitResult.SKIP_SUBTREE;
                if(++visited[0]>50000)throw new IOException("游戏目录中的文件过多");
                return FileVisitResult.CONTINUE;
            }
            public FileVisitResult visitFile(Path file,BasicFileAttributes a)throws IOException {
                if(++visited[0]>50000)throw new IOException("游戏目录中的文件过多");
                if(!a.isRegularFile() || a.isSymbolicLink() || resource(file.getFileName().toString()))return FileVisitResult.CONTINUE;
                boolean inSaves=false;
                Path parent=file.getParent();
                while(parent!=null && !parent.equals(root)){if(saveFolder(parent.getFileName().toString()))inSaves=true;parent=parent.getParent();}
                if(inSaves || namedSave(file.getFileName().toString())){
                    total[0]+=a.size();
                    if(files.size()>=MAX_FILES || total[0]>MAX_BYTES)throw new IOException("存档超过导出大小限制（512 MB / 10000 个文件）");
                    files.add(new Entry(root,file,a));
                }
                return FileVisitResult.CONTINUE;
            }
        });
        files.sort(Comparator.comparing(e->e.name));Set<String> names=new HashSet<>();
        for(Entry entry:files)if(entry.name.length()>1024 || !allowedSavePath(entry.name) || !names.add(entry.name.toLowerCase(Locale.ROOT)))throw new IOException("存档文件路径有冲突，无法导出");
        return files;
    }
    static int count(File game)throws IOException { return inventory(game).size(); }
    static String filename(String title){
        String name=title.replaceAll("[\\p{Cntrl}\\\\/:*?\"<>|]","_").trim();
        if(name.isEmpty())name="game";
        if(name.length()>64)name=name.substring(0,64);
        return name+"-存档-"+Instant.now().toString().replace(':','-')+".zip";
    }
    private static MessageDigest digest(){
        try{return MessageDigest.getInstance("SHA-256");}catch(NoSuchAlgorithmException impossible){throw new AssertionError(impossible);}
    }
    private static String hex(byte[] hash){StringBuilder out=new StringBuilder();for(byte b:hash)out.append(String.format(Locale.ROOT,"%02x",b&255));return out.toString();}
    private static String quoted(String value){
        StringBuilder text=new StringBuilder("\"");
        for(char c:value.toCharArray()){
            if(c=='"' || c=='\\')text.append('\\').append(c);
            else if(c<32)text.append(String.format(Locale.ROOT,"\\u%04x",(int)c));
            else text.append(c);
        }
        return text.append('"').toString();
    }
    private static void unchanged(Entry entry)throws IOException {
        BasicFileAttributes a=Files.readAttributes(entry.file,BasicFileAttributes.class,LinkOption.NOFOLLOW_LINKS);
        if(!a.isRegularFile() || a.size()!=entry.size || !a.lastModifiedTime().equals(entry.modified) || !Objects.equals(a.fileKey(),entry.key))
            throw new IOException("存档在导出时发生变化，请退出游戏后重试");
    }
    private static void put(ZipOutputStream zip,String name,byte[] data)throws IOException {
        zip.putNextEntry(new ZipEntry(name));zip.write(data);zip.closeEntry();
    }
    static Backup create(File game,File cache,String title,String appVersion,Progress progress)throws IOException {
        List<Entry> files=inventory(game);
        if(files.isEmpty())throw new IOException("未找到可导出的存档或全局进度");
        Path root=game.toPath().toRealPath(),cachePath=cache.toPath().toAbsolutePath().normalize();
        if(cachePath.startsWith(root))throw new IOException("导出缓存不能放在游戏目录内");
        Files.createDirectories(cachePath);
        if(cachePath.toRealPath().startsWith(root))throw new IOException("导出缓存不能放在游戏目录内");
        Path temp=Files.createTempFile(cachePath,"save-export-",".zip");boolean complete=false;long total=0;
        Map<String,String> hashes=new LinkedHashMap<>();byte[] buffer=new byte[64*1024];
        try {
            StringBuilder manifest=new StringBuilder("{\n  \"format\":\"org.openbgi.save-backup\",\n  \"version\":1,\n  \"game\":")
                .append(quoted(title)).append(",\n  \"appVersion\":").append(quoted(appVersion)).append(",\n  \"exportedAt\":")
                .append(quoted(Instant.now().toString())).append(",\n  \"files\":[\n");
            try(ZipOutputStream zip=new ZipOutputStream(new BufferedOutputStream(Files.newOutputStream(temp)))){
                int done=0;
                for(Entry entry:files){
                    if(Thread.currentThread().isInterrupted())throw new InterruptedIOException("导出已取消");
                    unchanged(entry);MessageDigest hash=digest();long bytes=0;
                    ZipEntry member=new ZipEntry(entry.name);member.setTime(entry.modified.toMillis());zip.putNextEntry(member);
                    try(InputStream in=Files.newInputStream(entry.file,LinkOption.NOFOLLOW_LINKS)){
                        int n;while((n=in.read(buffer))!=-1){bytes+=n;if(bytes>entry.size)throw new IOException("存档大小发生变化");zip.write(buffer,0,n);hash.update(buffer,0,n);}
                    }
                    zip.closeEntry();unchanged(entry);if(bytes!=entry.size)throw new IOException("存档大小发生变化");
                    String sha=hex(hash.digest());hashes.put(entry.name,sha);total+=bytes;
                    if(done>0)manifest.append(",\n");manifest.append("    {\"path\":").append(quoted(entry.name))
                        .append(",\"bytes\":").append(bytes).append(",\"sha256\":").append(quoted(sha)).append('}');
                    if(progress!=null)progress.update(++done,files.size());else done++;
                }
                manifest.append("\n  ]\n}\n");byte[] metadata=manifest.toString().getBytes(StandardCharsets.UTF_8);
                if(metadata.length>MAX_METADATA)throw new IOException("存档文件列表过大");put(zip,MANIFEST,metadata);
                String help="OpenBGI 存档备份\n游戏："+title+"\n\n恢复：先退出游戏，在同版本游戏的菜单中选择“导入存档”，选择本 ZIP，核对信息后确认覆盖。也可按原目录结构手动放回游戏目录。\n包含存档及全局进度；不包含游戏资源、应用设置和运行日志。\n";
                put(zip,README,help.getBytes(StandardCharsets.UTF_8));
            }
            List<Entry> after=inventory(game);
            if(after.size()!=files.size())throw new IOException("存档列表发生变化，请退出游戏后重试");
            for(int i=0;i<files.size();i++)if(!files.get(i).same(after.get(i)))throw new IOException("存档发生变化，请退出游戏后重试");
            try(ZipFile zip=new ZipFile(temp.toFile())){
                if(zip.size()!=files.size()+2)throw new IOException("存档包校验失败");
                for(Entry entry:files){
                    MessageDigest hash=digest();long bytes=0;
                    try(InputStream in=zip.getInputStream(zip.getEntry(entry.name))){int n;while((n=in.read(buffer))!=-1){hash.update(buffer,0,n);bytes+=n;}}
                    if(bytes!=entry.size || !hex(hash.digest()).equals(hashes.get(entry.name)))throw new IOException("存档包校验失败");
                }
            }
            complete=true;return new Backup(temp.toFile(),files.size(),total);
        }finally{if(!complete)Files.deleteIfExists(temp);}
    }
}
