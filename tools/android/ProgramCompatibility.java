/* SPDX-License-Identifier: GPL-2.0-only */
package org.openbgi.port;
import java.io.*;
import java.nio.charset.StandardCharsets;
import java.nio.file.*;
import java.util.*;

/** Inventories imported VM programs, never executes them or extracts archive members. */
final class ProgramCompatibility {
    static final int MAX_PROGRAM=8*1024*1024,MAX_PROGRAMS=4096,MAX_DETAILS=32;
    interface Analyzer {
        String select(String gamesJson,String profile,byte[] boot);
        int[] analyze(byte[] program);
    }
    interface Progress { void update(int done,int total); }
    private static final class Entry {
        final File file;final String name,source;final long offset,size;
        Entry(File file,String name,String source,long offset,long size){this.file=file;this.name=name;this.source=source;this.offset=offset;this.size=size;}
    }
    static final class Finding {
        final int opcode,kind;int occurrences;
        final List<String> locations=new ArrayList<>();
        Finding(int opcode,int kind){this.opcode=opcode;this.kind=kind;}
    }
    static final class Result {
        String profile="",error="";int programs,checked,skipped,problems;long instructions;boolean limited;
        final SortedMap<Integer,Finding> findings=new TreeMap<>();
        final List<String> problemLocations=new ArrayList<>();
        String summary(){
            if(!error.isEmpty() || profile.isEmpty())return "检查未完成";
            if(!findings.isEmpty())return "发现 "+findings.size()+" 种缺失或不支持的指令";
            if(checked==0 || skipped>0 || problems>0 || limited)return "检查不完整";
            return "静态检查未发现缺失指令";
        }
        String report(){
            StringBuilder text=new StringBuilder(summary()).append("\n\n引擎版本：").append(profile.isEmpty()?"未识别":profile)
                .append("\n已检查 ").append(checked).append(" / ").append(programs).append(" 个程序，").append(instructions).append(" 条指令。");
            if(!error.isEmpty())text.append("\n").append(error);
            if(profile.isEmpty())text.append("\n请在游戏设置中选择引擎版本后重新检查。");
            int shown=0;
            for(Finding finding:findings.values()){
                if(shown++==MAX_DETAILS){text.append("\n\n其余结果未展开。");break;}
                text.append("\n\n$").append(String.format(Locale.ROOT,finding.opcode>255?"%04X":"%02X",finding.opcode))
                    .append(finding.kind==1?"：尚未实现":finding.kind==3?"：当前平台无法启动外部程序":"：当前版本不支持");
                if(finding.kind==3)text.append("\n指令已接入，启动失败会返回脚本；依赖外部 Windows EXE 的功能仍不可用。");
                for(String location:finding.locations)text.append("\n").append(location);
                if(finding.occurrences>finding.locations.size())text.append("\n另有 ").append(finding.occurrences-finding.locations.size()).append(" 个程序使用。");
            }
            if(skipped>0)text.append("\n\n未能读取或分析 ").append(skipped).append(" 个程序。");
            if(problems>0){
                text.append("\n有 ").append(problems).append(" 个程序的文件头或代码存在异常。");
                for(String location:problemLocations)text.append("\n").append(location);
            }
            if(limited)text.append("\n部分资源未纳入检查，结果不完整。");
            text.append("\n\n检查范围为导入资源中的 ._bp 程序。动态加载、Windows EXE 内的资源和其他脚本格式不在此检查范围。\n静态检查不代表完整流程兼容。");
            return text.toString();
        }
    }
    private static void cancelled()throws InterruptedIOException {
        if(Thread.currentThread().isInterrupted())throw new InterruptedIOException("检查已取消");
    }
    private static void files(File dir,int depth,List<File> out,Result r)throws IOException {
        cancelled();if(depth>16){r.limited=true;return;}
        File[] list=dir.listFiles();if(list==null)throw new IOException("无法读取游戏目录");
        Arrays.sort(list,Comparator.comparing(File::getName));
        for(File file:list){
            if(out.size()>=20000){r.limited=true;return;}
            if(Files.isSymbolicLink(file.toPath())){r.limited=true;continue;}
            if(file.isDirectory()){
                String name=file.getName();
                if(!name.startsWith(".") && !name.equalsIgnoreCase("UserData") && !name.equalsIgnoreCase("Backup"))files(file,depth+1,out,r);
            }else if(file.isFile())out.add(file);
        }
    }
    private static boolean program(String name){return name.toLowerCase(Locale.ROOT).endsWith("._bp");}
    private static List<Entry> inventory(File root,Result r)throws IOException {
        List<File> all=new ArrayList<>();files(root,0,all,r);
        Map<String,Entry> loose=new TreeMap<>();List<Entry> entries=new ArrayList<>();
        for(File file:all)if(program(file.getName())){
            String name=root.toPath().relativize(file.toPath()).toString().replace('\\','/');
            String key=name.toLowerCase(Locale.ROOT);
            if(loose.containsKey(key)){r.limited=true;continue;}
            if(loose.size()>=MAX_PROGRAMS){r.limited=true;continue;}
            loose.put(key,new Entry(file,name,name,0,file.length()));
        }
        entries.addAll(loose.values());
        all.sort(Comparator.comparingInt((File f)->f.getName().equalsIgnoreCase("system.arc")?0:1).thenComparing(File::toString));
        long budget=32L*1024*1024;int archives=0;
        for(File file:all){
            cancelled();if(!file.getName().toLowerCase(Locale.ROOT).endsWith(".arc"))continue;
            if(++archives>512 || budget<=0){r.limited=true;break;}
            try(RandomAccessFile in=new RandomAccessFile(file,"r")){
                byte[] head=new byte[16];in.readFully(head);
                String magic=new String(head,0,12,StandardCharsets.US_ASCII);
                boolean arc20=magic.equals("BURIKO ARC20");
                if(!arc20 && !magic.equals("PackFile    ")){r.limited=true;continue;}
                int record=arc20?128:32,names=arc20?96:16;
                long count=ArchiveTextSamples.u32(head,12),bytes=count*record,base=16+bytes;
                if(count>32768 || bytes>budget || base>in.length()){r.limited=true;continue;}
                budget-=bytes;byte[] index=new byte[(int)bytes];in.readFully(index);
                for(int i=0;i<count;i++){
                    if((i&255)==0)cancelled();int start=i*record,end=0;
                    while(end<names && index[start+end]!=0)end++;
                    String name=new String(index,start,end,StandardCharsets.ISO_8859_1).replace('\\','/');
                    if(!program(name) || loose.containsKey(name.toLowerCase(Locale.ROOT)))continue;
                    if(entries.size()>=MAX_PROGRAMS){r.limited=true;break;}
                    long off=base+ArchiveTextSamples.u32(index,start+names),size=ArchiveTextSamples.u32(index,start+names+4);
                    String source=root.toPath().relativize(file.toPath()).toString()+" / "+name;
                    if(off>in.length() || size>in.length()-off){r.skipped++;continue;}
                    entries.add(new Entry(file,name,source,off,size));
                }
            }catch(InterruptedIOException stop){throw stop;}catch(IOException bad){r.limited=true;}
        }
        r.programs=entries.size()+r.skipped;
        return entries;
    }
    private static byte[] read(Entry entry)throws IOException {
        cancelled();
        if(entry.size<16 || entry.size>MAX_PROGRAM)throw new IOException("程序大小超出检查范围");
        byte[] data=new byte[(int)entry.size];
        try(RandomAccessFile in=new RandomAccessFile(entry.file,"r")){in.seek(entry.offset);in.readFully(data);}
        byte[] decoded=ArchiveTextSamples.decode(data,MAX_PROGRAM);cancelled();return decoded;
    }
    private static File table(File game,File resources){
        File file=new File(resources,"games.json");return file.isFile()?file:new File(game,".openbgi/games.json");
    }
    static Result scan(File game,File resources,String preference,Analyzer analyzer,Progress progress)throws IOException {
        Result r=new Result();List<Entry> entries=inventory(resources,r);byte[] boot=null;long budget=128L*1024*1024;
        if(preference.equals("auto"))for(Entry entry:entries)if(entry.name.equalsIgnoreCase("ipl._bp")){
            try{boot=read(entry);}catch(InterruptedIOException stop){throw stop;}catch(IOException bad){r.limited=true;}
            break;
        }
        File config=table(game,resources);
        String profile=analyzer.select(config.isFile()?config.toString():"",preference,boot);
        if(profile==null || profile.isEmpty())return r;
        r.profile=profile;int done=0;
        for(Entry entry:entries){
            cancelled();progress.update(done++,entries.size());
            if(budget<entry.size){r.skipped++;r.limited=true;continue;}
            try{
                byte[] data=read(entry);budget-=entry.size+data.length;
                if(budget<0){r.skipped++;r.limited=true;continue;}
                int[] result=analyzer.analyze(data);
                cancelled();
                if(result==null || result.length<5 || result[0]!=1){r.skipped++;continue;}
                int count=result[4];
                if(count<0 || count>64 || result.length!=5+count*3)throw new IOException("无效的检查结果");
                r.checked++;r.instructions+=result[1];
                if(result[2]>0){
                    r.problems++;
                    if(r.problemLocations.size()<8){
                        String source=entry.source;
                        if(source.length()>480)source=source.substring(0,128)+"…"+source.substring(source.length()-350);
                        r.problemLocations.add(source);
                    }
                }
                if(result[3]>count)r.limited=true;
                for(int i=0;i<count;i++){
                    int opcode=result[5+i*3],offset=result[6+i*3],kind=result[7+i*3],key=(kind<<16)|opcode;
                    Finding finding=r.findings.get(key);
                    if(finding==null){finding=new Finding(opcode,kind);r.findings.put(key,finding);}
                    finding.occurrences++;
                    String source=entry.source;
                    if(source.length()>480)source=source.substring(0,128)+"…"+source.substring(source.length()-350);
                    if(finding.locations.size()<3)finding.locations.add(source+String.format(Locale.ROOT," @ $%08X",offset));
                }
            }catch(InterruptedIOException stop){throw stop;}catch(IOException bad){r.skipped++;}
        }
        progress.update(entries.size(),entries.size());return r;
    }
    private static String stamp(File game,File resources,String preference,String version){
        File file=table(game,resources);
        return version+"|"+preference+"|"+file.lastModified()+"|"+file.length();
    }
    static void save(File game,File resources,String preference,String version,Result result)throws IOException {
        Path dir=new File(game,".openbgi").toPath();Files.createDirectories(dir);
        Properties p=new Properties();p.setProperty("stamp",stamp(game,resources,preference,version));
        p.setProperty("summary",result.summary());p.setProperty("report",result.report());p.setProperty("appVersion",version);
        Path target=dir.resolve("compatibility.properties"),temp=dir.resolve("compatibility.properties.tmp");
        try{
            try(OutputStream out=Files.newOutputStream(temp)){p.store(out,"OpenBGI static compatibility check");}
            Files.move(temp,target,StandardCopyOption.ATOMIC_MOVE,StandardCopyOption.REPLACE_EXISTING);
        }finally{Files.deleteIfExists(temp);}
    }
    static String cachedSummary(File game,File resources,String preference,String version){
        File file=new File(game,".openbgi/compatibility.properties");if(!file.isFile())return "兼容性：未检查";
        if(file.length()>512*1024)return "兼容性：需要重新检查";
        try(InputStream in=new FileInputStream(file)){
            Properties p=new Properties();p.load(in);
            if(!stamp(game,resources,preference,version).equals(p.getProperty("stamp")))return "兼容性：需要重新检查";
            return p.getProperty("summary","兼容性：未检查");
        }catch(IOException|IllegalArgumentException bad){return "兼容性：未检查";}
    }
}
