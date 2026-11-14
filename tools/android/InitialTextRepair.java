/* SPDX-License-Identifier: GPL-2.0-only */
package org.openbgi.port;
import java.nio.*;
import java.nio.charset.*;

/** One immutable snapshot of initial bytes. No guessing, mutation or persistence. */
final class InitialTextRepair {
    static final int[] MODES={0,2,1}; // GameTextCodec: SJIS, GBK, UTF-8
    static final String[] LABELS={"SJIS","GBK","UTF-8"};
    private final byte[] original;
    InitialTextRepair(byte[] bytes){original=bytes==null?new byte[0]:bytes.clone();}
    boolean hasNonAscii(){for(byte b:original)if((b&128)!=0)return true;return false;}
    static final class Candidate {
        final String text,error;
        Candidate(String text,String error){this.text=text;this.error=error;}
        boolean valid(){return error==null;}
        String label(int choice){
            if(!valid())return LABELS[choice]+" · "+error;
            String shortText=text.codePointCount(0,text.length())>64?text.substring(0,text.offsetByCodePoints(0,64))+"…":text;
            return LABELS[choice]+"\n"+shortText;
        }
    }
    Candidate candidate(int choice,Charset target,int limit,boolean rejectAscii){
        if(choice<0 || choice>=MODES.length)return new Candidate(null,"未知编码");
        final String text;
        try{text=GameTextCodec.charset(MODES[choice]).newDecoder().onMalformedInput(CodingErrorAction.REPORT)
            .onUnmappableCharacter(CodingErrorAction.REPORT).decode(ByteBuffer.wrap(original)).toString();}
        catch(CharacterCodingException bad){return new Candidate(null,"无法完整解码");}
        final ByteBuffer encoded;
        try{encoded=target.newEncoder().onMalformedInput(CodingErrorAction.REPORT)
            .onUnmappableCharacter(CodingErrorAction.REPORT).encode(CharBuffer.wrap(text));}
        catch(CharacterCodingException bad){return new Candidate(null,"当前游戏编码无法表示");}
        if(encoded.remaining()>Math.max(0,limit))return new Candidate(null,"超过本栏字节上限");
        String accepted=GameTextCodec.insertion(text,0,text.length(),"",0,0,target,Math.max(0,limit),rejectAscii);
        if(!text.equals(accepted))return new Candidate(null,"包含本栏不允许的字符");
        return new Candidate(text,null);
    }
}
