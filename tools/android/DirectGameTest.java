/* SPDX-License-Identifier: GPL-2.0-only */
package org.openbgi.port;
import java.io.*;
import java.nio.charset.StandardCharsets;
import java.nio.file.*;
import java.nio.file.attribute.*;
import java.util.*;

public final class DirectGameTest {
    static int checks;
    static void check(boolean value,String message){checks++;if(!value)throw new AssertionError(message);}
    interface Action{void run()throws IOException;}
    static void reject(Action action)throws IOException{checks++;try{action.run();throw new AssertionError("Expected rejection");}catch(IOException expected){}}
    public static void main(String[] args)throws Exception {
        Path root=Files.createTempDirectory("direct-game-");
        try {
            File source=Files.createDirectory(root.resolve("原版 Game 日本語")).toFile();
            Files.write(new File(source,"system.arc").toPath(),new byte[]{1,2,3});
            // Sparse fixture proves resource size does not determine entry size.
            try(RandomAccessFile file=new RandomAccessFile(new File(source,"large.arc"),"rw")){file.setLength(64L*1024*1024);}
            Files.write(new File(source,"BGI.gdb").toPath(),"progress".getBytes(StandardCharsets.UTF_8));
            File library=Files.createDirectory(root.resolve("games")).toFile(),entry=new File(library,"first");
            DirectGame.register(entry,source);
            check(DirectGame.linked(entry),"Persisted reference");
            check(DirectGame.resources(entry).equals(source.getCanonicalFile()),"Unicode/spaces resolve exactly");
            check(!new File(entry,"system.arc").exists()&&!new File(entry,"large.arc").exists(),"No resource copies");
            check(new File(entry,DirectGame.REFERENCE).length()<3000,"Only a small directory reference");
            check(DirectGame.find(library,new File(source,"." )).equals(entry),"Canonical duplicate directory detected");
            check(DirectGame.find(library,library)==null,"Other directories not matched");
            GameSettings settings=new GameSettings();settings.chooseEncoding("gbk");settings.save(entry);
            check(GameSettings.load(entry).encoding.equals("gbk"),"Settings stay per entry");
            check(!new File(source,".openbgi/settings.ini").exists(),"Source settings untouched");
            FileTime sourceTime=Files.getLastModifiedTime(source.toPath());
            Set<PosixFilePermission> permissions=Files.getPosixFilePermissions(source.toPath());
            Files.setPosixFilePermissions(source.toPath(),PosixFilePermissions.fromString("r-xr-xr-x"));
            check(DirectGame.validate(source).equals(source.getCanonicalFile()),"Read-only directory can be linked");
            File data=DirectGame.data(entry,source);
            check(SaveBackup.count(data)==1,"Original save copied to private directory");
            check(!new File(data,"large.arc").exists() && !new File(data,"system.arc").exists(),"Only saves copied");
            check(Files.readAllLines(data.toPath().resolve(".openbgi/original-saves.txt")).equals(Arrays.asList("BGI.gdb")),
                "Migrated original stays hidden when its private copy is deleted");
            File cache=Files.createDirectory(root.resolve("cache")).toFile();
            SaveBackup.Backup backup=SaveBackup.create(data,cache,"test","local",(a,b)->{});
            Files.write(new File(data,"BGI.gdb").toPath(),"changed".getBytes(StandardCharsets.UTF_8));
            try(InputStream in=new FileInputStream(backup.file);SaveRestore.Prepared prepared=SaveRestore.prepare(in,cache)){
                prepared.apply(data,(a,b)->{});
            }
            check(Files.readString(new File(data,"BGI.gdb").toPath()).equals("progress"),"Export/import uses private saves");
            Files.delete(new File(data,"BGI.gdb").toPath());
            check(SaveBackup.count(DirectGame.data(entry,source))==0,"Migration runs once");
            check(Files.readString(new File(source,"BGI.gdb").toPath()).equals("progress"),"Original save untouched");
            try(GameDataLock engine=GameDataLock.acquire(entry)){
                reject(()->{try(GameDataLock other=GameDataLock.acquire(entry)){} });
            }
            check(Files.getLastModifiedTime(source.toPath()).equals(sourceTime)&&!new File(source,".openbgi").exists(),"Original directory unchanged");
            Files.setPosixFilePermissions(source.toPath(),permissions);
            reject(()->DirectGame.register(entry,source));
            File ordinary=Files.createDirectory(root.resolve("copied")).toFile();
            check(DirectGame.resources(ordinary).equals(ordinary),"Existing copied imports unchanged");
            File localized=Files.createDirectory(root.resolve("localized")).toFile();
            Files.write(localized.toPath().resolve("SYSTEM.C2.ARC"),new byte[]{1});
            File localizedEntry=new File(library,"localized");
            reject(()->DirectGame.register(localizedEntry,localized));
            Files.write(localized.toPath().resolve("SysPrg.CN.arc"),new byte[]{2});
            DirectGame.register(localizedEntry,localized);
            check(ArchiveLanguage.resolve(DirectGame.resources(localizedEntry),"")==ArchiveLanguage.CN,
                "Mixed-case localized boot pair works without system.arc");
            File missing=new File(root.toFile(),"missing");reject(()->DirectGame.register(new File(library,"bad"),missing));
            File packed=Files.createDirectory(root.resolve("packed")).toFile(),packedEntry=new File(library,"packed");
            DirectGame.register(packedEntry,packed,staging->
                Files.write(DirectGame.data(staging,packed).toPath().resolve("ipl._bp"),new byte[]{1}));
            check(ArchiveLanguage.hasBootFiles(GamePatch.resourceLayers(packedEntry,packed)),
                "Private patch can supply the boot program");
            check(!new File(packed,"ipl._bp").exists(),"Boot program absent from original");
            Files.write(new File(entry,DirectGame.REFERENCE).toPath(),"relative/path".getBytes(StandardCharsets.UTF_8));
            reject(()->DirectGame.resources(entry));
            Files.write(new File(entry,DirectGame.REFERENCE).toPath(),source.getCanonicalPath().getBytes(StandardCharsets.UTF_8));
            GameImport.remove(entry.toPath());
            check(new File(source,"large.arc").length()==64L*1024*1024,"Removing entry preserves all resources");
            check(Arrays.equals(Files.readAllBytes(new File(source,"BGI.gdb").toPath()),"progress".getBytes(StandardCharsets.UTF_8)),"Removing entry preserves saves");
            File absentEntry=new File(library,"offline");DirectGame.register(absentEntry,source);
            Files.move(source.toPath(),root.resolve("moved"));
            check(DirectGame.linked(absentEntry)&&!DirectGame.resources(absentEntry).exists(),"Offline entries retained without substituting another game");
            System.out.println("DIRECT_GAME_PASS: "+checks+" checks; no resource copy, persisted paths, private settings, save routing/lock, removal, offline directories");
        }finally{GameImport.remove(root);}
    }
}
