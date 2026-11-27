/* SPDX-License-Identifier: GPL-2.0-only */
package org.openbgi.port;
import java.io.*;
import java.nio.file.*;
import java.nio.charset.StandardCharsets;
import java.util.*;

public final class ProgramCompatibilityTest {
    static int checks;
    static void check(boolean ok){if(!ok)throw new AssertionError("check "+(checks+1));checks++;}
    static byte[] program(int opcode){
        byte[] file=new byte[18];EncodingProbeTest.put(file,0,16);EncodingProbeTest.put(file,4,2);file[16]=(byte)opcode;file[17]=0x17;return file;
    }
    static final class Analyzer implements ProgramCompatibility.Analyzer {
        String table,preference;byte[] boot;int analyzed;
        public String select(String table,String preference,byte[] boot){
            this.table=table;this.preference=preference;this.boot=boot;
            return preference.equals("auto")?(boot==null?null:"1.636"):preference;
        }
        public int[] analyze(byte[] data){
            analyzed++;if(data.length<18 || data[0]!=16)return new int[]{0,0,0,0,0};
            if(data[4]>data.length-16)return new int[]{1,2,1,0,0};
            return data[16]==0x56?new int[]{1,2,0,1,1,0x56,0,1}:new int[]{1,2,0,0,0};
        }
    }
    static ProgramCompatibility.Result scan(Path dir,String profile,Analyzer a)throws Exception {
        return ProgramCompatibility.scan(dir.toFile(),dir.toFile(),profile,a,(done,total)->check(done>=0 && done<=total));
    }

    static void containerTests(Path root)throws Exception {
        File image=root.resolve("source.zip").toFile(), game=root.resolve("container-entry").toFile();
        Files.write(image.toPath(),program(0x17));
        try(DiscImage.Source source=DiscImage.parse(image,new String[]{
            "ipl._bp\t0\t18\t18\t0\t6", "blocked._bp\t0\t4\t4\t12\t0", "portrait.bmp\t0\t4\t4\t14\t0"
        })){
            ContainerGame.register(game,image,ContainerGame.Kind.ZIP,source,
                staging->ContainerGame.data(staging,source));
            try(ResourceLayers layers=ResourceLayers.ofImage(game,source)){
                ProgramCompatibility.Result result=ProgramCompatibility.scan(layers,"auto",new Analyzer(),(d,n)->{});
                check(result.checked==1 && result.unreadableEntries==2 &&
                    result.unreadablePaths.equals(Arrays.asList("blocked._bp","portrait.bmp")));
                check(result.status()==ProgramCompatibility.Status.PARTIAL);
                File data=ContainerGame.data(game);
                Files.write(data.toPath().resolve("blocked._bp"),program(0x17));
            }
            try(ResourceLayers layers=ResourceLayers.ofImage(game,source)){
                ProgramCompatibility.Result result=ProgramCompatibility.scan(layers,"auto",new Analyzer(),(d,n)->{});
                check(result.checked==2 && result.unreadableEntries==2 && result.status()==ProgramCompatibility.Status.PASS);
                ProgramCompatibility.save(layers,"auto","test",result,"2 unreadable entries");
                check(ProgramCompatibility.cachedSummary(game,game,"auto","test").status==ProgramCompatibility.Status.PASS);
                Files.write(image.toPath(),new byte[]{0},StandardOpenOption.APPEND);
                boolean rejected=false;
                try{ProgramCompatibility.save(layers,"auto","test",result,"stale report");}
                catch(IOException changed){rejected=true;}
                check(rejected);
            }
            check(ProgramCompatibility.cachedSummary(game,game,"auto","test").status==ProgramCompatibility.Status.RECHECK);
        }
    }

    static void tests(Path root)throws Exception {
        ProgramCompatibility.Result platform=new ProgramCompatibility.Result();platform.profile="1.616";
        platform.findings.put(0x381e0,new ProgramCompatibility.Finding(0x81e0,3));
        check(platform.status()==ProgramCompatibility.Status.FINDINGS && platform.findings.get(0x381e0).kind==3);
        Path game=Files.createDirectory(root.resolve("game"));Files.createDirectory(game.resolve("Archive"));
        byte[] archive=EncodingProbeTest.archive(new String[]{"ipl._bp","scrgrp7._bp"},
            new byte[][]{EncodingProbeTest.dsc(program(0x17)),EncodingProbeTest.dsc(program(0x56))},true);
        Path arc=game.resolve("Archive/System.ARC");Files.write(arc,archive);Analyzer a=new Analyzer();
        ProgramCompatibility.Result result=scan(game,"auto",a);
        check(result.programs==2 && result.checked==2 && result.findings.size()==1);
        check(result.profile.equals("1.636") && result.instructions==4);
        check(Arrays.equals(a.boot,program(0x17)) && a.preference.equals("auto"));
        ProgramCompatibility.Finding missing=result.findings.get(0x10056);
        check(missing!=null && missing.locations.get(0).contains("scrgrp7._bp") && missing.locations.get(0).endsWith("$00000000"));
        check(Arrays.equals(archive,Files.readAllBytes(arc)));
        Files.write(game.resolve("scrgrp7._bp"),program(0x5d));result=scan(game,"auto",new Analyzer());
        check(result.programs==2 && result.checked==2 && result.findings.isEmpty());
        check(result.status()==ProgramCompatibility.Status.PASS);
        Path hidden=Files.createDirectory(game.resolve(".openbgi"));Files.write(hidden.resolve("ignored._bp"),program(0x56));
        Path saves=Files.createDirectory(game.resolve("UserData"));Files.write(saves.resolve("ignored._bp"),program(0x56));
        result=scan(game,"auto",new Analyzer());check(result.programs==2 && result.findings.isEmpty());
        ProgramCompatibility.save(game.toFile(),game.toFile(),"auto","test-build",result,"Localized report");
        check(ProgramCompatibility.cachedSummary(game.toFile(),game.toFile(),"auto","test-build").status==result.status());
        check(ProgramCompatibility.cachedSummary(game.toFile(),game.toFile(),"auto","other-build").status==ProgramCompatibility.Status.RECHECK);
        check(ProgramCompatibility.cachedSummary(game.toFile(),game.toFile(),"1.616","test-build").status==ProgramCompatibility.Status.RECHECK);
        Path cached=hidden.resolve("compatibility.properties");
        Properties legacy=new Properties();
        try(InputStream in=Files.newInputStream(cached)){legacy.load(in);}
        legacy.remove("status");legacy.remove("findingCount");
        legacy.setProperty("summary","发现 6 种缺失或不支持的指令");
        try(OutputStream out=Files.newOutputStream(cached)){legacy.store(out,null);}
        ProgramCompatibility.Summary old=ProgramCompatibility.cachedSummary(game.toFile(),game.toFile(),"auto","test-build");
        check(old.status==ProgramCompatibility.Status.FINDINGS && old.findingCount==6);
        Files.write(game.resolve("games.json"),"{}".getBytes(StandardCharsets.UTF_8));a=new Analyzer();scan(game,"auto",a);
        check(a.table.equals(game.resolve("games.json").toString()));
        check(ProgramCompatibility.cachedSummary(game.toFile(),game.toFile(),"auto","test-build").status==ProgramCompatibility.Status.RECHECK);
        Files.write(hidden.resolve("compatibility.properties"),"bad=\\uZZZZ".getBytes(StandardCharsets.US_ASCII));
        check(ProgramCompatibility.cachedSummary(game.toFile(),game.toFile(),"auto","test-build").status==ProgramCompatibility.Status.RECHECK);
        Path empty=Files.createDirectory(root.resolve("empty"));result=scan(empty,"auto",new Analyzer());
        check(result.checked==0 && result.profile.isEmpty() && result.status()==ProgramCompatibility.Status.INCOMPLETE);
        result=scan(empty,"1.636",new Analyzer());check(result.status()==ProgramCompatibility.Status.PARTIAL);
        Path broken=Files.createDirectory(root.resolve("broken"));Files.write(broken.resolve("bad._bp"),new byte[]{1});
        result=scan(broken,"1.636",new Analyzer());check(result.skipped==1 && result.programs==1 && result.checked==0);
        try(RandomAccessFile file=new RandomAccessFile(broken.resolve("large._bp").toFile(),"rw")){file.setLength(ProgramCompatibility.MAX_PROGRAM+1L);}
        result=scan(broken,"1.636",new Analyzer());check(result.skipped==2 && result.programs==2);
        Files.write(broken.resolve("invalid.arc"),new byte[]{1,2,3});result=scan(broken,"1.636",new Analyzer());check(result.limited);
        Path pack=Files.createDirectory(root.resolve("pack"));Files.write(pack.resolve("system.arc"),EncodingProbeTest.archive(new String[]{"ipl._bp"},new byte[][]{program(0x17)},false));
        check(scan(pack,"auto",new Analyzer()).checked==1);
        Path shortened=Files.createDirectory(root.resolve("shortened"));byte[] shortFile=program(0x17);
        EncodingProbeTest.put(shortFile,4,32);Files.write(shortened.resolve("shortened._bp"),shortFile);
        result=scan(shortened,"1.636",new Analyzer());
        check(result.checked==1 && result.skipped==0 && result.problems==1 && result.findings.isEmpty());
        check(result.problemLocations.get(0).contains("shortened._bp"));
        check(result.status()==ProgramCompatibility.Status.PARTIAL);
        Thread.currentThread().interrupt();
        try{scan(game,"auto",new Analyzer());throw new AssertionError("Cancellation ignored");}catch(InterruptedIOException expected){checks++;}finally{Thread.interrupted();}
        Path config=Files.createDirectory(root.resolve("external-entry"));
        result=ProgramCompatibility.scan(config.toFile(),game.toFile(),"1.616",new Analyzer(),(done,total)->{});
        check(result.profile.equals("1.616") && result.checked==2);
        ProgramCompatibility.save(config.toFile(),game.toFile(),"1.616","test",result,"Localized report");
        check(Files.isRegularFile(config.resolve(".openbgi/compatibility.properties")) && !Files.exists(config.resolve(".openbgi/compatibility.properties.tmp")));
        containerTests(root);
        System.out.println("Program compatibility: "+checks+" checks passed");
    }
    public static void main(String[] args)throws Exception {
        Path root=Files.createTempDirectory("openbgi-compat-");
        try{tests(root);}finally{try(java.util.stream.Stream<Path> files=Files.walk(root)){files.sorted(Comparator.reverseOrder()).forEach(p->{try{Files.delete(p);}catch(IOException e){throw new UncheckedIOException(e);}});}}
    }
}
