/* SPDX-License-Identifier: GPL-2.0-only */
package org.openbgi.port;

import java.io.File;
import java.util.*;

/** Localized archive families used by BGI distributions. Keep priorities in sync with paths.c. */
enum ArchiveLanguage {
    CN("cn", "简体中文", "gbk", "c2", "cn"),
    TC("tc", "繁體中文", "big5", "t2", "tc", "c2", "cn"),
    EN("en", "English", "sjis", "e2", "en");

    final String id, label, encoding;
    private final List<String> suffixes;

    ArchiveLanguage(String id, String label, String encoding, String... suffixes) {
        this.id = id;
        this.label = label;
        this.encoding = encoding;
        this.suffixes = Arrays.asList(suffixes);
    }

    static ArchiveLanguage fromId(String id) {
        for (ArchiveLanguage language : values()) if (language.id.equals(id)) return language;
        return null;
    }

    static boolean hasDefault(File root) {
        return hasDefault(root, null);
    }

    static boolean hasDefault(File root, File patch) {
        return hasDefault(Arrays.asList(root, patch));
    }
    static boolean hasDefault(List<File> layers) { return hasDefault(rootFiles(layers)); }

    private static boolean hasDefault(Set<String> files) {
        return files.contains("system.arc") || files.contains("ipl._bp");
    }

    static boolean hasBootFiles(File root) {
        return hasBootFiles(Collections.singletonList(root));
    }
    static boolean hasBootFiles(List<File> layers) {
        Set<String> files = rootFiles(layers);
        return hasDefault(files) || !available(files).isEmpty();
    }
    /** The same test over names only, for a disc image the launcher does not walk. */
    static boolean hasBootFiles(Set<String> names) {
        return hasDefault(names) || !availableNames(names).isEmpty();
    }

    private static Set<String> rootFiles(List<File> layers) {
        Map<String,File> merged = new HashMap<>();
        for (File dir : layers) {
            File[] files = dir == null || !dir.isDirectory() ? null : dir.listFiles();
            if (files != null) for (File file : files) {
                String name = file.getName().toLowerCase(Locale.ROOT);
                if (name.startsWith("system.") || name.startsWith("sysprg.") || name.equals("ipl._bp"))
                    merged.put(name, file);
            }
        }
        merged.values().removeIf(file -> !file.isFile() || file.length() == 0);
        return merged.keySet();
    }

    private boolean hasOwn(Set<String> files, String base) {
        return files.contains(base + "." + suffixes.get(0) + ".arc") ||
               files.contains(base + "." + suffixes.get(1) + ".arc");
    }

    private static List<ArchiveLanguage> available(Set<String> files) {
        return availableNames(files);
    }

    /** The language pairs whose own archives are all present, from names alone. */
    static List<ArchiveLanguage> availableNames(Set<String> files) {
        List<ArchiveLanguage> result = new ArrayList<>();
        for (ArchiveLanguage language : values())
            if (language.hasOwn(files, "system") && language.hasOwn(files, "sysprg")) result.add(language);
        return result;
    }

    static List<ArchiveLanguage> available(File root) { return available(root, null); }

    static List<ArchiveLanguage> available(File root, File patch) { return available(Arrays.asList(root, patch)); }
    static List<ArchiveLanguage> available(List<File> layers) { return available(rootFiles(layers)); }

    static ArchiveLanguage resolve(File root, String stored) {
        return resolve(root, null, stored);
    }

    static ArchiveLanguage resolve(File root, File patch, String stored) {
        return resolve(Arrays.asList(root, patch), stored);
    }

    static ArchiveLanguage resolve(List<File> layers, String stored) {
        Set<String> files = rootFiles(layers);
        List<ArchiveLanguage> choices = available(files);
        ArchiveLanguage selected = fromId(stored);
        if (selected != null && choices.contains(selected)) return selected;
        if (hasDefault(files) || choices.isEmpty()) return null;
        return choices.get(0);
    }

    /** resolve() over names alone: a disc image reports its root without being walked. */
    static ArchiveLanguage resolveNames(Set<String> files, String stored) {
        List<ArchiveLanguage> choices = availableNames(files);
        ArchiveLanguage selected = fromId(stored);
        if (selected != null && choices.contains(selected)) return selected;
        if (hasDefault(files) || choices.isEmpty()) return null;
        return choices.get(0);
    }

    static Map<String,File> layeredFiles(File root, List<File> base, File patch, List<File> extra, ArchiveLanguage language) {
        Map<String,File> result = new LinkedHashMap<>();
        addLayer(result, root, base, language);
        if (patch != null) addLayer(result, patch, extra, language);
        return result;
    }

    static void addLayer(Map<String,File> result, File root, List<File> files, ArchiveLanguage language) {
        if (language != null) {
            for (File file : language.select(files)) {
                String path = displayPath(root, null, file).toLowerCase(Locale.ROOT);
                result.put(logicalName(path), file);
            }
        }
        for (File file : files) {
            String path = displayPath(root, null, file).toLowerCase(Locale.ROOT);
            if (language == null || !path.endsWith(".arc")) result.put(path, file);
        }
    }

    /** addLayer() over names and sizes, for a layer the launcher does not walk (a disc image). */
    static void addEntries(Map<String,String> result, Map<String,Long> files, ArchiveLanguage language) {
        if (language != null) {
            Map<String,String> selected = new LinkedHashMap<>();
            for (Map.Entry<String,Long> item : files.entrySet()) {
                String name = item.getKey(), leaf = name.substring(name.lastIndexOf('/') + 1);
                if (!name.endsWith(".arc")) continue;
                int rank = language.priority(leaf);
                if (rank < 0 || (rank < language.suffixes.size() && item.getValue() == 0)) continue;
                String key = name.substring(0, name.lastIndexOf('/') + 1) + logicalName(leaf);
                String old = selected.get(key);
                if (old == null || rank < language.priority(old.substring(old.lastIndexOf('/') + 1))) selected.put(key, name);
            }
            for (Map.Entry<String,String> item : selected.entrySet()) result.put(item.getKey(), item.getValue());
        }
        for (String path : files.keySet())
            if (language == null || !path.endsWith(".arc")) result.put(path, path);
    }

    static String displayPath(File root, File patch, File file) {
        File layer = patch != null && file.toPath().startsWith(patch.toPath()) ? patch : root;
        return layer.toPath().relativize(file.toPath()).toString().replace(File.separatorChar, '/');
    }

    static String displayPath(List<File> layers, File file) {
        for(int i=layers.size()-1;i>=0;--i) {
            File root=layers.get(i);
            if(root!=null&&file.toPath().startsWith(root.toPath()))return displayPath(root,null,file);
        }
        return file.getName();
    }

    private static String suffix(String name) {
        int length = name.length();
        if (length < 8 || name.charAt(length - 7) != '.' || !name.endsWith(".arc")) return "";
        String suffix = name.substring(length - 6, length - 4);
        for (ArchiveLanguage language : values())
            if (language.suffixes.contains(suffix)) return suffix;
        return "";
    }

    static String logicalName(String name) {
        name = name.toLowerCase(Locale.ROOT);
        return suffix(name).isEmpty() ? name : name.substring(0, name.length() - 7) + ".arc";
    }

    private int priority(String name) {
        String suffix = suffix(name.toLowerCase(Locale.ROOT));
        return suffix.isEmpty() ? suffixes.size() : suffixes.indexOf(suffix);
    }

    List<File> select(List<File> files) {
        Map<File,File> selected = new LinkedHashMap<>();
        for (File file : files) {
            String name = file.getName();
            if (!name.toLowerCase(Locale.ROOT).endsWith(".arc")) continue;
            int rank = priority(name);
            if (rank < 0 || (rank < suffixes.size() && file.length() == 0)) continue;
            File key = new File(file.getParentFile(), logicalName(name));
            File old = selected.get(key);
            if (old == null || rank < priority(old.getName())) selected.put(key, file);
        }
        return new ArrayList<>(selected.values());
    }
}
