/* SPDX-License-Identifier: GPL-2.0-only */
package org.openbgi.port;
import java.io.*;
import java.nio.channels.*;
import java.nio.file.*;

/** Shared by the launcher and :engine; released by the OS if a process ends. */
final class GameDataLock implements AutoCloseable {
    private final FileChannel channel;private final FileLock lock;
    private GameDataLock(FileChannel channel,FileLock lock){this.channel=channel;this.lock=lock;}
    static GameDataLock acquire(File game)throws IOException {
        Path root=game.toPath();
        if(!Files.isDirectory(root,LinkOption.NOFOLLOW_LINKS))throw new IOException("游戏目录不存在");
        Path config=root.resolve(".openbgi");
        if(Files.isSymbolicLink(config))throw new IOException("游戏配置目录不能使用符号链接");
        Files.createDirectories(config);
        FileChannel channel=FileChannel.open(config.resolve("save-transfer.lock"),StandardOpenOption.CREATE,StandardOpenOption.WRITE,LinkOption.NOFOLLOW_LINKS);
        try{
            FileLock lock;
            try{lock=channel.tryLock();}catch(OverlappingFileLockException busy){lock=null;}
            if(lock==null)throw new IOException("游戏或存档操作正在运行，请先退出游戏并稍后重试");
            return new GameDataLock(channel,lock);
        }catch(IOException|RuntimeException error){channel.close();throw error;}
    }
    public void close(){try{lock.release();}catch(IOException ignored){}finally{try{channel.close();}catch(IOException ignored){}}}
}
