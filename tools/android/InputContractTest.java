/* SPDX-License-Identifier: GPL-2.0-only */
package org.openbgi.port;
import java.io.*;
import java.nio.file.*;
import java.nio.charset.StandardCharsets;
import java.util.*;

public final class InputContractTest {
    static int checks;
    static void check(boolean ok){if(!ok)throw new AssertionError("check "+(checks+1));checks++;}
    static final class Events implements TouchInput.Sink,TouchInput.Timer,InputKeys.Sink {
        final ArrayList<String> events=new ArrayList<>();final ArrayList<Runnable> timers=new ArrayList<>();
        public void pointer(int a,int b,float x,float y,int w,int h,float wheel){events.add("p"+a+":"+b+":"+(int)x+":"+(int)y+":"+(int)wheel);}
        public void later(Runnable r,int ms){timers.add(r);}
        public void key(int a,int vk,int repeat){events.add("k"+a+":"+vk+":"+repeat);}
        void fire(){ArrayList<Runnable> old=new ArrayList<>(timers);timers.clear();for(Runnable r:old)r.run();}
        long count(String prefix){return events.stream().filter(e->e.startsWith(prefix)).count();}
    }
    static void gestures(){
        Events e=new Events();TouchInput t=new TouchInput(e,e,false,10,500);
        t.down(4,100,100,1280,720,true);t.up(100,100);e.fire();check(e.count("p1:0:")==1 && e.count("p2:0:")==1 && e.count("p1:1:")==0);
        e.events.clear();t.down(4,100,100,1280,720,true);e.fire();t.up(100,100);check(e.count("p1:1:")==1 && e.count("p2:1:")==1 && e.count("p1:0:")==0);
        e.events.clear();t.down(4,100,100,1280,720,true);t.secondPointer();e.fire();t.up(100,100);t.up(100,100);check(e.count("p1:1:")==1 && e.count("p2:1:")==1);
        e.events.clear();t.down(4,100,100,1280,720,true);t.move(100,225);t.up(100,225);e.fire();check(e.count("p3:")==1 && e.events.contains("p3:0:100:225:2") && e.count("p1:")==0);
        e.events.clear();t.down(4,100,100,1280,720,true);t.move(100,120);t.up(100,120);e.fire();check(e.count("p1:")==0 && e.count("p3:")==0);
        e.events.clear();t.down(4,100,100,1280,720,true);t.cancel();e.fire();check(e.count("p1:")==0);
        e.events.clear();t.down(4,100,100,1280,720,false);t.up(100,100);e.fire();check(e.events.isEmpty());
        e.events.clear();t=new TouchInput(e,e,true,10,500);t.down(1,100,100,1280,720,true);t.move(150,120);t.move(-20,120);t.up(-20,120);e.fire();
        check(e.count("p1:0:")==1 && e.events.contains("p1:0:100:100:0") && e.events.contains("p2:0:-20:120:0") && e.count("p1:1:")==0);
        e.events.clear();t.down(1,100,100,1280,720,true);t.move(150,120);t.cancel();e.fire();check(e.count("p1:0:")==1 && e.count("p2:0:")==1);
        e.events.clear();t.down(1,100,100,1280,720,true);t.move(150,120);t.secondPointer();t.up(150,120);e.fire();check(e.count("p2:0:")==1 && e.count("p1:1:")==0);
        e.events.clear();t.down(1,100,100,1280,720,true);t.up(100,100);t.down(1,200,200,1280,720,true);e.fire();check(e.count("p1:1:")==1);t.up(200,200);
    }
    static void keys(){
        check(InputKeys.map(29,false)==65 && InputKeys.map(54,false)==90);check(InputKeys.map(7,false)==48 && InputKeys.map(16,false)==57);
        check(InputKeys.map(131,false)==112 && InputKeys.map(142,false)==123);check(InputKeys.map(96,false)==13 && InputKeys.map(97,false)==27);
        check(InputKeys.map(96,true)==27 && InputKeys.map(97,true)==13);check(InputKeys.map(3,false)==0 && InputKeys.map(24,false)==0);
        Events e=new Events();InputKeys k=new InputKeys(e);
        k.physical(1,113,17,true,0);k.physical(1,114,17,true,0);check(e.events.size()==1);k.physical(1,113,17,false,0);check(e.events.size()==1);
        k.physical(1,114,17,false,0);check(e.events.equals(Arrays.asList("k0:17:0","k1:17:0")));
        e.events.clear();k.physical(1,66,13,true,0);k.physical(2,96,13,true,0);k.releaseDevice(1);check(e.events.size()==1);k.releaseDevice(2);check(e.events.size()==2 && e.events.get(1).equals("k1:13:0"));
        e.events.clear();k.physical(1,19,38,true,0);k.axes(1,0,-1,0,0,0,0);k.axes(1,0,0,0,0,0,0);check(e.events.size()==1);k.physical(1,19,38,false,0);check(e.events.size()==2);
        e.events.clear();k.axes(4,0,0,0,0,1,1);k.axes(4,0,0,0,0,0,1);check(e.events.size()==1);k.axes(4,0,0,0,0,0,0);check(e.events.size()==2);
        e.events.clear();k.physical(-1,66,13,true,0);k.tap(13);check(e.events.equals(Arrays.asList("k0:13:0","k0:13:1")));k.clear();check(e.events.get(2).equals("k1:13:0"));
        e.events.clear();k.physical(0,66,13,false,0);k.physical(0,99,256,true,0);k.tap(0);check(e.events.isEmpty());
        k.axes(0,-1,0,0,0,0,0);k.axes(0,1,0,0,0,0,0);k.clear();check(e.events.equals(Arrays.asList("k0:37:0","k1:37:0","k0:39:0","k1:39:0")));
        e.events.clear();check(k.toggleShortcut(17) && k.shortcutHeld());check(e.events.equals(Arrays.asList("k0:17:0")));
        check(!k.toggleShortcut(17) && !k.shortcutHeld());check(e.events.equals(Arrays.asList("k0:17:0","k1:17:0")));
        e.events.clear();k.physical(3,113,17,true,0);k.toggleShortcut(17);k.releaseShortcut();check(e.events.equals(Arrays.asList("k0:17:0")));
        k.physical(3,113,17,false,0);check(e.events.equals(Arrays.asList("k0:17:0","k1:17:0")));
        e.events.clear();k.toggleShortcut(17);k.physical(3,113,17,true,0);k.physical(3,113,17,false,0);
        check(e.events.equals(Arrays.asList("k0:17:0")));k.clear();check(!k.shortcutHeld() && e.events.equals(Arrays.asList("k0:17:0","k1:17:0")));
        e.events.clear();k.toggleShortcut(17);k.toggleShortcut(65);k.releaseShortcut();
        check(e.events.equals(Arrays.asList("k0:17:0","k1:17:0","k0:65:0","k1:65:0")));
        e.events.clear();check(!k.toggleShortcut(0) && !k.toggleShortcut(256) && !k.shortcutHeld() && e.events.isEmpty());
    }
    static void settings() throws Exception {
        Path root=Files.createTempDirectory("openbgi-input-settings-");File a=root.resolve("游戏 A").toFile(),b=root.resolve("game B").toFile();
        Files.createDirectories(new File(a,".openbgi").toPath());Files.write(new File(a,".openbgi/encoding.txt").toPath(),"gbk".getBytes(StandardCharsets.UTF_8));
        GameSettings s=GameSettings.load(a);check(s.encoding.equals("gbk") && s.profile.equals("auto") && s.scale==0 && s.virtualKeys);
        check(s.skipKey==17);
        s.profile="1.616";s.scale=2;s.touch=1;s.virtualKeys=false;s.swapAB=true;s.saveKey=116;s.loadKey=120;s.backlogKey=33;s.skipKey=65;s.save(a);
        GameSettings v=GameSettings.load(a);check(v.profile.equals("1.616") && v.scale==2 && v.touch==1 && !v.virtualKeys && v.swapAB && v.saveKey==116 && v.loadKey==120 && v.backlogKey==33);
        check(v.skipKey==65 && GameSettings.load(b).skipKey==17);
        check(!new File(a,".openbgi/settings.ini.tmp").exists());check(GameSettings.load(b).encoding.equals("sjis"));
        GameSettings.load(b).save(b);check(GameSettings.load(a).scale==2 && GameSettings.load(b).scale==0);
        v.encoding="utf8";check(GameSettings.load(a).encoding.equals("gbk")); // cancelled edit never writes
        byte[] before=Files.readAllBytes(new File(a,".openbgi/settings.ini").toPath());v.profile="1.616 --productname=bad";
        try{v.save(a);throw new AssertionError("bad profile accepted");}catch(IOException expected){checks++;}
        check(Arrays.equals(before,Files.readAllBytes(new File(a,".openbgi/settings.ini").toPath())));
        Files.write(new File(a,".openbgi/settings.ini").toPath(),"encoding=bad\nprofile=1.616 --bad\nscale=99\nmenuKey=0\nvirtualKeys=bad\n".getBytes(StandardCharsets.UTF_8));
        s=GameSettings.load(a);check(s.encoding.equals("gbk") && s.profile.equals("auto") && s.scale==0 && s.menuKey==27 && s.virtualKeys);
        check(s.skipKey==17);
        Files.write(new File(a,".openbgi/settings.ini").toPath(),"skipKey=999\n".getBytes(StandardCharsets.UTF_8));
        check(GameSettings.load(a).skipKey==17);s.skipKey=0;
        try{s.save(a);throw new AssertionError("bad skip key accepted");}catch(IOException expected){checks++;}
        check(new String(Files.readAllBytes(new File(a,".openbgi/encoding.txt").toPath()),StandardCharsets.UTF_8).equals("gbk"));
        File files=root.resolve("files").toFile();files.mkdirs();Files.write(new File(files,"active-game.txt").toPath(),"../escape".getBytes(StandardCharsets.UTF_8));
        try{GameSettings.activeGame(files);throw new AssertionError("path escaped");}catch(IOException expected){checks++;}
        Files.write(new File(files,"active-game.txt").toPath(),"a-b1\n".getBytes(StandardCharsets.UTF_8));check(GameSettings.activeGame(files).equals(new File(files,"games/a-b1")));
    }
    static void viewport(){
        Viewport v=new Viewport(2248,1080,1280,720,0);check(v.x==164 && v.y==0 && v.w==1920 && v.h==1080);
        check(v.contains(164,0) && !v.contains(163,0) && !v.contains(2084,0));
        v=new Viewport(2248,1080,1280,720,2);check(v.x==484 && v.y==180 && v.w==1280 && v.h==720);
        v=new Viewport(2248,1080,1280,720,1);check(v.x==0 && v.y==0 && v.w==2248 && v.h==1080);
        v=new Viewport(640,480,1280,720,2);check(v.x==0 && v.y==60 && v.w==640 && v.h==360);
        check(!new Viewport(0,0,1280,720,0).contains(0,0));
    }
    public static void main(String[] args) throws Exception {
        if(args.length>0){
            for(int sw:new int[]{0,1,640,1280,1920,2248})for(int sh:new int[]{0,1,480,720,1080})
                for(int[] c:new int[][]{{1280,720},{1920,1080},{800,600}})for(int mode=0;mode<3;mode++){
                    Viewport v=new Viewport(sw,sh,c[0],c[1],mode);System.out.println(v.x+","+v.y+","+v.w+","+v.h);
                }return;
        }
        gestures();keys();settings();viewport();System.out.println("Input/settings: "+checks+" checks passed");
    }
}
