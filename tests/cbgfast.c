/* SPDX-License-Identifier: GPL-2.0-only */
/* Include the decoder to exercise its private bit reader and predictor without
 * exporting test-only entry points from the engine. See the Makefile rule. */
#include "../src/core/cbg.c"
#if defined(__linux__)
#include <sys/mman.h>
#include <unistd.h>
#endif

#define CHECK(v) do { if(!(v)) { fprintf(stderr,"cbgfast:%d: %s\n",__LINE__,#v); exit(1); } } while(0)
static uint32_t seed = 9387;
static uint32_t Rnd(void) { seed ^= seed<<13; seed ^= seed>>17; seed ^= seed<<5; return seed; }

static void PredictorReference(uint8_t* p, const RawImageHeader_t* h)
{
    unsigned bytes = (h->bpp & 65535u)/8, stride = h->width*bytes;
    for(unsigned y=0;y<h->height;++y) for(unsigned x=0;x<h->width;++x) for(unsigned c=0;c<bytes;++c) {
        unsigned at=y*stride+x*bytes+c;
        int upper=y?p[at-stride]:-1, left=x?p[at-bytes]:-1;
        unsigned prediction=upper<0?(left<0?0:(unsigned)left):left<0?(unsigned)upper:(unsigned)(upper+left)/2;
        p[at]=(uint8_t)(p[at]+prediction);
    }
}

static void Predictors(void)
{
    for(unsigned test=0;test<4000;++test) {
        RawImageHeader_t h={0}; h.width=(uint16_t)(Rnd()%67); h.height=(uint16_t)(Rnd()%43); h.bpp=(test%4+1)*8;
        size_t bytes=(size_t)h.width*h.height*(h.bpp/8), offset=16+test%4, capacity=offset+bytes+16;
        uint8_t* a=malloc(capacity); uint8_t* b=malloc(capacity);
        CHECK(a && b);
        for(size_t i=0;i<capacity;++i) a[i]=test%7==0?255:test%11==0?0:(uint8_t)Rnd();
        memcpy(b,a,capacity);
        PredictorReference(a+offset,&h); CbgUnfilter(b+offset,&h);
        CHECK(!memcmp(a,b,capacity)); free(a);free(b);
    }
}

static void Huffman(void)
{
    unsigned deepest=0;
    for(unsigned test=0;test<1200;++test) {
        uint32_t weights[256]={0}; CbgNode_t nodes[CBG_NODES];
        uint8_t expected[257],actual[257],bits[257*256/8+1]={0};
        unsigned leaves[256],used=0,count=1+Rnd()%257,bitCount=0;
        if(test%3==0) weights[Rnd()%256]=1;
        else if(test%3==1) { // a deliberately deep tree exercises the slow path
            weights[0]=weights[1]=1;
            for(unsigned i=2;i<24;++i) weights[i]=weights[i-1]+weights[i-2];
        } else for(unsigned i=0;i<256;++i) if(Rnd()%3) weights[i]=1+Rnd()%10000;
        for(unsigned i=0;i<256;++i) if(weights[i]) leaves[used++]=i;
        CHECK(used);
        int root=CbgBuildTree(nodes,weights,256); CHECK(root>=0);
        for(unsigned i=0;i<count;++i) {
            uint8_t reverse[256]; unsigned n=0,node=leaves[Rnd()%used];expected[i]=(uint8_t)node;
            while(nodes[node].parent!=0xffffffffu) {
                unsigned parent=nodes[node].parent; CHECK(n<256);
                reverse[n++]=(uint8_t)(nodes[parent].right==node);node=parent;
            }
            if(n>deepest) deepest=n;
            while(n) { if(reverse[--n]) bits[bitCount/8]|=(uint8_t)(0x80u>>(bitCount%8)); ++bitCount; }
        }
        size_t bytes=(bitCount+7)/8;
#if defined(__linux__)
        size_t page=(size_t)sysconf(_SC_PAGESIZE),span=((bytes+page-1)/page)*page;
        uint8_t* map=mmap(NULL,span+page,PROT_READ|PROT_WRITE,MAP_PRIVATE|MAP_ANONYMOUS,-1,0);
        CHECK(map!=MAP_FAILED && !mprotect(map+span,page,PROT_NONE));
        uint8_t* source=map+span-bytes;memcpy(source,bits,bytes);
#else
        uint8_t* source=bits;
#endif
        CbgHuffDecode(actual,source,nodes,(uint32_t)root,count);
        CHECK(!memcmp(actual,expected,count));
        CbgHuffDecode(actual,NULL,nodes,(uint32_t)root,0); // zero symbols must not read src
#if defined(__linux__)
        CHECK(!munmap(map,span+page));
#endif
    }
    CHECK(deepest>8);
}

int main(void)
{
    Predictors();Huffman();
    puts("PASS cbgfast: 4000 predictor/canary comparisons, 1200 Huffman streams, deep/single-symbol trees and guarded final bytes");
    return 0;
}
