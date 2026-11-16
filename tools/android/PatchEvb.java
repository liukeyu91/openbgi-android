/* This file is part of OpenBGI (https://openbgi.net).
 * SPDX-License-Identifier: GPL-2.0-only */
package org.openbgi.port;

import java.io.*;
import java.nio.*;
import java.nio.charset.*;
import java.util.*;

/** Bounded external-layout EVB reader, also used at validated PE offsets. */
final class PatchEvb implements Closeable {
    static final long MAX_RESOURCE=256L*1024*1024;
    static final int MAX_INDEX=32*1024*1024,MAX_ENTRIES=100000;
    static final class Entry {
        final String name;final long offset,stored,size;
        Entry(String name,long offset,long stored,long size){this.name=name;this.offset=offset;this.stored=stored;this.size=size;}
    }
    final List<Entry> entries;final long base;
    private final RandomAccessFile file;
    private PatchEvb(RandomAccessFile file,long base,List<Entry> entries){this.file=file;this.base=base;this.entries=Collections.unmodifiableList(entries);}
    static long u32(byte[] b,int at){return (b[at]&255L)|((b[at+1]&255L)<<8)|((b[at+2]&255L)<<16)|((b[at+3]&255L)<<24);}
    static byte[] exact(RandomAccessFile file,int count)throws IOException{byte[] b=new byte[count];file.readFully(b);return b;}
    static boolean magic(byte[] b){return b.length>=4&&b[0]=='E'&&b[1]=='V'&&b[2]=='B'&&b[3]==0;}
    static void safeName(String name)throws IOException{
        if(name.isEmpty()||name.length()>255||name.equals(".")||name.equals("..")||name.endsWith(".")||name.endsWith(" "))throw new IOException("补丁资源名称无效");
        for(int i=0;i<name.length();i++)if(name.charAt(i)<32||"/\\:<>\"|?*".indexOf(name.charAt(i))>=0)throw new IOException("补丁资源路径无效");
        String stem=name.split("\\.",2)[0].toUpperCase(Locale.ROOT);
        if(stem.matches("CON|PRN|AUX|NUL|COM[1-9]|LPT[1-9]")||name.equalsIgnoreCase(".openbgi"))throw new IOException("补丁资源名称保留");
    }
    static PatchEvb open(File source,long base)throws IOException{
        RandomAccessFile f=new RandomAccessFile(source,"r");
        try{
            long length=f.length();if(base<0||base>length-80)throw new IOException("补丁头越界");
            f.seek(base);if(!magic(exact(f,64)))throw new IOException("补丁格式不匹配");
            long tableSize=u32(exact(f,16),0),end=base+tableSize+68,payload=end;
            if(tableSize+68<80||tableSize+68>MAX_INDEX||end>length)throw new IOException("补丁目录大小无效");
            f.seek(base+79);long pending=1;int nodes=0;List<Entry> entries=new ArrayList<>();Set<String> names=new HashSet<>();
            while(pending>0){
                if(f.getFilePointer()+16>end||++nodes>MAX_ENTRIES)throw new IOException("补丁目录截断或条目过多");
                byte[] header=exact(f,16);long children=u32(header,12);ByteArrayOutputStream text=new ByteArrayOutputStream();
                boolean terminated=false;
                while(f.getFilePointer()+2<=end&&text.size()<=8192){int a=f.readUnsignedByte(),b=f.readUnsignedByte();if(a==0&&b==0){terminated=true;break;}text.write(a);text.write(b);}
                if(!terminated)throw new IOException("补丁文件名未结束");
                String name;
                try{name=StandardCharsets.UTF_16LE.newDecoder().onMalformedInput(CodingErrorAction.REPORT).decode(ByteBuffer.wrap(text.toByteArray())).toString();}
                catch(CharacterCodingException e){throw new IOException("补丁文件名编码无效",e);}
                int kind=f.readUnsignedByte();pending--;
                if(kind==3){
                    if(nodes!=1||!(name.equals("%DEFAULT FOLDER%")||name.isEmpty()))throw new IOException("此补丁使用尚未支持的嵌套目录");
                    exact(f,25);pending+=children;
                }else if(kind==2){
                    byte[] meta=exact(f,53);long size=u32(meta,2),stored=u32(meta,49);safeName(name);
                    if(!names.add(name.toLowerCase(Locale.ROOT)))throw new IOException("补丁含重复资源："+name);
                    if(size>MAX_RESOURCE||stored>MAX_RESOURCE||stored>length-payload)throw new IOException("补丁资源范围无效："+name);
                    entries.add(new Entry(name,payload,stored,size));payload+=stored;
                }else throw new IOException("补丁目录类型不支持");
                if(pending>MAX_ENTRIES||f.getFilePointer()>end)throw new IOException("补丁目录条数无效");
            }
            long padding=end-f.getFilePointer();if(padding!=0&&padding!=4)throw new IOException("补丁目录条数不完整");
            for(int i=0;i<padding;i++)if(f.read()!=0)throw new IOException("补丁目录填充无效");
            return new PatchEvb(f,base,entries);
        }catch(IOException|RuntimeException error){f.close();throw error;}
    }
    /** Scan static bytes; loader string literals are not valid directory candidates. */
    static PatchEvb embedded(File source)throws IOException{
        if(source.length()<64||source.length()>256L*1024*1024)return null;
        List<Long> candidates=new ArrayList<>();
        try(RandomAccessFile f=new RandomAccessFile(source,"r")){
            byte[] dos=exact(f,64);if(dos[0]!='M'||dos[1]!='Z')return null;
            long pe=u32(dos,60);if(pe>f.length()-4)return null;f.seek(pe);byte[] sig=exact(f,4);
            if(sig[0]!='P'||sig[1]!='E'||sig[2]!=0||sig[3]!=0)return null;
            f.seek(0);byte[] block=new byte[65536];long absolute=0;int state=0,n;
            while((n=f.read(block))>0){
                for(int i=0;i<n;i++){
                    int b=block[i]&255;int expected=state==0?'E':state==1?'V':state==2?'B':0;
                    if(b==expected){if(++state==4){candidates.add(absolute+i-3);state=0;if(candidates.size()>128)throw new IOException("补丁嵌入数据候选过多");}}
                    else state=b=='E'?1:0;
                }absolute+=n;
            }
        }
        PatchEvb chosen=null;
        try{
            for(long at:candidates){
                PatchEvb candidate;
                try{candidate=open(source,at);}catch(IOException malformed){continue;}
                if(candidate.entries.isEmpty()){candidate.close();continue;}
                if(chosen!=null){candidate.close();throw new IOException("存在多个有效汉化资源包，无法确定覆盖顺序");}
                chosen=candidate;
            }
            return chosen;
        }catch(IOException error){if(chosen!=null)chosen.close();throw error;}
    }
    void decode(Entry entry,OutputStream out)throws IOException{
        file.seek(entry.offset);
        if(entry.stored==entry.size){copy(file,out,entry.size);return;}
        if(entry.stored<12)throw new IOException("压缩补丁数据截断");
        byte[] h=exact(file,8);long table=u32(h,0);
        if(table<12||table>entry.stored||table>MAX_INDEX||(table-8)%4!=0)throw new IOException("压缩块目录无效");
        byte[] blocks=exact(file,(int)table-8);List<Integer> sizes=new ArrayList<>();long packed=0;
        for(int at=0;at<blocks.length;at+=12){long n=u32(blocks,at);if(n<=0||n>1024*1024)throw new IOException("压缩块大小无效");sizes.add((int)n);packed+=n;}
        if(packed!=entry.stored-table)throw new IOException("压缩块总长度不符");
        long done=0;
        for(int n:sizes){
            if(Thread.currentThread().isInterrupted())throw new InterruptedIOException("汉化处理已取消");
            byte[] raw=PatchAplib.decompress(exact(file,n),(int)Math.min(65536,entry.size-done));
            if(raw.length==0||raw.length>entry.size-done)throw new IOException("补丁展开长度不符");
            out.write(raw);done+=raw.length;
        }
        if(done!=entry.size)throw new IOException("补丁展开长度不符");
    }
    private static void copy(RandomAccessFile in,OutputStream out,long remaining)throws IOException{
        byte[] buffer=new byte[65536];while(remaining>0){if(Thread.currentThread().isInterrupted())throw new InterruptedIOException("汉化处理已取消");int n=in.read(buffer,0,(int)Math.min(buffer.length,remaining));if(n<0)throw new EOFException("补丁载荷截断");out.write(buffer,0,n);remaining-=n;}
    }
    public void close()throws IOException{file.close();}
}
