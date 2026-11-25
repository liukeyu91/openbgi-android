/* SPDX-License-Identifier: GPL-2.0-only */
package org.openbgi.port;

import java.io.*;
import java.nio.charset.StandardCharsets;
import java.nio.file.Files;
import java.util.*;

final class RunReport {
    private static final int MAX_LOG = 4*1024*1024;
    private static final String UNAVAILABLE = "unavailable";

    private static BufferedReader log(File file) throws IOException {
        return file.isFile()&&file.length()<=MAX_LOG
            ? new BufferedReader(new InputStreamReader(new FileInputStream(file),StandardCharsets.UTF_8)) : null;
    }

    static Map<String,String> fields(File file) throws IOException {
        Map<String,String> result = new LinkedHashMap<>();
        try (BufferedReader input=log(file)) {
            String line;
            while (input!=null && (line=input.readLine())!=null) {
                int colon=line.indexOf(": ");
                if(colon>=0)result.putIfAbsent(line.substring(0,colon),line.substring(colon+2));
            }
        }
        return result;
    }

    static String render(File logs,Map<String,String> session) throws IOException {
        String epoch=session.getOrDefault("Session epoch ms","");
        if(!epoch.matches("[0-9]{1,16}"))return "Game session: unavailable\n";
        String prefix="Run "+epoch+" ";
        Map<String,String> run=new HashMap<>();
        for(String name:new String[]{"engine-previous.txt","engine-current.txt"}) {
            try(BufferedReader input=log(new File(logs,name))) {
                String line;
                while(input!=null && (line=input.readLine())!=null) {
                    if(!line.startsWith(prefix))continue;
                    int colon=line.indexOf(": ",prefix.length());
                    if(colon>=0)run.put(line.substring(prefix.length(),colon),line.substring(colon+2));
                }
            }
        }

        String program=run.getOrDefault("program",UNAVAILABLE);
        String error=run.containsKey("script-error")?"script "+run.get("script-error"):"none recorded";
        for(String name:new String[]{"native-crash.txt","native-crash-previous.txt"}) {
            File file=new File(logs,name);
            Map<String,String> crash=fields(file);
            String signal=crash.getOrDefault("Signal","");
            if(!epoch.equals(crash.get("Session epoch ms")) || !signal.matches("[0-9]{1,3}"))continue;
            error="native signal "+signal+" (exit "+(128+Integer.parseInt(signal))+")";
            try(BufferedReader input=log(file)) {
                String line;
                boolean breadcrumbs=false;
                while(input!=null && (line=input.readLine())!=null) {
                    if(line.startsWith("Last VM activity"))breadcrumbs=true;
                    else if(line.startsWith("Memory maps"))break;
                    else if(breadcrumbs && line.matches("T[0-9]+ .+ \\+0x[0-9a-f]+ ip=0x[0-9a-f]+ opcode=0x[0-9a-f]+"))
                        program=line;
                }
            }
            break;
        }
        for(String name:new String[]{"java-crash.txt","java-crash-previous.txt"}) {
            if(epoch.equals(fields(new File(logs,name)).get("Session epoch ms"))) {
                if(!error.startsWith("native signal"))error="Java exception";
                break;
            }
        }

        return "Session epoch ms: "+epoch+"\n"+
            "Game: "+session.getOrDefault("Game title",UNAVAILABLE)+"\n"+
            "Session APK: "+session.getOrDefault("OpenBGI",UNAVAILABLE)+"\n"+
            "Native SHA256: "+session.getOrDefault("Native SHA256",UNAVAILABLE)+"\n"+
            "Engine selection: "+session.getOrDefault("Engine selection",UNAVAILABLE)+"\n"+
            "Effective engine: "+run.getOrDefault("profile",UNAVAILABLE)+"\n"+
            "Encoding: "+run.getOrDefault("encoding",session.getOrDefault("Encoding",UNAVAILABLE))+"\n"+
            "Archive language: "+session.getOrDefault("Archive language",UNAVAILABLE)+"\n"+
            "Overlay content SHA256: "+session.getOrDefault("Overlay content SHA256",UNAVAILABLE)+"\n"+
            "Extracted patch record SHA256: "+session.getOrDefault("Extracted patch record SHA256",UNAVAILABLE)+"\n"+
            "Last VM program: "+program+"\n"+
            "Error: "+error+"\n"+
            "Engine exit: "+run.getOrDefault("exit","not recorded")+"\n";
    }
}
