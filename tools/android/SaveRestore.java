/* SPDX-License-Identifier: GPL-2.0-only */
package org.openbgi.port;

import java.io.*;
import java.nio.charset.StandardCharsets;
import java.nio.file.*;
import java.nio.file.attribute.*;
import java.security.*;
import java.util.*;
import java.util.zip.*;

/** Verified staging plus a durable rollback journal, before any live save is replaced. */
final class SaveRestore {
    static final int MAX_METADATA=SaveBackup.MAX_METADATA;
    private static final int MAX_JOURNAL=MAX_METADATA*2;
    static final String TRANSACTION="save-restore-transaction";
    private static final long MAX_ARCHIVE=SaveBackup.MAX_BYTES+16L*1024*1024;
    interface Progress { void update(int done,int total); }
    private static final class Entry {
        final String name,hash;final long bytes;
        Entry(String name,long bytes,String hash)throws IOException {
            if(!SaveBackup.allowedSavePath(name) || name.length()>1024 || bytes<0 || bytes>SaveBackup.MAX_BYTES || !hash.matches("[0-9a-f]{64}"))throw new IOException("备份文件列表无效");
            this.name=name;this.bytes=bytes;this.hash=hash;
        }
    }
    static final class Prepared implements AutoCloseable {
        final Path directory;final String gameName;final int count;final long bytes;
        private final List<Entry> entries;
        Prepared(Path directory,String gameName,List<Entry> entries){
            this.directory=directory;this.gameName=gameName;this.entries=entries;count=entries.size();long total=0;for(Entry e:entries)total+=e.bytes;bytes=total;
        }
        void apply(File game,Progress progress)throws IOException { applyPrepared(this,game,progress); }
        public void close(){try{remove(directory);}catch(IOException ignored){}}
    }
    @SuppressWarnings("unchecked")
    private static Map<String,Object> object(Object o)throws IOException {if(!(o instanceof Map))throw new IOException("备份信息无效");return (Map<String,Object>)o;}
    @SuppressWarnings("unchecked")
    private static List<Object> array(Object o)throws IOException {if(!(o instanceof List))throw new IOException("备份文件列表无效");return (List<Object>)o;}
    private static String text(Object o)throws IOException {if(!(o instanceof String))throw new IOException("备份信息无效");return (String)o;}
    private static long number(Object o)throws IOException {if(!(o instanceof Long))throw new IOException("备份信息无效");return (Long)o;}
    private static MessageDigest digest(){try{return MessageDigest.getInstance("SHA-256");}catch(NoSuchAlgorithmException e){throw new AssertionError(e);}}
    private static String hex(byte[] hash){StringBuilder s=new StringBuilder();for(byte b:hash)s.append(String.format(Locale.ROOT,"%02x",b&255));return s.toString();}
    private static String quoted(String value){
        StringBuilder s=new StringBuilder("\"");for(char c:value.toCharArray()){
            if(c=='"' || c=='\\')s.append('\\').append(c);else if(c<32)s.append(String.format(Locale.ROOT,"\\u%04x",(int)c));else s.append(c);
        }return s.append('"').toString();
    }
    private static Path safe(Path root,String name)throws IOException {
        if(name.isEmpty() || name.startsWith("/") || name.contains("\\") || name.indexOf('\0')>=0 || name.matches("^[A-Za-z]:.*"))throw new IOException("备份文件路径无效");
        for(String part:name.split("/",-1))if(part.isEmpty() || part.equals(".") || part.equals(".."))throw new IOException("备份文件路径无效");
        Path result=root.resolve(name).normalize();if(!result.startsWith(root) || result.equals(root))throw new IOException("备份文件路径越界");return result;
    }
    private static void parents(Path root,Path file,boolean create)throws IOException {
        Path current=root;
        for(Path part:root.relativize(file.getParent())){
            current=current.resolve(part);
            if(Files.exists(current,LinkOption.NOFOLLOW_LINKS)){
                if(!Files.isDirectory(current,LinkOption.NOFOLLOW_LINKS))throw new IOException("存档目录被文件或符号链接占用");
            }else if(create)Files.createDirectory(current);
        }
    }
    private static void regular(Path file)throws IOException {
        if(!Files.isRegularFile(file,LinkOption.NOFOLLOW_LINKS))throw new IOException("存档文件无效或使用了符号链接");
    }
    private static long transfer(InputStream in,OutputStream out,long limit,MessageDigest hash)throws IOException {
        byte[] buffer=new byte[64*1024];long bytes=0;int n;
        while((n=in.read(buffer))!=-1){
            if(Thread.currentThread().isInterrupted())throw new InterruptedIOException("存档操作已取消");
            bytes+=n;if(bytes>limit)throw new IOException("备份文件超过大小限制");
            if(out!=null)out.write(buffer,0,n);if(hash!=null)hash.update(buffer,0,n);
        }return bytes;
    }
    private static byte[] read(InputStream in,int limit)throws IOException {try(InputStream src=in;ByteArrayOutputStream out=new ByteArrayOutputStream()){transfer(src,out,limit,null);return out.toByteArray();}}
    private static String hash(Path file)throws IOException {
        regular(file);MessageDigest hash=digest();try(InputStream in=Files.newInputStream(file,LinkOption.NOFOLLOW_LINKS)){transfer(in,null,SaveBackup.MAX_BYTES,hash);}return hex(hash.digest());
    }
    private static void verify(Path file,Entry entry)throws IOException {
        regular(file);if(Files.size(file)!=entry.bytes || !hash(file).equals(entry.hash))throw new IOException("存档校验失败："+entry.name);
    }
    private static void copy(Path from,Path to)throws IOException {
        regular(from);Files.createDirectories(to.getParent());
        try(InputStream in=Files.newInputStream(from,LinkOption.NOFOLLOW_LINKS);FileOutputStream out=new FileOutputStream(to.toFile())){
            transfer(in,out,SaveBackup.MAX_BYTES,null);out.getFD().sync();
        }
    }
    private static void replace(Path from,Path root,Path destination)throws IOException {
        parents(root,destination,true);Path temp=Files.createTempFile(transaction(root),".restore-",".tmp");
        try{copy(from,temp);Files.move(temp,destination,StandardCopyOption.ATOMIC_MOVE,StandardCopyOption.REPLACE_EXISTING);}
        finally{Files.deleteIfExists(temp);}
    }
    private static void writeAtomic(Path destination,byte[] bytes)throws IOException {
        Path temp=Files.createTempFile(destination.getParent(),".journal-",".tmp");
        try{try(FileOutputStream out=new FileOutputStream(temp.toFile())){out.write(bytes);out.getFD().sync();}
            Files.move(temp,destination,StandardCopyOption.ATOMIC_MOVE,StandardCopyOption.REPLACE_EXISTING);
        }finally{Files.deleteIfExists(temp);}
    }
    private static void remove(Path directory)throws IOException {
        if(!Files.exists(directory,LinkOption.NOFOLLOW_LINKS))return;
        Files.walkFileTree(directory,new SimpleFileVisitor<Path>(){
            public FileVisitResult visitFile(Path file,BasicFileAttributes a)throws IOException {Files.delete(file);return FileVisitResult.CONTINUE;}
            public FileVisitResult postVisitDirectory(Path dir,IOException error)throws IOException {if(error!=null)throw error;Files.delete(dir);return FileVisitResult.CONTINUE;}
        });
    }
    private static List<Entry> entries(Map<String,Object> manifest)throws IOException {
        List<Entry> out=new ArrayList<>();Set<String> names=new HashSet<>();long bytes=0;
        for(Object value:array(manifest.get("files"))){
            Map<String,Object> row=object(value);Entry entry=new Entry(text(row.get("path")),number(row.get("bytes")),text(row.get("sha256")));
            bytes+=entry.bytes;if(out.size()>=SaveBackup.MAX_FILES || bytes>SaveBackup.MAX_BYTES || !names.add(entry.name.toLowerCase(Locale.ROOT)))throw new IOException("备份文件过多、过大或路径重复");out.add(entry);
        }
        if(out.isEmpty())throw new IOException("备份中没有存档");out.sort(Comparator.comparing(e->e.name));return out;
    }
    static Prepared prepare(InputStream source,File cache)throws IOException {
        Files.createDirectories(cache.toPath());Path dir=Files.createTempDirectory(cache.toPath(),"save-import-");boolean complete=false;
        try{
            Path archive=dir.resolve("backup.zip");try(OutputStream out=Files.newOutputStream(archive)){transfer(source,out,MAX_ARCHIVE,null);}
            String game;List<Entry> entries;
            try(ZipFile zip=new ZipFile(archive.toFile())){
                ZipEntry info=zip.getEntry(SaveBackup.MANIFEST);if(info==null)throw new IOException("请选择 OpenBGI 导出的存档 ZIP");
                Map<String,Object> manifest=object(Json.parse(new String(read(zip.getInputStream(info),MAX_METADATA),StandardCharsets.UTF_8)));
                if(!"org.openbgi.save-backup".equals(manifest.get("format")) || number(manifest.get("version"))!=1)throw new IOException("不支持这个存档备份格式");
                game=text(manifest.get("game"));entries=entries(manifest);Map<String,Entry> expected=new HashMap<>();for(Entry entry:entries)expected.put(entry.name,entry);
                Set<String> found=new HashSet<>();Enumeration<? extends ZipEntry> all=zip.entries();int members=0;
                while(all.hasMoreElements()){
                    ZipEntry member=all.nextElement();String name=member.getName();
                    if(++members>SaveBackup.MAX_FILES*2+10 || !found.add(name))throw new IOException("备份条目过多或重复");
                    if(member.isDirectory()){safe(dir,name.endsWith("/")?name.substring(0,name.length()-1):name);continue;}
                    if(name.equals(SaveBackup.MANIFEST) || name.equals(SaveBackup.README)){if(member.getSize()>MAX_METADATA)throw new IOException("备份说明过大");continue;}
                    Entry entry=expected.get(name);if(entry==null || member.getSize()!=entry.bytes)throw new IOException("备份文件与列表不一致");
                    Path destination=safe(dir.resolve("data"),name);Files.createDirectories(destination.getParent());MessageDigest hash=digest();long bytes;
                    try(InputStream in=zip.getInputStream(member);OutputStream out=Files.newOutputStream(destination)){bytes=transfer(in,out,entry.bytes,hash);}
                    if(bytes!=entry.bytes || !hex(hash.digest()).equals(entry.hash))throw new IOException("存档校验失败："+entry.name);
                }
                for(Entry entry:entries)if(!found.contains(entry.name))throw new IOException("备份中缺少存档文件");
            }
            Files.delete(archive);complete=true;return new Prepared(dir,game,entries);
        }finally{if(!complete)remove(dir);}
    }

    private static Path transaction(Path root)throws IOException {
        Path config=root.resolve(".openbgi");
        if(Files.exists(config,LinkOption.NOFOLLOW_LINKS) && !Files.isDirectory(config,LinkOption.NOFOLLOW_LINKS))throw new IOException("游戏配置目录无效");
        return config.resolve(TRANSACTION);
    }
    private static void retire(Path txn)throws IOException {
        if(!Files.exists(txn,LinkOption.NOFOLLOW_LINKS))return;
        Path done=txn.resolveSibling(".save-restore-done-"+UUID.randomUUID());
        Files.move(txn,done,StandardCopyOption.ATOMIC_MOVE);
        try{remove(done);}catch(IOException ignored){} // no pending journal remains after the rename
    }
    private static void cleanCompleted(Path config)throws IOException {
        if(!Files.isDirectory(config,LinkOption.NOFOLLOW_LINKS))return;
        try(DirectoryStream<Path> stream=Files.newDirectoryStream(config,".save-restore-done-*")){
            for(Path dir:stream)if(dir.getFileName().toString().matches("\\.save-restore-done-[a-f0-9-]{36}"))try{remove(dir);}catch(IOException ignored){}
        }
    }
    static void recover(File game)throws IOException {
        Path root=game.toPath().toRealPath(),txn=transaction(root);cleanCompleted(txn.getParent());
        if(!Files.exists(txn,LinkOption.NOFOLLOW_LINKS))return;
        if(!Files.isDirectory(txn,LinkOption.NOFOLLOW_LINKS))throw new IOException("未完成的存档恢复记录无效");
        Path journal=txn.resolve("journal.json");
        if(Files.isRegularFile(txn.resolve("COMMITTED"),LinkOption.NOFOLLOW_LINKS) || !Files.exists(journal,LinkOption.NOFOLLOW_LINKS)){retire(txn);return;}
        regular(journal);
        Map<String,Object> record=object(Json.parse(new String(read(Files.newInputStream(journal),MAX_JOURNAL),StandardCharsets.UTF_8)));
        if(!"org.openbgi.save-restore".equals(record.get("format")) || number(record.get("version"))!=1)throw new IOException("未完成的存档恢复记录无效");
        List<Entry> entries=entries(record);Map<String,Map<String,Object>> rows=new HashMap<>();
        for(Object value:array(record.get("files"))){Map<String,Object> row=object(value);rows.put(text(row.get("path")),row);}
        Map<String,String> current=new HashMap<>();
        /* Validate every rollback copy and destination before changing any file. */
        for(Entry entry:entries){
            Map<String,Object> row=rows.get(entry.name);Object previous=row.get("previous");if(!(previous instanceof Boolean))throw new IOException("恢复记录无效");
            String oldHash="";
            if(Boolean.TRUE.equals(previous)){
                oldHash=text(row.get("oldSha256"));Entry old=new Entry(entry.name,number(row.get("oldBytes")),oldHash);
                verify(safe(txn.resolve("previous"),entry.name),old);number(row.get("oldModified"));
            }
            Path target=safe(root,entry.name);parents(root,target,false);
            if(Files.exists(target,LinkOption.NOFOLLOW_LINKS)){
                String actual=hash(target);
                if(!actual.equals(entry.hash) && !actual.equals(oldHash))throw new IOException("中断后存档发生变化，无法自动恢复："+entry.name);
                current.put(entry.name,actual);
            }
        }
        List<Path> directories=new ArrayList<>();
        for(Object value:array(record.get("directories"))){
            String name=text(value);boolean parent=false;for(Entry entry:entries)if(entry.name.startsWith(name+"/"))parent=true;
            if(!parent)throw new IOException("恢复目录记录无效");directories.add(safe(root,name));
        }
        for(Entry entry:entries){
            Map<String,Object> row=rows.get(entry.name);Path target=safe(root,entry.name);
            if(Boolean.TRUE.equals(row.get("previous"))){
                if(!Objects.equals(current.get(entry.name),row.get("oldSha256"))){
                    replace(safe(txn.resolve("previous"),entry.name),root,target);
                    Files.setLastModifiedTime(target,FileTime.fromMillis(number(row.get("oldModified"))));
                }
            }else if(current.containsKey(entry.name))Files.delete(target);
        }
        directories.sort(Comparator.comparingInt(Path::getNameCount).reversed());
        for(Path directory:directories)try{Files.deleteIfExists(directory);}catch(DirectoryNotEmptyException ignored){}
        retire(txn);
    }
    private static void applyPrepared(Prepared prepared,File game,Progress progress)throws IOException {
        Path root=game.toPath().toRealPath();recover(game);
        for(Entry entry:prepared.entries){verify(safe(prepared.directory.resolve("data"),entry.name),entry);parents(root,safe(root,entry.name),false);}
        Path txn=transaction(root);Files.createDirectories(txn.getParent());Files.createDirectory(txn);
        boolean journalWritten=false,committed=false;
        try{
            StringBuilder journal=new StringBuilder("{\"format\":\"org.openbgi.save-restore\",\"version\":1,\"files\":[");
            Set<String> directories=new TreeSet<>();Map<String,String> previous=new HashMap<>();int done=0;long oldBytes=0;
            for(Entry entry:prepared.entries){
                Path target=safe(root,entry.name);if(done++>0)journal.append(',');
                journal.append("{\"path\":").append(quoted(entry.name)).append(",\"bytes\":").append(entry.bytes)
                    .append(",\"sha256\":").append(quoted(entry.hash));
                if(Files.exists(target,LinkOption.NOFOLLOW_LINKS)){
                    regular(target);BasicFileAttributes before=Files.readAttributes(target,BasicFileAttributes.class,LinkOption.NOFOLLOW_LINKS);
                    oldBytes+=before.size();if(oldBytes>SaveBackup.MAX_BYTES)throw new IOException("原存档超过恢复备份大小限制");
                    String oldHash=hash(target);Path backup=safe(txn.resolve("previous"),entry.name);copy(target,backup);
                    if(!hash(backup).equals(oldHash) || !hash(target).equals(oldHash) || Files.size(target)!=before.size())throw new IOException("原存档发生变化，请退出游戏后重试");
                    previous.put(entry.name,oldHash);journal.append(",\"previous\":true,\"oldBytes\":").append(before.size())
                        .append(",\"oldSha256\":").append(quoted(oldHash)).append(",\"oldModified\":").append(before.lastModifiedTime().toMillis());
                }else{
                    journal.append(",\"previous\":false");
                    Path dir=target.getParent();while(!dir.equals(root) && !Files.exists(dir,LinkOption.NOFOLLOW_LINKS)){
                        directories.add(root.relativize(dir).toString().replace(File.separatorChar,'/'));dir=dir.getParent();
                    }
                }
                journal.append('}');
            }
            journal.append("],\"directories\":[");done=0;for(String name:directories){if(done++>0)journal.append(',');journal.append(quoted(name));}journal.append("]}");
            byte[] journalBytes=journal.toString().getBytes(StandardCharsets.UTF_8);if(journalBytes.length>MAX_JOURNAL)throw new IOException("恢复文件列表过大");
            writeAtomic(txn.resolve("journal.json"),journalBytes);journalWritten=true;done=0;
            for(Entry entry:prepared.entries){
                Path target=safe(root,entry.name);parents(root,target,false);String oldHash=previous.get(entry.name);
                if(oldHash!=null){if(!hash(target).equals(oldHash))throw new IOException("原存档在导入时发生变化");}
                else if(Files.exists(target,LinkOption.NOFOLLOW_LINKS))throw new IOException("目标存档在导入时发生变化");
                replace(safe(prepared.directory.resolve("data"),entry.name),root,target);verify(target,entry);
                if(progress!=null)progress.update(++done,prepared.count);else done++;
            }
            writeAtomic(txn.resolve("COMMITTED"),new byte[]{1});committed=true;
        }catch(IOException|RuntimeException error){
            if(journalWritten)try{recover(game);}catch(IOException rollback){
                error.addSuppressed(rollback);throw new IOException("导入未完成，原存档恢复尚未完成；请保留游戏目录并重试："+rollback.getMessage(),error);
            }
            throw error;
        }finally{
            if(committed || !journalWritten)try{retire(txn);}catch(IOException ignored){}
        }
    }

    /** Small bounded JSON reader: duplicate keys, trailing data and noninteger numbers are rejected. */
    static final class Json {
        final String input;int at;
        Json(String input){this.input=input;}
        static Object parse(String input)throws IOException {
            if(input.length()>MAX_JOURNAL)throw new IOException("备份信息过大");Json p=new Json(input);Object value=p.value(0);p.space();if(p.at!=input.length())throw p.bad();return value;
        }
        IOException bad(){return new IOException("存档备份信息格式错误");}
        void space(){while(at<input.length() && " \t\r\n".indexOf(input.charAt(at))>=0)at++;}
        boolean take(char c){space();if(at<input.length() && input.charAt(at)==c){at++;return true;}return false;}
        Object value(int depth)throws IOException {
            space();if(depth>32 || at>=input.length())throw bad();char c=input.charAt(at);
            if(c=='"')return string();
            if(c=='{'){
                at++;Map<String,Object> map=new LinkedHashMap<>();if(take('}'))return map;
                do{space();if(at>=input.length() || input.charAt(at)!='"')throw bad();String key=string();if(!take(':') || map.containsKey(key))throw bad();map.put(key,value(depth+1));if(map.size()>SaveBackup.MAX_FILES+128)throw bad();}while(take(','));
                if(!take('}'))throw bad();return map;
            }
            if(c=='['){
                at++;List<Object> list=new ArrayList<>();if(take(']'))return list;
                do{if(list.size()>SaveBackup.MAX_FILES+128)throw bad();list.add(value(depth+1));}while(take(','));if(!take(']'))throw bad();return list;
            }
            for(String word:new String[]{"true","false","null"})if(input.startsWith(word,at)){
                at+=word.length();return word.equals("null")?null:word.equals("true");
            }
            int start=at;if(c=='-')at++;while(at<input.length() && input.charAt(at)>='0' && input.charAt(at)<='9')at++;
            if(at==start || (at==start+1 && c=='-'))throw bad();
            String token=input.substring(start,at);if(token.matches("-?0[0-9]+"))throw bad();
            try{return Long.valueOf(token);}catch(NumberFormatException error){throw bad();}
        }
        String string()throws IOException {
            if(input.charAt(at++)!='"')throw bad();StringBuilder s=new StringBuilder();
            while(at<input.length()){
                char c=input.charAt(at++);if(c=='"')return s.toString();if(c<32)throw bad();
                if(c=='\\'){
                    if(at>=input.length())throw bad();c=input.charAt(at++);
                    switch(c){
                        case '"':case '\\':case '/':break;
                        case 'b':c='\b';break;case 'f':c='\f';break;case 'n':c='\n';break;case 'r':c='\r';break;case 't':c='\t';break;
                        case 'u':if(at+4>input.length())throw bad();try{c=(char)Integer.parseInt(input.substring(at,at+4),16);}catch(NumberFormatException bad){throw bad();}at+=4;break;
                        default:throw bad();
                    }
                }
                s.append(c);if(s.length()>32768)throw bad();
            }throw bad();
        }
    }
}
