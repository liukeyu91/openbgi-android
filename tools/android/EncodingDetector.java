/* SPDX-License-Identifier: GPL-2.0-only */
package org.openbgi.port;
import java.nio.*;
import java.nio.charset.*;
import java.util.*;

/** Suggestions from decoded scene strings, never from compressed/binary bytes. */
final class EncodingDetector {
    static final String[] ENCODINGS={"sjis","gbk","utf8"};
    private static final String CHINESE="的了这们说没吗个为会来时对从过还让给着后里样么话欢爱觉现开关档读设选确键欢应当处与渐进";
    static Charset charset(int i){return GameTextCodec.charset(i==1?2:i==2?1:i==3?3:0);}
    static String decode(byte[] bytes,int i){
        try{return charset(i).newDecoder().onMalformedInput(CodingErrorAction.REPORT).onUnmappableCharacter(CodingErrorAction.REPORT).decode(ByteBuffer.wrap(bytes)).toString();}
        catch(CharacterCodingException error){return null;}
    }
    private static double score(String s){
        if(s==null)return -10000;double score=0;int cjk=0;
        for(int at=0;at<s.length();){int c=s.codePointAt(at);at+=Character.charCount(c);
            if(c<32 && c!=9 && c!=10 && c!=13)return -10000;
            if(c<128)continue;
            if(c>=0x3041 && c<=0x3096){score+=3;cjk++;}
            else if(c>=0x30a1 && c<=0x30fa){score+=2;cjk++;}
            else if(c>=0x3400 && c<=0x9fff){score+=1;if(CHINESE.indexOf(c)>=0)score+=1.8;cjk++;}
            else if(c>=0xff61 && c<=0xff9f)score-=2;
            else if(c>=0xe000 && c<=0xf8ff || c==0xfffd)score-=5;
            else if(c>=0x3000 && c<=0x303f || c>=0xff00 && c<=0xff60)score+=.25;
            else score-=1;
        }
        return cjk>=3?score:-10000;
    }
    static final class Result {
        final int[] votes=new int[3];final double[] weights=new double[3];
        String recommended;boolean mixed;int ambiguous,usable;
        String advice(){
            if(usable==0)return "没有找到足够的可解码文本，请手动选择。";
            return (recommended==null?"证据不足，暂不推荐编码。":"建议 "+recommended.toUpperCase(Locale.ROOT)+"，请对照预览确认。")+
                (mixed?"\n部分样本判断不一致，可能混合编码或存在误判；请逐项核对。":"");
        }
        String summary(){
            return advice()+"\n抽样判断：SJIS "+votes[0]+" / GBK "+votes[1]+" / UTF-8 "+votes[2]+"，有歧义 "+ambiguous;
        }
    }
    static Result analyze(List<ArchiveTextSamples.Sample> samples){
        Result r=new Result();
        for(ArchiveTextSamples.Sample sample:samples){
            double[] s=new double[3];for(int i=0;i<3;i++)s[i]=score(decode(sample.bytes,i));
            int best=0;for(int i=1;i<3;i++)if(s[i]>s[best])best=i;
            if(s[best]<=0)continue;r.usable++;
            double second=-10000;for(int i=0;i<3;i++)if(i!=best)second=Math.max(second,s[i]);
            if(s[best]-second<Math.max(3,Math.abs(s[best])*.2)){r.ambiguous++;continue;}
            r.votes[best]++;r.weights[best]+=Math.min(60,s[best]);
        }
        int first=0;for(int i=1;i<3;i++)if(r.weights[i]>r.weights[first])first=i;
        int second=first==0?1:0;for(int i=0;i<3;i++)if(i!=first && r.weights[i]>r.weights[second])second=i;
        if(r.votes[first]>=3 && r.weights[first]>=r.weights[second]*1.5 && r.ambiguous<r.usable*.75)r.recommended=ENCODINGS[first];
        for(int i=0;i<3;i++)if(i!=first && r.votes[i]>=3 && r.weights[i]>=r.weights[first]*.3)r.mixed=true;
        return r;
    }
    static String preview(byte[] sample,int encoding){
        String decoded=decode(sample,encoding);
        return decoded==null?"〔含无效字节，无法按此编码完整解码〕":decoded;
    }
    static List<ArchiveTextSamples.Sample> previewSamples(List<ArchiveTextSamples.Sample> samples){
        List<ArchiveTextSamples.Sample> chosen=new ArrayList<>();
        for(ArchiveTextSamples.Sample sample:samples){
            int best=0;
            for(int enc=0;enc<3;enc++){
                String s=decode(sample.bytes,enc);if(s==null)continue;int count=0;
                for(int at=0;at<s.length();){int c=s.codePointAt(at);at+=Character.charCount(c);
                    if(c>=0x3400 && c<=0x9fff || c>=0x3041 && c<=0x30fa)count++;
                }best=Math.max(best,count);
            }
            if(best>=5)chosen.add(sample);if(chosen.size()==3)break;
        }
        if(chosen.isEmpty())chosen.addAll(samples.subList(0,Math.min(3,samples.size())));
        return chosen;
    }
}
