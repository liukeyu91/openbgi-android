/* SPDX-License-Identifier: GPL-2.0-only */
package org.openbgi.port;

import android.app.LocaleManager;
import android.content.Context;
import android.content.SharedPreferences;
import android.content.res.Configuration;
import android.icu.util.ULocale;
import android.os.Build;
import android.os.LocaleList;
import java.util.Locale;

final class AppLanguage {
    private enum Language {
        SYSTEM("", ""),
        SIMPLIFIED_CHINESE("zh-Hans", "简体中文"),
        TRADITIONAL_CHINESE("zh-Hant", "繁體中文"),
        ENGLISH("en", "English"),
        JAPANESE("ja", "日本語"),
        KOREAN("ko", "한국어"),
        SPANISH("es", "Español"),
        FRENCH("fr", "Français"),
        GERMAN("de", "Deutsch");

        final String tag;
        final String label;

        Language(String tag, String label) {
            this.tag = tag;
            this.label = label;
        }
    }

    static String[] labels(Context context) {
        Language[] languages = Language.values();
        String[] labels = new String[languages.length];
        labels[0] = context.getString(R.string.system_language);
        for (int i = 1; i < labels.length; i++) labels[i] = languages[i].label;
        return labels;
    }

    private AppLanguage() {}

    private static SharedPreferences preferences(Context context) {
        return context.getSharedPreferences("app-language", Context.MODE_PRIVATE);
    }

    static Context wrap(Context base) {
        if (Build.VERSION.SDK_INT >= 33) {
            // Carry over an explicit choice made before an Android OS upgrade.
            SharedPreferences prefs = preferences(base);
            if (prefs.contains("language")) {
                LocaleManager manager = base.getSystemService(LocaleManager.class);
                String tag = prefs.getString("language", "");
                prefs.edit().remove("language").commit();
                if (manager.getApplicationLocales().isEmpty()) {
                    manager.setApplicationLocales(LocaleList.forLanguageTags(tag));
                }
            }
            return base;
        }
        String tag = preferences(base).getString("language", "");
        if (tag.isEmpty()) return base;
        Configuration config = new Configuration();
        config.setLocales(LocaleList.forLanguageTags(tag));
        return base.createConfigurationContext(config);
    }

    static int selected(Context context) {
        Locale locale;
        if (Build.VERSION.SDK_INT >= 33) {
            LocaleList locales = context.getSystemService(LocaleManager.class).getApplicationLocales();
            if (locales.isEmpty()) return Language.SYSTEM.ordinal();
            locale = locales.get(0);
        } else {
            locale = Locale.forLanguageTag(preferences(context).getString("language", ""));
        }
        String language = locale.getLanguage();
        if (language.equals("zh")) {
            String script = ULocale.addLikelySubtags(ULocale.forLocale(locale)).getScript();
            return (script.equals("Hant") ? Language.TRADITIONAL_CHINESE : Language.SIMPLIFIED_CHINESE).ordinal();
        }
        for (Language choice : Language.values()) {
            if (choice.tag.equals(language)) return choice.ordinal();
        }
        return Language.SYSTEM.ordinal();
    }

    static boolean set(Context context, int index) {
        String tag = Language.values()[index].tag;
        if (Build.VERSION.SDK_INT >= 33) {
            context.getSystemService(LocaleManager.class).setApplicationLocales(LocaleList.forLanguageTags(tag));
            return true;
        }
        // The engine starts in a separate process and must see the new value on disk.
        return preferences(context).edit().putString("language", tag).commit();
    }
}
