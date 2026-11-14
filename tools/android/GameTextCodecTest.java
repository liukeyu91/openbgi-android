/* SPDX-License-Identifier: GPL-2.0-only */
package org.openbgi.port;
import java.nio.charset.Charset;

public final class GameTextCodecTest {
    private static int checks;
    private static void check(String expected, String source, String dest, int from, int to, int encoding, int limit, boolean reject) {
        Charset charset = GameTextCodec.charset(encoding);
        String actual = GameTextCodec.insertion(source, 0, source.length(), dest, from, to, charset, limit, reject);
        if(!expected.equals(actual)) throw new AssertionError("Expected " + expected + " got " + actual);
        checks++;
    }
    public static void main(String[] args) {
        check("TestAB", "TestABCD", "", 0, 0, 2, 6, false);
        check("测试AB", "测试ABCD", "", 0, 0, 2, 6, false);
        check("日本AB", "日本ABCD", "", 0, 0, 0, 6, false);
        check("中文😀AB", "中文😀ABCD", "", 0, 0, 1, 12, false);
        check("", "😀", "", 0, 0, 1, 3, false); // never split a surrogate pair / UTF-8 scalar
        check("", "😀", "", 0, 0, 2, 20, false); // no lossy GBK replacement
        check("中", "A中 B", "", 0, 0, 2, 20, true);
        check("文", "文", "中文AB", 0, 1, 2, 6, false); // replacing selection reclaims bytes
        check("", "文", "中文AB", 4, 4, 2, 6, false);
        check("AB", "A\u0000\nB", "", 0, 0, 1, 10, false);
        System.out.println("GameTextCodec: " + checks + " checks passed");
    }
}
