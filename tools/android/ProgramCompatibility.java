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
        final Blob blob;final String name,source;final long offset,size;
        Entry(Blob blob,String name,String source,long offset,long size){this.blob=blob;this.name=name;this.source=source;this.offset=offset;this.size=size;}
    }
    static final class Finding {
        final int opcode,kind;int occurrences;
        final List<String> locations=new ArrayList<>();
        Finding(int opcode,int kind){this.opcode=opcode;this.kind=kind;}
    }
    enum Status { UNCHECKED, RECHECK, INCOMPLETE, FINDINGS, PARTIAL, PASS }
    static final class Summary {
        final Status status;
        final int findingCount;
        Summary(Status status,int findingCount){this.status=status;this.findingCount=findingCount;}
    }
    static final class Result {
        String profile="",error="";int programs,checked,skipped,problems;long instructions;boolean limited;
        ArchiveLanguage language;
        final SortedMap<Integer,Finding> findings=new TreeMap<>();
        final List<String> problemLocations=new ArrayList<>();
        Status status(){
            if(!error.isEmpty() || profile.isEmpty())return Status.INCOMPLETE;
            if(!findings.isEmpty())return Status.FINDINGS;
            if(checked==0 || skipped>0 || problems>0 || limited)return Status.PARTIAL;
            return Status.PASS;
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
    private static List<Entry> inventory(ResourceLayers layers,Result r)throws IOException {
        Map<String,Blob> all=new LinkedHashMap<>();Map<String,String> display=new LinkedHashMap<>();
        if(layers.disc!=null){
            Map<String,Long> sizes=new LinkedHashMap<>();Map<String,DiscImage.Entry> named=new HashMap<>();
            for(DiscImage.Entry entry:layers.disc.entries){
                if(!entry.file())continue;
                String key=entry.path.toLowerCase(Locale.ROOT);
                sizes.put(key,entry.size);named.put(key,entry);
            }
            Map<String,String> chosen=new LinkedHashMap<>();
            ArchiveLanguage.addEntries(chosen,sizes,r.language);
            for(Map.Entry<String,String> item:chosen.entrySet()){
                DiscImage.Entry entry=named.get(item.getValue());
                if(entry==null)continue;
                all.put(item.getKey(),Blob.image(layers.disc,entry));display.put(item.getKey(),item.getValue());
            }
        }else{
            for(File layer:layers.directories()){
                Map<String,File> foundMap=new LinkedHashMap<>();
                List<File> found=new ArrayList<>();files(layer,0,found,r);
                ArchiveLanguage.addLayer(foundMap,layer,found,r.language);
                for(Map.Entry<String,File> item:foundMap.entrySet()){
                    all.put(item.getKey(),Blob.file(item.getValue()));
                    display.put(item.getKey(),ArchiveLanguage.displayPath(layers.directories(),item.getValue()));
                }
            }
        }
        Map<String,Entry> loose=new TreeMap<>();List<Entry> entries=new ArrayList<>();
        for(Map.Entry<String,Blob> item:all.entrySet())if(program(item.getKey())){
            Blob blob=item.getValue();String source=display.get(item.getKey());
            if(loose.size()>=MAX_PROGRAMS){r.limited=true;continue;}
            loose.put(item.getKey(),new Entry(blob,source,source,0,blob.size()));
        }
        entries.addAll(loose.values());
        List<String> paths=new ArrayList<>(all.keySet());
        paths.sort(Comparator.comparingInt((String path)->path.equals("system.arc")?0:1).thenComparing(path->path));
        long budget=32L*1024*1024;int archives=0;
        for(String path:paths){
            cancelled();if(!path.endsWith(".arc"))continue;
            Blob blob=all.get(path);
            if(++archives>512 || budget<=0){r.limited=true;break;}
            try{
                long length=blob.size();
                byte[] head=blob.read(0,16);
                String magic=new String(head,0,12,StandardCharsets.US_ASCII);
                boolean arc20=magic.equals("BURIKO ARC20");
                if(!arc20 && !magic.equals("PackFile    ")){r.limited=true;continue;}
                int record=arc20?128:32,names=arc20?96:16;
                long count=ArchiveTextSamples.u32(head,12),bytes=count*record,base=16+bytes;
                if(count>32768 || bytes>budget || base>length){r.limited=true;continue;}
                budget-=bytes;byte[] index=blob.read(16,(int)bytes);
                for(int i=0;i<count;i++){
                    if((i&255)==0)cancelled();int start=i*record,end=0;
                    while(end<names && index[start+end]!=0)end++;
                    String name=new String(index,start,end,StandardCharsets.ISO_8859_1).replace('\\','/');
                    if(!program(name) || loose.containsKey(name.toLowerCase(Locale.ROOT)))continue;
                    if(entries.size()>=MAX_PROGRAMS){r.limited=true;break;}
                    long off=base+ArchiveTextSamples.u32(index,start+names),size=ArchiveTextSamples.u32(index,start+names+4);
                    String source=display.get(path)+" / "+name;
                    if(off>length || size>length-off){r.skipped++;continue;}
                    entries.add(new Entry(blob,name,source,off,size));
                }
            }catch(InterruptedIOException stop){throw stop;}catch(IOException bad){r.limited=true;}
        }
        r.programs=entries.size()+r.skipped;
        return entries;
    }
    private static byte[] read(Entry entry)throws IOException {
        cancelled();
        if(entry.size<16 || entry.size>MAX_PROGRAM)throw new IOException("程序大小超出检查范围");
        byte[] data=entry.blob.read(entry.offset,(int)entry.size);
        byte[] decoded=ArchiveTextSamples.decode(data,MAX_PROGRAM);cancelled();return decoded;
    }
    static File table(File game,File resources){
        File file=new File(resources,"games.json");return file.isFile()?file:new File(game,".openbgi/games.json");
    }
    static byte[] bootProgram(File game,File resources,GameSettings settings)throws IOException{
        try(ResourceLayers layers=ResourceLayers.of(game,resources)){return bootProgram(layers,settings);}
    }
    static byte[] bootProgram(ResourceLayers layers,GameSettings settings)throws IOException{
        Result result=new Result();
        result.language=layers.resolve(settings.archiveLanguage);
        for(Entry entry:inventory(layers,result)){
            if(entry.name.equalsIgnoreCase("ipl._bp") &&
               (entry.source.equalsIgnoreCase("ipl._bp") ||
                ArchiveLanguage.logicalName(entry.source.split(" / ",2)[0]).equals("system.arc")))
                return read(entry);
        }
        return null;
    }
    static Result scan(File game,File resources,String preference,Analyzer analyzer,Progress progress)throws IOException{
        try(ResourceLayers layers=ResourceLayers.of(game,resources)){return scan(layers,preference,analyzer,progress);}
    }
    static Result scan(ResourceLayers layers,String preference,Analyzer analyzer,Progress progress)throws IOException{
        Result r=new Result();
        r.language=layers.resolve(GameSettings.load(layers.game).archiveLanguage);
        List<Entry> entries=inventory(layers,r);byte[] boot=null;long budget=128L*1024*1024;
        if(preference.equals("auto"))for(Entry entry:entries)if(entry.name.equalsIgnoreCase("ipl._bp")){
            try{boot=read(entry);}catch(InterruptedIOException stop){throw stop;}catch(IOException bad){r.limited=true;}
            break;
        }
        File config=table(layers.game,layers.base);
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
    private static String stamp(ResourceLayers layers,String preference,String version,ArchiveLanguage language)throws IOException {
        File game=layers.game;
        File patch=GamePatch.active(game),file=table(game,layers.base);
        File data=DirectGame.privateLayer(game);
        if(data==null)data=ContainerGame.data(game);
        return version+"|"+preference+"|"+file.lastModified()+"|"+file.length()+
            (language==null?"":"|"+language.id)+(patch==null?"":"|"+patch.getName())+
            (data==null?"":"|"+new File(data,".openbgi/translation.properties").lastModified());
    }
    static void save(ResourceLayers layers,String preference,String version,Result result,String report)throws IOException {
        File game=layers.game;
        Path dir=new File(game,".openbgi").toPath();Files.createDirectories(dir);
        Properties p=new Properties();p.setProperty("stamp",stamp(layers,preference,version,result.language));
        p.setProperty("status",result.status().name());p.setProperty("findingCount",Integer.toString(result.findings.size()));
        p.setProperty("report",report);p.setProperty("appVersion",version);
        Path target=dir.resolve("compatibility.properties"),temp=dir.resolve("compatibility.properties.tmp");
        try{
            try(OutputStream out=Files.newOutputStream(temp)){p.store(out,"OpenBGI static compatibility check");}
            Files.move(temp,target,StandardCopyOption.ATOMIC_MOVE,StandardCopyOption.REPLACE_EXISTING);
        }finally{Files.deleteIfExists(temp);}
    }
    static void save(File game,File resources,String preference,String version,Result result,String report)throws IOException {
        try(ResourceLayers layers=ResourceLayers.of(game,resources)){save(layers,preference,version,result,report);}
    }
    static Summary cachedSummary(File game,File resources,String preference,String version){
        try(ResourceLayers layers=ResourceLayers.summary(game,resources)){return cachedSummary(layers,preference,version);}
        catch(IOException unavailable){return new Summary(Status.RECHECK,0);}
    }
    static Summary cachedSummary(ResourceLayers layers,String preference,String version){
        File file=new File(layers.game,".openbgi/compatibility.properties");if(!file.isFile())return new Summary(Status.UNCHECKED,0);
        if(file.length()>512*1024)return new Summary(Status.RECHECK,0);
        try(InputStream in=new FileInputStream(file)){
            Properties p=new Properties();p.load(in);
            ArchiveLanguage language=layers.resolve(GameSettings.load(layers.game).archiveLanguage);
            if(!stamp(layers,preference,version,language).equals(p.getProperty("stamp")))return new Summary(Status.RECHECK,0);
            String status=p.getProperty("status");
            if(status!=null)return new Summary(Status.valueOf(status),Integer.parseInt(p.getProperty("findingCount","0")));
            String legacy=p.getProperty("summary","");
            if(legacy.equals("检查未完成"))return new Summary(Status.INCOMPLETE,0);
            if(legacy.equals("检查不完整"))return new Summary(Status.PARTIAL,0);
            if(legacy.equals("静态检查未发现缺失指令"))return new Summary(Status.PASS,0);
            String prefix="发现 ",suffix=" 种缺失或不支持的指令";
            if(legacy.startsWith(prefix)&&legacy.endsWith(suffix))
                return new Summary(Status.FINDINGS,Integer.parseInt(legacy.substring(prefix.length(),legacy.length()-suffix.length())));
            return new Summary(Status.RECHECK,0);
        }catch(IOException|IllegalArgumentException bad){return new Summary(Status.RECHECK,0);}
    }
}
