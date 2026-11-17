/* This file is part of OpenBGI (https://openbgi.net).
 * SPDX-License-Identifier: GPL-2.0-only */
package org.openbgi.port;

import java.io.*;
import java.nio.file.*;
import java.security.*;
import java.util.*;
import java.util.regex.Pattern;

/** Expands translation resources locally before an imported game is published. */
final class PatchImport {
    interface Progress{void update(String message);}
    static final class Source{
        final String format,path,key;final String[] select,exclude;
        Source(String format,String path,String key,String[] select,String[] exclude){this.format=format;this.path=path;this.key=key;this.select=select;this.exclude=exclude;}
    }
    static final class Profile{
        final String id,engine,encoding,fontFamily;final Source[] sources;
        Profile(String id,String engine,String encoding,Source[] sources){this(id,engine,encoding,"",sources);}
        Profile(String id,String engine,String encoding,String fontFamily,Source[] sources){this.id=id;this.engine=engine;this.encoding=encoding;this.fontFamily=fontFamily;this.sources=sources;}
    }
    static final class Result{
        String profile="",engine="",encoding="",fontFamily="";int resources,preserved,containers,repaired;
        String summary(){return repaired>0?"已处理汉化资源 "+resources+" 项，其中修复 "+repaired+" 项。":resources>0?"已展开汉化资源 "+resources+" 项。":preserved>0?"汉化资源已就绪。":!profile.isEmpty()?"已识别汉化游戏设置。":containers>0?"未发现可展开的游戏资源。":"未发现需要展开的汉化资源。";}
        void apply(GameSettings settings){
            if(!encoding.isEmpty())settings.chooseEncoding(encoding);
            if(!engine.isEmpty()&&Arrays.asList(EngineProfiles.NAMES).contains(engine))settings.profile=engine;
            if(!fontFamily.isEmpty())settings.fontFamily=fontFamily;
            if(!profile.isEmpty())settings.translationProfile=profile;
        }
    }
    private static final class Item{
        final String name;final long size;final Object entry;
        Item(String name,long size,Object entry){this.name=name;this.size=size;this.entry=entry;}
    }
    private static final class Pack implements Closeable{
        final File source;final PatchEvb evb;final PatchCpk cpk;final List<Item> items=new ArrayList<>();
        Pack(File source,PatchEvb evb,PatchCpk cpk){this.source=source;this.evb=evb;this.cpk=cpk;
            if(evb!=null)for(PatchEvb.Entry e:evb.entries)items.add(new Item(e.name,e.size,e));
            if(cpk!=null)for(PatchCpk.Entry e:cpk.entries)items.add(new Item(e.name,e.size,e));}
        void decode(Item item,OutputStream out)throws IOException{if(evb!=null)evb.decode((PatchEvb.Entry)item.entry,out);else cpk.decode((PatchCpk.Entry)item.entry,out);}
        public void close()throws IOException{if(evb!=null)evb.close();if(cpk!=null)cpk.close();}
    }
    private static final class Job{
        final Pack pack;final Item item;
        Job(Pack pack,Item item){this.pack=pack;this.item=item;}
    }
    static boolean excluded(String name){
        String n=name.toLowerCase(Locale.ROOT);
        return n.equals("bgi.gdb")||n.equals("bgi.chs")||n.equals("bgi.hvl")||n.equals("bgi.hvc")||n.equals("bgi.dat")||n.equals("bgierror.txt")||n.equals(".openbgi")
            ||n.matches("bgi[0-9]{4,}\\.cad")||n.endsWith(".sud")||n.endsWith(".sav")||n.endsWith(".save")||n.endsWith(".bak")||n.endsWith(".exe")||n.endsWith(".dll")||n.endsWith(".key");
    }
    private static File lookup(File root,String relative)throws IOException{
        File current=root;
        for(String part:relative.split("/")){
            if(part.isEmpty()||part.equals(".")||part.equals("..")||part.contains("\\")||part.contains(":"))throw new IOException("汉化配置路径无效");
            File[] children=current.listFiles();if(children==null)return null;File found=null;
            for(File child:children)if(child.getName().equalsIgnoreCase(part)){
                if(found!=null)throw new IOException("目录存在大小写重名项："+part);found=child;}
            if(found==null)return null;current=found;
        }
        Path canonical=current.toPath().toRealPath();if(!canonical.startsWith(root.toPath().toRealPath()))throw new IOException("汉化资源路径越界");
        return current;
    }
    private static boolean matches(String name,String[] patterns){
        for(String pattern:patterns){StringBuilder regex=new StringBuilder("(?i)^");
            for(int i=0;i<pattern.length();i++){char c=pattern.charAt(i);regex.append(c=='*'?".*":c=='?'?".":Pattern.quote(String.valueOf(c)));}
            if(name.matches(regex.append('$').toString()))return true;}
        return false;
    }
    private static Pack open(File root,Source spec)throws IOException{
        File file=lookup(root,spec.path);if(file==null||!file.isFile())throw new IOException("缺少汉化资源："+spec.path);
        if(spec.format.equals("bgi-arc"))return null; // Native archives are already read directly by OpenBGI.
        if(spec.format.equals("evb-external"))return new Pack(file,PatchEvb.open(file,0),null);
        if(spec.format.equals("evb-pe")){PatchEvb evb=PatchEvb.embedded(file);if(evb==null)throw new IOException("汉化程序中未找到有效资源包");return new Pack(file,evb,null);}
        if(spec.format.equals("cpk-v2")){File key=lookup(root,spec.key);if(key==null)throw new IOException("汉化资源缺少配套密钥文件");return new Pack(file,null,PatchCpk.open(file,key));}
        throw new IOException("汉化资源格式不支持");
    }
    private static byte[] header(File file)throws IOException{try(InputStream in=new FileInputStream(file)){byte[] b=new byte[4];int at=0,n;while(at<4&&(n=in.read(b,at,4-at))>0)at+=n;return b;}}
    private static String digest(File file)throws IOException{
        try{MessageDigest hash=MessageDigest.getInstance("SHA-256");try(InputStream in=new FileInputStream(file)){byte[] b=new byte[65536];int n;while((n=in.read(b))!=-1)hash.update(b,0,n);}StringBuilder s=new StringBuilder();for(byte b:hash.digest())s.append(String.format(Locale.ROOT,"%02x",b&255));return s.toString();}
        catch(NoSuchAlgorithmException e){throw new IOException(e);}
    }
    static Result process(File game,Profile[] profiles,Progress progress)throws IOException{
        Path root=game.toPath().toRealPath();Path config=root.resolve(".openbgi");
        if(Files.isSymbolicLink(config))throw new IOException("配置目录不能使用符号链接");
        Files.createDirectories(config);Path work=Files.createTempDirectory(config,"patch-import-");
        List<Pack> packs=new ArrayList<>();List<Path> installed=new ArrayList<>();Result result=new Result();
        Map<Path,Path> backups=new LinkedHashMap<>();boolean keepRecovery=false;
        Properties record=new Properties(),previous=new Properties();
        Path previousReport=config.resolve("translation.properties");
        try{
            if(Files.isRegularFile(previousReport)&&Files.size(previousReport)<=4*1024*1024)
                try(InputStream in=Files.newInputStream(previousReport)){previous.load(in);}
            progress.update("正在检测汉化补丁…");Profile chosen=null;Set<String> used=new HashSet<>();
            for(Profile profile:profiles){boolean complete=true;for(Source source:profile.sources){File f=lookup(game,source.path);if(f==null||!f.isFile()){complete=false;break;}if(!source.key.isEmpty()&&lookup(game,source.key)==null){complete=false;break;}}
                if(complete){chosen=profile;break;}}
            List<Job> jobs=new ArrayList<>();
            if(chosen!=null){result.profile=chosen.id;result.engine=chosen.engine;result.encoding=chosen.encoding;result.fontFamily=chosen.fontFamily;
                for(Source source:chosen.sources){
                    File input=lookup(game,source.path);used.add(input.getCanonicalPath());Pack pack=open(game,source);if(pack==null)continue;packs.add(pack);
                    for(Item item:pack.items)if(!excluded(item.name)&&matches(item.name,source.select)&&!matches(item.name,source.exclude))jobs.add(new Job(pack,item));
                }
            }
            // External/embedded packages use the same generic path, including
            // movie packs accompanying a known profile. Profiles settle update
            // precedence; otherwise differing duplicate resources are rejected.
            File[] children=game.listFiles();if(children==null)throw new IOException("无法读取游戏目录");
            Arrays.sort(children,(a,b)->a.getName().compareToIgnoreCase(b.getName()));
            boolean hasEvb=false;for(Pack p:packs)if(p.evb!=null)hasEvb=true;
            for(File file:children){
                if(!file.isFile()||Files.isSymbolicLink(file.toPath())||used.contains(file.getCanonicalPath()))continue;
                byte[] magic=header(file);Pack pack=null;
                if(PatchEvb.magic(magic))pack=new Pack(file,PatchEvb.open(file,0),null);
                else if(magic[0]=='M'&&magic[1]=='Z'&&file.getName().toLowerCase(Locale.ROOT).endsWith(".exe")){
                    PatchEvb evb=PatchEvb.embedded(file);if(evb!=null)pack=new Pack(file,evb,null);
                }
                if(pack!=null){hasEvb=true;packs.add(pack);used.add(file.getCanonicalPath());for(Item item:pack.items)if(!excluded(item.name))jobs.add(new Job(pack,item));}
            }
            // A CPK often duplicates an EVB bundle's expanded graphics. Prefer
            // the EVB representation when available; decode standalone CPKs.
            if(!hasEvb)for(File file:children){
                if(!file.isFile()||used.contains(file.getCanonicalPath())||!file.getName().toLowerCase(Locale.ROOT).endsWith(".cpk"))continue;
                String name=file.getName();File key=lookup(game,name.substring(0,name.length()-4)+".key");
                if(key==null)continue;
                byte[] keyMagic=header(key);
                if((keyMagic[0]^0x13)!='C'||(keyMagic[1]^0x13)!='P'||(keyMagic[2]^0x13)!='K'||(keyMagic[3]^0x13)!=0)continue;
                Pack pack=new Pack(file,null,PatchCpk.open(file,key));packs.add(pack);for(Item item:pack.items)if(!excluded(item.name))jobs.add(new Job(pack,item));
            }
            long total=0;for(Job job:jobs){PatchEvb.safeName(job.item.name);total=Math.addExact(total,job.item.size);}
            if(total>8L*1024*1024*1024||total>game.getUsableSpace())throw new IOException("手机剩余空间不足以展开汉化资源");
            Map<String,Path> staged=new LinkedHashMap<>(),targets=new HashMap<>();Map<String,String> hashes=new HashMap<>();Set<String> preserved=new HashSet<>(),repairs=new HashSet<>();int done=0;
            for(Job job:jobs){
                if(Thread.currentThread().isInterrupted())throw new InterruptedIOException("汉化处理已取消");
                String lower=job.item.name.toLowerCase(Locale.ROOT);
                File existing=lookup(game,job.item.name);
                boolean repair=false;
                if(existing!=null){
                    if(!existing.isFile())throw new IOException("资源与目录重名："+job.item.name);
                    String owned=previous.getProperty("resource."+lower,"");
                    boolean managed=owned.matches("[0-9a-f]{64}")&&!Files.isSymbolicLink(existing.toPath())&&owned.equals(digest(existing));
                    repair=managed || job.pack.evb!=null&&!Files.isSymbolicLink(existing.toPath())&&job.pack.evb.matchesStored((PatchEvb.Entry)job.item.entry,existing);
                    if(!repair){if(preserved.add(lower))result.preserved++;continue;}
                }
                Path output=work.resolve("resource-"+(done++));progress.update("正在展开汉化资源："+done+" / "+jobs.size());
                try(OutputStream stream=new BufferedOutputStream(Files.newOutputStream(output,StandardOpenOption.CREATE_NEW))){job.pack.decode(job.item,stream);}
                if(Files.size(output)!=job.item.size)throw new IOException("汉化资源展开长度不符："+job.item.name);
                String hash=digest(output.toFile());
                String seen=hashes.putIfAbsent(lower,hash);
                if(seen!=null&&!seen.equals(hash))throw new IOException("汉化资源存在冲突，请保留同一版本的补丁："+job.item.name);
                if(repair&&hash.equals(digest(existing))){
                    record.setProperty("resource."+lower,hash);Files.delete(output);
                    if(preserved.add(lower))result.preserved++;continue;
                }
                if(staged.containsKey(lower)){
                    if(!hashes.get(lower).equals(hash))throw new IOException("汉化资源存在冲突，请保留同一版本的补丁："+job.item.name);
                    Files.delete(output);continue;
                }
                staged.put(lower,output);hashes.put(lower,hash);record.setProperty("resource."+lower,hash);
                targets.put(lower,repair?existing.toPath():root.resolve(lower));if(repair)repairs.add(lower);
            }
            // Replace only our recorded, unmodified outputs or an exact old
            // compression envelope. Keep rollback copies until publication.
            for(Map.Entry<String,Path> entry:staged.entrySet()){
                Path target=targets.get(entry.getKey());
                if(repairs.contains(entry.getKey())){
                    Path backup=work.resolve("original-"+backups.size());Files.copy(target,backup);backups.put(target,backup);
                    Files.move(entry.getValue(),target,StandardCopyOption.ATOMIC_MOVE,StandardCopyOption.REPLACE_EXISTING);
                }else{Files.move(entry.getValue(),target);installed.add(target);}
            }
            result.resources=staged.size();result.containers=packs.size();result.repaired=repairs.size();
            record.setProperty("profile",result.profile);record.setProperty("encoding",result.encoding);record.setProperty("engine",result.engine);
            record.setProperty("fontFamily",result.fontFamily);
            record.setProperty("expanded",Integer.toString(result.resources));record.setProperty("preserved",Integer.toString(result.preserved));
            record.setProperty("repaired",Integer.toString(result.repaired));
            Path report=work.resolve("translation.properties");try(OutputStream out=Files.newOutputStream(report)){record.store(out,"OpenBGI offline patch import");}
            Files.move(report,config.resolve("translation.properties"),StandardCopyOption.REPLACE_EXISTING);
            return result;
        }catch(IOException|RuntimeException error){
            for(Path path:installed)try{Files.deleteIfExists(path);}catch(IOException cleanup){error.addSuppressed(cleanup);}
            for(Map.Entry<Path,Path> backup:backups.entrySet())try{Files.move(backup.getValue(),backup.getKey(),StandardCopyOption.ATOMIC_MOVE,StandardCopyOption.REPLACE_EXISTING);}
                catch(IOException restore){keepRecovery=true;error.addSuppressed(restore);}
            throw error;
        }
        finally{for(Pack pack:packs)try{pack.close();}catch(IOException ignored){}if(!keepRecovery)GameImport.remove(work);}
    }
}
