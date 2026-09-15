#version 450
layout(set=0,binding=0) uniform sampler2D atlas;
layout(push_constant) uniform Frame {
    float width; float height; float position; float strike;
    float scale; float clipTop; float clipBottom; float dpr;
    float bodyOpacity;
} frame;
layout(location=0) in vec2 local;
layout(location=1) in vec2 world;
layout(location=2) in vec2 texcoord;
layout(location=3) flat in vec4 color;
layout(location=4) flat in vec4 edge;
layout(location=5) flat in vec4 shape;
layout(location=6) flat in vec4 flags;
layout(location=0) out vec4 outputColor;

void main() {
    if (shape.w>0 && (world.y<frame.clipTop || world.y>=frame.clipBottom)) discard;
    vec4 c=color;
    if (shape.w < 0.0) {
        float feather = max(0.5 / frame.dpr, 0.001);
        float edgeDistance = min(local.y, shape.y - local.y);
        float coverage = smoothstep(0.0, feather, edgeDistance);
        c.a *= coverage;
    } else if (shape.w>0 && shape.w<4) {
        if (shape.x<=0 || shape.y<=0) discard;
        // Independent box coverage preserves right-angle corners and smooth
        // subpixel motion, including note lanes narrower than one pixel.
        vec2 coverage=clamp(local*frame.dpr+.5,0,1)
                     -clamp((local-shape.xy)*frame.dpr+.5,0,1);
        c.a*=coverage.x*coverage.y;
        if (shape.w==1) {
            c.a*=mix(.16,1,clamp(local.y/shape.y,0,1));
        } else if (shape.w==2) {
            float u=clamp(local.x/shape.x,0,1);
            c.a*=u<.32 ? mix(.66,1,u/.32) : mix(1,.78,(u-.32)/.68);
        }
    } else if (flags.z>0) {
        c.a *= texture(atlas,texcoord).a;
    } else if (flags.w>0) {
        vec2 normalized=local/shape.xy*2-1;
        float radius=length(normalized);
        if (radius>1) discard;
        if (flags.w==2) c.a*=1-radius;
    } else if (shape.w==4) {
        float distanceToLine=abs(local.y-(6-6*local.x/max(shape.x,1)));
        if (distanceToLine>.7) discard;
    } else if (shape.z>0) {
        float nearest=min(min(local.x,shape.x-local.x),min(local.y,shape.y-local.y));
        if (nearest<shape.z) {
            bool vertical=min(local.x,shape.x-local.x)<shape.z;
            float along=vertical ? local.y : local.x;
            if (flags.y==0 || mod(along/max(shape.z,1),6)<4) {
                c=vec4(edge.rgb*edge.a+c.rgb*c.a*(1-edge.a),edge.a+c.a*(1-edge.a));
                outputColor=c;
                return;
            }
        }
    }
    outputColor=vec4(c.rgb*c.a,c.a);
}
