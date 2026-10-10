#version 450
// 实例顶点着色器：生成六个矩形顶点，并在 GPU 上按播放时间计算音符纵向几何。
// location 0..6 与 VulkanQuad 七个 vec4 属性保持一致。
layout(location=0) in vec4 rectangle;
layout(location=1) in vec4 times;
layout(location=2) in vec4 fill;
layout(location=3) in vec4 border;
layout(location=4) in vec4 activeBorder;
layout(location=5) in vec4 uv;
layout(location=6) in vec4 options;
// 与 C++ FrameConstants 的字段顺序及 72 字节布局一致。
layout(push_constant) uniform Frame {
    // width/height 是输出物理像素；position 是相对时间基准的秒；strike 是逻辑像素 Y。
    float width; float height; float position; float strike;
    // scale 是逻辑像素/秒，裁剪边界是逻辑像素；dpr 最后把几何统一映射到输出像素。
    float scale; float clipTop; float clipBottom; float dpr;
    float bodyOpacity;
    float noteHaloStrength;
    float noteEdgeStrength;
    float noteSheenStrength;
    float strikeGlowStrength;
    float keyGlowStrength;
    float particleStrength;
    // 导出居中偏移使用物理像素；animationSeconds 仅驱动视觉相位，不决定音符事件时间。
    float offsetX; float offsetY; float animationSeconds;
} frame;
layout(location=0) out vec2 local;
layout(location=1) out vec2 world;
layout(location=2) out vec2 texcoord;
layout(location=3) flat out vec4 color;
layout(location=4) flat out vec4 edge;
layout(location=5) flat out vec4 shape;
layout(location=6) flat out vec4 flags;

// 图元类型：负数为移动网格线，0 为 UI，1..4 为尾部/主体/音头/震音。
void main() {
    // 一个实例用两个三角形覆盖矩形，gl_VertexIndex 由每实例六顶点的绘制契约限定。
    const vec2 corners[6] = vec2[6](vec2(0,0),vec2(1,0),vec2(1,1),vec2(0,0),vec2(1,1),vec2(0,1));
    vec2 corner = corners[gl_VertexIndex];
    vec4 r = rectangle;
    float kind = times.w;
    float notePadding = 0.0;
    if (kind < 0.0) {
        // 线条两侧各扩展半个物理像素，片元阶段求平滑覆盖率，保留连续逻辑位置。
        r.y -= 0.5 / frame.dpr;
        r.w += 1.0 / frame.dpr;
    }
    float stroke = options.x;
    color = fill;
    edge = border;
    flags=options;
    if (kind > 0.0) edge = activeBorder;
    if (kind > 0) {
        // times 和 position 使用同一相对秒基准；松键与发声结束分别决定主体及尾部。
        float startY = frame.strike - (times.x-frame.position)*frame.scale;
        float keyY = frame.strike - (times.y-frame.position)*frame.scale;
        float endY = frame.strike - (times.z-frame.position)*frame.scale;
        // 极短音也保留至少四个逻辑像素高，保证远景下可辨识。
        float top = min(keyY,startY-4);
        float h = max(4,startY-top);
        stroke = 0;
        if (kind == 1) {
            r = vec4(r.x+r.z*.31,endY,r.z*.38,max(0,keyY-endY));
        } else if (kind == 2) {
            r.y=top; r.w=h;
        } else if (kind == 3) {
            r=vec4(r.x,startY-2.5,r.z,2.5);
            color=activeBorder;
        } else {
            r=vec4(r.x+3,top+h*.5-3,max(0,r.z-6),6);
            // 震音颜色由 CPU 共享主题提供；主体过短时隐藏震音线。
            if (h<=14) r.z=0;
        }
        // 发声结束后在 0.24 秒内淡出，密度透明度只调节尾部和主体。
        color.a *= clamp(1-(frame.position-times.z)/.24,0,1);
        if (kind<=2) color.a*=frame.bodyOpacity;
    }
    // 特效启用时扩大栅格化矩形，片元阶段在外扩区域生成方形光晕。
    if (kind > 0.0 && kind < 4.0 && flags.z > 0.5) {
        notePadding = max(1.0, options.x);
        r.xy -= vec2(notePadding);
        r.zw += vec2(notePadding * 2.0);
    }
    local=corner*r.zw;
    // 方形音符边缘额外包含半个物理像素，以容纳解析抗锯齿覆盖率。
    if (kind>0 && kind<4 && r.z>0 && r.w>0)
        local += (corner*2-1)*(.5/frame.dpr);
    world=r.xy+local;
    texcoord=uv.xy+corner*uv.zw;
    shape=vec4(r.zw,stroke,kind);
    // 先转物理像素并叠加导出偏移，再映射到 Vulkan 裁剪坐标。
    vec2 physicalWorld = world * frame.dpr + vec2(frame.offsetX, frame.offsetY);
    gl_Position=vec4(physicalWorld.x/frame.width*2-1,physicalWorld.y/frame.height*2-1,0,1);
}
