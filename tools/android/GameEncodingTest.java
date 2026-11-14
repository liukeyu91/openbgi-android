/* SPDX-License-Identifier: GPL-2.0-only */
package org.openbgi.port;
import java.io.*;
import java.nio.file.*;
import java.nio.charset.StandardCharsets;
import java.util.*;

/** Import/startup integration, migration and explicit-override regressions. */
public final class GameEncodingTest {
    private static int checks;
    private static final String[] ZH={"今天的天气很好，我想出去走一走。","这个女孩说话的时候，有一点害羞。","午休结束后，我坐下来打开笔记本。","我们不能一直保持等待和被动，对吧？"};
    private static final String[] JP={"昼休みが終わって、ノートを開いた。","今日はいい天気だから、散歩に行きましょう。","彼女は少し恥ずかしそうに笑っていた。","あなたと一緒に過ごす時間が好きです。"};
    private static void check(boolean value,String label){if(!value)throw new AssertionError(label);checks++;}
    private static Path game(Path root,String name,String encoding)throws Exception {
        Path dir=Files.createDirectory(root.resolve(name));
        String charset=encoding.equals("sjis")?"windows-31j":encoding.equals("gbk")?"GBK":"UTF-8";
        byte[] script=EncodingProbeTest.dsc(EncodingProbeTest.scene(encoding.equals("sjis")?JP:ZH,charset));
        Files.write(dir.resolve("system.arc"),EncodingProbeTest.archive(new String[]{"a_prologue01"},new byte[][]{script},true));
        Files.write(dir.resolve("slot.sud"),new byte[]{1,2,3});return dir;
    }
    private static void write(Path dir,String name,String text)throws IOException {
        Files.createDirectories(dir.resolve(".openbgi"));Files.write(dir.resolve(".openbgi/"+name),text.getBytes(StandardCharsets.UTF_8));
    }
    private static File noScan(){return new File("unused"){
        @Override public File[] listFiles(){throw new AssertionError("Unexpected repeated scan");}
    };}
    private static void tests(Path root)throws Exception {
        for(String encoding:new String[]{"sjis","gbk","utf8"}){
            Path dir=game(root,"new-"+encoding,encoding);byte[] archive=Files.readAllBytes(dir.resolve("system.arc"));
            check(!GameSettings.load(dir.toFile()).encodingReady(),"New import must be unresolved");
            GameSettings settings=GameEncoding.prepare(dir.toFile(),dir.toFile());
            check(settings.automaticEncoding && settings.encodingReady() && encoding.equals(settings.encoding),"Automatic "+encoding);
            settings=GameSettings.load(dir.toFile());check(settings.automaticEncoding && settings.encodingReady() && encoding.equals(settings.encoding),"Persist result "+encoding);
            check(GameEncoding.prepare(dir.toFile(),noScan()).encoding.equals(encoding),"Cached result "+encoding);
            Properties props=new Properties();try(InputStream in=Files.newInputStream(dir.resolve(".openbgi/settings.ini"))){props.load(in);}
            check(encoding.equals(props.getProperty("encoding")) && "auto".equals(props.getProperty("encodingMode")),"Native encoding and mode "+encoding);
            check(Arrays.equals(archive,Files.readAllBytes(dir.resolve("system.arc"))) && Arrays.equals(new byte[]{1,2,3},Files.readAllBytes(dir.resolve("slot.sud"))),"Resources/saves unchanged "+encoding);
        }
        Path old=game(root,"old-default","gbk");write(old,"encoding.txt","sjis");write(old,"settings.ini","scale=2\ntouch=1\nmenuKey=112\n");
        GameSettings s=GameEncoding.prepare(old.toFile(),old.toFile());
        check(s.automaticEncoding && s.encoding.equals("gbk") && s.encodingReady(),"Repair old implicit SJIS");
        check(s.scale==2 && s.touch==1 && s.menuKey==112,"Other preferences preserved");
        check(new String(Files.readAllBytes(old.resolve(".openbgi/encoding.txt")),StandardCharsets.UTF_8).equals("sjis"),"Legacy file unchanged; resolved settings take precedence");
        Path explicit=game(root,"explicit-sjis","gbk");write(explicit,"encoding.txt","sjis");write(explicit,"settings.ini","encoding=sjis\nscale=1\n");
        byte[] before=Files.readAllBytes(explicit.resolve(".openbgi/settings.ini"));
        s=GameEncoding.prepare(explicit.toFile(),noScan());
        check(!s.automaticEncoding && s.encoding.equals("sjis") && s.encodingReady(),"Explicit old SJIS preserved");
        check(Arrays.equals(before,Files.readAllBytes(explicit.resolve(".openbgi/settings.ini"))),"Manual configuration not rewritten");
        s.chooseEncoding("auto");s.save(explicit.toFile());s=GameEncoding.prepare(explicit.toFile(),explicit.toFile());
        check(s.automaticEncoding && s.encoding.equals("gbk") && s.encodingReady(),"Re-enable automatic mode");
        s.chooseEncoding("utf8");s.save(explicit.toFile());s=GameEncoding.prepare(explicit.toFile(),noScan());
        check(!s.automaticEncoding && s.encoding.equals("utf8") && s.encodingReady(),"Manual override after detection");
        for(String encoding:new String[]{"gbk","utf8"}){
            Path legacy=game(root,"legacy-"+encoding,"sjis");write(legacy,"encoding.txt",encoding);
            s=GameEncoding.prepare(legacy.toFile(),noScan());check(!s.automaticEncoding && s.encoding.equals(encoding),"Legacy override "+encoding);
        }
        Path empty=Files.createDirectory(root.resolve("empty"));Files.write(empty.resolve("system.arc"),new byte[]{1});
        s=GameEncoding.prepare(empty.toFile(),empty.toFile());
        check(s.automaticEncoding && !s.encodingReady() && s.encodingDetection.equals("unknown"),"No text must not silently launch SJIS");
        check(!GameEncoding.prepare(empty.toFile(),noScan()).encodingReady(),"Uncertain result persists");
        s.chooseEncoding("sjis");s.save(empty.toFile());check(GameEncoding.prepare(empty.toFile(),noScan()).encodingReady(),"Explicit SJIS allows launch without samples");
        Path mixed=game(root,"mixed","gbk");Files.write(mixed.resolve("b_prologue02"),EncodingProbeTest.scene(JP,"windows-31j"));
        s=GameEncoding.prepare(mixed.toFile(),mixed.toFile());check(s.encodingDetection.equals("mixed") && !s.encodingReady(),"Mixed scripts require confirmation");
        Path overlay=game(root,"overlay","sjis");Files.write(overlay.resolve("a_prologue01"),EncodingProbeTest.scene(ZH,"GBK"));
        s=GameEncoding.prepare(overlay.toFile(),overlay.toFile());check(s.encodingReady() && s.encoding.equals("gbk"),"Loose patch shadows original archive");
        File config=Files.createDirectory(root.resolve("external-entry")).toFile();s=GameEncoding.prepare(config,overlay.toFile());
        check(s.encodingReady() && s.encoding.equals("gbk") && new File(config,".openbgi/settings.ini").isFile(),"Resource and configuration paths can differ");
        Path cancel=game(root,"cancel","gbk");Thread.currentThread().interrupt();
        try{GameEncoding.prepare(cancel.toFile(),cancel.toFile());throw new AssertionError("Cancellation ignored");}
        catch(InterruptedIOException expected){check(!Files.exists(cancel.resolve(".openbgi/settings.ini")),"Cancelled scan does not persist fallback");}
        finally{Thread.interrupted();}
        s=GameSettings.load(old.toFile());s.encodingDetectorVersion=0;s.save(old.toFile());
        s=GameEncoding.prepare(old.toFile(),old.toFile());check(s.encodingReady() && s.encodingDetectorVersion==GameSettings.ENCODING_DETECTOR_VERSION,"Stale detector re-evaluated");
        System.out.println("Automatic game encoding: "+checks+" checks passed");
    }
    public static void main(String[] args)throws Exception {
        Path root=Files.createTempDirectory("openbgi-auto-encoding-");
        try{tests(root);}finally{
            try(java.util.stream.Stream<Path> files=Files.walk(root)){files.sorted(Comparator.reverseOrder()).forEach(p->{try{Files.delete(p);}catch(IOException e){throw new UncheckedIOException(e);}});}
        }
    }
}
