/* SPDX-License-Identifier: GPL-2.0-only */
package org.openbgi.port;
import java.io.*;
import java.nio.charset.StandardCharsets;
import java.nio.file.*;
import java.security.*;
import java.util.*;
import java.util.zip.*;

public final class SaveBackupTest {
    static int checks;
    static void check(boolean ok){if(!ok)throw new AssertionError("check "+(checks+1));checks++;}
    static void write(Path root,String name,byte[] bytes)throws Exception {
        Path file=root.resolve(name);Files.createDirectories(file.getParent());Files.write(file,bytes);
    }
    static byte[] read(InputStream in)throws Exception {
        try(InputStream input=in;ByteArrayOutputStream out=new ByteArrayOutputStream()){
            byte[] buffer=new byte[4096];int n;while((n=input.read(buffer))!=-1)out.write(buffer,0,n);return out.toByteArray();
        }
    }
    static String sha(byte[] b)throws Exception {
        StringBuilder out=new StringBuilder();for(byte c:MessageDigest.getInstance("SHA-256").digest(b))out.append(String.format(Locale.ROOT,"%02x",c&255));return out.toString();
    }
    static long cached(Path cache)throws Exception {try(java.util.stream.Stream<Path> files=Files.list(cache)){return files.count();}}
    static void tests(Path root)throws Exception {
        Path game=Files.createDirectory(root.resolve("game")),cache=Files.createDirectory(root.resolve("cache"));
        List<String> wanted=Arrays.asList("UserData/SLOT_001.SUD","UserData/Thumb.png","UserData/Profiles/玩家.dat",
            "BGI.gdb","BGI0007.cad","Custom/quick.sud","UserData/empty");
        for(String name:wanted)write(game,name,name.endsWith("empty")?new byte[0]:name.getBytes(StandardCharsets.UTF_8));
        for(String name:Arrays.asList("system.arc",".openbgi/settings.ini",".openbgi/last-played.txt","Archive/old.sud",
            "Extras/UserData/old.sud","Backup/UserData/old.sud","UserData/resources.arc","UserData/tool.exe","screen.png"))write(game,name,new byte[]{99});
        Path outside=root.resolve("outside.sud");Files.write(outside,new byte[]{42});
        try{Files.createSymbolicLink(game.resolve("UserData/link.sud"),outside);Files.createSymbolicLink(game.resolve("Save"),root);}catch(UnsupportedOperationException|IOException unavailable){}
        check(SaveBackup.count(game.toFile())==wanted.size());
        List<Integer> progress=new ArrayList<>();
        SaveBackup.Backup backup=SaveBackup.create(game.toFile(),cache.toFile(),"游戏\"名称","fixture",(done,total)->{check(total==wanted.size());progress.add(done);});
        check(backup.count==wanted.size() && progress.size()==wanted.size());
        long total=0;
        try(ZipFile zip=new ZipFile(backup.file)){
            check(zip.size()==wanted.size()+2);
            String manifest=new String(read(zip.getInputStream(zip.getEntry(SaveBackup.MANIFEST))),StandardCharsets.UTF_8);
            check(manifest.contains("org.openbgi.save-backup") && manifest.contains("游戏\\\"名称"));
            for(String name:wanted){
                byte[] original=Files.readAllBytes(game.resolve(name)),restored=read(zip.getInputStream(zip.getEntry(name)));
                check(Arrays.equals(original,restored));check(manifest.contains(sha(original)));total+=original.length;
                check(!zip.getEntry(name).getName().startsWith("/") && !zip.getEntry(name).getName().contains("\\"));
            }
            check(zip.getEntry("UserData/Thumb.png")!=null && zip.getEntry("UserData/thumb.png")==null);
            check(zip.getEntry("system.arc")==null && zip.getEntry(".openbgi/settings.ini")==null && zip.getEntry("Extras/UserData/old.sud")==null);
            check(zip.getEntry("UserData/link.sud")==null && zip.getEntry("Save/outside.sud")==null);
        }
        check(backup.bytes==total);Files.delete(backup.file.toPath());check(cached(cache)==0);
        check(Files.readAllBytes(outside)[0]==42);
        for(String name:wanted)check(Arrays.equals(Files.readAllBytes(game.resolve(name)),name.endsWith("empty")?new byte[0]:name.getBytes(StandardCharsets.UTF_8)));
        Path empty=Files.createDirectory(root.resolve("empty"));
        try{SaveBackup.create(empty.toFile(),cache.toFile(),"empty","fixture",null);throw new AssertionError("empty export");}catch(IOException expected){checks++;}
        check(cached(cache)==0);
        try{SaveBackup.create(game.toFile(),game.resolve("cache").toFile(),"x","fixture",null);throw new AssertionError("cache in game");}catch(IOException expected){checks++;}
        check(!Files.exists(game.resolve("cache")));
        Path changing=Files.createDirectory(root.resolve("changing"));write(changing,"BGI.gdb",new byte[]{1});
        try{SaveBackup.create(changing.toFile(),cache.toFile(),"x","fixture",(done,count)->{
            try{Files.write(changing.resolve("new.sud"),new byte[]{2});}catch(IOException e){throw new UncheckedIOException(e);}
        });throw new AssertionError("changed snapshot accepted");}catch(IOException expected){checks++;}
        check(cached(cache)==0 && Files.readAllBytes(changing.resolve("BGI.gdb"))[0]==1);
        Files.delete(changing.resolve("new.sud"));
        try{SaveBackup.create(changing.toFile(),cache.toFile(),"x","fixture",(done,count)->{
            try{Files.write(changing.resolve("BGI.gdb"),new byte[]{3,4});}catch(IOException e){throw new UncheckedIOException(e);}
        });throw new AssertionError("modified file accepted");}catch(IOException expected){checks++;}
        check(cached(cache)==0 && Arrays.equals(Files.readAllBytes(changing.resolve("BGI.gdb")),new byte[]{3,4}));
        Thread.currentThread().interrupt();
        try{SaveBackup.create(game.toFile(),cache.toFile(),"x","fixture",null);throw new AssertionError("interrupted export");}catch(InterruptedIOException expected){checks++;}
        finally{Thread.interrupted();}
        check(cached(cache)==0);
        String filename=SaveBackup.filename("../a\\b:*?\"<>|\n");check(filename.endsWith(".zip") && !filename.contains("/") && !filename.contains("\\") && !filename.contains("\n"));
        Path huge=Files.createDirectory(root.resolve("huge"));
        try(RandomAccessFile sparse=new RandomAccessFile(huge.resolve("large.sud").toFile(),"rw")){sparse.setLength(SaveBackup.MAX_BYTES+1);}
        try{SaveBackup.count(huge.toFile());throw new AssertionError("oversized export");}catch(IOException expected){checks++;}
    }
    public static void main(String[] args)throws Exception {
        Path root=Files.createTempDirectory("openbgi-save-backup-test-");
        try{tests(root);System.out.println("Save export: "+checks+" checks passed");}
        finally{try(java.util.stream.Stream<Path> files=Files.walk(root)){files.sorted(Comparator.reverseOrder()).forEach(p->{try{Files.delete(p);}catch(IOException e){throw new UncheckedIOException(e);}});}}
    }
}
