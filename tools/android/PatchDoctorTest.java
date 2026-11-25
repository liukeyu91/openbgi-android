/* SPDX-License-Identifier: GPL-2.0-only */
package org.openbgi.port;

import java.io.*;
import java.nio.charset.StandardCharsets;
import java.nio.file.*;
import java.util.*;

final class PatchDoctorTest {
    private static void check(boolean value){if(!value)throw new AssertionError("Patch check failed");}
    private static boolean has(PatchDoctor.Result result,PatchDoctor.Code code){
        return result.findings.stream().anyMatch(f->f.code==code);
    }
    private static void writeManifest(Path file,String json)throws IOException{
        Files.write(file,json.getBytes(StandardCharsets.UTF_8));
    }
    public static void main(String[] args)throws Exception{
        Path root=Files.createTempDirectory("openbgi-patch-check");
        Path manifest=root.resolve(GamePatch.MANIFEST);
        Files.createDirectories(manifest.getParent());
        String[] zh={"午休结束后，我坐下来打开笔记本。","我们不能一直保持等待和被动，对吧？",
            "今天的天气很好，我想出去走一走。","这个女孩说话的时候，有一点害羞。"};
        PatchDoctor.Target target=new PatchDoctor.Target("SampleGame","1.680",PatchDoctor.bootHash(new byte[]{1,2,3}));
        GameSettings settings=new GameSettings();
        settings.chooseEncoding("gbk");
        Path arc=root.resolve("story.arc");
        try{
            Files.write(arc,EncodingProbeTest.archive(new String[]{"prologue"},new byte[][]{EncodingProbeTest.scene(zh,"GBK")},true));
            writeManifest(manifest,"{\"encoding\":\"gbk\",\"target\":{\"product\":\"SampleGame\",\"engine\":\"1.680\",\"bootSha256\":\""+target.bootHash+"\"}}");
            PatchDoctor.Result result=PatchDoctor.check(root.toFile(),settings,target,(f,p)->new int[0]);
            check(result.status==PatchDoctor.Status.PASS&&result.archives==1&&result.samples==4);

            writeManifest(manifest,"{\"encoding\":\"sjis\",\"fontSize\":24,\"textBoxWidth\":40}");
            result=PatchDoctor.check(root.toFile(),settings,target,(f,p)->new int[]{p[0]});
            check(result.status==PatchDoctor.Status.WARNING&&has(result,PatchDoctor.Code.WRONG_ENCODING)&&
                has(result,PatchDoctor.Code.BAD_GLYPHS)&&has(result,PatchDoctor.Code.TEXT_WIDTH));

            writeManifest(manifest,"{\"target\":{\"product\":\"OtherGame\",\"engine\":\"1.659\",\"bootSha256\":\""+PatchDoctor.bootHash(new byte[0])+"\"}}");
            result=PatchDoctor.check(root.toFile(),settings,target,(f,p)->new int[0]);
            check(result.status==PatchDoctor.Status.REJECT&&has(result,PatchDoctor.Code.WRONG_PRODUCT)&&
                has(result,PatchDoctor.Code.WRONG_ENGINE)&&has(result,PatchDoctor.Code.WRONG_BOOT));

            writeManifest(manifest,"{}");
            Files.write(arc,new byte[]{1,2,3});
            result=PatchDoctor.check(root.toFile(),settings,target,(f,p)->new int[0]);
            check(result.status==PatchDoctor.Status.REJECT&&has(result,PatchDoctor.Code.BAD_ARCHIVE));

            File game=Files.createDirectory(root.resolve("game")).toFile();
            GameImport.Archive file=new GameImport.Archive("x",1,()->new ByteArrayInputStream(new byte[]{1}));
            GamePatch.install(game,Collections.singletonList(file),false,(a,b)->{});
            File active=GamePatch.active(game);
            File staged;
            try(GamePatch.Prepared patch=GamePatch.prepare(game,Collections.singletonList(file),false,(a,b)->{})){
                staged=patch.files;
                check(active.equals(GamePatch.active(game))&&staged.isDirectory());
            }
            check(active.equals(GamePatch.active(game))&&!staged.exists());
            GameImport.Archive child=new GameImport.Archive("X/file",1,()->new ByteArrayInputStream(new byte[]{1}));
            try(GamePatch.Prepared ignored=GamePatch.prepare(game,Arrays.asList(file,child),false,(a,b)->{})){
                throw new AssertionError("File/directory conflict was accepted");
            }catch(GamePatch.NameConflict expected){check(active.equals(GamePatch.active(game)));}
        }finally{GameImport.remove(root);}
        System.out.println("Patch Doctor tests passed");
    }
}
