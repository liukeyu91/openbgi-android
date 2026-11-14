/* SPDX-License-Identifier: GPL-2.0-only */
package org.openbgi.port;
import java.io.*;

/** Resolves automatic encoding off the UI thread before the VM is started. */
final class GameEncoding {
    static GameSettings prepare(File game,File resources) throws IOException {
        GameSettings settings=GameSettings.load(game);
        if(!settings.needsEncodingDetection())return settings;
        String detected=null,status;
        try {
            EncodingDetector.Result result=EncodingDetector.analyze(ArchiveTextSamples.scan(resources).samples);
            status=result.mixed?"mixed":result.recommended==null?"unknown":"detected";
            if(status.equals("detected"))detected=result.recommended;
        } catch(InterruptedIOException cancelled) { throw cancelled; }
        catch(IOException failed) { status="error"; }
        // Re-read to preserve settings changed while sampling. Manual choice wins.
        settings=GameSettings.load(game);
        if(!settings.needsEncodingDetection())return settings;
        if(detected!=null)settings.encoding=detected;
        settings.encodingDetection=status;
        settings.encodingDetectorVersion=GameSettings.ENCODING_DETECTOR_VERSION;
        settings.save(game);
        return settings;
    }
}
