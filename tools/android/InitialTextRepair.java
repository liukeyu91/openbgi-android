/* SPDX-License-Identifier: GPL-2.0-only */
package org.openbgi.port;
import java.nio.*;
import java.nio.charset.*;

/** One immutable snapshot of initial bytes. No guessing, mutation or persistence. */
final class InitialTextRepair {
    static final int[] MODES={0,2,1,3};
    static final String[] LABELS={"SJIS","GBK","UTF-8","Big5"};
    private final byte[] original;
    InitialTextRepair(byte[] bytes){original=bytes==null?new byte[0]:bytes.clone();}
    boolean hasNonAscii(){for(byte b:original)if((b&128)!=0)return true;return false;}
    enum Error { UNKNOWN_ENCODING, DECODE, UNREPRESENTABLE, TOO_LONG, DISALLOWED }
    static final class Candidate {
        final String text;
        final Error error;
        Candidate(String text,Error error){this.text=text;this.error=error;}
        boolean valid(){return error==null;}
        String label(int choice){
            if(!valid())return LABELS[choice]+" · "+error;
            String shortText=text.codePointCount(0,text.length())>64?text.substring(0,text.offsetByCodePoints(0,64))+"…":text;
            return LABELS[choice]+"\n"+shortText;
        }
    }
    Candidate candidate(int choice,Charset target,int limit,boolean rejectAscii){
        if(choice<0 || choice>=MODES.length)return new Candidate(null,Error.UNKNOWN_ENCODING);
        final String text;
        try{text=GameTextCodec.charset(MODES[choice]).newDecoder().onMalformedInput(CodingErrorAction.REPORT)
            .onUnmappableCharacter(CodingErrorAction.REPORT).decode(ByteBuffer.wrap(original)).toString();}
        catch(CharacterCodingException bad){return new Candidate(null,Error.DECODE);}
        final ByteBuffer encoded;
        try{encoded=target.newEncoder().onMalformedInput(CodingErrorAction.REPORT)
            .onUnmappableCharacter(CodingErrorAction.REPORT).encode(CharBuffer.wrap(text));}
        catch(CharacterCodingException bad){return new Candidate(null,Error.UNREPRESENTABLE);}
        if(encoded.remaining()>Math.max(0,limit))return new Candidate(null,Error.TOO_LONG);
        String accepted=GameTextCodec.insertion(text,0,text.length(),"",0,0,target,Math.max(0,limit),rejectAscii);
        if(!text.equals(accepted))return new Candidate(null,Error.DISALLOWED);
        return new Candidate(text,null);
    }
}
