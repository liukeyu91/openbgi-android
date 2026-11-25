/* SPDX-License-Identifier: GPL-2.0-only */
package org.openbgi.port;

import android.content.BroadcastReceiver;
import android.content.Context;
import android.content.Intent;
import android.content.IntentFilter;
import android.media.AudioAttributes;
import android.media.AudioFocusRequest;
import android.media.AudioManager;
import android.os.Build;
import android.os.Handler;
import android.os.Looper;

/** Owns exactly one focus request and the foreground-only noisy receiver. */
final class GameAudioFocus {
    private final Context context;
    private final AudioFocusController controller;
    private boolean registered;
    private final BroadcastReceiver noisy = new BroadcastReceiver() {
        @Override public void onReceive(Context c, Intent intent) {
            if(AudioManager.ACTION_AUDIO_BECOMING_NOISY.equals(intent.getAction())) controller.noisy();
        }
    };
    GameAudioFocus(Context context, AudioFocusController.Observer observer) {
        this.context = context;
        AudioManager manager = (AudioManager)context.getSystemService(Context.AUDIO_SERVICE);
        controller = new AudioFocusController(new AudioFocusController.Platform() {
            private AudioFocusRequest request;
            public int request(AudioFocusController.Change listener) {
                request = new AudioFocusRequest.Builder(AudioManager.AUDIOFOCUS_GAIN)
                    .setAudioAttributes(new AudioAttributes.Builder().setUsage(AudioAttributes.USAGE_GAME)
                        .setContentType(AudioAttributes.CONTENT_TYPE_MUSIC).build())
                    .setWillPauseWhenDucked(true).setAcceptsDelayedFocusGain(true)
                    .setOnAudioFocusChangeListener(listener::changed, new Handler(Looper.getMainLooper())).build();
                return manager.requestAudioFocus(request);
            }
            public void abandon() {
                if(request != null) { manager.abandonAudioFocusRequest(request); request = null; }
            }
        }, observer);
    }
    boolean running() { return controller.running(); }
    boolean foreground() { return controller.foreground(); }
    String message() {
        switch(controller.reason()) {
            case HEADPHONES: return context.getString(R.string.audio_headphones);
            case DENIED: return context.getString(R.string.audio_denied);
            case WAITING: return context.getString(R.string.audio_waiting);
            default: return context.getString(R.string.audio_interrupted);
        }
    }
    void resume() {
        if(!registered) {
            IntentFilter filter = new IntentFilter(AudioManager.ACTION_AUDIO_BECOMING_NOISY);
            // This protected system broadcast must also arrive from system components.
            if(Build.VERSION.SDK_INT >= 33) context.registerReceiver(noisy, filter, Context.RECEIVER_EXPORTED);
            else context.registerReceiver(noisy, filter);
            registered = true;
        }
        controller.resume();
    }
    void pause() {
        controller.pause();
        if(registered) { context.unregisterReceiver(noisy); registered = false; }
    }
    void retry() { controller.retry(); }
    void close() {
        controller.close();
        if(registered) { context.unregisterReceiver(noisy); registered = false; }
    }
}
