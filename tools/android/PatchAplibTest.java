/* SPDX-License-Identifier: GPL-2.0-only */
package org.openbgi.port;

import java.io.IOException;
import java.util.ArrayList;
import java.util.Arrays;
import java.util.Random;

/** Hand-encoded synthetic tokens only; no commercial resource fixtures. */
public final class PatchAplibTest {
    private static int checks;

    private interface Checked { void run() throws IOException; }

    private static void rejects(Checked action) throws IOException {
        checks++;
        try { action.run(); }
        catch (IOException expected) { return; }
        throw new AssertionError("Malformed block accepted");
    }

    private static void equal(byte[] expected, byte[] packed, int maximum) throws IOException {
        checks++;
        if (!Arrays.equals(expected, PatchAplib.decompress(packed, maximum))) {
            throw new AssertionError("Decoded data differs");
        }
    }

    private static final class Tokens {
        final ArrayList<Integer> bytes = new ArrayList<>();
        int tagPosition;
        int used = 8;
        boolean lastMatch;

        Tokens(int first) { raw(first); }
        void raw(int value) { bytes.add(value & 255); }
        void bit(int value) {
            if (used == 8) { tagPosition = bytes.size(); raw(0); used = 0; }
            bytes.set(tagPosition, bytes.get(tagPosition) | ((value & 1) << (7 - used++)));
        }
        void prefix(int... values) { for (int value : values) bit(value); }
        void gamma(int value) {
            if (value < 2) throw new AssertionError("Gamma test value");
            for (int shift = 30 - Integer.numberOfLeadingZeros(value); shift >= 0; shift--) {
                bit(value >>> shift); bit(shift == 0 ? 0 : 1);
            }
        }
        void literal(int value) { bit(0); raw(value); lastMatch = false; }
        void tiny(int distance) {
            prefix(1, 1, 1);
            for (int shift = 3; shift >= 0; shift--) bit(distance >>> shift);
            lastMatch = false;
        }
        void shortMatch(int distance, int length) {
            prefix(1, 1, 0); raw((distance << 1) | (length - 2)); lastMatch = true;
        }
        void longMatch(int distance, int length) {
            prefix(1, 0);
            gamma((distance >>> 8) + (lastMatch ? 2 : 3));
            raw(distance);
            int extra = distance < 128 || distance >= 32000 ? 2 : distance >= 1280 ? 1 : 0;
            gamma(length - extra); lastMatch = true;
        }
        void repeat(int length) { prefix(1, 0); gamma(2); gamma(length); lastMatch = true; }
        byte[] finish() { prefix(1, 1, 0); raw(0); return array(); }
        byte[] array() {
            byte[] result = new byte[bytes.size()];
            for (int i = 0; i < result.length; i++) result[i] = bytes.get(i).byteValue();
            return result;
        }
    }

    public static void main(String[] args) throws Exception {
        equal(new byte[]{65}, new byte[]{65, (byte) 0xc0, 0}, 1);
        Tokens t = new Tokens(65); t.literal(66); equal(new byte[]{65, 66}, t.finish(), 2);
        t = new Tokens(65); t.shortMatch(1, 3); equal(new byte[]{65, 65, 65, 65}, t.finish(), 4);
        t = new Tokens(65); t.tiny(1); t.tiny(0); equal(new byte[]{65, 65, 0}, t.finish(), 3);
        t = new Tokens(65); t.longMatch(1, 6); byte[] seven = new byte[7]; Arrays.fill(seven, (byte)65); equal(seven, t.finish(), 7);
        t = new Tokens(65); t.shortMatch(1, 2); t.literal(66); t.repeat(3);
        equal(new byte[]{65,65,65,66,66,66,66}, t.finish(), 7);
        t = new Tokens(65); t.shortMatch(1, 2); t.tiny(0); t.repeat(2);
        equal(new byte[]{65,65,65,0,0,0}, t.finish(), 6);
        t = new Tokens(65); t.longMatch(1, 4); t.longMatch(1, 4);
        byte[] nine = new byte[9]; Arrays.fill(nine, (byte)65); equal(nine, t.finish(), 9);

        for (int distance : new int[]{127, 128, 1279, 1280, 31999, 32000}) {
            int length = distance < 128 || distance >= 32000 ? 4 : distance >= 1280 ? 3 : 2;
            t = new Tokens(0);
            byte[] expected = new byte[distance + length];
            for (int i = 1; i < distance; i++) { t.literal(i & 255); expected[i] = (byte)i; }
            for (int i = 0; i < length; i++) expected[distance + i] = expected[i];
            t.longMatch(distance, length); equal(expected, t.finish(), expected.length);
        }
        t = new Tokens(65); t.longMatch(1, 65535);
        byte[] full = new byte[65536]; Arrays.fill(full, (byte)65); equal(full, t.finish(), full.length);

        final Tokens badOffset = new Tokens(65); badOffset.shortMatch(2, 2);
        rejects(() -> PatchAplib.decompress(badOffset.finish(), 10));
        final Tokens badTiny = new Tokens(65); badTiny.tiny(2);
        rejects(() -> PatchAplib.decompress(badTiny.finish(), 10));
        final Tokens badRepeat = new Tokens(65); badRepeat.repeat(2);
        rejects(() -> PatchAplib.decompress(badRepeat.finish(), 10));
        final Tokens zeroDistance = new Tokens(65); zeroDistance.longMatch(0, 4);
        rejects(() -> PatchAplib.decompress(zeroDistance.finish(), 10));
        final Tokens badGamma = new Tokens(65); badGamma.prefix(1, 0);
        for (int i = 0; i < 40; i++) badGamma.prefix(1, 1);
        rejects(() -> PatchAplib.decompress(badGamma.array(), 65536));
        final Tokens tooLarge = new Tokens(65); tooLarge.longMatch(1, 10);
        rejects(() -> PatchAplib.decompress(tooLarge.finish(), 10));
        final byte[] valid = new Tokens(65).finish();
        for (int i = 0; i < valid.length; i++) {
            final byte[] truncated = Arrays.copyOf(valid, i);
            rejects(() -> PatchAplib.decompress(truncated, 5));
        }
        rejects(() -> PatchAplib.decompress(Arrays.copyOf(valid, valid.length + 1), 5));
        rejects(() -> PatchAplib.decompress(null, 5));
        rejects(() -> PatchAplib.decompress(valid, 0));
        rejects(() -> PatchAplib.decompress(valid, 65537));
        rejects(() -> PatchAplib.decompress(new byte[PatchAplib.MAX_INPUT_BYTES + 1], 5));

        Random random = new Random(81432);
        for (int i = 0; i < 3000; i++) {
            byte[] noise = new byte[random.nextInt(128)]; random.nextBytes(noise);
            try {
                byte[] decoded = PatchAplib.decompress(noise, 512);
                if (decoded.length > 512) throw new AssertionError("Output bound");
            } catch (IOException expected) { /* Expected for most random streams. */ }
            checks++;
        }
        System.out.println("PatchAplib: " + checks + " synthetic checks passed");
    }
}
