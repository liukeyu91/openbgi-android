/* SPDX-License-Identifier: GPL-2.0-only */
package org.openbgi.port;

import java.nio.charset.Charset;

/** Script limits count encoded bytes, never Java UTF-16 code units. */
final class GameTextCodec {
    static Charset charset(int encoding) {
        return Charset.forName(encoding == 2 ? "GBK" : encoding == 1 ? "UTF-8" : "windows-31j");
    }
    static String insertion(CharSequence source, int start, int end, CharSequence dest,
            int dstart, int dend, Charset charset, int limit, boolean rejectAscii) {
        String before = dest.subSequence(0, dstart).toString();
        String after = dest.subSequence(dend, dest.length()).toString();
        StringBuilder accepted = new StringBuilder();
        for(int i = start; i < end;) {
            int cp = Character.codePointAt(source, i);
            int count = Character.charCount(cp);
            if(i + count > end) break;
            i += count;
            if(cp < 0x20 || cp == 0x7f || (rejectAscii && cp < 0x80)) continue;
            String symbol = new String(Character.toChars(cp));
            if(!charset.newEncoder().canEncode(symbol)) continue;
            String candidate = before + accepted + symbol + after;
            if(candidate.getBytes(charset).length > limit) break;
            accepted.append(symbol);
        }
        return accepted.toString();
    }
}
