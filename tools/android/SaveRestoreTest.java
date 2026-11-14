/* SPDX-License-Identifier: GPL-2.0-only */
package org.openbgi.port;
import java.io.*;
import java.nio.charset.StandardCharsets;
import java.nio.file.*;
import java.nio.file.attribute.*;
import java.util.*;
import java.util.zip.*;

/** End-to-end restore, hostile inputs, and interruption recovery using disposable games. */
public final class SaveRestoreTest {
    static int checks;
    interface Action { void run()throws Exception; }
    static final class Crash extends Error {}
    static void check(boolean ok){if(!ok)throw new AssertionError("restore check "+(checks+1));checks++;}
    static void refused(Action action)throws Exception {
        boolean failed=false;try{action.run();}catch(IOException expected){failed=true;}check(failed);
    }
    static void write(Path root,String name,String text)throws Exception {SaveBackupTest.write(root,name,text.getBytes(StandardCharsets.UTF_8));}
    static String read(Path root,String name)throws Exception {return new String(Files.readAllBytes(root.resolve(name)),StandardCharsets.UTF_8);}
    static Path txn(Path game){return game.resolve(".openbgi/"+SaveRestore.TRANSACTION);}
    static byte[] zipped(String path,String content,String extra,String manifestEdit)throws Exception {
        byte[] bytes=content.getBytes(StandardCharsets.UTF_8);String manifest="{\"format\":\"org.openbgi.save-backup\",\"version\":1,\"game\":\"fixture\",\"files\":[{\"path\":\""+
            path.replace("\\","\\\\")+"\",\"bytes\":"+bytes.length+",\"sha256\":\""+SaveBackupTest.sha(bytes)+"\"}]}";
        if(manifestEdit!=null)manifest=manifestEdit.replace("$HASH",SaveBackupTest.sha(bytes));
        ByteArrayOutputStream out=new ByteArrayOutputStream();
        try(ZipOutputStream zip=new ZipOutputStream(out)){
            zip.putNextEntry(new ZipEntry(SaveBackup.MANIFEST));zip.write(manifest.getBytes(StandardCharsets.UTF_8));zip.closeEntry();
            zip.putNextEntry(new ZipEntry(path));zip.write(bytes);zip.closeEntry();
            if(extra!=null){zip.putNextEntry(new ZipEntry(extra));zip.write(1);zip.closeEntry();}
        }return out.toByteArray();
    }
    static SaveRestore.Prepared prepare(Path cache,byte[] zip)throws Exception {
        return SaveRestore.prepare(new ByteArrayInputStream(zip),cache.toFile());
    }
    static void delete(Path root)throws Exception {
        Files.walkFileTree(root,new SimpleFileVisitor<Path>(){
            public FileVisitResult visitFile(Path f,BasicFileAttributes a)throws IOException{Files.delete(f);return FileVisitResult.CONTINUE;}
            public FileVisitResult postVisitDirectory(Path d,IOException e)throws IOException{if(e!=null)throw e;Files.delete(d);return FileVisitResult.CONTINUE;}
        });
    }
    static void tests(Path root)throws Exception {
        Path source=Files.createDirectory(root.resolve("source")),cache=Files.createDirectory(root.resolve("cache"));
        for(String path:Arrays.asList("BGI.gdb","UserData/a.sud","UserData/new/slot.sud","UserData/空白.dat"))write(source,path,"new:"+path);
        SaveBackup.Backup export=SaveBackup.create(source.toFile(),cache.toFile(),"fixture","test",null);
        byte[] zip=Files.readAllBytes(export.file.toPath());Files.delete(export.file.toPath());
        Path target=Files.createDirectory(root.resolve("target"));
        write(target,"BGI.gdb","old-global");write(target,"UserData/a.sud","old-slot");write(target,"UserData/keep.sud","keep");
        write(target,"system.arc","resource");write(target,".openbgi/settings.ini","settings");
        try(GameDataLock lock=GameDataLock.acquire(target.toFile())){refused(()->GameDataLock.acquire(target.toFile()));}
        try(GameDataLock lock=GameDataLock.acquire(target.toFile())){check(lock!=null);}
        long oldTime=Files.getLastModifiedTime(target.resolve("BGI.gdb")).toMillis();
        try(SaveRestore.Prepared restore=prepare(cache,zip)){
            check(restore.count==4 && restore.gameName.equals("fixture"));
            boolean failed=false;try{restore.apply(target.toFile(),(done,total)->{if(done==3)throw new IllegalStateException("fault");});}catch(IllegalStateException expected){failed=true;}check(failed);
            check(read(target,"BGI.gdb").equals("old-global"));check(read(target,"UserData/a.sud").equals("old-slot"));
            check(!Files.exists(target.resolve("UserData/new")));check(!Files.exists(target.resolve("UserData/空白.dat")));
            check(!Files.exists(txn(target)));check(Files.getLastModifiedTime(target.resolve("BGI.gdb")).toMillis()==oldTime);
            boolean crashed=false;try{restore.apply(target.toFile(),(done,total)->{if(done==3)throw new Crash();});}catch(Crash expected){crashed=true;}check(crashed);
            check(Files.exists(txn(target).resolve("journal.json")));check(read(target,"BGI.gdb").equals("new:BGI.gdb"));
            SaveRestore.recover(target.toFile());SaveRestore.recover(target.toFile());
            check(read(target,"BGI.gdb").equals("old-global"));check(read(target,"UserData/a.sud").equals("old-slot"));check(!Files.exists(target.resolve("UserData/new")));check(!Files.exists(txn(target)));
            List<Integer> progress=new ArrayList<>();restore.apply(target.toFile(),(done,total)->{check(total==4);progress.add(done);});
            check(progress.equals(Arrays.asList(1,2,3,4)));
            for(String path:Arrays.asList("BGI.gdb","UserData/a.sud","UserData/new/slot.sud","UserData/空白.dat"))check(read(target,path).equals("new:"+path));
            check(read(target,"UserData/keep.sud").equals("keep"));check(read(target,"system.arc").equals("resource"));check(read(target,".openbgi/settings.ini").equals("settings"));
            check(!Files.exists(txn(target)));
        }
        check(SaveBackupTest.cached(cache)==0);
        // A valid COMMITTED marker means cleanup only, even if a process ends before journal removal.
        write(target,"BGI.gdb","before-committed");
        try(SaveRestore.Prepared restore=prepare(cache,zip)){
            try{restore.apply(target.toFile(),(done,total)->{if(done==total)throw new Crash();});}catch(Crash expected){}
            write(txn(target),"COMMITTED","1");SaveRestore.recover(target.toFile());
            check(read(target,"BGI.gdb").equals("new:BGI.gdb"));check(!Files.exists(txn(target)));
        }
        // Unexpected third-party modifications block rollback instead of being silently overwritten.
        write(target,"BGI.gdb","before-external");
        try(SaveRestore.Prepared restore=prepare(cache,zip)){
            try{restore.apply(target.toFile(),(done,total)->{if(done==1)throw new Crash();});}catch(Crash expected){}
            write(target,"BGI.gdb","external-change");refused(()->SaveRestore.recover(target.toFile()));
            check(read(target,"BGI.gdb").equals("external-change"));check(Files.exists(txn(target)));
            write(target,"BGI.gdb","new:BGI.gdb");SaveRestore.recover(target.toFile());check(read(target,"BGI.gdb").equals("before-external"));
            try{restore.apply(target.toFile(),(done,total)->{if(done==2)throw new Crash();});}catch(Crash expected){}
            write(txn(target),"previous/BGI.gdb","corrupt");refused(()->SaveRestore.recover(target.toFile()));
            check(read(target,"BGI.gdb").equals("new:BGI.gdb"));check(Files.exists(txn(target)));
            write(txn(target),"previous/BGI.gdb","before-external");SaveRestore.recover(target.toFile());check(read(target,"BGI.gdb").equals("before-external"));
        }
        for(String invalid:Arrays.asList("../escape.sud","UserData/../../escape.sud","/outside.sud","C:/outside.sud","UserData\\slot.sud",".openbgi/settings.ini","Archive/a.sud","UserData/system.arc","system.arc","UserData/../a.sud")){
            refused(()->{try(SaveRestore.Prepared p=prepare(cache,zipped(invalid,"x",null,null))){}});check(SaveBackupTest.cached(cache)==0);
        }
        refused(()->{try(SaveRestore.Prepared p=prepare(cache,zipped("BGI.gdb","x","unlisted.sud",null))){}});
        String header="{\"format\":\"org.openbgi.save-backup\",\"version\":1,\"game\":\"fixture\",\"files\":";
        String row="{\"path\":\"BGI.gdb\",\"bytes\":1,\"sha256\":\"$HASH\"}";
        for(String invalid:Arrays.asList(header+"["+row.replace("$HASH",String.join("",Collections.nCopies(64,"0")))+"]}",
            header+"["+row.replace("\"bytes\":1","\"bytes\":536870913")+"]}",header+"["+row+","+row+"]}",
            header+"["+row+","+row.replace("BGI.gdb","bgi.gdb")+"]}",header+"[]}",header+"["+row+"]}extra",
            header+"["+row+"] ,\"version\":1}",header+"["+row.replace("\"bytes\":1","\"bytes\":1.0")+"]}")){
            refused(()->{try(SaveRestore.Prepared p=prepare(cache,zipped("BGI.gdb","x",null,invalid))){}});check(SaveBackupTest.cached(cache)==0);
        }
        byte[] metadataTooLarge=zipped("BGI.gdb","x",null,String.join("",Collections.nCopies(SaveRestore.MAX_METADATA+1," ")));
        refused(()->{try(SaveRestore.Prepared p=prepare(cache,metadataTooLarge)){}});check(SaveBackupTest.cached(cache)==0);
        refused(()->{try(SaveRestore.Prepared p=prepare(cache,zipped("BGI.gdb","x","../invalid/",null))){}});
        // An inconsistent uncompressed size must also be rejected before writes.
        refused(()->{try(SaveRestore.Prepared p=prepare(cache,zipped("BGI.gdb","xx",null,header+"["+row+"]}"))){}});
        // Valid-looking ZIP whose decompressed data differs from its declared hash.
        byte[] good=zipped("BGI.gdb","x",null,null);String wrong=header+"["+row.replace("$HASH",SaveBackupTest.sha(new byte[]{'y'}))+"]}";
        refused(()->{try(SaveRestore.Prepared p=prepare(cache,zipped("BGI.gdb","x",null,wrong))){}});
        refused(()->{try(SaveRestore.Prepared p=prepare(cache,new byte[]{1,2,3})){}});
        try(SaveRestore.Prepared restore=prepare(cache,good)){
            Path blocked=Files.createDirectory(root.resolve("blocked"));Files.createDirectory(blocked.resolve("BGI.gdb"));
            refused(()->restore.apply(blocked.toFile(),null));check(Files.isDirectory(blocked.resolve("BGI.gdb")));check(!Files.exists(txn(blocked)));
            Path linkGame=Files.createDirectory(root.resolve("link-game")),outside=root.resolve("outside");Files.write(outside,new byte[]{42});
            Files.createSymbolicLink(linkGame.resolve("BGI.gdb"),outside);refused(()->restore.apply(linkGame.toFile(),null));check(Files.readAllBytes(outside)[0]==42);
        }
        Path parentLink=Files.createDirectory(root.resolve("parent-link"));Files.createSymbolicLink(parentLink.resolve("UserData"),target.resolve("UserData"));
        try(SaveRestore.Prepared restore=prepare(cache,zip)){refused(()->restore.apply(parentLink.toFile(),null));}
        Thread.currentThread().interrupt();refused(()->prepare(cache,good));Thread.interrupted();check(SaveBackupTest.cached(cache)==0);
    }
    public static void main(String[] args)throws Exception {
        checks=0;Path root=Files.createTempDirectory("openbgi-save-restore-");
        try{tests(root);System.out.println("Save restore: "+checks+" checks passed");}finally{delete(root);}
    }
}
