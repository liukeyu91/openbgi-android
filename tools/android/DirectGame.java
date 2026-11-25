/* SPDX-License-Identifier: GPL-2.0-only */
package org.openbgi.port;
import java.io.*;
import java.nio.charset.StandardCharsets;
import java.nio.file.*;

/** A linked entry reads resources in place and owns its writable data. */
final class DirectGame {
    static final String REFERENCE=".openbgi/source-path.txt";
    static final String DATA=".openbgi/data";
    static boolean linked(File entry){return new File(entry,REFERENCE).isFile();}
    static File resources(File entry)throws IOException {
        File reference=new File(entry,REFERENCE);
        if(!reference.exists())return entry;
        if(!reference.isFile()||reference.length()>3000)throw new IOException("游戏目录记录无效，请移除条目后重新添加");
        String path=new String(Files.readAllBytes(reference.toPath()),StandardCharsets.UTF_8);
        if(!new File(path).isAbsolute()||path.indexOf('\n')>=0||path.indexOf('\r')>=0||path.indexOf(0)>=0)
            throw new IOException("游戏目录记录无效，请移除条目后重新添加");
        return new File(path);
    }
    static File validate(File source)throws IOException {
        File directory=source.getCanonicalFile();String path=directory.getPath();
        if(path.getBytes(StandardCharsets.UTF_8).length>3000||path.indexOf('\n')>=0||path.indexOf('\r')>=0)
            throw new IOException("游戏目录路径过长或包含换行");
        if(!directory.isDirectory()||!directory.canRead())throw new IOException("无法读取游戏目录，请检查存储权限或存储卡");
        return directory;
    }
    static File data(File entry,File source) throws IOException {
        if(!linked(entry))return source;
        Path data=entry.toPath().resolve(DATA);
        if(Files.isDirectory(data,LinkOption.NOFOLLOW_LINKS))return data.toFile();
        Path staging=Files.createTempDirectory(data.getParent(),"data-");
        try {
            SaveBackup.seed(source,staging);
            Files.move(staging,data,StandardCopyOption.ATOMIC_MOVE);
        }finally{GameImport.remove(staging);}
        return data.toFile();
    }
    static File privateLayer(File entry) {
        File data=new File(entry,DATA);
        return linked(entry)&&data.isDirectory()?data:null;
    }
    static File find(File library,File source)throws IOException {
        File canonical=source.getCanonicalFile();File[] entries=library.listFiles();
        if(entries!=null)for(File entry:entries)if(linked(entry))try{
            if(resources(entry).getCanonicalFile().equals(canonical))return entry;
        }catch(IOException ignored){}
        return null;
    }
    static void register(File entry,File source)throws IOException {
        register(entry,source,staging->{});
    }
    static void register(File entry,File source,GameImport.Prepare prepare)throws IOException {
        File directory=validate(source);
        Path target=entry.toPath(),staging=target.resolveSibling(".import-"+entry.getName());
        if(Files.exists(target))throw new IOException("游戏条目已存在");
        Files.createDirectories(target.getParent());
        Files.createDirectory(staging);
        try {
            Files.createDirectory(staging.resolve(".openbgi"));
            Files.write(staging.resolve(REFERENCE),directory.getPath().getBytes(StandardCharsets.UTF_8),StandardOpenOption.CREATE_NEW);
            prepare.run(staging.toFile());
            File data=privateLayer(staging.toFile());
            if(!ArchiveLanguage.hasBootFiles(java.util.Arrays.asList(directory,data)))
                throw new IOException("目录根部缺少可启动的游戏资源，请选择完整游戏目录");
            Files.move(staging,target,StandardCopyOption.ATOMIC_MOVE);
        }catch(IOException|RuntimeException failure){
            try{GameImport.remove(staging);}catch(IOException cleanup){failure.addSuppressed(cleanup);}throw failure;
        }
    }
}
