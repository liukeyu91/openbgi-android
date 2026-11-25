/* SPDX-License-Identifier: GPL-2.0-only */
package org.openbgi.port;
import java.io.*;
import java.nio.charset.StandardCharsets;
import java.nio.file.*;
import java.util.*;

/**
 * A disc image is probed through the same bytes the engine reads. This test
 * builds an image file plus the index lines the native mount reports, so the
 * whole index path runs without a device, a native library or a real ISO.
 */
public final class DiscImageTest {
    static int checks;
    static void check(boolean value,String message){checks++;if(!value)throw new AssertionError(message);}
    interface Action{void run()throws IOException;}
    static void reject(Action action,String message)throws IOException{
        checks++;try{action.run();throw new AssertionError("Expected rejection: "+message);}catch(IOException expected){}
    }
    /** Append `data` to the image and record its index line. */
    static final class Builder {
        final ByteArrayOutputStream image=new ByteArrayOutputStream();
        final List<String> lines=new ArrayList<>();
        byte[] add(String path,byte[] data){
            int pad=image.size()%4;
            for(int i=0;i<pad;i++)image.write(0);
            long at=image.size();
            image.write(data,0,data.length);
            lines.add(path+"\t"+at+"\t"+data.length+"\t6");
            return data;
        }
        void directory(String path){lines.add(path+"\t0\t0\t7");}
        void raw(String line){lines.add(line);}
        File write(Path root,String name)throws IOException{File file=root.resolve(name).toFile();Files.write(file.toPath(),image.toByteArray());return file;}
    }
    static String[] dialogue(){
        return new String[]{"午休结束后，我坐下来打开笔记本。","我们不能一直保持等待和被动，对吧？",
            "今天的天气很好，我想出去走一走。","这个女孩说话的时候，有一点害羞。"};
    }
    /** A ZIP with explicit sizes in the local headers, so the test knows each
     *  entry's byte range (no data descriptors) and can feed the index lines. */
    static final class ZipBuilder {
        private static final class Item {
            byte[] name; int method, csize, usize; long crc, local;
        }
        final ByteArrayOutputStream zip=new ByteArrayOutputStream();
        final List<String> lines=new ArrayList<>();
        private final List<Item> items=new ArrayList<>();
        private void le16(int v){zip.write(v&0xff);zip.write((v>>>8)&0xff);}
        private void le32(long v){for(int i=0;i<4;i++)zip.write((int)(v>>>(8*i))&0xff);}
        void add(String name,byte[] data,boolean deflate)throws IOException{
            byte[] nameBytes=name.getBytes(StandardCharsets.UTF_8);
            int method=deflate?8:0;
            byte[] body=data;
            if(deflate){
                java.util.zip.Deflater deflater=new java.util.zip.Deflater(java.util.zip.Deflater.DEFAULT_COMPRESSION,true);
                ByteArrayOutputStream compressed=new ByteArrayOutputStream();
                try{
                    deflater.setInput(data);deflater.finish();
                    byte[] buffer=new byte[8192];
                    while(!deflater.finished()){int n=deflater.deflate(buffer);compressed.write(buffer,0,n);}
                }finally{deflater.end();}
                body=compressed.toByteArray();
            }
            java.util.zip.CRC32 crc=new java.util.zip.CRC32();crc.update(data);
            Item item=new Item();
            item.name=nameBytes;item.method=method;item.crc=crc.getValue();
            item.csize=body.length;item.usize=data.length;item.local=zip.size();
            le32(0x04034b50L);le16(20);le16(0);le16(method);le16(0);le16(0);
            le32(item.crc);le32(item.csize);le32(item.usize);
            le16(nameBytes.length);le16(0);
            zip.write(nameBytes);zip.write(body);
            lines.add(name+"\t"+(item.local+30+nameBytes.length)+"\t"+item.usize+"\t"+item.csize+"\t"+method+"\t"+(method==0?6:2));
            items.add(item);
        }
        File write(Path root,String name)throws IOException{
            long cdStart=zip.size();
            for(Item item:items){
                le32(0x02014b50L);le16(45);le16(45);le16(0);le16(item.method);le16(0);le16(0);
                le32(item.crc);le32(item.csize);le32(item.usize);
                le16(item.name.length);le16(0);le16(0);le16(0);le16(0);le32(0);le32(item.local);
                zip.write(item.name);
            }
            long cdSize=zip.size()-cdStart;
            le32(0x06054b50L);le16(0);le16(0);le16(items.size());le16(items.size());
            le32(cdSize);le32(cdStart);le16(0);
            File file=root.resolve(name).toFile();Files.write(file.toPath(),zip.toByteArray());return file;
        }
    }
    public static void main(String[] args)throws Exception {
        Path root=Files.createTempDirectory("disc-image-");
        try {
            byte[] scene=EncodingProbeTest.dsc(EncodingProbeTest.scene(dialogue(),"GBK"));
            byte[] archive=EncodingProbeTest.archive(new String[]{"a_prologue01"},new byte[][]{scene},false);
            byte[] save="slot one".getBytes(StandardCharsets.UTF_8);

            Builder builder=new Builder();
            builder.directory("UserData/");
            builder.add("system.arc",archive);
            builder.add("system.cn.arc",new byte[]{1});
            builder.add("sysprg.cn.arc",new byte[]{2});
            builder.add("readme.txt","hello".getBytes(StandardCharsets.US_ASCII));
            builder.add("UserData/bgi0001.sud",save);
            File image=builder.write(root,"game.iso");
            DiscImage.Source source=DiscImage.parse(image,builder.lines.toArray(new String[0]));
            check(source!=null,"Index parsed");
            check(source.entries.size()==6,"Directory and file entries");
            check(source.find("system.arc")!=null && source.find("SYSTEM.ARC")!=null,"Lookup ignores case");
            check(source.find("UserData/bgi0001.sud").data>0,"Entry carries its byte range");
            check(source.rootNames().contains("system.arc") && source.rootNames().contains("sysprg.cn.arc"),"Root names");
            check(!source.rootNames().contains("userdata/bgi0001.sud"),"Nested files are not root names");
            check(source.hasBootFiles(),"Boot archive present");
            check(Arrays.equals(source.bytes(source.find("system.arc")),archive),"Range read matches the payload");
            byte[] slice=source.read(source.find("readme.txt"),1,3);
            check(new String(slice,StandardCharsets.US_ASCII).equals("ell"),"Positioned read");

            File virtual=Files.createDirectory(root.resolve("virtual")).toFile();
            ResourceLayers layers=ResourceLayers.ofImage(virtual,source);
            check(layers.available().equals(Arrays.asList(ArchiveLanguage.CN)),"Localized pair reported");
            check(layers.hasDefault(),"Neutral system.arc keeps the distribution default");
            check(layers.resolve("")==null,"Default wins while system.arc exists");
            check(layers.resolve("tc")==null,"A neutral boot stays the default over an unknown choice");
            check(layers.hasBootFiles(),"Boot files from the index");
            ArchiveTextSamples.Result samples=ArchiveTextSamples.scan(layers,null);
            check(samples.scenes==1 && samples.samples.size()==4,"Scene sampled from the image");
            check("gbk".equals(EncodingDetector.analyze(samples.samples).recommended),"Image text detects GBK");
            check(samples.samples.get(0).source.contains("system.arc / a_prologue01"),"Sample names its source");
            layers.close();

            // A translated image ships the localized pair without system.arc.
            Builder translated=new Builder();
            translated.add("system.cn.arc",archive);
            translated.add("sysprg.cn.arc",new byte[]{2});
            File translatedImage=translated.write(root,"translated.iso");
            DiscImage.Source translatedSource=DiscImage.parse(translatedImage,translated.lines.toArray(new String[0]));
            File translatedEntry=new File(root.toFile(),"translated");
            try(ResourceLayers translatedLayers=ResourceLayers.ofImage(translatedEntry,translatedSource)){
                check(translatedLayers.resolve("")==ArchiveLanguage.CN,"Translated image selects the Chinese pair");
                check(translatedLayers.resolve("tc")==ArchiveLanguage.CN,"Unknown choice falls back to the available pair");
                check(!translatedLayers.hasDefault(),"No neutral boot");
                check(translatedLayers.hasBootFiles(),"Localized boot counts");
            }

            File entry=new File(root.toFile(),"entry");
            ContainerGame.register(entry,image,ContainerGame.Kind.IMAGE,source,staging->ContainerGame.data(staging,source));
            check(ContainerGame.linked(entry),"Reference written");
            check(new File(entry,ContainerGame.SOURCE).length()<3000,"Only a small image reference");
            check(ContainerGame.source(entry).getCanonicalFile().equals(image.getCanonicalFile()),"Image path resolves");
            check(!new File(entry,"system.arc").exists(),"Resources are not copied");
            ContainerGame.Summary summary=ContainerGame.summary(entry);
            check(summary!=null && summary.boot && summary.fallback,"Summary records boot and default");
            check(summary.languages.equals(Arrays.asList(ArchiveLanguage.CN)),"Summary records languages");
            File data=ContainerGame.data(entry);
            check(data!=null && new File(data,"UserData/bgi0001.sud").isFile(),"Medium saves migrated");
            check(Arrays.equals(Files.readAllBytes(new File(data,"UserData/bgi0001.sud").toPath()),save),"Save bytes copied");
            check(Files.readAllLines(data.toPath().resolve(".openbgi/original-saves.txt")).equals(Arrays.asList("UserData/bgi0001.sud")),
                "Migrated original stays hidden");
            check(!new File(data,"readme.txt").exists(),"Resources stay out of the private layer");

            // The library reads the summary, so an unplugged image still lists.
            try(ResourceLayers cached=ResourceLayers.summary(entry,entry)){
                check(cached.hasBootFiles(),"Summary answers boot");
                check(cached.available().equals(Arrays.asList(ArchiveLanguage.CN)),"Summary answers languages");
                check(cached.resolve("cn")==ArchiveLanguage.CN,"Summary answers a stored choice");
            }

            // A ZIP archive: stored and deflated entries share one index and read path.
            ZipBuilder zipped=new ZipBuilder();
            zipped.add("system.arc",archive,true);
            zipped.add("system.cn.arc",new byte[]{1},false);
            zipped.add("sysprg.cn.arc",new byte[]{2},false);
            zipped.add("readme.txt","hello".getBytes(StandardCharsets.US_ASCII),false);
            zipped.add("UserData/bgi0001.sud",save,true);
            File zip=zipped.write(root,"game.zip");
            DiscImage.Source zippedSource=DiscImage.parse(zip,zipped.lines.toArray(new String[0]));
            DiscImage.Entry boot=zippedSource.find("system.arc");
            check(boot!=null && boot.method==8 && boot.csize<boot.size && !boot.stored && boot.file(),
                "A deflated entry is readable but not stored");
            check(Arrays.equals(zippedSource.bytes(boot),archive),"A deflated entry inflates to its payload");
            byte[] zipSlice=zippedSource.read(zippedSource.find("readme.txt"),1,3);
            check(new String(zipSlice,StandardCharsets.US_ASCII).equals("ell"),"A stored entry reads in place");
            check(zippedSource.rootNames().contains("system.arc"),"ZIP root names");
            File zipEntry=new File(root.toFile(),"zipentry");
            ContainerGame.register(zipEntry,zip,ContainerGame.Kind.ZIP,zippedSource,
                staging->ContainerGame.data(staging,zippedSource));
            check(ContainerGame.kind(zipEntry)==ContainerGame.Kind.ZIP,"Summary records the ZIP kind");
            try(ResourceLayers zipLayers=ResourceLayers.ofImage(zipEntry,zippedSource)){
                check(zipLayers.available().equals(Arrays.asList(ArchiveLanguage.CN)),"A ZIP reports the localized pair");
                ArchiveTextSamples.Result zipSamples=ArchiveTextSamples.scan(zipLayers,null);
                check(zipSamples.scenes==1 && zipSamples.samples.size()==4,"Scene sampled through the archive");
                check("gbk".equals(EncodingDetector.analyze(zipSamples.samples).recommended),"Archive text detects GBK");
            }
            check(new File(ContainerGame.data(zipEntry),"UserData/bgi0001.sud").isFile(),"Archive saves migrated");

            // Malformed indexes are refused rather than trusted.
            reject(()->DiscImage.parse(image,new String[]{"../escape\t0\t4\t6"}),"parent traversal");
            reject(()->DiscImage.parse(image,new String[]{"/absolute\t0\t4\t6"}),"absolute path");
            reject(()->DiscImage.parse(image,new String[]{"a\\b\t0\t4\t6"}),"backslash path");
            reject(()->DiscImage.parse(image,new String[]{"system.arc\t0\t999999999\t6"}),"range beyond the file");
            reject(()->DiscImage.parse(image,new String[]{"system.arc\t12\t4"}),"missing field");
            reject(()->DiscImage.parse(image,new String[]{"system.arc\tx\t4\t6"}),"non-numeric range");
            check(DiscImage.parse(image,new String[0])==null,"Empty index");
            reject(()->DiscImage.parse(zip,new String[]{"system.arc\t0\t4\t8\t0\t6"}),"stored size mismatch");
            reject(()->DiscImage.parse(zip,new String[]{"system.arc\t0\t4\t999999\t8\t2"}),"compressed range beyond the file");

            // Listed but unopenable entries keep their place, named with the reason:
            // a boot archive compressed with another method, or an encrypted one.
            DiscImage.Source locked=DiscImage.parse(zip,new String[]{
                "system.arc\t0\t4\t12\t12\t0","system.cn.arc\t0\t4\t4\t0\t0","readme.txt\t0\t4\t4\t0\t6"});
            check(locked!=null,"Unopenable entries still parse into an index");
            DiscImage.Entry bzip2=locked.find("system.arc");
            check(bzip2!=null&&!bzip2.readable&&bzip2.unsupported()&&!bzip2.file(),"An unsupported method is not a file");
            check(bzip2.reason().contains("BZIP2"),"The unsupported method is named");
            DiscImage.Entry secret=locked.find("system.cn.arc");
            check(secret!=null&&!secret.readable&&secret.encrypted()&&"已加密".equals(secret.reason()),"An encrypted entry is named");
            check(locked.unreadable().size()==2,"Unopenable entries are counted");
            check(!locked.hasBootFiles()&&ArchiveLanguage.hasBootFiles(locked.allRootNames()),
                "Boot resources are listed, just not readable");
            check(locked.describeUnreadable(1).startsWith("2 个条目")&&locked.describeUnreadable(1).contains("已加密"),
                "The summary counts the entries and their reasons");
            reject(()->locked.bytes(bzip2),"reading a bzip2 entry");
            reject(()->locked.bytes(secret),"reading an encrypted entry");
            reject(()->locked.read(bzip2,0,4),"positioned read of an unsupported entry");
            try{
                ContainerGame.register(new File(root.toFile(),"locked"),zip,ContainerGame.Kind.ZIP,locked);
                check(false,"Expected rejection: unreadable boot resources");
            }catch(IOException expected){
                check(expected.getMessage().contains("无法读取")&&expected.getMessage().contains("已加密"),
                    "The refusal names the reason instead of calling the archive incomplete");
            }

            // A deflated entry past the 256 MiB ceiling is refused before anything is allocated.
            DiscImage.Source oversized=DiscImage.parse(zip,new String[]{"huge.bin\t0\t"+(257L*1024*1024)+"\t4\t8\t2"});
            check(oversized!=null&&oversized.find("huge.bin")!=null,"An oversized entry parses");
            try{
                oversized.bytes(oversized.find("huge.bin"));
                check(false,"Expected rejection: entry past the inflate ceiling");
            }catch(IOException expected){
                check(expected.getMessage().contains("256 MiB"),"The refusal names the inflate ceiling");
            }

            check(ContainerGame.find(root.toFile(),image)!=null,"Duplicate image detected");
            Files.write(new File(entry,ContainerGame.SOURCE).toPath(),"relative/path".getBytes(StandardCharsets.UTF_8));
            reject(()->ContainerGame.source(entry),"relative reference refused");
            Files.write(new File(entry,ContainerGame.SOURCE).toPath(),image.getAbsolutePath().getBytes(StandardCharsets.UTF_8));

            // A source that disappeared keeps its entry and saves, and the
            // refusal says which of the two things went missing.
            File copy=new File(root.toFile(),"copy.iso");
            Files.copy(image.toPath(),copy.toPath(),StandardCopyOption.REPLACE_EXISTING);
            File gone=new File(root.toFile(),"gone");
            ContainerGame.register(gone,copy,ContainerGame.Kind.IMAGE,source,staging->ContainerGame.data(staging,source));
            check(Files.deleteIfExists(copy.toPath()),"Source moved away");
            try{
                ResourceLayers.of(gone,gone).close();
                check(false,"Expected rejection: source removed");
            }catch(IOException expected){
                check(expected.getMessage().contains("恢复后即可启动"),"The refusal tells the user the source is gone");
            }
            check(ContainerGame.linked(gone)&&ContainerGame.summary(gone)!=null,"The entry and its summary survive");
            check(new File(ContainerGame.data(gone),"UserData/bgi0001.sud").isFile(),"The migrated saves survive");
            try(ResourceLayers cached=ResourceLayers.summary(gone,gone)){
                check(cached.hasBootFiles(),"The cached summary still answers without the source");
            }
            Files.copy(image.toPath(),copy.toPath(),StandardCopyOption.REPLACE_EXISTING);
            check(ContainerGame.validate(ContainerGame.source(gone)).getCanonicalFile().equals(copy.getCanonicalFile()),
                "A restored source resolves again");
            check(Files.deleteIfExists(copy.toPath()),"Cleanup: restored source");
            System.out.println("DISC_IMAGE_PASS: "+checks+" checks; index parse, language, sampling, migration, summary, ZIP deflate, unreadable entries, missing source and validation");
        }finally{GameImport.remove(root);}
    }
}
