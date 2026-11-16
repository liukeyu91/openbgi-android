#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
/* SPDX-License-Identifier: GPL-2.0-only */
#include "../src/core/cbg2.c"
#if defined(__linux__)
#include <sys/mman.h>
#include <unistd.h>
#endif

#define CHECK(v) do { if(!(v)) { fprintf(stderr,"cbg2fast:%d: %s\n",__LINE__,#v); exit(1); } } while(0)
static uint32_t seed=72737;
static uint32_t Random(void){seed^=seed<<13;seed^=seed>>17;seed^=seed<<5;return seed;}
static uint32_t ReferenceBit(Bits_t* b)
{
    uint32_t value=b->pos<b->len?(b->p[b->pos]>>(7-b->bit))&1:0;
    if(++b->bit==8){b->bit=0;b->pos++;}
    return value;
}
static uint32_t ReferenceSymbol(Bits_t* b,const CbgNode_t* nodes,uint32_t root,uint32_t leaves)
{
    while(root>=leaves && root!=0xffffffffu)root=ReferenceBit(b)?nodes[root].right:nodes[root].left;
    return root<leaves?root:0;
}
static void PixelCopies(void)
{
    for(unsigned test=0;test<3200;test++){
        unsigned w=Random()%53,h=Random()%31,padW=(w+7)&~7u,bytes=1+test%4,offset=16+test%4;
        size_t sourceBytes=(size_t)padW*h*4,capacity=offset+(size_t)w*h*bytes+16;
        uint8_t* source=malloc(sourceBytes+4);uint8_t* a=malloc(capacity);uint8_t* b=malloc(capacity);
        CHECK(source && a && b);
        for(size_t i=0;i<sourceBytes+4;i++)source[i]=(uint8_t)Random();
        memset(a,0xa5,capacity);memset(b,0xa5,capacity);
        for(unsigned y=0;y<h;y++)for(unsigned x=0;x<w;x++)
            memcpy(a+offset+((size_t)y*w+x)*bytes,source+test%4+((size_t)y*padW+x)*4,bytes);
        CopyPixels(b+offset,source+test%4,w,h,padW,bytes*8);
        CHECK(!memcmp(a,b,capacity));free(b);free(a);free(source);
    }
}
int main(void)
{
    unsigned test,compared=0;
    for(test=0;test<1600;test++){
        CbgNode_t nodes[2*AC_SYMBOLS-1];uint32_t weights[AC_SYMBOLS]={0};
        uint32_t leaves=test%2?AC_SYMBOLS:DC_SYMBOLS;Huffman_t decoder;
        uint8_t bytes[64];unsigned i,len=test%65;
        if(test%5==0)weights[Random()%leaves]=1;
        else if(test%5==1){
            weights[0]=weights[1]=1;
            for(i=2;i<leaves && i<24;i++)weights[i]=weights[i-1]+weights[i-2];
        }else for(i=0;i<leaves;i++)weights[i]=1+Random()%10000;
        int root=BuildTree(nodes,weights,leaves);CHECK(root>=0);
        PrepareHuffman(&decoder,nodes,(uint32_t)root,leaves);
        for(i=0;i<len;i++)bytes[i]=(uint8_t)Random();
#if defined(__linux__)
        size_t page=(size_t)sysconf(_SC_PAGESIZE);
        uint8_t* map=mmap(NULL,page*2,PROT_READ|PROT_WRITE,MAP_PRIVATE|MAP_ANONYMOUS,-1,0);
        CHECK(map!=MAP_FAILED && !mprotect(map+page,page,PROT_NONE));
        uint8_t* source=map+page-len;memcpy(source,bytes,len);
#else
        uint8_t* source=bytes;
#endif
        for(unsigned bit=0;bit<8;bit++){
            Bits_t a={source,len,0,bit},b=a;
            for(i=0;i<128;i++){
                CHECK(ReadSymbol(&a,&decoder)==ReferenceSymbol(&b,nodes,(uint32_t)root,leaves));
                CHECK(a.pos==b.pos && a.bit==b.bit);compared++;
            }
        }
#if defined(__linux__)
        CHECK(!munmap(map,page*2));
#endif
    }
    puts("PASS cbg2fast: 1638400 symbol/position comparisons, DC/AC, deep/single-symbol trees and guarded/truncated streams");
    CHECK(compared==1638400);PixelCopies();
    puts("PASS cbg2fast: 3200 padded/unaligned pixel-copy comparisons at 8/16/24/32 bpp");return 0;
}
