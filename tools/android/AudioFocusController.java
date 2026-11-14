/* SPDX-License-Identifier: GPL-2.0-only */
package org.openbgi.port;

/** Main-thread state machine, independent of Android so interruption races can be tested. */
final class AudioFocusController {
    // AudioManager's focus change and request result values.
    static final int GAIN = 1, LOSS = -1, TRANSIENT = -2, DUCK = -3;
    static final int FAILED = 0, GRANTED = 1, DELAYED = 2;
    enum Reason { NONE, WAITING, OTHER_APP, HEADPHONES, DENIED }
    interface Change { void changed(int change); }
    interface Platform { int request(Change listener); void abandon(); }
    interface Observer { void updated(); }
    private final Platform platform;
    private final Observer observer;
    private boolean foreground, granted, confirmation, active, closed;
    private int generation;
    private Reason reason = Reason.NONE;

    AudioFocusController(Platform platform, Observer observer) { this.platform = platform; this.observer = observer; }
    boolean running() { return foreground && granted && !confirmation && !closed; }
    boolean foreground() { return foreground && !closed; }
    Reason reason() { return reason; }
    void resume() {
        if(closed || foreground) return;
        foreground = true;
        if(!confirmation) request();
        observer.updated();
    }
    void pause() {
        if(closed) return;
        foreground = false; granted = false; release(); observer.updated();
    }
    void retry() {
        if(closed || !foreground) return;
        confirmation = false; request(); observer.updated();
    }
    void noisy() {
        if(closed || !foreground) return;
        granted = false; confirmation = true; reason = Reason.HEADPHONES;
        release(); observer.updated();
    }
    void close() {
        if(closed) return;
        foreground = false; granted = false; closed = true; release(); observer.updated();
    }
    private void release() {
        ++generation; // Invalidate callbacks already posted by AudioManager.
        if(active) { active = false; platform.abandon(); }
    }
    private void request() {
        release();
        final int token = generation;
        active = true; granted = false; reason = Reason.WAITING;
        int result = platform.request(change -> change(token, change));
        if(token != generation || closed || !foreground) return;
        if(result == GRANTED) { granted = true; reason = Reason.NONE; }
        else if(result != DELAYED) {
            granted = false; confirmation = true; reason = Reason.DENIED; release();
        }
    }
    private void change(int token, int change) {
        if(token != generation || !active || closed || !foreground) return;
        if(change == GAIN && !confirmation) { granted = true; reason = Reason.NONE; }
        else if(change == TRANSIENT || change == DUCK) { granted = false; reason = Reason.OTHER_APP; }
        else if(change == LOSS) {
            granted = false; confirmation = true; reason = Reason.OTHER_APP; release();
        } else return;
        observer.updated();
    }
}
