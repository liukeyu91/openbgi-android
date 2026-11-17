/* SPDX-License-Identifier: GPL-2.0-only */
package org.openbgi.port;
import java.io.*;
import java.nio.file.*;
import java.util.*;
public final class TranslationRuntimeTest {
    static void check(boolean value){if(!value)throw new AssertionError();}
    public static void main(String[] args)throws Exception {
        Path root=Files.createTempDirectory("translation-runtime-");
        try {
            File entry=Files.createDirectory(root.resolve("entry")).toFile(),resources=Files.createDirectory(root.resolve("source")).toFile();
            Files.createDirectory(resources.toPath().resolve(".openbgi"));
            Properties report=new Properties();report.setProperty("profile","makinghd-evb-resources");
            try(OutputStream out=Files.newOutputStream(resources.toPath().resolve(".openbgi/translation.properties"))){report.store(out,"");}
            GameSettings manual=new GameSettings();manual.chooseEncoding("sjis");manual.profile="1.636";manual.save(entry);
            GameSettings settings=TranslationRuntime.prepare(entry,resources);
            check(settings.fontFamily.equals("Noto Sans CJK SC"));check(settings.encoding.equals("sjis")&&settings.profile.equals("1.636"));
            check(settings.translationProfile.equals("makinghd-evb-resources"));
            settings.fontFamily="";settings.save(entry);check(TranslationRuntime.prepare(entry,resources).fontFamily.isEmpty());
            settings.fontFamily="Noto Serif CJK SC";settings.save(entry);check(GameSettings.load(entry).fontFamily.equals(settings.fontFamily));
            settings.fontFamily="bad\nfont";try{settings.save(entry);throw new AssertionError();}catch(IOException expected){}
            File ordinary=Files.createDirectory(root.resolve("ordinary")).toFile();check(TranslationRuntime.prepare(ordinary,ordinary).fontFamily.isEmpty());
            System.out.println("TRANSLATION_RUNTIME_PASS: generated adapter font requirement, migration, manual choices, persistence and ordinary-game isolation");
        }finally{GameImport.remove(root);}
    }
}
