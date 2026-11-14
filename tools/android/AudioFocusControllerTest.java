/* SPDX-License-Identifier: GPL-2.0-only */
package org.openbgi.port;

public final class AudioFocusControllerTest {
    private static int checks;
    private static void check(boolean ok) { if(!ok) throw new AssertionError("check " + (checks + 1)); checks++; }
    private static final class Fake implements AudioFocusController.Platform {
        AudioFocusController.Change callback;
        int result = 1, requests, abandons;
        public int request(AudioFocusController.Change cb) { requests++; callback = cb; return result; }
        public void abandon() { abandons++; }
        void send(int event) { callback.changed(event); }
    }
    public static void main(String[] args) {
        Fake p = new Fake();
        AudioFocusController c = new AudioFocusController(p, () -> {});
        check(!c.running()); c.resume(); check(c.running() && p.requests == 1);
        c.resume(); check(p.requests == 1);
        p.send(-2); check(!c.running()); p.send(1); check(c.running());
        p.send(-3); check(!c.running()); p.send(1); check(c.running());
        AudioFocusController.Change old = p.callback;
        p.send(-1); check(!c.running() && p.abandons == 1);
        old.changed(1); check(!c.running());
        c.pause(); c.resume(); check(!c.running() && p.requests == 1);
        c.retry(); check(c.running() && p.requests == 2);
        c.noisy(); check(!c.running() && c.reason() == AudioFocusController.Reason.HEADPHONES);
        p.send(1); check(!c.running());
        c.pause(); c.resume(); check(!c.running() && p.requests == 2);
        p.result = 0; c.retry(); check(!c.running() && c.reason() == AudioFocusController.Reason.DENIED);
        p.send(1); check(!c.running());
        p.result = 2; c.retry(); check(!c.running() && c.reason() == AudioFocusController.Reason.WAITING);
        p.send(1); check(c.running());
        old = p.callback; c.pause(); old.changed(1); check(!c.running());
        p.result = 1; c.resume(); check(c.running()); old.changed(-1); check(c.running());
        c.pause(); int requests = p.requests; c.noisy(); c.resume(); check(c.running() && p.requests == requests + 1);
        c.close(); check(!c.running()); p.send(1); check(!c.running());
        requests = p.requests; c.resume(); c.retry(); check(p.requests == requests);
        p = new Fake(); c = new AudioFocusController(p, () -> {});
        p.result = 2; c.resume(); check(!c.running()); old = p.callback;
        c.pause(); p.result = 1; c.resume(); old.changed(1); old.changed(-2); check(c.running());
        p.send(-2); c.noisy(); p.send(1); check(!c.running() && c.reason() == AudioFocusController.Reason.HEADPHONES);
        c.retry(); check(c.running());
        System.out.println("AudioFocusController: " + checks + " checks passed");
    }
}
