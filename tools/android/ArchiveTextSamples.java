/* SPDX-License-Identifier: GPL-2.0-only
 * Read-only bounded sampler. DSC/container logic follows src/core/dsc.c,
 * hash.c and src/scn/scnfile.c. It never executes game programs.
 */
package org.openbgi.port;
import java.io.*;
import java.nio.charset.StandardCharsets;
import java.nio.file.*;
import java.util.*;

final class ArchiveTextSamples {
    static final int MAX_FILE=4*1024*1024, MAX_SAMPLES=160;
    static final class Sample {
        final String source;final byte[] bytes;
        Sample(String source,byte[] bytes){this.source=source;this.bytes=bytes;}
    }
    static final class Result {
        final List<Sample> samples=new ArrayList<>();
        int archives,scenes,skipped;boolean limited;
    }
    private static final class Entry {
        Blob blob;String name,source;long offset,size;
        Entry(Blob blob,String n,long o,long s,String source){this.blob=blob;name=n;offset=o;size=s;this.source=source;}
    }
    private static void cancelled() throws InterruptedIOException {
        if(Thread.currentThread().isInterrupted())throw new InterruptedIOException("已取消");
    }
    static long u32(byte[] b,int p)throws IOException {
        if(p<0 || p>b.length-4)throw new IOException("数据截断");
        return (b[p]&255L)|((b[p+1]&255L)<<8)|((b[p+2]&255L)<<16)|((b[p+3]&255L)<<24);
    }
    static boolean magic(byte[] b,String s){
        byte[] m=s.getBytes(StandardCharsets.US_ASCII);if(b.length<m.length)return false;
        for(int i=0;i<m.length;i++)if(b[i]!=m[i])return false;return true;
    }
    private static int priority(String name){
        String n=name.toLowerCase(Locale.ROOT);
        if(n.contains("prologue") || n.startsWith("start") || n.startsWith("opening"))return 0;
        if(n.equals("main") || n.equals("init") || n.startsWith("common"))return 1;
        return 2;
    }
    private static boolean possible(String n){
        String s=n.toLowerCase(Locale.ROOT);
        return !s.isEmpty() && !s.contains("/") && !s.contains("\\") && !s.contains("..") && !s.startsWith(".") &&
            (s.indexOf('.')<0 || s.endsWith(".bsc") || s.endsWith(".bss") || s.endsWith(".scn"));
    }
    private static void files(File dir,int depth,List<File> out,Result r)throws IOException {
        cancelled();if(depth>8){r.limited=true;return;}
        File[] list=dir.listFiles();if(list==null)return;
        // Deterministic bounded traversal; do not traverse links, saves or app config.
        Arrays.sort(list,Comparator.comparing(File::getName));
        for(File f:list){
            if(out.size()>=12000){r.limited=true;return;}
            if(Files.isSymbolicLink(f.toPath()))continue;
            if(f.isDirectory()){
                String n=f.getName();if(!n.startsWith(".") && !n.equalsIgnoreCase("UserData") && !n.equalsIgnoreCase("Backup"))files(f,depth+1,out,r);
            }else if(f.isFile())out.add(f);
        }
    }
    static Result scan(File game)throws IOException {
        return scan(game,null);
    }
    static Result scan(File game,ArchiveLanguage language)throws IOException {
        return scan(game,null,language);
    }
    static Result scan(File game,File patch,ArchiveLanguage language)throws IOException {
        return scan(Arrays.asList(game,patch),language);
    }
    static Result scan(List<File> layers,ArchiveLanguage language)throws IOException {
        return scan(layers,language,false);
    }
    static Result scanPatch(File patch)throws IOException {
        return scan(Collections.singletonList(patch),null,true);
    }
    /** Directories or a disc image: a disc game's base layer is the mounted index. */
    static Result scan(ResourceLayers layers,ArchiveLanguage language)throws IOException {
        return layers.disc==null?scan(layers.directories(),language,false):scanImage(layers.disc,language);
    }
    private static Result scan(List<File> layers,ArchiveLanguage language,boolean patchCheck)throws IOException {
        Result r=new Result();Map<String,File> all=new LinkedHashMap<>();
        for(File layer:layers)if(layer!=null){
            List<File> found=new ArrayList<>();files(layer,0,found,r);
            ArchiveLanguage.addLayer(all,layer,found,language);
        }
        List<Entry> entries=new ArrayList<>();Set<String> loose=new HashSet<>();long[] indexBudget={16*1024*1024};
        for(Map.Entry<String,File> item:all.entrySet())if(possible(patchCheck?item.getValue().getName():item.getKey())){
            String name=item.getKey();File file=item.getValue();
            loose.add(name);
            if(file.length()>0 && file.length()<=MAX_FILE)
                entries.add(new Entry(Blob.file(file),name,0,file.length(),ArchiveLanguage.displayPath(layers,file)));
        }
        for(Map.Entry<String,File> item:all.entrySet()){
            cancelled();String path=item.getKey();
            if(!path.endsWith(".arc"))continue;
            if(r.archives>=128 || indexBudget[0]<=0){r.limited=true;break;}r.archives++;
            archive(Blob.file(item.getValue()),ArchiveLanguage.displayPath(layers,item.getValue()),loose,entries,r,indexBudget);
        }
        sample(entries,r,patchCheck);
        return r;
    }
    /** The same two passes over an image index: no directory is walked. */
    private static Result scanImage(DiscImage.Source disc,ArchiveLanguage language)throws IOException {
        Result r=new Result();
        Map<String,Long> sizes=new LinkedHashMap<>();Map<String,DiscImage.Entry> named=new HashMap<>();
        for(DiscImage.Entry entry:disc.entries){
            if(!entry.file())continue;
            String key=entry.path.toLowerCase(Locale.ROOT);
            sizes.put(key,entry.size);named.put(key,entry);
        }
        Set<String> loose=new HashSet<>();long[] indexBudget={16*1024*1024};
        for(String name:sizes.keySet())if(possible(name))loose.add(name);
        Map<String,String> all=new LinkedHashMap<>();
        ArchiveLanguage.addEntries(all,sizes,language);
        List<Entry> entries=new ArrayList<>();
        for(Map.Entry<String,String> item:all.entrySet()){
            DiscImage.Entry entry=named.get(item.getValue());
            if(entry==null || !possible(item.getKey()))continue;
            if(entry.size>0 && entry.size<=MAX_FILE)
                entries.add(new Entry(Blob.image(disc,entry),item.getKey(),0,entry.size,item.getValue()));
        }
        for(Map.Entry<String,String> item:all.entrySet()){
            cancelled();String path=item.getKey();
            if(!path.endsWith(".arc"))continue;
            DiscImage.Entry entry=named.get(item.getValue());
            if(entry==null)continue;
            if(r.archives>=128 || indexBudget[0]<=0){r.limited=true;break;}r.archives++;
            archive(Blob.image(disc,entry),item.getValue(),loose,entries,r,indexBudget);
        }
        sample(entries,r,false);
        return r;
    }
    /** Append one archive's scene members; they read through the archive's own blob. */
    private static void archive(Blob blob,String display,Set<String> loose,List<Entry> entries,Result r,long[] indexBudget)throws IOException {
        byte[] h;
        try{h=blob.read(0,16);}catch(EOFException shortFile){r.skipped++;r.limited=true;return;}
        boolean arc20=magic(h,"BURIKO ARC20");
        if(!arc20 && !magic(h,"PackFile    "))return;
        int record=arc20?128:32,names=arc20?96:16;long count=u32(h,12),base=16+count*record,length=blob.size();
        if(count>20000 || base>length || base-16>indexBudget[0]){r.skipped++;r.limited=true;return;}
        indexBudget[0]-=base-16;
        byte[] e=blob.read(16,(int)(base-16));
        for(int i=0;i<count;i++){
            if((i&255)==0)cancelled();int start=i*record,end=0;while(end<names && e[start+end]!=0)end++;
            String name=new String(e,start,end,StandardCharsets.ISO_8859_1);
            if(!possible(name) || loose.contains(name.toLowerCase(Locale.ROOT)))continue;
            long off=base+u32(e,start+names),size=u32(e,start+names+4);
            if(size<=0 || size>MAX_FILE || off>length || size>length-off)continue;
            if(entries.size()<8192)entries.add(new Entry(blob,name,off,size,display+" / "+name));
            else r.limited=true;
        }
    }
    private static void sample(List<Entry> entries,Result r,boolean patchCheck)throws IOException {
        entries.sort(Comparator.comparingInt((Entry e)->priority(e.name)).thenComparing(e->e.name).thenComparing(e->e.source));
        Set<String> seen=new HashSet<>();long budget=32*1024*1024;int tried=0;
        for(Entry e:entries){
            cancelled();if(r.samples.size()>=MAX_SAMPLES || r.scenes>=8 || tried++>=128 || budget<=0){r.limited=true;break;}
            if(!seen.add(e.name.toLowerCase(Locale.ROOT)))continue;
            try{
                byte[] prefix=e.blob.read(e.offset,(int)Math.min(e.size,32));
                if(!magic(prefix,"DSC FORMAT 1.00\0") && !magic(prefix,"BurikoCompiledScriptVer1.00\0") &&
                    !(prefix.length>=20 && u32(prefix,0)==0x10 && u32(prefix,4)==0 && u32(prefix,12)==0x20 && u32(prefix,16)==0x11))continue;
                long expanded=magic(prefix,"DSC FORMAT 1.00\0")?u32(prefix,20):e.size;
                if(expanded>MAX_FILE || e.size+expanded>budget){r.skipped++;r.limited=true;continue;}
                budget-=e.size+expanded;
                byte[] stored=e.blob.read(e.offset,(int)e.size);
                byte[] data=decode(stored);List<byte[]> strings=strings(data);
                int added=0;
                for(byte[] s:strings){
                    if(r.samples.size()>=MAX_SAMPLES || added>=24)break;
                    int high=0;for(byte v:s)if((v&128)!=0)high++;
                    if((patchCheck||high>=4) && s.length>=6 && s.length<=512){r.samples.add(new Sample(e.source,s));added++;}
                }
                if(added>0)r.scenes++;
            }catch(InterruptedIOException stop){throw stop;}catch(IOException bad){r.skipped++;}
        }
    }
    static List<byte[]> strings(byte[] data)throws IOException {
        int base;
        if(magic(data,"BurikoCompiledScriptVer1.00\0")){
            long b=28+u32(data,28);if(b<36 || b>data.length || (b&3)!=0)throw new IOException("脚本头错误");base=(int)b;
        }else if(data.length>=20 && u32(data,0)==0x10 && u32(data,4)==0 && u32(data,12)==0x20 && u32(data,16)==0x11)base=0;
        else throw new IOException("不是支持的场景容器");
        int end=data.length-base,q=base;Set<Integer> references=new LinkedHashSet<>();
        while(q<=data.length-4 && q-base<end){
            long token=u32(data,q);int operands=0,reference=-1;
            if(token<=3 || token==8 || token==9 || token==10 || token==0x19 || token==0x3f)operands=1;
            if(token==0x7f)operands=2;if(token==0x7b)operands=3;
            if(q>data.length-4-operands*4)throw new IOException("指令截断");
            if(token==3 || token==0x7f)reference=q+4;if(token==0x7b)reference=q+12;
            if(reference>=0){long off=u32(data,reference);if(off<end)end=(int)off;if(token==3 && off<=Integer.MAX_VALUE && references.size()<8192)references.add((int)off);}
            q+=4+operands*4;
        }
        if(q-base!=end || end<0 || (end&3)!=0 || (data.length>base+end && data[data.length-1]!=0))throw new IOException("字符串表边界错误");
        List<byte[]> result=new ArrayList<>();
        // SCN_LINE/SRCINFO source paths are metadata, not dialogue evidence.
        for(int ref:references){
            if(ref<end || ref>=data.length-base)throw new IOException("字符串引用越界");
            int start=base+ref,i=start;while(i<data.length && data[i]!=0 && i-start<=512)i++;
            if(i<data.length && i>start && i-start<=512){
                byte[] text=Arrays.copyOfRange(data,start,i);
                // A SJIS trail byte may itself be 0x5c (e.g. ソ): do not reject
                // arbitrary backslash bytes as if they were path separators.
                boolean path=text.length>=3 && ((text[0]>='A' && text[0]<='Z') || (text[0]>='a' && text[0]<='z')) && text[1]==':' && (text[2]=='/' || text[2]=='\\');
                if(!path)result.add(text);
            }
        }
        return result;
    }
    private static final class Bits {
        final byte[] data;int at=0x220,bit;
        Bits(byte[] data){this.data=data;}
        int read(int count)throws IOException {
            int value=0;for(int i=0;i<count;i++){
                if(at>=data.length)throw new EOFException("DSC 位流截断");
                value=(value<<1)|((data[at]>>(7-bit))&1);if(++bit==8){bit=0;at++;}
            }return value;
        }
    }
    static byte[] decode(byte[] src)throws IOException {
        return decode(src,MAX_FILE);
    }
    static byte[] decode(byte[] src,int maxFile)throws IOException {
        if(!magic(src,"DSC FORMAT 1.00\0"))return src;
        if(src.length<0x220)throw new IOException("DSC 头截断");
        long n=u32(src,20),symbols=u32(src,24);if(n>maxFile || symbols>n || n==0)throw new IOException("DSC 长度无效");
        int key=(int)u32(src,16);List<Integer> codes=new ArrayList<>();
        for(int i=0;i<512;i++){
            long lo=20021L*(key&65535),hi=(20021L*(key>>>16)+346L*(key&0xffffffffL)+(lo>>>16))&65535;
            key=(int)((hi<<16)+(lo&65535)+1);int len=((src[32+i]&255)-(int)(hi&255))&255;
            if(len>0){if(len>32)throw new IOException("DSC 码长无效");codes.add((len<<16)|i);}
        }
        if(codes.isEmpty())throw new IOException("DSC 空码表");Collections.sort(codes);
        int[][] child=new int[1023][2];int[] value=new int[1023];Arrays.fill(value,-1);
        List<Integer> level=new ArrayList<>();level.add(0);int next=1,index=0;
        for(int depth=0;index<codes.size();depth++){
            if(depth>32 || level.isEmpty())throw new IOException("DSC 树无效");
            List<Integer> following=new ArrayList<>();int used=0;
            while(index<codes.size() && (codes.get(index)>>>16)==depth){
                if(used>=level.size())throw new IOException("DSC 码表过满");value[level.get(used++)]=codes.get(index++)&511;
            }
            if(index<codes.size())for(int i=used;i<level.size();i++){
                if(next+1>=1023)throw new IOException("DSC 树过大");int node=level.get(i);
                child[node][0]=next;child[node][1]=next+1;following.add(next++);following.add(next++);
            }
            else if(used!=level.size())throw new IOException("DSC 码表不完整");
            level=following;
        }
        byte[] out=new byte[(int)n];Bits bits=new Bits(src);int pos=0;
        for(int i=0;i<symbols;i++){
            if((i&1023)==0)cancelled();int node=0,depth=0;
            while(value[node]<0){if(++depth>32)throw new IOException("DSC 树循环");node=child[node][bits.read(1)];}
            int symbol=value[node];
            if(symbol<256){if(pos>=out.length)throw new IOException("DSC 输出溢出");out[pos++]=(byte)symbol;}
            else{
                int distance=bits.read(12)+2,count=(symbol&255)+2;
                if(distance>pos || count>out.length-pos)throw new IOException("DSC 回溯越界");
                for(int j=0;j<count;j++){out[pos]=out[pos-distance];pos++;}
            }
        }
        if(pos!=out.length)throw new IOException("DSC 输出长度不符");return out;
    }
}
