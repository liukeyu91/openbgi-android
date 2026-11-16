/* SPDX-License-Identifier: GPL-2.0-only */
package org.openbgi.port;

import java.io.*;
import java.nio.charset.StandardCharsets;
import java.nio.file.*;
import java.util.*;
import java.util.zip.DeflaterOutputStream;

/** Self-created CPK fixtures, plus RFC 3713 Camellia and independent Python vectors. */
public final class PatchCpkTest {
    static void check(boolean value,String message) { if(!value)throw new AssertionError(message); }
    static byte[] hex(String value) { byte[] out=new byte[value.length()/2];for(int i=0;i<out.length;i++)out[i]=(byte)Integer.parseInt(value.substring(i*2,i*2+2),16);return out; }
    static void put(byte[] bytes,int at,long value) { for(int i=0;i<4;i++)bytes[at+i]=(byte)(value>>>(8*i)); }
    static final String FIXED_CPK="Q1FJAwYFBgcICQoLDA0ODxAREhMUFRYXGBkaGxwdHh8AAQIDBAUGBwgJCgsMDQ4PEBESExQVFhcYGRobHB0eHwABAgMEBQYHCAkKCwwNDg8QERITFBUWFxgZGhscHR4fAAECAwQFBgcICQoLDA0ODxAREhMUFRYXGBkaGxwdHh8AAQIDBAUGBwkJCgtpb3ZxawUGBwgJCgsMDQ4PEBESExQVFhcYGRobHB0eHxABAgMcBQYHCAkKCzuipLq+4w5JejWnqGNT7ZjYnTiUGZBrIg==";
    static final String FIXED_KEY="UENYExITExMTEhEQFxYVFBsaGRgfHh0cAwIBAAcGBQQLCgkIDw4NDBMSERAXFhUUGxoZGB8eHRwDAgEABwYFBAsKCQgPDg0MAxMTEwMTExM6OTg/Pj08IyIhICcmJSQrSE9OTUxzcnFwd3Z1dHt6eQ==";

    static byte[][] fixture(String name,byte[] data,boolean trailing,int declaredAdjustment) throws IOException {
        byte[] headerKey=new byte[32],indexKey=new byte[32],key=new byte[16],nonce=new byte[16];
        for(int i=0;i<32;i++) { headerKey[i]=(byte)i;indexKey[i]=(byte)(i+7); }
        for(int i=0;i<16;i++) { key[i]=(byte)(i+13);nonce[i]=(byte)(i+53); }
        ByteArrayOutputStream packed=new ByteArrayOutputStream();
        try(DeflaterOutputStream deflate=new DeflaterOutputStream(packed)) { deflate.write(data); }
        if(trailing)packed.write(1);
        byte[] payload=packed.toByteArray(),counter=PatchCpk.nonceCounter(new PatchCamellia(key),nonce),mask=new byte[16];
        PatchCamellia cipher=new PatchCamellia(key);
        for(int at=0;at<payload.length;at+=16) {
            cipher.processBlock(counter,0,mask,0);PatchCpk.increment(counter);
            for(int j=0;j<16&&at+j<payload.length;j++)payload[at+j]^=mask[j];
        }
        byte[] archive=new byte[184+payload.length],sidecar=new byte[112];
        archive[0]='C';archive[1]='P';archive[2]='K';put(archive,4,2);put(archive,136,1);
        for(int i=0;i<140;i++)archive[i]^=headerKey[i%32];
        byte[] index=new byte[44],rawName=name.getBytes(StandardCharsets.US_ASCII);
        System.arraycopy(rawName,0,index,0,rawName.length);put(index,32,data.length+declaredAdjustment);put(index,36,payload.length);
        for(int i=0;i<44;i++)archive[140+i]=(byte)(index[i]^indexKey[i%32]);
        System.arraycopy(payload,0,archive,184,payload.length);
        sidecar[0]='C';sidecar[1]='P';sidecar[2]='K';put(sidecar,4,1);
        System.arraycopy(headerKey,0,sidecar,8,32);System.arraycopy(indexKey,0,sidecar,40,32);
        put(sidecar,72,16);put(sidecar,76,16);System.arraycopy(key,0,sidecar,80,16);System.arraycopy(nonce,0,sidecar,96,16);
        for(int i=0;i<sidecar.length;i++)sidecar[i]^=0x13;
        return new byte[][]{archive,sidecar};
    }

    static void write(Path root,byte[][] pair) throws IOException { Files.write(root.resolve("toy.cpk"),pair[0]);Files.write(root.resolve("toy.key"),pair[1]); }
    static PatchCpk open(Path root) throws IOException { return PatchCpk.open(root.resolve("toy.cpk").toFile(),root.resolve("toy.key").toFile()); }
    interface Failing { void run() throws IOException; }
    static void reject(Failing action) throws IOException { try { action.run();throw new AssertionError("Bad CPK accepted"); }catch(IOException expected) {} }

    public static void main(String[] args) throws Exception {
        byte[] rfcKey=hex("0123456789abcdeffedcba9876543210"),block=new byte[16];
        PatchCamellia cipher=new PatchCamellia(rfcKey);cipher.processBlock(rfcKey,0,block,0);
        check(Arrays.equals(block,hex("67673138549669730857065648eabe43")),"RFC 3713 encryption vector");
        byte[] nonce=new byte[16];for(int i=0;i<16;i++)nonce[i]=(byte)i;
        check(Arrays.equals(PatchCpk.nonceCounter(cipher,nonce),hex("8f073f84fb28632c170e354c06f54baf")),"Independent nonce CMAC vector");
        Path root=Files.createTempDirectory("cpk-java-test-");
        try {
            write(root,new byte[][]{Base64.getDecoder().decode(FIXED_CPK),Base64.getDecoder().decode(FIXED_KEY)});
            try(PatchCpk cpk=open(root)) {
                check(cpk.entries.size()==1&&cpk.entries.get(0).name.equals("intro"),"Independent fixture index");
                check(new String(cpk.read(cpk.entries.get(0)),StandardCharsets.UTF_8).equals("Synthetic 中文"),"Independent fixture payload");
            }
            for(int size:new int[]{0,1,15,16,17,65535,65536,65537,150123}) {
                byte[] data=new byte[size];new Random(7381+size).nextBytes(data);write(root,fixture("intro",data,false,0));
                try(PatchCpk cpk=open(root)) { check(Arrays.equals(cpk.read(cpk.entries.get(0)),data),"Chunk boundary "+size); }
            }
            for(String name:new String[]{"../escape","..\\escape","C:escape","CON","intro.",""}) {
                write(root,fixture(name,new byte[]{1},false,0));reject(()->{try(PatchCpk ignored=open(root)) {}});
            }
            byte[][] pair=fixture("intro",new byte[]{1,2,3},false,0);pair[1][0]^=1;write(root,pair);
            reject(()->{try(PatchCpk ignored=open(root)) {}});
            pair=fixture("intro",new byte[]{1,2,3},false,0);pair[1][80]^=1;write(root,pair);
            reject(()->{try(PatchCpk cpk=open(root)) {cpk.read(cpk.entries.get(0));}});
            for(int adjustment:new int[]{-1,1}) {
                write(root,fixture("intro",new byte[]{1,2,3},false,adjustment));
                reject(()->{try(PatchCpk cpk=open(root)) {cpk.read(cpk.entries.get(0));}});
            }
            write(root,fixture("intro",new byte[]{1,2,3},true,0));
            reject(()->{try(PatchCpk cpk=open(root)) {cpk.read(cpk.entries.get(0));}});
            pair=fixture("intro",new byte[]{1,2,3},false,0);pair[0]=Arrays.copyOf(pair[0],pair[0].length-1);write(root,pair);
            reject(()->{try(PatchCpk ignored=open(root)) {}});
            pair=fixture("intro",new byte[]{1,2,3},false,0);pair[1][4]^=1;write(root,pair);
            reject(()->{try(PatchCpk ignored=open(root)) {}});
            write(root,fixture("intro",new byte[]{1,2,3},false,0));
            PatchCpk cpk=open(root);PatchCpk.Entry entry=cpk.entries.get(0);cpk.close();reject(()->cpk.read(entry));
            System.out.println("PATCH_CPK_PASS: RFC/CMAC/independent CPK vectors, chunk boundaries, bad keys/ranges/names/lengths/trailing data");
        } finally {
            Files.deleteIfExists(root.resolve("toy.cpk"));Files.deleteIfExists(root.resolve("toy.key"));Files.deleteIfExists(root);
        }
    }
}
