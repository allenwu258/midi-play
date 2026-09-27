#version 450
layout(set=0,binding=0) uniform sampler2D atlas;
layout(set=0,binding=1) uniform sampler2D background;
layout(push_constant) uniform Frame {
    float width; float height; float position; float strike;
    float scale; float clipTop; float clipBottom; float dpr;
    float bodyOpacity;
    float noteHaloStrength;
    float noteEdgeStrength;
    float noteSheenStrength;
    float strikeGlowStrength;
    float keyGlowStrength;
    float particleStrength;
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
        bool glass = flags.z > 0.5;
        vec2 normalized = clamp(local / shape.xy, 0.0, 1.0);
        if (glass) {
            float padding = max(1.0, flags.x);
            vec2 coreSize = max(shape.xy - vec2(padding * 2.0), vec2(0.001));
            vec2 coreLocal = local - vec2(padding);
            normalized = clamp(coreLocal / coreSize, 0.0, 1.0);
            // Keep the note silhouette strictly rectangular. The halo uses
            // the same box distance, so the glow follows the square corners
            // instead of reintroducing a rounded cap.
            float radius = 0.0;
            vec2 centered = coreLocal - coreSize * 0.5;
            vec2 q = abs(centered) - (coreSize * 0.5 - vec2(radius));
            float signedDistance = length(max(q, vec2(0.0)))
                           + min(max(q.x, q.y), 0.0) - radius;
            float antialias = max(0.65 / frame.dpr, 0.001);
            float coreMask = 1.0 - smoothstep(-antialias, antialias, signedDistance);
            float haloMask = 1.0 - smoothstep(0.0, padding * 1.15, signedDistance);
            float outerHalo = max(0.0, haloMask - coreMask);
            c.a *= coreMask;
            vec3 haloColor = mix(c.rgb, edge.rgb, 0.40);
            c.rgb = mix(c.rgb, haloColor, outerHalo * 0.72);
            c.a = min(1.0, c.a + outerHalo * frame.noteHaloStrength);

            float rimWidth = max(1.6 / frame.dpr, 0.75);
            float rim = (1.0 - smoothstep(0.0, rimWidth, -signedDistance)) * coreMask;
            vec3 edgeHighlight = mix(edge.rgb, vec3(1.0), 0.72);
            c.rgb = mix(c.rgb, edgeHighlight,
                        rim * frame.noteEdgeStrength * (shape.w == 3.0 ? 1.08 : 0.82));
            c.a = min(1.0, c.a + rim * 0.11 * frame.noteEdgeStrength);

            // A broad diagonal sheen produces a glass-like reflection rather
            // than a single hard stripe. Its position remains deterministic
            // for seeks and dropped frames because it is driven by music time.
            float sheenCoordinate = normalized.y * 0.84 + normalized.x * 0.16;
            float sheenCenter = fract(frame.position * 0.24 + flags.w);
            float sheen = 1.0 - smoothstep(0.0, 0.13,
                abs(sheenCoordinate - sheenCenter));
            sheen *= smoothstep(0.03, 0.16, normalized.y)
                   * smoothstep(0.03, 0.16, 1.0 - normalized.y);
            c.rgb = mix(c.rgb, vec3(1.0), sheen * frame.noteSheenStrength);
            c.a = min(1.0, c.a + sheen * 0.12 * frame.noteSheenStrength);

            // Keep a restrained luminous core inside the colored body. This
            // gives narrow lanes a readable center even when the surrounding
            // halo is composited over a bright background.
            float coreStripe = 1.0 - smoothstep(0.0, 0.44,
                abs(normalized.x - 0.5));
            c.rgb = mix(c.rgb, vec3(1.0), coreStripe * 0.12);
        } else {
            // Independent box coverage preserves the original renderer when
            // enhanced effects are disabled.
            vec2 coverage=clamp(local*frame.dpr+.5,0,1)
                         -clamp((local-shape.xy)*frame.dpr+.5,0,1);
            c.a*=coverage.x*coverage.y;
        }
        if (shape.w==1) {
            c.a*=mix(.16,1,normalized.y);
        } else if (shape.w==2) {
            float u=normalized.x;
            c.a*=u<.32 ? mix(.66,1,u/.32) : mix(1,.78,(u-.32)/.68);
        }
    } else if (shape.w==4) {
        float distanceToLine=abs(local.y-(6-6*local.x/max(shape.x,1)));
        if (distanceToLine>.7) discard;
    } else if (flags.z>0) {
        if (flags.z > 1.5) c *= texture(background, texcoord);
        else c.a *= texture(atlas,texcoord).a;
    } else if (flags.w>0) {
        vec2 normalized=local/shape.xy*2-1;
        float radius=length(normalized);
        if (radius>1) discard;
        if (flags.w==2) c.a*=1-radius;
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
