#version 450 core

out vec4 out_color;

uniform vec2 center;
uniform vec4 radius;
uniform vec2 half_size;
uniform bool horizontal;
uniform int blur_radius;
uniform bool do_blurring;
uniform float blur_shrink;
uniform sampler2D tex;

float rect_sdf (vec2 frag_pos, vec2 center, vec2 half_size, float radius) {
    return length(max(abs(frag_pos - center) - half_size + radius, 0.0)) - radius;
}

void main () {
    vec2 frag_pos = gl_FragCoord.xy;
    vec3 result;

    if (do_blurring) {
        vec2 tex_pos = frag_pos / textureSize(tex, 0);
        vec2 tex_offset = 1.0 / textureSize(tex, 0);
        result = texture(tex, tex_pos).rgb / (2*blur_radius - 1);

        if (horizontal) {
            for(int i = 1; i < blur_radius; ++i) {
                result += texture(tex, tex_pos + vec2(tex_offset.x * i, 0.0)).rgb / (2*blur_radius - 1);
                result += texture(tex, tex_pos - vec2(tex_offset.x * i, 0.0)).rgb / (2*blur_radius - 1);
            }
        } else {
            for(int i = 1; i < blur_radius; ++i) {
                result += texture(tex, tex_pos + vec2(0.0, tex_offset.y * i)).rgb / (2*blur_radius - 1);
                result += texture(tex, tex_pos - vec2(0.0, tex_offset.y * i)).rgb / (2*blur_radius - 1);
            }
        }
    } else {
        vec2 fpc = frag_pos - center;
        float r  = (fpc.x > 0.0) ? ((fpc.y > 0.0) ? radius.x : radius.z) : ((fpc.y > 0.0) ? radius.y : radius.w);
        float d  = rect_sdf(frag_pos, center, half_size, r);
        if (d > 0) discard;
        result = texture(tex, (frag_pos / blur_shrink) / textureSize(tex, 0).xy).rgb;
    }

    out_color = vec4(result, 1.0);
}
