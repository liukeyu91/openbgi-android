/* SPDX-License-Identifier: GPL-2.0-only */
package org.openbgi.port;
import java.io.*;
import java.util.*;

/** Reusable runtime requirements shared by old imports and direct folders. */
final class TranslationRuntime {
    static GameSettings prepare(File entry,File resources)throws IOException {
        GameSettings settings=GameSettings.load(entry);
        // A recorded adapter also records the user's decision to restore
        // script fonts. Migration must never re-enable an override each launch.
        if(!settings.translationProfile.isEmpty())return settings;
        Properties report=new Properties();File record=new File(resources,".openbgi/translation.properties");
        if(!record.isFile()||record.length()>4*1024*1024)return settings;
        try(InputStream in=new FileInputStream(record)){report.load(in);}
        String id=report.getProperty("profile","");
        for(PatchImport.Profile profile:PatchProfiles.ALL)if(profile.id.equals(id)){
            settings.translationProfile=id;
            if(settings.fontFamily.isEmpty())settings.fontFamily=profile.fontFamily;
            // Existing manual encoding/engine choices win during migration.
            settings.save(entry);break;
        }
        return settings;
    }
}
