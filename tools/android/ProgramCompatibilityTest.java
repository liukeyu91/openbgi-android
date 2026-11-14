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
    static void tests(Path root)throws Exception {
        Path game=Files.createDirectory(root.resolve("game"));Files.createDirectory(game.resolve("Archive"));
        byte[] archive=EncodingProbeTest.archive(new String[]{"ipl._bp","scrgrp7._bp"},
            new byte[][]{EncodingProbeTest.dsc(program(0x17)),EncodingProbeTest.dsc(program(0x56))},true);
        Path arc=game.resolve("Archive/System.ARC");Files.write(arc,archive);Analyzer a=new Analyzer();
        ProgramCompatibility.Result result=scan(game,"auto",a);
        check(result.programs==2 && result.checked==2 && result.findings.size()==1);
        check(result.profile.equals("1.636") && result.instructions==4);
        check(Arrays.equals(a.boot,program(0x17)) && a.preference.equals("auto"));
        check(result.report().contains("$56") && result.report().contains("scrgrp7._bp") && result.report().contains("$00000000"));
        check(Arrays.equals(archive,Files.readAllBytes(arc)));
        Files.write(game.resolve("scrgrp7._bp"),program(0x5d));result=scan(game,"auto",new Analyzer());
        check(result.programs==2 && result.checked==2 && result.findings.isEmpty());
        check(result.summary().equals("静态检查未发现缺失指令"));
        Path hidden=Files.createDirectory(game.resolve(".openbgi"));Files.write(hidden.resolve("ignored._bp"),program(0x56));
        Path saves=Files.createDirectory(game.resolve("UserData"));Files.write(saves.resolve("ignored._bp"),program(0x56));
        result=scan(game,"auto",new Analyzer());check(result.programs==2 && result.findings.isEmpty());
        ProgramCompatibility.save(game.toFile(),game.toFile(),"auto","test-build",result);
        check(ProgramCompatibility.cachedSummary(game.toFile(),game.toFile(),"auto","test-build").equals(result.summary()));
        check(ProgramCompatibility.cachedSummary(game.toFile(),game.toFile(),"auto","other-build").contains("重新检查"));
        check(ProgramCompatibility.cachedSummary(game.toFile(),game.toFile(),"1.616","test-build").contains("重新检查"));
        Files.write(game.resolve("games.json"),"{}".getBytes(StandardCharsets.UTF_8));a=new Analyzer();scan(game,"auto",a);
        check(a.table.equals(game.resolve("games.json").toString()));
        check(ProgramCompatibility.cachedSummary(game.toFile(),game.toFile(),"auto","test-build").contains("重新检查"));
        Files.write(hidden.resolve("compatibility.properties"),"bad=\\uZZZZ".getBytes(StandardCharsets.US_ASCII));
        check(ProgramCompatibility.cachedSummary(game.toFile(),game.toFile(),"auto","test-build").contains("未检查"));
        Path empty=Files.createDirectory(root.resolve("empty"));result=scan(empty,"auto",new Analyzer());
        check(result.checked==0 && result.profile.isEmpty() && result.summary().equals("检查未完成"));
        result=scan(empty,"1.636",new Analyzer());check(result.summary().equals("检查不完整"));
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
        check(result.report().contains("文件头或代码存在异常") && result.report().contains("shortened._bp"));
        check(result.summary().equals("检查不完整"));
        Thread.currentThread().interrupt();
        try{scan(game,"auto",new Analyzer());throw new AssertionError("Cancellation ignored");}catch(InterruptedIOException expected){checks++;}finally{Thread.interrupted();}
        Path config=Files.createDirectory(root.resolve("external-entry"));
        result=ProgramCompatibility.scan(config.toFile(),game.toFile(),"1.616",new Analyzer(),(done,total)->{});
        check(result.profile.equals("1.616") && result.checked==2);
        ProgramCompatibility.save(config.toFile(),game.toFile(),"1.616","test",result);
        check(Files.isRegularFile(config.resolve(".openbgi/compatibility.properties")) && !Files.exists(config.resolve(".openbgi/compatibility.properties.tmp")));
        System.out.println("Program compatibility: "+checks+" checks passed");
    }
    public static void main(String[] args)throws Exception {
        Path root=Files.createTempDirectory("openbgi-compat-");
        try{tests(root);}finally{try(java.util.stream.Stream<Path> files=Files.walk(root)){files.sorted(Comparator.reverseOrder()).forEach(p->{try{Files.delete(p);}catch(IOException e){throw new UncheckedIOException(e);}});}}
    }
}
