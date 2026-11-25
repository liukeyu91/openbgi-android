/* SPDX-License-Identifier: GPL-2.0-only */
package org.openbgi.port;

import java.io.*;
import java.nio.charset.StandardCharsets;
import java.nio.file.*;
import java.util.*;

final class RunReportTest {
    private static void put(Path root,String name,String text) throws IOException {
        Files.write(root.resolve(name),text.getBytes(StandardCharsets.UTF_8));
    }

    private static void check(boolean ok) {
        if(!ok)throw new AssertionError("Incorrect run summary");
    }

    public static void main(String[] args) throws IOException {
        Path root=Files.createTempDirectory("openbgi-run-report-");
        try {
            File logs=root.toFile();
            check(RunReport.render(logs,Collections.emptyMap()).equals("Game session: unavailable\n"));
            put(root,"session-current.txt","Session epoch ms: 42\nGame title: Game\nOpenBGI: 1.0.1-android (37)\n"+
                "Engine selection: auto\nEncoding: sjis\nArchive language: tc\n");
            put(root,"engine-current.txt","Run 40 script-error: $9001\nRun 42 profile: 1.680\n"+
                "Run 42 encoding: big5\nRun 42 program: scene._bp +0x10 (opcode $9166)\n"+
                "Run 42 script-error: $9166\nRun 42 exit: 0\n");
            put(root,"native-crash.txt","Session epoch ms: 40\nSignal: 11\n");
            Map<String,String> session=RunReport.fields(root.resolve("session-current.txt").toFile());
            String summary=RunReport.render(logs,session);
            check(summary.contains("Effective engine: 1.680\n") && summary.contains("Encoding: big5\n"));
            check(summary.contains("Last VM program: scene._bp +0x10 (opcode $9166)\n") &&
                summary.contains("Error: script $9166\n") && !summary.contains("9001"));
            put(root,"native-crash.txt","Session epoch ms: 42\nSignal: 11\n"+
                "Last VM activity:\nT1 scene2._bp +0x12 ip=0x24 opcode=0x9166\nMemory maps\n");
            summary=RunReport.render(logs,session);
            check(summary.contains("Error: native signal 11 (exit 139)\n") && summary.contains("scene2._bp +0x12"));
            Files.delete(root.resolve("native-crash.txt"));
            put(root,"java-crash.txt","Session epoch ms: 42\n");
            check(RunReport.render(logs,session).contains("Error: Java exception\n"));
        } finally { GameImport.remove(root); }
        System.out.println("Run report tests passed");
    }
}
