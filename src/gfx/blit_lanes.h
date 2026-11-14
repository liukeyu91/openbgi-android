/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef BGI_BLIT_LANES_H
#define BGI_BLIT_LANES_H
#include <stdint.h>

/* Separate BGRA bytes into four 16-bit lanes. Weights add to at most 128,
 * so each product/sum fits its lane without carrying into its neighbour. */
static inline uint64_t Blit_ExpandBytes(uint32_t pixel)
{
    uint64_t p=pixel;
    p=(p | (p<<16)) & UINT64_C(0x0000ffff0000ffff);
    return (p | (p<<8)) & UINT64_C(0x00ff00ff00ff00ff);
}
static inline uint32_t Blit_PackBytes(uint64_t p)
{
    p=(p | (p>>8)) & UINT64_C(0x0000ffff0000ffff);
    return (uint32_t)(p | (p>>16));
}
/* Exactly d + floor((s-d)*weight/128), preserving the destination's top
 * byte. Do not replace alpha 255 with a copy: its original weight is 127. */
static inline uint32_t Blit_BlendRgb7(uint32_t dst,uint32_t src,unsigned weight)
{
    const uint32_t rb=0x00ff00ffu;
    unsigned inv=128-weight;
    uint32_t br=(((src&rb)*weight+(dst&rb)*inv)>>7)&rb;
    uint32_t g=(((src&0xff00u)*weight+(dst&0xff00u)*inv)>>7)&0xff00u;
    return (dst&0xff000000u)|br|g;
}

/* Two independent 8.8 weights, whose sum is at most 256. Keep each
 * channel's floor separately, including when the weights sum to 255. */
static inline uint32_t Blit_WeightedRgb8(uint32_t dst,uint32_t src,unsigned sourceWeight,unsigned destWeight)
{
    const uint32_t rb=0x00ff00ffu;
    uint32_t br=(((src&rb)*sourceWeight+(dst&rb)*destWeight)>>8)&rb;
    uint32_t g=(((src&0xff00u)*sourceWeight+(dst&0xff00u)*destWeight)>>8)&0xff00u;
    return br|g;
}
#endif
