/* SPDX-License-Identifier: GPL-2.0-only */
package org.openbgi.port;

import java.io.*;
import java.nio.charset.StandardCharsets;
import java.util.*;
import java.util.zip.DataFormatException;
import java.util.zip.Inflater;

/** Bounded CPK-v2 resource reader. No target DLL is loaded or executed. */
final class PatchCpk implements Closeable {
    static final long MAX_RESOURCE = 256L * 1024 * 1024;
    static final int MAX_ENTRIES = 100000, MAX_READ = 32 * 1024 * 1024;

    static final class Entry {
        final String name;
        final long offset, stored, size;
        private final byte[] key, nonce;
        private Entry(String name,long offset,long stored,long size,byte[] key,byte[] nonce) {
            this.name=name;this.offset=offset;this.stored=stored;this.size=size;
            this.key=key;this.nonce=nonce;
        }
    }

    final List<Entry> entries;
    private final RandomAccessFile file;
    private final Set<Entry> owned;
    private final long length, modified;
    private final File path;
    private boolean closed;

    private PatchCpk(File path,RandomAccessFile file,List<Entry> entries,long length,long modified) {
        this.path=path;this.file=file;this.entries=Collections.unmodifiableList(entries);
        this.owned=Collections.newSetFromMap(new IdentityHashMap<Entry,Boolean>());
        this.owned.addAll(entries);this.length=length;this.modified=modified;
    }

    static long u32(byte[] b,int at) {
        return (b[at]&255L)|((b[at+1]&255L)<<8)|((b[at+2]&255L)<<16)|((b[at+3]&255L)<<24);
    }

    static PatchCpk open(File archive,File sidecar) throws IOException {
        long keySize=sidecar.length();
        if(keySize<40 || keySize>40L+MAX_ENTRIES*72L)throw bad("密钥长度无效");
        byte[] raw=new byte[(int)keySize];
        try(DataInputStream input=new DataInputStream(new FileInputStream(sidecar))) {
            input.readFully(raw);
            if(input.read()!=-1)throw bad("密钥文件在读取时发生变化");
        }
        for(int i=0;i<raw.length;i++)raw[i]^=0x13;
        long count=u32(raw,4);
        if(raw[0]!='C'||raw[1]!='P'||raw[2]!='K'||raw[3]!=0 || count>MAX_ENTRIES || keySize!=40+count*72)
            throw bad("密钥头或条数无效");
        RandomAccessFile file=new RandomAccessFile(archive,"r");
        try {
            long length=file.length(),modified=archive.lastModified(),base=140+count*44;
            if(base>length)throw bad("目录被截断");
            byte[] header=new byte[140];file.readFully(header);
            for(int i=0;i<header.length;i++)header[i]^=raw[8+i%32];
            if(header[0]!='C'||header[1]!='P'||header[2]!='K'||header[3]!=0 || u32(header,4)!=2 ||u32(header,136)!=count)
                throw bad("资源包与密钥不匹配");
            List<Entry> entries=new ArrayList<>((int)count);Set<String> names=new HashSet<>();
            for(int i=0;i<count;i++) {
                byte[] record=new byte[44];file.readFully(record);int keyAt=40+i*72;
                for(int j=0;j<record.length;j++)record[j]^=raw[keyAt+j%32];
                String name=name(record);
                if(!names.add(name.toLowerCase(Locale.ROOT)))throw bad("资源名称重复："+name);
                long size=u32(record,32),stored=u32(record,36),offset=base+u32(record,40);
                if(size>MAX_RESOURCE||stored>MAX_RESOURCE||offset>length||stored>length-offset)
                    throw bad("资源长度或范围无效："+name);
                if(u32(raw,keyAt+32)!=16||u32(raw,keyAt+36)!=16)throw bad("不支持的密钥或 nonce 长度");
                entries.add(new Entry(name,offset,stored,size,
                    Arrays.copyOfRange(raw,keyAt+40,keyAt+56),Arrays.copyOfRange(raw,keyAt+56,keyAt+72)));
            }
            List<Entry> ranges=new ArrayList<>(entries);
            Collections.sort(ranges,(a,b)->Long.compare(a.offset,b.offset));
            long end=base;
            for(Entry entry:ranges)if(entry.stored>0) {
                if(entry.offset<end)throw bad("资源载荷范围重叠");
                end=entry.offset+entry.stored;
            }
            Arrays.fill(raw,(byte)0);
            return new PatchCpk(archive,file,entries,length,modified);
        } catch(IOException|RuntimeException error) {
            Arrays.fill(raw,(byte)0);file.close();throw error;
        }
    }

    private static String name(byte[] record) throws IOException {
        int end=0;
        while(end<32 && record[end]!=0) {
            int c=record[end]&255;
            if(c<32||c>126||"/\\:<>\"|?*".indexOf(c)>=0)throw bad("非法资源名称");
            end++;
        }
        if(end==0)throw bad("空资源名称");
        String name=new String(record,0,end,StandardCharsets.US_ASCII);
        if(name.equals(".")||name.equals("..")||name.endsWith(" ")||name.endsWith("."))throw bad("非法资源名称");
        String stem=name.split("\\.",2)[0].toUpperCase(Locale.ROOT);
        if(stem.matches("CON|PRN|AUX|NUL|COM[1-9]|LPT[1-9]"))throw bad("保留资源名称");
        return name;
    }

    /** EAX nonce OMAC: CMAC(key, 16 zero bytes || 16-byte nonce). No tag is stored. */
    static byte[] nonceCounter(PatchCamellia cipher,byte[] nonce) {
        byte[] zero=new byte[16],l=new byte[16],k1=new byte[16],last=new byte[16],result=new byte[16];
        cipher.processBlock(zero,0,l,0);
        int carry=0;
        for(int i=15;i>=0;i--) { int v=l[i]&255;k1[i]=(byte)((v<<1)|carry);carry=v>>>7; }
        if(carry!=0)k1[15]^=(byte)0x87;
        for(int i=0;i<16;i++)last[i]=(byte)(nonce[i]^k1[i]^l[i]);
        cipher.processBlock(last,0,result,0);
        return result;
    }

    static void increment(byte[] counter) {
        for(int i=counter.length-1;i>=0;i--)if(++counter[i]!=0)break;
    }

    synchronized void decode(Entry entry,OutputStream output) throws IOException {
        if(closed)throw bad("资源包已关闭");
        if(!owned.contains(entry))throw bad("资源条目不属于此包");
        if(file.length()!=length||path.lastModified()!=modified)throw bad("资源包在读取时发生变化");
        PatchCamellia cipher=new PatchCamellia(entry.key);
        byte[] counter=nonceCounter(cipher,entry.nonce),mask=new byte[16];
        byte[] input=new byte[65536],decoded=new byte[65536];
        Inflater inflater=new Inflater();long remaining=entry.stored,written=0;
        file.seek(entry.offset);
        try {
            while(remaining>0) {
                int n=(int)Math.min(input.length,remaining);file.readFully(input,0,n);remaining-=n;
                for(int at=0;at<n;at+=16) {
                    cipher.processBlock(counter,0,mask,0);increment(counter);
                    for(int j=0;j<16 && at+j<n;j++)input[at+j]^=mask[j];
                }
                inflater.setInput(input,0,n);
                while(!inflater.needsInput()&&!inflater.finished()) {
                    int got=inflater.inflate(decoded);
                    if(got>entry.size-written)throw bad("解压结果超过声明长度");
                    if(got>0) { output.write(decoded,0,got);written+=got; }
                    else if(inflater.needsDictionary())throw bad("不支持的压缩字典");
                    else if(!inflater.needsInput()&&!inflater.finished())throw bad("压缩流无法继续解码");
                }
                if(inflater.finished()&&(remaining!=0||inflater.getRemaining()!=0))throw bad("压缩流后存在多余数据");
            }
            if(!inflater.finished()||inflater.getRemaining()!=0||written!=entry.size)throw bad("压缩流被截断或解压长度不符");
            if(file.length()!=length||path.lastModified()!=modified)throw bad("资源包在读取时发生变化");
        } catch(DataFormatException error) {
            throw new IOException("汉化 CPK：压缩数据无效",error);
        } finally {
            inflater.end();Arrays.fill(input,(byte)0);Arrays.fill(decoded,(byte)0);
        }
    }

    byte[] read(Entry entry) throws IOException {
        if(entry.size>MAX_READ)throw bad("此资源需要流式读取");
        ByteArrayOutputStream output=new ByteArrayOutputStream((int)entry.size);
        decode(entry,output);return output.toByteArray();
    }

    public synchronized void close() throws IOException {
        if(!closed) {
            closed=true;
            for(Entry entry:entries) { Arrays.fill(entry.key,(byte)0);Arrays.fill(entry.nonce,(byte)0); }
            file.close();
        }
    }

    private static IOException bad(String message) { return new IOException("汉化 CPK："+message); }
}
