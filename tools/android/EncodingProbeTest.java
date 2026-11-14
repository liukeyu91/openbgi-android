/* SPDX-License-Identifier: GPL-2.0-only */
package org.openbgi.port;
import java.io.*;
import java.nio.charset.*;
import java.nio.file.*;
import java.util.*;

public final class EncodingProbeTest {
    static int checks;
    static void check(boolean ok){if(!ok)throw new AssertionError("check "+(checks+1));checks++;}
    static void put(byte[] b,int at,long v){for(int i=0;i<4;i++)b[at+i]=(byte)(v>>>(8*i));}
    static byte[] scene(String[] lines,String charset)throws Exception {
        ByteArrayOutputStream text=new ByteArrayOutputStream();int code=(lines.length*3+1)*4;
        byte[] head=new byte[64+code];System.arraycopy("BurikoCompiledScriptVer1.00\0".getBytes("US-ASCII"),0,head,0,28);put(head,28,36);
        for(int i=0;i<lines.length;i++){
            put(head,64+i*12,3);put(head,68+i*12,code+text.size());put(head,72+i*12,0x140);
            text.write(lines[i].getBytes(charset));text.write(0);
        }
        put(head,head.length-4,0x1b);ByteArrayOutputStream out=new ByteArrayOutputStream();out.write(head);out.write(text.toByteArray());return out.toByteArray();
    }
    static byte[] dsc(byte[] data)throws Exception {
        byte[] out=new byte[0x220+(data.length*9+7)/8];System.arraycopy("DSC FORMAT 1.00\0".getBytes("US-ASCII"),0,out,0,16);
        put(out,20,data.length);put(out,24,data.length);int key=123456;put(out,16,key);
        for(int i=0;i<512;i++){
            long lo=20021L*(key&65535),hi=(20021L*(key>>>16)+346L*(key&0xffffffffL)+(lo>>>16))&65535;
            key=(int)((hi<<16)+(lo&65535)+1);out[32+i]=(byte)(9+hi);
        }
        int bit=0;for(byte b:data)for(int j=8;j>=0;j--){if(((b&255)>>>j &1)!=0)out[0x220+bit/8]|=(byte)(1<<(7-bit%8));bit++;}
        return out;
    }
    static byte[] archive(String[] names,byte[][] contents,boolean arc20)throws Exception {
        int record=arc20?128:32,nameLen=arc20?96:16,base=16+record*names.length,total=base;
        for(byte[] data:contents)total+=data.length;byte[] out=new byte[total];
        System.arraycopy((arc20?"BURIKO ARC20":"PackFile    ").getBytes("US-ASCII"),0,out,0,12);put(out,12,names.length);int pos=base;
        for(int i=0;i<names.length;i++){
            byte[] name=names[i].getBytes("US-ASCII");System.arraycopy(name,0,out,16+i*record,Math.min(name.length,nameLen));
            put(out,16+i*record+nameLen,pos-base);put(out,20+i*record+nameLen,contents[i].length);
            System.arraycopy(contents[i],0,out,pos,contents[i].length);pos+=contents[i].length;
        }return out;
    }
    static List<ArchiveTextSamples.Sample> samples(String[] lines,String charset){
        List<ArchiveTextSamples.Sample> s=new ArrayList<>();for(String line:lines)s.add(new ArchiveTextSamples.Sample("fixture",line.getBytes(Charset.forName(charset))));return s;
    }
    static void tests()throws Exception {
        String[] zh={"午休结束后，我坐下来打开笔记本。","我们不能一直保持等待和被动，对吧？","今天的天气很好，我想出去走一走。","这个女孩说话的时候，有一点害羞。"};
        String[] jp={"昼休みが終わって、ノートを開いた。","今日はいい天気だから、散歩に行きましょう。","彼女は少し恥ずかしそうに笑っていた。","あなたと一緒に過ごす時間が好きです。"};
        for(String encoding:EncodingDetector.ENCODINGS){
            String charset=encoding.equals("sjis")?"windows-31j":encoding.equals("gbk")?"GBK":"UTF-8";
            EncodingDetector.Result detected=EncodingDetector.analyze(samples(encoding.equals("sjis")?jp:zh,charset));
            check(encoding.equals(detected.recommended));check(!detected.mixed);
        }
        check(EncodingDetector.analyze(samples(new String[]{"ASCII only","No dialogue"},"UTF-8")).recommended==null);
        check(EncodingDetector.analyze(samples(new String[]{"中文","青葉"},"GBK")).recommended==null);
        List<ArchiveTextSamples.Sample> mixed=samples(zh,"GBK");mixed.addAll(samples(jp,"windows-31j"));check(EncodingDetector.analyze(mixed).mixed);
        check(EncodingDetector.decode(new byte[]{(byte)0xc4,(byte)0xe3},2)==null);
        check(EncodingDetector.preview(new byte[]{(byte)0xff},2).contains("无效字节"));
        byte[] data=scene(zh,"GBK");check(ArchiveTextSamples.strings(data).size()==zh.length);
        check(ArchiveTextSamples.strings(scene(new String[]{"ソフトウェアを使ってみましょう。"},"windows-31j")).size()==1);
        check(ArchiveTextSamples.strings(scene(new String[]{"E:\\開発\\Script\\test.bs5"},"windows-31j")).isEmpty());
        check(new String(EncodingDetector.previewSamples(samples(zh,"GBK")).get(0).bytes,"GBK").equals(zh[0]));
        byte[] compressed=dsc(data);check(Arrays.equals(data,ArchiveTextSamples.decode(compressed)));
        check(ArchiveTextSamples.decode(data)==data);
        for(int len:new int[]{0,3,27,31,100,543}){
            byte[] cut=Arrays.copyOf(compressed,len);
            try{byte[] value=ArchiveTextSamples.decode(cut);check(len<16 && value==cut);}catch(IOException expected){checks++;}
        }
        byte[] bomb=compressed.clone();put(bomb,20,0xffffffffL);try{ArchiveTextSamples.decode(bomb);throw new AssertionError("bomb");}catch(IOException expected){checks++;}
        byte[] zero=compressed.clone();put(zero,20,0);try{ArchiveTextSamples.decode(zero);throw new AssertionError("empty DSC");}catch(IOException expected){checks++;}
        byte[] count=compressed.clone();put(count,24,0xffffffffL);try{ArchiveTextSamples.decode(count);throw new AssertionError("symbol count");}catch(IOException expected){checks++;}
        byte[] cut=Arrays.copyOf(compressed,compressed.length-3);try{ArchiveTextSamples.decode(cut);throw new AssertionError("truncated bits");}catch(IOException expected){checks++;}
        byte[] badHeader=data.clone();put(badHeader,28,0xffffffffL);try{ArchiveTextSamples.strings(badHeader);throw new AssertionError("header overflow");}catch(IOException expected){checks++;}
        Path dir=Files.createTempDirectory("openbgi-probe-");
        try{
            Files.createDirectories(dir.resolve("Archive"));
            Path arc=dir.resolve("Archive/MixedCase.arc");byte[] bytes=archive(new String[]{"a_prologue01"},new byte[][]{compressed},true);Files.write(arc,bytes);
            ArchiveTextSamples.Result r=ArchiveTextSamples.scan(dir.toFile());check(r.scenes==1 && r.samples.size()==4);check("gbk".equals(EncodingDetector.analyze(r.samples).recommended));
            check(r.samples.get(0).source.contains("MixedCase.arc"));check(Arrays.equals(bytes,Files.readAllBytes(arc)));
            Files.write(dir.resolve("a_prologue01"),scene(jp,"windows-31j"));r=ArchiveTextSamples.scan(dir.toFile());
            check(r.scenes==1 && "sjis".equals(EncodingDetector.analyze(r.samples).recommended));check(r.samples.get(0).source.equals("a_prologue01"));
            Files.delete(dir.resolve("a_prologue01"));Files.write(arc,archive(new String[]{"a_prologue01"},new byte[][]{compressed},false));check(ArchiveTextSamples.scan(dir.toFile()).samples.size()==4);
            Files.write(arc,Arrays.copyOf(bytes,30));check(ArchiveTextSamples.scan(dir.toFile()).samples.isEmpty());
            Files.write(arc,archive(new String[]{"../escape"},new byte[][]{compressed},true));check(ArchiveTextSamples.scan(dir.toFile()).samples.isEmpty());
            Files.write(arc,bytes);Thread.currentThread().interrupt();try{ArchiveTextSamples.scan(dir.toFile());throw new AssertionError("cancel");}catch(InterruptedIOException expected){checks++;}finally{Thread.interrupted();}
            Files.createDirectories(dir.resolve("UserData"));Files.write(dir.resolve("UserData/a_prologue02"),scene(jp,"windows-31j"));check(ArchiveTextSamples.scan(dir.toFile()).scenes==1);
        }finally{
            try(java.util.stream.Stream<Path> paths=Files.walk(dir)){paths.sorted(Comparator.reverseOrder()).forEach(p->{try{Files.delete(p);}catch(IOException e){throw new UncheckedIOException(e);}});}
        }
        System.out.println("Encoding probe: "+checks+" checks passed");
    }
    public static void main(String[] args)throws Exception {
        if(args.length==0){tests();return;}
        if(args.length==2 && args[0].equals("--fixtures")){
            String[] jp={"今日はいい天気だから、散歩に行きましょう。","彼女は少し恥ずかしそうに笑っていた。","あなたと一緒に過ごす時間が好きです。"};
            String[] zh={"今天的天气很好，我想出去走一走。","这个女孩说话的时候，有一点害羞。","午休结束后，我坐下来打开笔记本。"};
            for(String encoding:new String[]{"sjis","utf8","empty"}){
                Path game=Paths.get(args[1],"fixture-"+encoding);Files.createDirectories(game.resolve(".openbgi"));
                Files.write(game.resolve("system.arc"),new byte[0]);
                Files.write(game.resolve(".openbgi/name.txt"),("Fixture "+encoding.toUpperCase(Locale.ROOT)).getBytes("UTF-8"));
                Files.write(game.resolve(".openbgi/encoding.txt"),"sjis".getBytes("US-ASCII"));
                if(!encoding.equals("empty"))Files.write(game.resolve("a_prologue01"),dsc(scene(encoding.equals("sjis")?jp:zh,encoding.equals("sjis")?"windows-31j":"UTF-8")));
            }return;
        }
        ArchiveTextSamples.Result samples=ArchiveTextSamples.scan(new File(args[0]));EncodingDetector.Result result=EncodingDetector.analyze(samples.samples);
        System.out.println("archives="+samples.archives+" scenes="+samples.scenes+" samples="+samples.samples.size()+" skipped="+samples.skipped+" limited="+samples.limited);
        System.out.println(result.summary());
        for(int j=0;j<Math.min(3,samples.samples.size());j++){
            ArchiveTextSamples.Sample s=samples.samples.get(j);System.out.println(s.source);
            for(int i=0;i<3;i++)System.out.println(EncodingDetector.ENCODINGS[i]+": "+EncodingDetector.preview(s.bytes,i));
        }
    }
}
