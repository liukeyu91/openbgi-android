/* SPDX-License-Identifier: GPL-2.0-only */
package org.openbgi.port;

/** Runs only in the launcher process; the VM lives in the separate engine process. */
final class NativeProgramInspector implements ProgramCompatibility.Analyzer {
    static { System.loadLibrary("openbgi"); }
    private static native String configure(String gamesJson,String profile,byte[] boot);
    private static native int[] inspect(byte[] program);
    static native String[] identify(String gamesJson,byte[] boot);
    static native int[] missingGlyphs(String family,int[] codePoints);
    public String select(String gamesJson,String profile,byte[] boot){return configure(gamesJson,profile,boot);}
    public int[] analyze(byte[] program){return inspect(program);}
}
