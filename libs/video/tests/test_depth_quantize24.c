/* Exhaustive independent double-reference check of the Z24 integer shortcut. */
#include <stdint.h>
#include <stdio.h>
#include <string.h>
static uint32_t quantize(uint32_t raw) {
    float d; memcpy(&d,&raw,4);
    if(!(d>0.0f)) return 0;
    if(d>=1.0f) return 0xffffffu;
    uint32_t e=(raw>>23)&255u;
    if(e<102u) return 0;
    uint32_t m=(raw&0x7fffffu)|0x800000u;
    if(e==126u) return m-(m!=0x800000u);
    uint32_t k=126u-e;
    return (m+((1u<<(k-1u))-1u))>>k;
}
int main(void) {
    for(uint32_t raw=0;raw<=0x3f800000u;raw++) {
        float d; memcpy(&d,&raw,4);
        uint32_t want=(uint32_t)((double)d*16777215.0+0.5);
        if(quantize(raw)!=want) {
            fprintf(stderr,"FAIL raw=%08x got=%u want=%u\n",raw,quantize(raw),want);
            return 1;
        }
    }
    const uint32_t edges[]={0x80000000,0xbf000000,0xff800000,0x7f800000,0x7fc00000,0xffc00000,0x40000000};
    const uint32_t expected[]={0,0,0,0xffffff,0,0,0xffffff};
    for(unsigned i=0;i<sizeof edges/sizeof *edges;i++) if(quantize(edges[i])!=expected[i]) return 2;
    puts("PASS: all 1,065,353,217 nonnegative binary32 depths through 1.0 match double quantization; clamp/NaN edges pass");
    return 0;
}
