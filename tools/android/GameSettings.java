/* SPDX-License-Identifier: GPL-2.0-only */
package org.openbgi.port;

import java.io.*;
import java.nio.charset.StandardCharsets;
import java.nio.file.*;
import java.util.*;

final class GameSettings {
    static final int ENCODING_DETECTOR_VERSION = 1;
    String encoding = "sjis", profile = "auto";
    boolean automaticEncoding = true;
    String encodingDetection = "";
    int encodingDetectorVersion;
    int scale, touch, saveKey = 0x53, loadKey = 0x4c, backlogKey, menuKey = 0x1b, skipKey = 0x11;
    boolean virtualKeys = true, swapAB;
    private static boolean member(String s, String[] options) { return Arrays.asList(options).contains(s); }
    private static int number(Properties p, String key, int fallback, int min, int max) {
        try { int n = Integer.parseInt(p.getProperty(key, "")); return n >= min && n <= max ? n : fallback; }
        catch(NumberFormatException bad) { return fallback; }
    }
    private static boolean bool(Properties p, String key, boolean fallback) {
        String s = p.getProperty(key); return "true".equals(s) || "false".equals(s) ? Boolean.parseBoolean(s) : fallback;
    }
    static GameSettings load(File game) throws IOException {
        GameSettings s = new GameSettings(); File config = new File(game, ".openbgi");
        File legacy = new File(config, "encoding.txt");
        if(legacy.isFile()) {
            String value = new String(Files.readAllBytes(legacy.toPath()), StandardCharsets.UTF_8).trim();
            if(member(value, new String[]{"sjis","gbk","utf8"})) s.encoding = value;
        }
        Properties p = new Properties(); File file = new File(config, "settings.ini");
        if(file.isFile()) try(InputStream in = new FileInputStream(file)) { p.load(in); }
        catch(IllegalArgumentException bad) { throw new IOException("设置文件格式错误", bad); }
        String value = p.getProperty("encoding", s.encoding);
        if(member(value, new String[]{"sjis","gbk","utf8"})) s.encoding = value;
        // Old imports wrote encoding.txt=sjis without consulting the detector.
        // The settings page always saved an explicit encoding in settings.ini.
        boolean explicit = member(p.getProperty("encoding", ""),new String[]{"sjis","gbk","utf8"});
        s.automaticEncoding = !explicit && s.encoding.equals("sjis");
        String mode = p.getProperty("encodingMode", "");
        if(mode.equals("auto")) s.automaticEncoding = true;
        else if(mode.equals("manual")) s.automaticEncoding = false;
        value = p.getProperty("encodingDetection", "");
        if(member(value,new String[]{"detected","unknown","mixed","error"}) && explicit) s.encodingDetection = value;
        s.encodingDetectorVersion = number(p,"encodingDetectorVersion",0,0,Integer.MAX_VALUE);
        value = p.getProperty("profile", "auto");
        if(member(value, EngineProfiles.NAMES)) s.profile = value;
        s.scale = number(p,"scale",0,0,2); s.touch = number(p,"touch",0,0,1);
        s.saveKey = number(p,"saveKey",0x53,1,255); s.loadKey = number(p,"loadKey",0x4c,1,255);
        s.backlogKey = number(p,"backlogKey",0,0,255); s.menuKey = number(p,"menuKey",0x1b,1,255);
        s.skipKey = number(p,"skipKey",0x11,1,255);
        s.virtualKeys = bool(p,"virtualKeys",true); s.swapAB = bool(p,"swapAB",false);
        return s;
    }
    boolean needsEncodingDetection() {
        return automaticEncoding && (encodingDetectorVersion != ENCODING_DETECTOR_VERSION ||
            encodingDetection.isEmpty() || encodingDetection.equals("error"));
    }
    boolean encodingReady() {
        return !automaticEncoding || (!needsEncodingDetection() && encodingDetection.equals("detected"));
    }
    void chooseEncoding(String choice) {
        if(choice.equals("auto")) {
            if(!automaticEncoding) { encodingDetection=""; encodingDetectorVersion=0; }
            automaticEncoding=true;
        } else {
            if(!member(choice,new String[]{"sjis","gbk","utf8"}))throw new IllegalArgumentException("Invalid encoding");
            encoding=choice; automaticEncoding=false;
        }
    }
    String encodingLabel() {
        if(!automaticEncoding)return encoding.equals("utf8")?"UTF-8":encoding.toUpperCase(Locale.ROOT);
        if(encodingReady())return "自动："+(encoding.equals("utf8")?"UTF-8":encoding.toUpperCase(Locale.ROOT));
        return needsEncodingDetection()?"自动：待检测":"自动：未确定";
    }
    String encodingStatus() {
        if(!automaticEncoding)return "已手动选择 "+encodingLabel()+"。";
        if(encodingReady())return "自动识别结果："+(encoding.equals("utf8")?"UTF-8":encoding.toUpperCase(Locale.ROOT))+"。";
        if(encodingDetection.equals("mixed"))return "样本可能使用多种编码，请预览并手动选择。";
        if(encodingDetection.equals("error"))return "无法读取文字样本，请预览或手动选择编码。";
        if(needsEncodingDetection())return "将在导入完成或启动游戏前检测文字编码。";
        return "样本不足或结果不明确，请预览并手动选择编码。";
    }
    void save(File game) throws IOException {
        if(!member(encoding,new String[]{"sjis","gbk","utf8"}) || !member(profile,EngineProfiles.NAMES) ||
            scale < 0 || scale > 2 || touch < 0 || touch > 1 || saveKey < 1 || saveKey > 255 ||
            loadKey < 1 || loadKey > 255 || backlogKey < 0 || backlogKey > 255 || menuKey < 1 || menuKey > 255 || skipKey < 1 || skipKey > 255)
            throw new IOException("无效的游戏设置");
        File config = new File(game,".openbgi"); Files.createDirectories(config.toPath());
        Properties p = new Properties();
        p.setProperty("encoding",encoding); p.setProperty("profile",profile);
        p.setProperty("encodingMode",automaticEncoding?"auto":"manual");
        p.setProperty("encodingDetection",encodingDetection);
        p.setProperty("encodingDetectorVersion",Integer.toString(encodingDetectorVersion));
        p.setProperty("scale",Integer.toString(scale)); p.setProperty("touch",Integer.toString(touch));
        p.setProperty("saveKey",Integer.toString(saveKey)); p.setProperty("loadKey",Integer.toString(loadKey));
        p.setProperty("backlogKey",Integer.toString(backlogKey)); p.setProperty("menuKey",Integer.toString(menuKey));
        p.setProperty("skipKey",Integer.toString(skipKey));
        p.setProperty("virtualKeys",Boolean.toString(virtualKeys)); p.setProperty("swapAB",Boolean.toString(swapAB));
        Path file = new File(config,"settings.ini").toPath(), temp = file.resolveSibling("settings.ini.tmp");
        try {
            try(OutputStream stream = Files.newOutputStream(temp)) { p.store(stream,"OpenBGI per-game settings"); }
            Files.move(temp,file,StandardCopyOption.ATOMIC_MOVE,StandardCopyOption.REPLACE_EXISTING);
        } finally { Files.deleteIfExists(temp); }
    }
    static File activeGame(File files) throws IOException {
        String id = new String(Files.readAllBytes(new File(files,"active-game.txt").toPath()),StandardCharsets.UTF_8).trim();
        if(!id.matches("[a-z0-9-]{1,79}")) throw new IOException("无效的游戏目录");
        return new File(new File(files,"games"),id);
    }
}
