/* SPDX-License-Identifier: GPL-2.0-only */
package org.openbgi.port;
import java.io.*;
import java.nio.charset.StandardCharsets;
import java.nio.file.*;

/** A library entry owns metadata only; resources and saves remain in the selected directory. */
final class DirectGame {
    static final String REFERENCE=".openbgi/source-path.txt";
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
        if(!directory.canWrite())throw new IOException("游戏目录不可写，无法保存进度；请选择可写目录或复制导入");
        return directory;
    }
    static File find(File library,File source)throws IOException {
        File canonical=source.getCanonicalFile();File[] entries=library.listFiles();
        if(entries!=null)for(File entry:entries)if(linked(entry))try{
            if(resources(entry).getCanonicalFile().equals(canonical))return entry;
        }catch(IOException ignored){}
        return null;
    }
    static void register(File entry,File source)throws IOException {
        File directory=validate(source);
        if(!GameImport.gamePresent(directory))throw new IOException("目录根部缺少非空的 system.arc 或 ipl._bp，请选择完整游戏目录");
        Path target=entry.toPath(),staging=target.resolveSibling(".import-"+entry.getName());
        if(Files.exists(target))throw new IOException("游戏条目已存在");
        Files.createDirectories(target.getParent());
        Files.createDirectory(staging);
        try {
            Files.createDirectory(staging.resolve(".openbgi"));
            Files.write(staging.resolve(REFERENCE),directory.getPath().getBytes(StandardCharsets.UTF_8),StandardOpenOption.CREATE_NEW);
            Files.move(staging,target,StandardCopyOption.ATOMIC_MOVE);
        }catch(IOException|RuntimeException failure){
            try{GameImport.remove(staging);}catch(IOException cleanup){failure.addSuppressed(cleanup);}throw failure;
        }
    }
}
