package org.openbgi.port;
import java.io.*;
import java.nio.file.*;
import java.util.*;
public class P0ImportTest {
    static GameImport.Archive file(String name,String data) {
        byte[] bytes=data.getBytes(java.nio.charset.StandardCharsets.UTF_8);
        return new GameImport.Archive(name,bytes.length,()->new ByteArrayInputStream(bytes));
    }
    static void check(boolean condition,String message) { if(!condition)throw new AssertionError(message); }
    static void reject(File dir,List<GameImport.Archive> files) throws IOException {
        try { GameImport.install(dir,files,(a,b)->{}); throw new AssertionError("Accepted bad import"); }
        catch(IOException expected) { check(!dir.exists(),"Partial game visible"); }
    }
    public static void main(String[] args) throws Exception {
        Path root=Files.createTempDirectory(Paths.get(args[0]),"p0-import-");
        try {
            File a=root.resolve("first").toFile(),b=root.resolve("second").toFile();
            GameImport.install(a,Arrays.asList(file("system.arc","first"),file("Archive/MiXeD.ARC","hello"),
                file("Nested/日本語/ReadMe.TXT","text"),file("games.json","[]"),new GameImport.Archive("Empty",0,null)),(x,y)->check(!a.exists(),"Atomic visibility"));
            check(Arrays.asList(new File(a,"Archive").list()).contains("MiXeD.ARC"),"Case preserved");
            check(Files.readString(a.toPath().resolve("Nested/日本語/ReadMe.TXT")).equals("text"),"Nested bytes preserved");
            check(new File(a,"Empty").isDirectory(),"Empty directory");
            Files.writeString(a.toPath().resolve("UserData.sav"),"save-a");
            GameImport.install(b,Arrays.asList(file("ipl._bp","second-boot"),file("Sub/Other.arc","other")),(x,y)->{});
            Files.writeString(b.toPath().resolve("UserData.sav"),"save-b");
            check(Files.readString(a.toPath().resolve("UserData.sav")).equals("save-a"),"Save isolation");
            reject(root.resolve("bad").toFile(),Arrays.asList(file("../escape","x"),file("ipl._bp","x")));
            reject(root.resolve("bad").toFile(),Arrays.asList(file("/absolute","x")));
            reject(root.resolve("bad").toFile(),Arrays.asList(file("ipl._bp","x"),file("ipl._bp","y")));
            reject(root.resolve("bad").toFile(),Arrays.asList(file("nested/ipl._bp","x")));
            reject(root.resolve("bad").toFile(),Arrays.asList(file("system.arc",""),file("ipl._bp","")));
            Path demo=root.resolve("empty-demo");Files.createDirectory(demo);Files.write(demo.resolve("system.arc"),new byte[0]);
            check(!GameImport.gamePresent(demo.toFile()),"Empty UI fixture must not appear as a game");
            reject(root.resolve("bad").toFile(),Arrays.asList(new GameImport.Archive("ipl._bp",30,()->new ByteArrayInputStream(new byte[2]))));
            Path stale=root.resolve(".import-bad"); Files.createDirectory(stale); Files.writeString(stale.resolve("stale"),"partial");
            GameImport.install(root.resolve("bad").toFile(),Arrays.asList(file("ipl._bp","retry")),(x,y)->{});
            check(!Files.exists(stale),"Interrupted import retry");
            try { GameImport.install(a,Arrays.asList(file("ipl._bp","overwrite")),(x,y)->{}); throw new AssertionError("Replaced existing game"); }
            catch(IOException expected) {}
            check(Files.readString(a.toPath().resolve("UserData.sav")).equals("save-a"),"Save preserved after reimport");
            System.out.println("P0_IMPORT_PASS: full tree, case, binary size, atomic publish, traversal, duplicates, retry, two save directories");
        } finally { GameImport.remove(root); }
    }
}
