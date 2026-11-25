/* SPDX-License-Identifier: GPL-2.0-only */
package org.openbgi.port;

import java.io.*;
import java.nio.charset.StandardCharsets;
import java.nio.file.*;
import java.security.MessageDigest;
import java.security.NoSuchAlgorithmException;
import java.util.*;
import java.util.stream.Stream;

/** Inspects the staged overlay before it becomes active. */
final class PatchDoctor {
    enum Status { PASS, WARNING, REJECT }
    enum Code {
        INVALID_MANIFEST, UNKNOWN_TARGET, WRONG_PRODUCT, WRONG_ENGINE, WRONG_BOOT,
        BAD_ARCHIVE, WRONG_ENCODING, UNDECODABLE_TEXT, MIXED_ENCODING, NO_TEXT, BAD_GLYPHS, NO_FONT,
        FONT_SETTING, TEXT_WIDTH, NAME_CONFLICT, IMPORT_FAILURE
    }
    static final class Finding {
        final Code code;
        final Object[] arguments;
        Finding(Code code,Object... arguments){this.code=code;this.arguments=arguments;}
    }
    static final class Result {
        Status status=Status.PASS;
        int archives,samples;
        final List<Finding> findings=new ArrayList<>();
        void add(Status severity,Code code,Object... arguments){
            if(severity.ordinal()>status.ordinal())status=severity;
            if(findings.size()<20)findings.add(new Finding(code,arguments));
        }
    }
    static final class Target {
        final String product,engine,bootHash;
        Target(String product,String engine,String bootHash){
            this.product=product;this.engine=engine;this.bootHash=bootHash;
        }
    }
    interface Glyphs { int[] missing(String family,int[] codePoints); }

    private static final class Manifest {
        String product="",engine="",bootHash="",encoding="",font="";
        int fontSize,textBoxWidth;
    }

    static Result check(File patch,GameSettings settings,Target target,Glyphs glyphs)throws IOException{
        Result result=new Result();
        Manifest manifest;
        try{manifest=manifest(new File(patch,GamePatch.MANIFEST).toPath());}
        catch(IOException bad){result.add(Status.REJECT,Code.INVALID_MANIFEST,bad.getMessage());return result;}

        compareTarget(result,manifest,target);
        checkArchives(patch.toPath(),result);
        if(result.status==Status.REJECT)return result;
        if(!manifest.font.isEmpty()&&!manifest.font.equalsIgnoreCase(settings.fontFamily))
            result.add(Status.WARNING,Code.FONT_SETTING,manifest.font);
        ArchiveTextSamples.Result text=ArchiveTextSamples.scanPatch(patch);
        result.samples=text.samples.size();
        if(result.samples==0){
            result.add(Status.WARNING,Code.NO_TEXT);
            return result;
        }
        EncodingDetector.Result detected=EncodingDetector.analyze(text.samples);
        String expected=manifest.encoding.isEmpty()?settings.encoding:manifest.encoding;
        if(detected.mixed)result.add(Status.WARNING,Code.MIXED_ENCODING);
        boolean usesBig5=expected.equals("big5");
        if(!usesBig5 && detected.recommended!=null && !expected.equals(detected.recommended)){
            result.add(Status.WARNING,Code.WRONG_ENCODING,expected,detected.recommended);
            expected=detected.recommended;
        }else if(!manifest.encoding.isEmpty() && settings.encodingReady() && !settings.encoding.equals(expected)){
            result.add(Status.WARNING,Code.WRONG_ENCODING,settings.encoding,expected);
        }
        int index=expected.equals("big5")?3:expected.equals("utf8")?2:expected.equals("gbk")?1:0;
        Set<Integer> points=new LinkedHashSet<>();
        int undecodable=0,widest=0;
        for(ArchiveTextSamples.Sample sample:text.samples){
            String decoded=EncodingDetector.decode(sample.bytes,index);
            if(decoded==null){undecodable++;continue;}
            int columns=0;
            for(int at=0;at<decoded.length();){
                int cp=decoded.codePointAt(at);
                at+=Character.charCount(cp);
                if(cp=='\n'||cp=='\r'){widest=Math.max(widest,columns);columns=0;continue;}
                if(Character.isISOControl(cp))continue;
                if(points.size()<2048)points.add(cp);
                columns+=cp<0x80||cp>=0xff61&&cp<=0xff9f?1:2;
            }
            widest=Math.max(widest,columns);
        }
        if(undecodable>0)result.add(Status.WARNING,Code.UNDECODABLE_TEXT,undecodable,expected);
        String family=manifest.font.isEmpty()?settings.fontFamily:manifest.font;
        if(family.isEmpty())family="Noto Sans CJK "+(index==3?"TC":index==1?"SC":"JP");
        if(!points.isEmpty()){
            int[] missing=glyphs.missing(family,points.stream().mapToInt(Integer::intValue).toArray());
            if(missing==null)result.add(Status.WARNING,Code.NO_FONT,family);
            else if(missing.length>0){
                StringBuilder examples=new StringBuilder();
                for(int i=0;i<Math.min(8,missing.length);i++){
                    if(i>0)examples.append(' ');
                    examples.append(String.format(Locale.ROOT,"U+%04X",missing[i]));
                }
                result.add(Status.WARNING,Code.BAD_GLYPHS,family,missing.length,examples.toString());
            }
        }
        if(manifest.fontSize>0 && (widest*manifest.fontSize+1)/2>manifest.textBoxWidth)
            result.add(Status.WARNING,Code.TEXT_WIDTH,(widest*manifest.fontSize+1)/2,manifest.textBoxWidth);
        return result;
    }

    private static void compareTarget(Result result,Manifest m,Target target){
        boolean unknown=false;
        if(!m.product.isEmpty()){
            if(target.product.isEmpty())unknown=true;
            else if(!m.product.equalsIgnoreCase(target.product))
                result.add(Status.REJECT,Code.WRONG_PRODUCT,m.product,target.product);
        }
        if(!m.engine.isEmpty()){
            if(target.engine.isEmpty())unknown=true;
            else if(!m.engine.equals(target.engine))result.add(Status.REJECT,Code.WRONG_ENGINE,m.engine,target.engine);
        }
        if(!m.bootHash.isEmpty()){
            if(target.bootHash.isEmpty())unknown=true;
            else if(!m.bootHash.equalsIgnoreCase(target.bootHash))result.add(Status.REJECT,Code.WRONG_BOOT);
        }
        if(unknown)result.add(Status.WARNING,Code.UNKNOWN_TARGET);
    }

    private static void checkArchives(Path root,Result result)throws IOException{
        try(Stream<Path> paths=Files.walk(root)){
            Iterator<Path> archives=paths.filter(p->Files.isRegularFile(p,LinkOption.NOFOLLOW_LINKS) &&
                p.getFileName().toString().toLowerCase(Locale.ROOT).endsWith(".arc")).iterator();
            while(archives.hasNext()){
                Path path=archives.next();
                result.archives++;
                try(RandomAccessFile in=new RandomAccessFile(path.toFile(),"r")){
                    byte[] header=new byte[16];
                    in.readFully(header);
                    int size=ArchiveTextSamples.magic(header,"BURIKO ARC20")?128:
                        ArchiveTextSamples.magic(header,"PackFile    ")?32:0;
                    long count=ArchiveTextSamples.u32(header,12);
                    if(size!=0 && 16+count*size<=in.length())continue;
                }catch(EOFException shortFile){}
                result.add(Status.REJECT,Code.BAD_ARCHIVE,root.relativize(path).toString());
            }
        }catch(UncheckedIOException error){throw error.getCause();}
    }

    private static Manifest manifest(Path path)throws IOException{
        Manifest m=new Manifest();
        if(!Files.exists(path))return m;
        if(Files.size(path)>128*1024)throw new IOException("patch.json > 128 KiB");
        Object payload;
        try{payload=SaveRestore.Json.parse(new String(Files.readAllBytes(path),StandardCharsets.UTF_8));}
        catch(IOException bad){throw new IOException("patch.json",bad);}
        if(!(payload instanceof Map))throw new IOException("patch.json");
        Map<?,?> values=(Map<?,?>)payload,target=values;
        if(values.containsKey("target")){
            if(!(values.get("target") instanceof Map))throw new IOException("target");
            target=(Map<?,?>)values.get("target");
        }
        m.product=text(target,"product");
        m.engine=text(target,"engine");
        m.bootHash=text(target,"bootSha256");
        m.encoding=text(values,"encoding").toLowerCase(Locale.ROOT);
        m.font=text(values,"fontFamily");
        m.fontSize=number(values,"fontSize",256);
        m.textBoxWidth=number(values,"textBoxWidth",8192);
        if(!m.encoding.isEmpty()&&!Arrays.asList("sjis","gbk","big5","utf8").contains(m.encoding))throw new IOException("encoding");
        if(!m.bootHash.isEmpty()&&!m.bootHash.matches("[0-9a-fA-F]{64}"))throw new IOException("target.bootSha256");
        if((m.fontSize==0)!=(m.textBoxWidth==0))throw new IOException("fontSize / textBoxWidth");
        return m;
    }

    private static String text(Map<?,?> map,String key)throws IOException{
        if(!map.containsKey(key))return "";
        Object value=map.get(key);
        if(!(value instanceof String)||((String)value).length()>255)throw new IOException(key);
        return (String)value;
    }

    private static int number(Map<?,?> map,String key,int max)throws IOException{
        if(!map.containsKey(key))return 0;
        Object value=map.get(key);
        if(!(value instanceof Long)||(long)value<1||(long)value>max)throw new IOException(key);
        return ((Long)value).intValue();
    }

    static String bootHash(byte[] boot){
        if(boot==null)return "";
        try {
            StringBuilder out=new StringBuilder(64);
            for(byte b:MessageDigest.getInstance("SHA-256").digest(boot))out.append(String.format(Locale.ROOT,"%02x",b&255));
            return out.toString();
        }catch(NoSuchAlgorithmException impossible){throw new AssertionError(impossible);}
    }
}
