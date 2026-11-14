/* SPDX-License-Identifier: GPL-2.0-only */
package org.openbgi.port;
import java.nio.charset.Charset;
import java.util.*;

public final class InitialTextRepairTest {
    static int checks;
    static void check(boolean ok){if(!ok)throw new AssertionError("check "+(checks+1));checks++;}
    static InitialTextRepair snapshot(String text,int mode){return new InitialTextRepair(text.getBytes(GameTextCodec.charset(mode)));}
    public static void main(String[] args){
        Charset gbk=GameTextCodec.charset(2),sjis=GameTextCodec.charset(0),utf8=GameTextCodec.charset(1);
        byte[] original="青葉".getBytes(sjis),copy=original.clone();InitialTextRepair name=new InitialTextRepair(original);
        check(name.hasNonAscii());InitialTextRepair.Candidate fixed=name.candidate(0,gbk,4,false);
        check(fixed.valid() && fixed.text.equals("青葉"));check(Arrays.equals(original,copy));
        check(fixed.text.getBytes(gbk).length==4);check(new String(fixed.text.getBytes(gbk),gbk).equals("青葉"));
        check(!Arrays.equals(fixed.text.getBytes(gbk),original)); // source and submission encodings differ
        Arrays.fill(original,(byte)'X');check(name.candidate(0,gbk,4,false).text.equals("青葉"));
        check(!name.candidate(0,gbk,3,false).valid()); // refuse truncation
        check(!name.candidate(0,gbk,0,false).valid());check(!name.candidate(0,gbk,-1,false).valid());
        InitialTextRepair chinese=snapshot("中文AB",2);
        check(chinese.candidate(1,utf8,8,false).text.equals("中文AB"));
        check(!chinese.candidate(1,utf8,7,false).valid());
        check(!chinese.candidate(1,utf8,20,true).valid()); // no partial ASCII stripping
        check(!chinese.candidate(2,utf8,40,false).valid()); // invalid UTF-8, no replacement characters
        InitialTextRepair emoji=snapshot("中😀",1);
        check(emoji.candidate(2,utf8,7,false).valid());check(!emoji.candidate(2,utf8,6,false).valid());
        check(!emoji.candidate(2,gbk,40,false).valid());check(!emoji.candidate(2,sjis,40,false).valid());
        check(!snapshot("A\nB",1).candidate(2,utf8,20,false).valid());
        check(!snapshot("A\u0000B",1).candidate(2,utf8,20,false).valid());
        check(!snapshot("ASCII",1).hasNonAscii());check(!new InitialTextRepair(null).hasNonAscii());
        check(new InitialTextRepair(null).candidate(0,gbk,0,false).text.isEmpty());
        check(!name.candidate(-1,gbk,30,false).valid());check(!name.candidate(3,gbk,30,false).valid());
        check(!name.candidate(2,utf8,30,false).label(2).contains("\ufffd"));
        InitialTextRepair longText=snapshot(String.join("",Collections.nCopies(65,"😀")),1);
        InitialTextRepair.Candidate longCandidate=longText.candidate(2,utf8,300,false);
        check(longCandidate.valid());String label=longCandidate.label(2);
        check(label.endsWith("…") && label.codePointCount(0,label.length())==6+64+1);
        check(longCandidate.text.codePointCount(0,longCandidate.text.length())==65); // preview elision never truncates submitted text
        System.out.println("Initial text repair: "+checks+" checks passed");
    }
}
