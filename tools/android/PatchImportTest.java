/* SPDX-License-Identifier: GPL-2.0-only */
package org.openbgi.port;
import java.io.*;
import java.nio.charset.StandardCharsets;
import java.nio.file.*;
import java.util.*;

/** End-to-end synthetic import tests; contains no commercial game resources. */
public final class PatchImportTest {
    static int checks;
    static void check(boolean ok,String message){checks++;if(!ok)throw new AssertionError(message);}
    static void put(byte[] b,int at,long value){for(int i=0;i<4;i++)b[at+i]=(byte)(value>>>(8*i));}
    static byte[] text(String value){return value.getBytes(StandardCharsets.UTF_8);}
    static byte[] evb(String[] names,byte[][] data,boolean compressed)throws IOException{
        ByteArrayOutputStream nodes=new ByteArrayOutputStream(),payload=new ByteArrayOutputStream();
        byte[] root=new byte[16];put(root,12,names.length);nodes.write(root);nodes.write("%DEFAULT FOLDER%".getBytes(StandardCharsets.UTF_16LE));nodes.write(new byte[]{0,0,3});nodes.write(new byte[25]);
        for(int i=0;i<names.length;i++){
            nodes.write(new byte[16]);nodes.write(names[i].getBytes(StandardCharsets.UTF_16LE));nodes.write(new byte[]{0,0,2});
            byte[] packed=data[i];if(compressed){byte[] header=new byte[12];put(header,0,12);put(header,8,packed.length);ByteArrayOutputStream b=new ByteArrayOutputStream();b.write(header);b.write(packed);packed=b.toByteArray();}
            byte[] meta=new byte[53];put(meta,2,compressed?1:data[i].length);put(meta,49,packed.length);nodes.write(meta);payload.write(packed);
        }
        byte[] header=new byte[79];header[0]='E';header[1]='V';header[2]='B';put(header,64,79+nodes.size()+4-68);
        ByteArrayOutputStream out=new ByteArrayOutputStream();out.write(header);nodes.writeTo(out);out.write(new byte[4]);payload.writeTo(out);return out.toByteArray();
    }
    static File game(Path base,String name)throws IOException{Path root=base.resolve(name);Files.createDirectories(root);Files.write(root.resolve("system.arc"),text("native"));return root.toFile();}
    static PatchImport.Result run(File game)throws IOException{return PatchImport.process(game,new PatchImport.Profile[0],(done,total)->{});}
    static byte[] equalEnvelope(){byte[] b=new byte[17];put(b,0,12);put(b,8,5);byte[] packed={65,(byte)0xaf,1,0x30,0};System.arraycopy(packed,0,b,12,5);return b;}
    static byte[] equalBundle()throws IOException{byte[] data=new byte[17];Arrays.fill(data,(byte)65);byte[] bundle=evb(new String[]{"intro"},new byte[][]{data},false);System.arraycopy(equalEnvelope(),0,bundle,bundle.length-17,17);return bundle;}
    interface Action{void run()throws IOException;}
    static void reject(Action action)throws IOException{checks++;try{action.run();throw new AssertionError("Invalid patch accepted");}catch(IOException expected){}}
    public static void main(String[] args)throws Exception{
        Path base=Files.createTempDirectory("patch-import-test-");
        try{
            File game=game(base,"plain");byte[] translated=text("synthetic 中文");
            Files.write(game.toPath().resolve("renamed.pck"),evb(new String[]{"intro","slot.sud","BGI.gdb","module.dll"},new byte[][]{translated,text("save"),text("progress"),text("MZ")},false));
            PatchImport.Result result=run(game);
            check(result.resources==1,"Only deployable resources expanded");check(Arrays.equals(Files.readAllBytes(game.toPath().resolve("intro")),translated),"Exact bytes");
            check(!new File(game,"slot.sud").exists()&&!new File(game,"module.dll").exists(),"No progress/module deployment");
            Files.write(game.toPath().resolve("intro"),text("user override"));result=run(game);
            check(result.resources==0&&result.preserved==1,"Idempotent and preserves loose overrides");check(new String(Files.readAllBytes(game.toPath().resolve("intro")),StandardCharsets.UTF_8).equals("user override"),"Override contents preserved");
            File tiny=game(base,"compressed");Files.write(tiny.toPath().resolve("compressed.klt"),evb(new String[]{"intro"},new byte[][]{new byte[]{65,(byte)0xc0,0}},true));
            check(run(tiny).resources==1&&Files.readAllBytes(tiny.toPath().resolve("intro"))[0]==65,"aPLib integration");
            File equal=game(base,"equal-size");Files.write(equal.toPath().resolve("patch.klt"),equalBundle());
            byte[] expected=new byte[17];Arrays.fill(expected,(byte)65);
            File original=game(base,"direct-original"),expanded=Files.createDirectory(base.resolve("direct-data")).toFile();
            Files.write(original.toPath().resolve("patch.klt"),equalBundle());
            Files.write(original.toPath().resolve("intro"),equalEnvelope());
            java.nio.file.attribute.FileTime modified=Files.getLastModifiedTime(original.toPath());
            result=PatchImport.process(original,expanded,new PatchImport.Profile[0],(a,b)->{});
            check(result.repaired==1&&Arrays.equals(Files.readAllBytes(expanded.toPath().resolve("intro")),expected),"Direct repair shadows the original envelope");
            check(Arrays.equals(Files.readAllBytes(original.toPath().resolve("intro")),equalEnvelope())&&
                Files.getLastModifiedTime(original.toPath()).equals(modified)&&!new File(original,".openbgi").exists(),"Direct patch processing leaves the original untouched");
            File legacy=game(base,"direct-legacy"),legacyData=Files.createDirectory(base.resolve("legacy-data")).toFile();
            Path legacyBundle=legacy.toPath().resolve("patch.klt");
            Files.write(legacyBundle,evb(new String[]{"intro"},new byte[][]{text("old output")},false));
            run(legacy);
            Files.write(legacyBundle,evb(new String[]{"intro"},new byte[][]{text("new output")},false));
            result=PatchImport.process(legacy,legacyData,new PatchImport.Profile[0],(a,b)->{});
            check(result.resources==1&&Files.readString(legacyData.toPath().resolve("intro")).equals("new output")&&
                Files.readString(legacy.toPath().resolve("intro")).equals("old output"),"Legacy managed results can be updated without writes to the original");
            check(run(equal).resources==1&&Arrays.equals(Files.readAllBytes(equal.toPath().resolve("intro")),expected),"Equal-size compression is decoded");
            Files.write(equal.toPath().resolve("intro"),equalEnvelope());result=run(equal);
            check(result.repaired==1&&result.resources==1&&Arrays.equals(Files.readAllBytes(equal.toPath().resolve("intro")),expected),"Repair exact old compressed envelope");
            result=run(equal);check(result.preserved==1&&result.repaired==0,"Repair is idempotent");
            Files.write(equal.toPath().resolve("intro"),text("custom override"));result=run(equal);
            check(result.repaired==0&&Arrays.equals(Files.readAllBytes(equal.toPath().resolve("intro")),text("custom override")),"Repair preserves user overrides");
            File rollback=game(base,"repair-rollback");Files.write(rollback.toPath().resolve("patch.klt"),equalBundle());Files.write(rollback.toPath().resolve("intro"),equalEnvelope());
            Path blocked=rollback.toPath().resolve(".openbgi/translation.properties");Files.createDirectories(blocked);Files.write(blocked.resolve("keep"),text("keep"));
            reject(()->run(rollback));check(Arrays.equals(Files.readAllBytes(rollback.toPath().resolve("intro")),equalEnvelope()),"Report failure rolls repair back");
            File lookalike=game(base,"stored-lookalike");byte[] invalid=equalEnvelope();Arrays.fill(invalid,12,17,(byte)0);
            Files.write(lookalike.toPath().resolve("patch.klt"),evb(new String[]{"intro"},new byte[][]{invalid},false));
            check(run(lookalike).resources==1&&Arrays.equals(Files.readAllBytes(lookalike.toPath().resolve("intro")),invalid),"Invalid compressed lookalike remains stored data");
            File carrier=game(base,"embedded");byte[] header=new byte[512];header[0]='M';header[1]='Z';put(header,60,64);header[64]='P';header[65]='E';header[100]='E';header[101]='V';header[102]='B';
            ByteArrayOutputStream pe=new ByteArrayOutputStream();pe.write(header);pe.write(evb(new String[]{"intro"},new byte[][]{translated},false));Files.write(carrier.toPath().resolve("patch.exe"),pe.toByteArray());
            check(run(carrier).resources==1,"Validated embedded EVB, skips literal magic");
            File conflicting=game(base,"conflict");Files.write(conflicting.toPath().resolve("a.pck"),evb(new String[]{"intro"},new byte[][]{text("a")},false));Files.write(conflicting.toPath().resolve("b.pck"),evb(new String[]{"intro"},new byte[][]{text("b")},false));
            reject(()->run(conflicting));check(!new File(conflicting,"intro").exists(),"Conflicts publish nothing");
            File duplicates=game(base,"identical");byte[] bundle=evb(new String[]{"intro"},new byte[][]{translated},false);Files.write(duplicates.toPath().resolve("a.pck"),bundle);Files.write(duplicates.toPath().resolve("b.chs"),bundle);
            check(run(duplicates).resources==1,"Identical duplicate movie/resource packs coalesce");
            File updates=game(base,"updates");Files.write(updates.toPath().resolve("base.klt"),evb(new String[]{"intro","other"},new byte[][]{text("old"),text("keep")},false));Files.write(updates.toPath().resolve("update.pck"),evb(new String[]{"intro"},new byte[][]{text("new")},false));
            PatchImport.Profile p=new PatchImport.Profile("test","1.636","gbk","Noto Sans CJK SC",new PatchImport.Source[]{new PatchImport.Source("evb-external","base.klt","",new String[]{"*"},new String[]{"intro"}),new PatchImport.Source("evb-external","update.pck","",new String[]{"*"},new String[0])});
            result=PatchImport.process(updates,new PatchImport.Profile[]{p},(done,total)->{});GameSettings settings=new GameSettings();result.apply(settings);
            check(result.resources==2&&new String(Files.readAllBytes(updates.toPath().resolve("intro")),StandardCharsets.UTF_8).equals("new"),"Explicit update precedence");
            check(settings.encoding.equals("gbk")&&!settings.automaticEncoding&&settings.profile.equals("1.636"),"Apply runtime settings");
            check(settings.fontFamily.equals("Noto Sans CJK SC")&&settings.translationProfile.equals("test"),"Font hook and adapter requirements applied");
            result=PatchImport.process(updates,new PatchImport.Profile[]{p},(done,total)->{});check(result.resources==0,"Repeated patch preserves ownership");
            Files.write(updates.toPath().resolve("update.pck"),evb(new String[]{"intro"},new byte[][]{text("updated")},false));
            result=PatchImport.process(updates,new PatchImport.Profile[]{p},(done,total)->{});
            check(result.resources==1&&Files.readString(updates.toPath().resolve("intro")).equals("updated"),"Official update replaces our unchanged old output");
            Files.write(updates.toPath().resolve("intro"),text("user supplement"));
            Files.write(updates.toPath().resolve("update.pck"),evb(new String[]{"intro"},new byte[][]{text("another update")},false));
            result=PatchImport.process(updates,new PatchImport.Profile[]{p},(done,total)->{});
            check(Files.readString(updates.toPath().resolve("intro")).equals("user supplement"),"Update preserves manual supplemental translation");
            File cpkOnly=game(base,"cpk");byte[][] pair=PatchCpkTest.fixture("intro",translated,false,0);Files.write(cpkOnly.toPath().resolve("toy.cpk"),pair[0]);Files.write(cpkOnly.toPath().resolve("toy.key"),pair[1]);
            check(run(cpkOnly).resources==1&&Arrays.equals(Files.readAllBytes(cpkOnly.toPath().resolve("intro")),translated),"CPK import path");
            File unrelated=game(base,"unrelated");Files.write(unrelated.toPath().resolve("movie.cpk"),text("a different container"));
            check(run(unrelated).resources==0,"Unknown CPK without this format's key does not break ordinary imports");
            File bad=game(base,"bad");Files.write(bad.toPath().resolve("broken.pck"),Arrays.copyOf(bundle,80));reject(()->run(bad));check(!new File(bad,"intro").exists(),"Malformed container rollback");
            File destination=base.resolve("published").toFile();byte[] boot=evb(new String[]{"ipl._bp"},new byte[][]{text("boot")},false);
            List<GameImport.Archive> sources=Arrays.asList(new GameImport.Archive("only.exe.pck",boot.length,()->new ByteArrayInputStream(boot)));
            GameImport.install(destination,sources,(a,b)->check(!destination.exists(),"Unpublished while copying"),staging->{PatchImport.process(staging,new PatchImport.Profile[0],(done,total)->{});check(!destination.exists(),"Unpublished while patching");});
            check(GameImport.gamePresent(destination),"Boot recovered before game-present validation");
            File failed=base.resolve("failed").toFile();byte[] broken=Arrays.copyOf(boot,80);
            reject(()->GameImport.install(failed,Arrays.asList(new GameImport.Archive("bad.pck",broken.length,()->new ByteArrayInputStream(broken))),(a,b)->{},staging->run(staging)));
            check(!failed.exists(),"Failed patch import not published");
            for(String name:new String[]{"../escape","BGI0001.cad","slot.sud"}){
                if(name.contains("/")){File unsafe=game(base,"unsafe");Files.write(unsafe.toPath().resolve("bad.pck"),evb(new String[]{name},new byte[][]{text("a")},false));reject(()->run(unsafe));}
                else check(PatchImport.excluded(name),"Save/cache excluded");
            }
            System.out.println("PATCH_IMPORT_PASS: "+checks+" checks; EVB, embedded PE, CPK, settings, update precedence, idempotence, conflicts, atomic publication");
        }finally{GameImport.remove(base);}
    }
}
