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
        try(ResourceLayers layers=ResourceLayers.of(game,resources)){
            return layers.disc==null ? prepare(layers,false) : prepareContainer(layers);
        }
    }

    static GameSettings prepareContainer(ResourceLayers layers) throws IOException {
        File game=layers.game;
        layers.disc.requireUnchanged(ContainerGame.requireSource(game));
        ContainerGame.Summary known=ContainerGame.summary(game);
        ContainerGame.Kind kind=ContainerGame.kind(game);
        if(!layers.hasBootFiles())throw new IOException(ContainerGame.unbootable(kind,layers.disc));
        if(ContainerGame.data(game)==null)
            try(GameDataLock lock=GameDataLock.acquire(game)){ContainerGame.data(game,layers.disc);}
        boolean refresh=known==null||!layers.disc.stamp.same(known.stamp);
        GameSettings settings=prepare(layers,refresh);
        if(refresh)ContainerGame.writeSummary(game,kind,layers.disc);
        return settings;
    }

    private static boolean resolveLanguage(ResourceLayers layers,GameSettings settings,boolean refresh) {
        if(refresh&&settings.automaticEncoding){
            settings.encodingDetection="";
            settings.encodingDetectorVersion=0;
        }
        return applyArchiveLanguage(layers,settings);
    }

    private static GameSettings prepare(ResourceLayers layers,boolean refresh) throws IOException {
        File game=layers.game;
        GameSettings settings=GameSettings.load(game);
        boolean changed=resolveLanguage(layers,settings,refresh);
        if(settings.needsEncodingDetection()){
            String detected=null,status;
            try {
                EncodingDetector.Result result=EncodingDetector.analyze(ArchiveTextSamples.scan(layers,null).samples);
                status=result.mixed?"mixed":result.recommended==null?"unknown":"detected";
                if(status.equals("detected"))detected=result.recommended;
            } catch(InterruptedIOException cancelled) { throw cancelled; }
            catch(IOException failed) { status="error"; }
            // Re-read to preserve settings changed while sampling. Manual choice wins.
            settings=GameSettings.load(game);
            changed=resolveLanguage(layers,settings,refresh);
            if(settings.needsEncodingDetection()){
                if(detected!=null)settings.encoding=detected;
                settings.encodingDetection=status;
                settings.encodingDetectorVersion=GameSettings.ENCODING_DETECTOR_VERSION;
                changed=true;
            }
        }
        if(layers.disc!=null)layers.disc.requireUnchanged(ContainerGame.requireSource(game));
        if(changed)settings.save(game);
        return settings;
    }
}
