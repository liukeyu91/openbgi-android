/* SPDX-License-Identifier: GPL-2.0-only */
package org.openbgi.port;
import java.io.*;
import java.util.*;

/** Resolves automatic encoding off the UI thread before the VM is started. */
final class GameEncoding {
    // Use the distribution's code page. Keep the generic detector for games without language variants.
    static boolean applyArchiveLanguage(File resources, GameSettings settings) {
        return applyArchiveLanguage(resources,null,settings);
    }
    static boolean applyArchiveLanguage(File resources, File patch, GameSettings settings) {
        return applyArchiveLanguage(Arrays.asList(resources,patch),settings);
    }
    static boolean applyArchiveLanguage(List<File> layers, GameSettings settings) {
        return applyLanguage(ArchiveLanguage.resolve(layers,settings.archiveLanguage),settings);
    }
    /** The layers may carry a disc image; only its names are consulted here. */
    static boolean applyArchiveLanguage(ResourceLayers layers, GameSettings settings) {
        return applyLanguage(layers.resolve(settings.archiveLanguage),settings);
    }
    private static boolean applyLanguage(ArchiveLanguage language, GameSettings settings) {
        String id=language==null?"":language.id;
        boolean changed=!settings.archiveLanguage.equals(id);
        settings.archiveLanguage=id;
        if(!settings.automaticEncoding)return changed;
        if(language==null) {
            if(changed){settings.encodingDetection="";settings.encodingDetectorVersion=0;}
            return changed;
        }
        changed|=!settings.encoding.equals(language.encoding) || !settings.encodingReady();
        settings.encoding=language.encoding;
        settings.encodingDetection="detected";
        settings.encodingDetectorVersion=GameSettings.ENCODING_DETECTOR_VERSION;
        return changed;
    }

    static GameSettings prepare(File game,File resources) throws IOException {
        GameSettings settings=GameSettings.load(game);
        try(ResourceLayers layers=ResourceLayers.of(game,resources)){
            if(applyArchiveLanguage(layers,settings))settings.save(game);
            if(!settings.needsEncodingDetection())return settings;
            String detected=null,status;
            try {
                EncodingDetector.Result result=EncodingDetector.analyze(ArchiveTextSamples.scan(layers,null).samples);
                status=result.mixed?"mixed":result.recommended==null?"unknown":"detected";
                if(status.equals("detected"))detected=result.recommended;
            } catch(InterruptedIOException cancelled) { throw cancelled; }
            catch(IOException failed) { status="error"; }
            // Re-read to preserve settings changed while sampling. Manual choice wins.
            settings=GameSettings.load(game);
            if(applyArchiveLanguage(layers,settings))settings.save(game);
            if(!settings.needsEncodingDetection())return settings;
            if(detected!=null)settings.encoding=detected;
            settings.encodingDetection=status;
            settings.encodingDetectorVersion=GameSettings.ENCODING_DETECTOR_VERSION;
            settings.save(game);
        }
        return settings;
    }
}
