#version 450
layout(set=0,binding=0) uniform sampler2D depth_image;
layout(location=0) out vec4 color;
uint quantize(uint raw) {
    float d=uintBitsToFloat(raw);
    if (!(d>0.0)) return 0;
    if (d>=1.0) return 0xffffffu;
    uint e=(raw>>23)&255u;
    if (e<102u) return 0;
    uint m=(raw&0x7fffffu)|0x800000u;
    if(e==126u) return m-uint(m!=0x800000u);
    uint k=126u-e;
    return (m+((1u<<(k-1u))-1u))>>k;
}
void main() {
    uint v=quantize(floatBitsToUint(texelFetch(depth_image,ivec2(gl_FragCoord.xy),0).r));
    color=vec4((v>>8)&255u,v&255u,0u,v>>16)/255.0;
}
