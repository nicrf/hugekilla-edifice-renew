// The shaders of the sokol_gfx backend (renderer_sokol.cpp), in sokol-shdc's annotated
// GLSL; CMake turns this file into edifice.glsl.h (Metal, HLSL, GLSL, WGSL).
//
// "draw" is the old fixed-function pipeline as the game set it up: the vertex color
// (clamped, as GL clamped glColor) times the texture (GL_MODULATE; a white texture
// when texturing is off), then linear fog by eye distance. Blending, depth and
// culling are pipeline states. "present" copies the frame to the window.

@ctype mat4 Mat4

@vs draw_vs
layout(binding=0) uniform draw_vs_params {
	mat4 proj;      // GL's, with z mapped to 0..1 where the API wants it (Metal, D3D11, WebGPU)
	mat4 modelview;
};

in vec3 position;
in vec2 texcoord0;
in vec4 color0;

out vec2 uv;
out vec4 color;
out float fogdist;

void main() {
	// in two products, as GL did: the same rounding
	vec4 eye = modelview * vec4(position, 1.0);
	gl_Position = proj * eye;
	// GL's fog coordinate: the eye-space depth |z|, not the radial distance
	fogdist = abs(eye.z);
	uv = texcoord0;
	color = clamp(color0, 0.0, 1.0);
}
@end

@fs draw_fs
layout(binding=0) uniform texture2D tex;
layout(binding=0) uniform sampler smp;
layout(binding=1) uniform draw_fs_params {
	vec4 fogcolor;
	vec4 fog;       // x: end/(end-start), y: 1/(end-start); fog off: 1, 0
};

in vec2 uv;
in vec4 color;
in float fogdist;

out vec4 frag_color;

void main() {
	vec4 c = texture(sampler2D(tex, smp), uv) * color;
	float f = clamp(fog.x - fogdist * fog.y, 0.0, 1.0);   // GL_LINEAR: (end-d)/(end-start)
	frag_color = vec4(mix(fogcolor.rgb, c.rgb, f), c.a);
}
@end

@program draw draw_vs draw_fs

// a triangle over the whole window, no vertex buffer
@vs present_vs
layout(binding=0) uniform present_vs_params {
	vec4 flip;      // v = flip.x + flip.y * y: the offscreen image is upside down where the origin is top left
};

out vec2 uv;

void main() {
	vec2 p = vec2(float((gl_VertexIndex << 1) & 2), float(gl_VertexIndex & 2));
	gl_Position = vec4(p * 2.0 - 1.0, 0.0, 1.0);
	uv = vec2(p.x, flip.x + flip.y * p.y);
}
@end

@fs present_fs
layout(binding=0) uniform texture2D frame;
layout(binding=0) uniform sampler framesmp;

in vec2 uv;
out vec4 frag_color;

void main() {
	frag_color = texture(sampler2D(frame, framesmp), uv);
}
@end

@program present present_vs present_fs
