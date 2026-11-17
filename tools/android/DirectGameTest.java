/* SPDX-License-Identifier: GPL-2.0-only */
package org.openbgi.port;
import java.io.*;
import java.nio.charset.StandardCharsets;
import java.nio.file.*;
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
            check(SaveBackup.count(DirectGame.resources(entry))==1,"Export inventories original saves");
            File cache=Files.createDirectory(root.resolve("cache")).toFile();
            SaveBackup.Backup backup=SaveBackup.create(DirectGame.resources(entry),cache,"test","local",(a,b)->{});
            Files.write(new File(source,"BGI.gdb").toPath(),"changed".getBytes(StandardCharsets.UTF_8));
            try(InputStream in=new FileInputStream(backup.file);SaveRestore.Prepared prepared=SaveRestore.prepare(in,cache)){
                prepared.apply(DirectGame.resources(entry),(a,b)->{});
            }
            check(Arrays.equals(Files.readAllBytes(new File(source,"BGI.gdb").toPath()),"progress".getBytes(StandardCharsets.UTF_8)),"Export/import round trip restores original-directory save");
            check(!new File(entry,"BGI.gdb").exists(),"Restore never writes saves into metadata entry");
            try(GameDataLock engine=GameDataLock.acquire(DirectGame.resources(entry))){
                reject(()->{try(GameDataLock other=GameDataLock.acquire(source)){} });
            }
            reject(()->DirectGame.register(entry,source));
            File ordinary=Files.createDirectory(root.resolve("copied")).toFile();
            check(DirectGame.resources(ordinary).equals(ordinary),"Existing copied imports unchanged");
            File missing=new File(root.toFile(),"missing");reject(()->DirectGame.register(new File(library,"bad"),missing));
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
