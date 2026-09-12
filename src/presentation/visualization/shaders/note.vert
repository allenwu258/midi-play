#version 450
layout(location=0) in vec4 rectangle;
layout(location=1) in vec4 times;
layout(location=2) in vec4 fill;
layout(location=3) in vec4 border;
layout(location=4) in vec4 activeBorder;
layout(location=5) in vec4 uv;
layout(location=6) in vec4 options;
layout(push_constant) uniform Frame {
    float width; float height; float position; float strike;
    float scale; float clipTop; float clipBottom; float dpr;
} frame;
layout(location=0) out vec2 local;
layout(location=1) out vec2 world;
layout(location=2) out vec2 texcoord;
layout(location=3) flat out vec4 color;
layout(location=4) flat out vec4 edge;
layout(location=5) flat out vec4 shape;
layout(location=6) flat out vec4 flags;

void main() {
    const vec2 corners[6] = vec2[6](vec2(0,0),vec2(1,0),vec2(1,1),vec2(0,0),vec2(1,1),vec2(0,1));
    vec2 corner = corners[gl_VertexIndex];
    vec4 r = rectangle;
    float kind = times.w;
    float stroke = options.x;
    color = fill;
    edge = border;
    bool isActiveNow = times.x <= frame.position && times.z > frame.position;
    if (kind > 0) {
        float startY = frame.strike - (times.x-frame.position)*frame.scale;
        float keyY = frame.strike - (times.y-frame.position)*frame.scale;
        float endY = frame.strike - (times.z-frame.position)*frame.scale;
        float top = min(keyY,startY-4);
        float h = max(4,startY-top);
        stroke = 0;
        if (kind == 1) {
            r = vec4(r.x+r.z*.2,endY,r.z*.6,max(2,keyY-endY));
        } else if (kind == 2) {
            r.y=top; r.w=h; stroke=isActiveNow ? 2 : 1;
            edge=isActiveNow ? activeBorder : border;
        } else if (kind == 3) {
            r=vec4(r.x+1,startY-.5,max(0,r.z-2),1);
            color=isActiveNow ? activeBorder : border;
        } else {
            r=vec4(r.x+3,top+h*.5-3,max(0,r.z-6),6);
            color=vec4(1,1,1,110.0/255);
            if (h<=14) r.z=0;
        }
    }
    local=corner*r.zw;
    world=r.xy+local;
    texcoord=uv.xy+corner*uv.zw;
    shape=vec4(r.zw,stroke,kind);
    flags=options;
    gl_Position=vec4(world.x/frame.width*2-1,world.y/frame.height*2-1,0,1);
}
