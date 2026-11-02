// The shaders of the Ultra graphics (renderer_ultra.cpp), sokol-shdc's annotated GLSL,
// compiled for Metal, HLSL 5 and GLSL 4.10 (not the web: WebGL2 stays Classic).
//
// The scene is drawn in linear HDR (RGBA16F): the sky, the lit materials (PBR:
// Cook-Torrance GGX with a directional sun and the sky's light as ambient), the
// cascaded shadow maps, the water and the grass; the game's other draws (particles,
// effects, clouds) keep the classic look, decoded to linear. Then bloom, god rays,
// exposure, ACES and FXAA make the frame.
//
// Conventions: world y is up; the sun direction points towards the sun. The sky
// LUT (computed on the CPU) is an equirectangular map: u = azimuth, v = 0.5 +-
// sqrt(|elevation|/90 degrees) / 2 (detail near the horizon). Screen-space images
// are sampled at gl_FragCoord / size, the same on every API.

@ctype mat4 Mat4

@block common
const float PI = 3.14159265;

float hash12(vec2 p) {
	vec3 p3 = fract(vec3(p.xyx) * 0.1031);
	p3 += dot(p3, p3.yzx + 33.33);
	return fract((p3.x + p3.y) * p3.z);
}

float vnoise(vec2 p) {
	vec2 i = floor(p);
	vec2 f = fract(p);
	vec2 u = f * f * (3.0 - 2.0 * f);
	return mix(mix(hash12(i), hash12(i + vec2(1.0, 0.0)), u.x),
		mix(hash12(i + vec2(0.0, 1.0)), hash12(i + vec2(1.0, 1.0)), u.x), u.y);
}

vec3 srgb_to_linear(vec3 c) {
	return c * (c * (c * 0.305306011 + 0.682171111) + 0.012522878);
}

// the HD grass (sRGB) as the map's ground: less vivid, its hue
vec3 grass_tint(vec3 g, vec3 hue) {
	g = mix(g, vec3(dot(g, vec3(0.3, 0.59, 0.11))), 0.5);
	return g * mix(vec3(1.0), hue, 0.5) * vec3(0.78, 0.8, 0.62);
}
@end

// the scene's light, sky and fog: the same for every draw of a frame
@block scene_ub
layout(binding=2) uniform scene_params {
	vec4 sundir;        // xyz towards the sun; w: sun disk visibility (0 below the horizon)
	vec4 suncolor;      // rgb: the sun's light at the ground; w: time in seconds
	vec4 skyzenith;     // ambient from above (radiance); w: haze (Mie strength)
	vec4 skyhorizon;    // ambient from the horizon; w: water height
	vec4 groundcolor;   // ambient from below; w: underwater (0, 1)
	vec4 eyepos;        // camera, world; w: fog end
	vec4 fogparams;     // x: density at the water's height, y: height falloff, z: classic brightness, w: shadows on
	vec4 watercolor;    // rgb: deep water scattering; w: water absorption
	mat4 invview;       // eye to world
	mat4 cascades[4];   // world to the shadow atlas: uv, depth
	vec4 cascadeends;   // eye depth where each cascade ends
	vec4 shadowparams;  // x: number of cascades, y: texel size of a cascade, z: depth bias, w: normal offset
};
@end

@block sky_fn
vec2 sky_uv(vec3 d) {
	float az = atan(d.x, d.z);
	float el = asin(clamp(d.y, -1.0, 1.0));
	float v = 0.5 + 0.5 * sign(el) * sqrt(abs(el) / (PI * 0.5));
	return vec2(az / (2.0 * PI) + 0.5, v);
}

vec3 sky_color(vec3 d) {
	return textureLod(sampler2D(skylut, lutsmp), sky_uv(d), 0.0).rgb;
}

// Henyey-Greenstein
float hg_phase(float c, float g) {
	float g2 = g * g;
	return (1.0 - g2) / (4.0 * PI * pow(max(1.0 + g2 - 2.0 * g * c, 1e-4), 1.5));
}

// the color of the air between the eye and a point along d: the sky near the
// horizon there, plus the sun's light scattered forward by the haze
vec3 fog_color(vec3 d) {
	vec3 h = normalize(vec3(d.x, clamp(d.y, 0.03, 0.25), d.z));
	vec3 c = sky_color(h);
	c += suncolor.rgb * skyzenith.w * hg_phase(dot(d, sundir.xyz), 0.72) * 0.12;
	return c;
}

// height fog (exponential, integrated along the ray) and the end of the land
float fog_amount(vec3 wpos) {
	vec3 rd = wpos - eyepos.xyz;
	float dist = length(rd);
	float a = fogparams.x;
	float b = fogparams.y;
	float h0 = eyepos.y - skyhorizon.w;
	float k = b * rd.y;
	float integral = a * exp(-b * h0) * dist * (abs(k) > 1e-3 ? (1.0 - exp(-k)) / k : 1.0);
	float f = 1.0 - exp(-max(integral, 0.0));
	return max(f, smoothstep(eyepos.w * 0.5, eyepos.w * 0.96, dist));
}

// the sky's light on something facing up and sideways (foam, grass)
vec3 hemi_sky() {
	return mix(skyhorizon.rgb, skyzenith.rgb, 0.6);
}

vec3 apply_fog(vec3 c, vec3 wpos) {
	vec3 rd = wpos - eyepos.xyz;
	float dist = length(rd);
	if (groundcolor.w > 0.5) {
		// under water: the light fades and turns to the water's color within a few meters
		vec3 absorb = exp(-dist * vec3(0.45, 0.16, 0.11));
		vec3 deep = watercolor.rgb * (suncolor.rgb * 0.25 + skyzenith.rgb * 0.5);
		return c * absorb + deep * (1.0 - exp(-dist * 0.16));
	}
	return mix(c, fog_color(rd / max(dist, 1e-4)), fog_amount(wpos));
}
@end

@block pbr_fn
float d_ggx(float nh, float a) {
	float a2 = a * a;
	float d = nh * nh * (a2 - 1.0) + 1.0;
	return a2 / (PI * d * d + 1e-7);
}

float v_smith(float nv, float nl, float a) {
	float a2 = a * a;
	float gv = nl * sqrt(nv * nv * (1.0 - a2) + a2);
	float gl = nv * sqrt(nl * nl * (1.0 - a2) + a2);
	return 0.5 / (gv + gl + 1e-5);
}

// light from the whole sky and the ground, for a normal: a hemisphere
vec3 hemi(vec3 n) {
	vec3 sky = mix(skyhorizon.rgb, skyzenith.rgb, clamp(n.y, 0.0, 1.0));
	return mix(groundcolor.rgb, sky, clamp(n.y * 0.5 + 0.5, 0.0, 1.0));
}

vec3 shade(vec3 albedo, float rough, float metal, float ao, vec3 n, vec3 v, float shadow) {
	vec3 l = sundir.xyz;
	vec3 h = normalize(l + v);
	float nl = max(dot(n, l), 0.0);
	float nv = max(dot(n, v), 1e-3);
	float nh = max(dot(n, h), 0.0);
	float vh = max(dot(v, h), 0.0);
	float a = max(rough * rough, 0.002);
	vec3 f0 = mix(vec3(0.04), albedo, metal);
	vec3 f = f0 + (1.0 - f0) * pow(1.0 - vh, 5.0);
	vec3 spec = d_ggx(nh, a) * v_smith(nv, nl, a) * f;
	vec3 kd = (1.0 - f) * (1.0 - metal);
	vec3 direct = (kd * albedo / PI + spec) * suncolor.rgb * nl * shadow;
	vec3 irr = hemi(n);
	vec3 r = reflect(-v, n);
	vec3 env = mix(sky_color(r), irr, rough * rough);
	vec3 fa = f0 + (max(vec3(1.0 - rough), f0) - f0) * pow(1.0 - nv, 5.0);
	// the sky seen in a reflection is darker where the sun is hidden
	float specocc = mix(0.35, 1.0, shadow) * ao;
	vec3 ambient = (1.0 - fa) * (1.0 - metal) * albedo * irr * ao + fa * env * specocc;
	return direct + ambient;
}
@end

@block shadow_fn
// cascaded shadow map, an atlas of 2x2 cascades, 3x3 hardware PCF taps
float shadow_at(vec3 wpos, vec3 n, float eyedepth) {
	if (fogparams.w < 0.5) return 1.0;
	int count = int(shadowparams.x);
	// (no dynamic index into a vec4: the GL target flattens the block)
	vec4 ce = cascadeends;
	int ci = eyedepth < ce.x ? 0 : (eyedepth < ce.y ? 1 : (eyedepth < ce.z ? 2 : (eyedepth < ce.w ? 3 : 4)));
	if (ci >= count) return 1.0;
	float cend = ci == 0 ? ce.x : (ci == 1 ? ce.y : (ci == 2 ? ce.z : ce.w));
	float nl = clamp(dot(n, sundir.xyz), 0.0, 1.0);
	// further cascades have bigger texels: push the point out along its normal by about one
	float scale = 1.0 + float(ci) * 1.5;
	vec3 p = wpos + n * shadowparams.w * scale * (1.0 - nl * 0.6);
	vec4 sc = cascades[ci] * vec4(p, 1.0);
	vec3 c = sc.xyz / sc.w;
	float bias = shadowparams.z * scale;
	float s = 0.0;
	float t = shadowparams.y;
	for (int y = -1; y <= 1; y++) {
		for (int x = -1; x <= 1; x++) {
			s += textureLod(sampler2DShadow(shadowmap, shadowsmp), vec3(c.xy + vec2(float(x), float(y)) * t, c.z - bias), 0.0);
		}
	}
	s /= 9.0;
	// fade out at the end of the last cascade
	float fade = (ci == count - 1) ? smoothstep(cend * 0.85, cend, eyedepth) : 0.0;
	return mix(s, 1.0, fade);
}
@end

//---------------------------------------------------------------- classic draws in HDR

@vs hdrdraw_vs
layout(binding=0) uniform hdrdraw_vs_params {
	mat4 proj;
	mat4 modelview;
};

in vec3 position;
in vec2 texcoord0;
in vec4 color0;

out vec2 uv;
out vec4 color;
out vec3 eye;

void main() {
	vec4 e = modelview * vec4(position, 1.0);
	gl_Position = proj * e;
	eye = e.xyz;
	uv = texcoord0;
	color = clamp(color0, 0.0, 1.0);
}
@end

@fs hdrdraw_fs
@include_block common
@include_block scene_ub
layout(binding=0) uniform texture2D tex;
layout(binding=0) uniform sampler smp;
layout(binding=1) uniform texture2D skylut;
layout(binding=1) uniform sampler lutsmp;
layout(binding=1) uniform hdrdraw_fs_params {
	vec4 fogcolor;
	vec4 fog;       // classic linear fog: x end/(end-start), y 1/(end-start)
};
@include_block sky_fn

in vec2 uv;
in vec4 color;
in vec3 eye;

out vec4 frag_color;

void main() {
	vec4 c = texture(sampler2D(tex, smp), uv) * color;
	vec3 rgb = srgb_to_linear(c.rgb) * fogparams.z;
	if (fog.y > 0.0) {
		// the classic fog's range, the Ultra sky's color
		vec3 wdir = (invview * vec4(eye, 0.0)).xyz;
		float d = length(eye);
		float f = clamp(fog.x - abs(eye.z) * fog.y, 0.0, 1.0);
		if (groundcolor.w > 0.5)
			rgb = apply_fog(rgb, eyepos.xyz + wdir);
		else
			rgb = mix(fog_color(wdir / max(d, 1e-4)), rgb, f);
	}
	frag_color = vec4(rgb, c.a);
}
@end

@program hdrdraw hdrdraw_vs hdrdraw_fs

//---------------------------------------------------------------- lit materials

@vs lit_vs
@include_block common
layout(binding=0) uniform lit_vs_params {
	mat4 proj;
	mat4 modelview;
	mat4 model;
	vec4 wind;          // x: time, y: strength, z: sway (foliage), w: clip below the water (reflection)
};

in vec3 position;
in vec2 texcoord0;
in vec4 color0;
in vec3 normal0;

out vec2 uv;
out vec4 color;
out vec3 wpos;
out vec3 wnormal;
out float eyedepth;

void main() {
	vec3 pos = position;
	if (wind.z > 0.0) {
		// trees and flowers: the higher, the more they bend (in the model's own
		// units and axes), with gusts; the phase from where they stand
		vec3 o = (model * vec4(0.0, 0.0, 0.0, 1.0)).xyz;
		float h = max(position.y, 0.0);
		float t = wind.x;
		float gust = 0.6 + 0.4 * sin(t * 0.7 + o.x * 0.05) * sin(t * 0.31 + o.z * 0.07);
		vec2 sway = vec2(sin(t * 1.3 + o.x * 0.3 + h * 0.2), cos(t * 1.1 + o.z * 0.3 + h * 0.2)) * gust * wind.y * wind.z * h * h;
		pos.xz += sway;
	}
	vec4 w = model * vec4(pos, 1.0);
	vec4 e = modelview * vec4(pos, 1.0);
	gl_Position = proj * e;
	wpos = w.xyz;
	wnormal = mat3(model) * normal0;
	eyedepth = abs(e.z);
	uv = texcoord0;
	color = color0;
}
@end

@fs lit_fs
@include_block common
@include_block scene_ub
layout(binding=0) uniform texture2D tex;
layout(binding=0) uniform sampler smp;
layout(binding=1) uniform texture2D skylut;
layout(binding=1) uniform sampler lutsmp;
layout(binding=2) uniform texture2D landnrm;
layout(binding=3) uniform texture2D landsplat;
layout(binding=2) uniform sampler landsmp;
layout(binding=4) uniform texture2DArray hdalb;
layout(binding=5) uniform texture2DArray hdnrm;
layout(binding=6) uniform texture2DArray hdorm;
layout(binding=3) uniform sampler hdsmp;
layout(binding=7) uniform texture2D shadowmap;
@image_sample_type shadowmap depth
layout(binding=4) uniform sampler shadowsmp;
@sampler_type shadowsmp comparison
layout(binding=1) uniform lit_fs_params {
	vec4 material;      // x: kind (0 terrain, 1 block, 2 model), y: roughness, z: metallic, w: HD layer (-1 none)
	vec4 land;          // x: land size, y: clip below the water (reflection), z: underwater side, w: -
};
@include_block sky_fn
@include_block pbr_fn
@include_block shadow_fn

in vec2 uv;
in vec4 color;
in vec3 wpos;
in vec3 wnormal;
in float eyedepth;

out vec4 frag_color;

// HD layers: 0 grass, 1 rock, 2 sand, 3 snow, 4 dirt, 5 foam, 6..15 the blocks, 16 grass2
struct Mat {
	vec3 albedo;
	vec3 nrm;       // tangent space
	float ao;
	float rough;
	float height;
};

Mat hd_sample(vec2 p, float layer) {
	Mat m;
	m.albedo = texture(sampler2DArray(hdalb, hdsmp), vec3(p, layer)).rgb;
	m.nrm = texture(sampler2DArray(hdnrm, hdsmp), vec3(p, layer)).rgb * 2.0 - 1.0;
	vec3 orm = texture(sampler2DArray(hdorm, hdsmp), vec3(p, layer)).rgb;
	m.ao = orm.r;
	m.rough = orm.g;
	m.height = orm.b;
	return m;
}

// a layer at two scales, the second turned, mixed by a noise: no visible tiling
Mat hd_layer(vec2 p, float layer, float scale) {
	Mat a = hd_sample(p * scale, layer);
	vec2 q = vec2(p.x * 0.8 - p.y * 0.6, p.x * 0.6 + p.y * 0.8) * scale * 0.37 + 0.31;
	Mat b = hd_sample(q, layer);
	float k = smoothstep(0.3, 0.7, vnoise(p * 0.035));
	Mat m;
	m.albedo = mix(a.albedo, b.albedo, k);
	m.nrm = mix(a.nrm, b.nrm, k);
	m.ao = mix(a.ao, b.ao, k);
	m.rough = mix(a.rough, b.rough, k);
	m.height = mix(a.height, b.height, k);
	return m;
}

Mat mat_mix(Mat a, Mat b, float k) {
	Mat m;
	m.albedo = mix(a.albedo, b.albedo, k);
	m.nrm = mix(a.nrm, b.nrm, k);
	m.ao = mix(a.ao, b.ao, k);
	m.rough = mix(a.rough, b.rough, k);
	m.height = mix(a.height, b.height, k);
	return m;
}

// height blending: where two layers meet, the higher one shows (stones over sand)
float hblend(float wa, float ha, float wb, float hb) {
	float da = wa + ha * 0.6;
	float db = wb + hb * 0.6;
	float m = max(da, db) - 0.2;
	float ba = max(da - m, 0.0);
	float bb = max(db - m, 0.0);
	return bb / max(ba + bb, 1e-4);
}

// a normal map without tangents (Schuler): the frame from the derivatives
vec3 perturb(vec3 n, vec3 p, vec2 t, vec3 tn) {
	vec3 dp1 = dFdx(p);
	vec3 dp2 = dFdy(p);
	vec2 duv1 = dFdx(t);
	vec2 duv2 = dFdy(t);
	vec3 dp2perp = cross(dp2, n);
	vec3 dp1perp = cross(n, dp1);
	vec3 tt = dp2perp * duv1.x + dp1perp * duv2.x;
	vec3 bb = dp2perp * duv1.y + dp1perp * duv2.y;
	float invmax = inversesqrt(max(dot(tt, tt), dot(bb, bb)) + 1e-12);
	return normalize(mat3(tt * invmax, bb * invmax, n) * tn);
}

void main() {
	if (land.y > 0.5 && wpos.y < skyhorizon.w - 0.05) discard;
	vec3 v = normalize(eyepos.xyz - wpos);
	vec3 n;
	vec3 albedo;
	float rough = material.y;
	float metal = material.z;
	float ao = 1.0;
	int kind = int(material.x + 0.5);
	vec4 base = texture(sampler2D(tex, smp), uv);
	if (kind == 0) {
		// terrain: normal and splat weights from the land (computed on the CPU)
		vec2 lp = (wpos.xz + 0.5) / land.x;
		vec3 gn = normalize(texture(sampler2D(landnrm, landsmp), lp).xyz * 2.0 - 1.0);
		vec4 sp = texture(sampler2D(landsplat, landsmp), lp);
		// the map's own ground: its average color sets the hue, a white one is snow
		vec3 classic = srgb_to_linear(textureLod(sampler2D(tex, smp), uv, 7.0).rgb);
		float snowy = smoothstep(0.45, 0.7, min(classic.r, min(classic.g, classic.b)));
		vec2 p = wpos.xz;
		// two grasses, mixed at a large scale: no field looks the same twice
		Mat grass = mat_mix(hd_layer(p, 0.0, 0.17), hd_layer(p, 16.0, 0.15), smoothstep(0.35, 0.8, vnoise(p * 0.021 + 7.3)) * 0.7);
		Mat dirt = hd_layer(p, 4.0, 0.19);
		Mat sand = hd_layer(p, 2.0, 0.26);
		Mat snow = hd_layer(p, 3.0, 0.09);
		// rock: triplanar, the slopes are steep
		vec3 bw = pow(abs(gn), vec3(4.0));
		bw /= bw.x + bw.y + bw.z;
		Mat rx = hd_sample(wpos.zy * 0.23, 1.0);
		Mat ry = hd_sample(wpos.xz * 0.23, 1.0);
		Mat rz = hd_sample(wpos.xy * 0.23, 1.0);
		Mat rock = mat_mix(mat_mix(rx, rz, bw.z / max(bw.x + bw.z, 1e-4)), ry, bw.y);
		float dirtw = 1.0 - clamp(sp.r + sp.g + sp.b + sp.a, 0.0, 1.0);
		float snoww = max(sp.a, snowy * (1.0 - sp.g));
		// grass tinted like the map's ground (greener, yellower), less vivid than the photo
		vec3 hue = classic / max(dot(classic, vec3(0.333)), 1e-3);
		grass.albedo = grass_tint(grass.albedo, hue);
		dirt.albedo = mix(dirt.albedo, vec3(dot(dirt.albedo, vec3(0.3, 0.59, 0.11))), 0.6) * vec3(0.8, 0.72, 0.62);
		rock.albedo = mix(rock.albedo, vec3(dot(rock.albedo, vec3(0.3, 0.59, 0.11))), 0.3) * vec3(0.62, 0.6, 0.57);
		Mat m = grass;
		m = mat_mix(m, dirt, hblend(sp.r, grass.height, dirtw, dirt.height));
		m = mat_mix(m, sand, hblend(1.0 - sp.b, m.height, sp.b, sand.height));
		m = mat_mix(m, rock, hblend(1.0 - sp.g, m.height, sp.g, rock.height));
		m = mat_mix(m, snow, hblend(1.0 - snoww, m.height, snoww, snow.height));
		albedo = srgb_to_linear(m.albedo);
		// wet near the water's edge: darker and glossier
		float wet = 1.0 - smoothstep(0.0, 0.6, wpos.y - skyhorizon.w);
		albedo *= 1.0 - 0.4 * wet;
		rough = mix(m.rough, 0.25, wet);
		ao = m.ao;
		// the land's frame: x along u, z along v
		vec3 t = normalize(vec3(1.0, 0.0, 0.0) - gn * gn.x);
		vec3 b = normalize(cross(t, gn));
		n = normalize(gn + (t * m.nrm.x - b * m.nrm.y) * 0.8);
		metal = 0.0;
	}
	else {
		vec3 gn = wnormal;
		if (dot(gn, gn) < 1e-6) gn = cross(dFdx(wpos), dFdy(wpos));
		gn = normalize(gn);
		// faces seen from the camera's side (flat-shaded blocks: the derivatives' sign varies by API)
		if (dot(gn, v) < 0.0) gn = -gn;
		if (material.w < -1.5) {
			// no texture: the classic look came from the vertex light alone; dark metal
			albedo = vec3(0.05, 0.05, 0.055);
			metal = 0.7;
			rough = 0.38;
			n = gn;
		}
		else if (material.w >= 0.0) {
			Mat m = hd_sample(uv, material.w);
			albedo = srgb_to_linear(m.albedo);
			ao = m.ao;
			rough = m.rough;
			n = perturb(gn, wpos, uv, normalize(vec3(m.nrm.xy * 1.2, m.nrm.z)));
		}
		else {
			albedo = srgb_to_linear(base.rgb);
			// the vertex color was the classic light; only its hue stays (team tints, blue parts)
			vec3 tint = color.rgb / max(max(color.r, max(color.g, color.b)), 1e-3);
			albedo *= tint;
			// PBR from the classic texture (guns, players, objects): a bump from its
			// luminance; grey, darkish texels are metal (gun parts), coloured ones rough
			// (cloth, grips, skin)
			vec2 ts = 1.0 / vec2(textureSize(sampler2D(tex, smp), 0));
			float h0 = dot(base.rgb, vec3(0.3, 0.59, 0.11));
			float hx = dot(texture(sampler2D(tex, smp), uv + vec2(ts.x, 0.0)).rgb, vec3(0.3, 0.59, 0.11));
			float hy = dot(texture(sampler2D(tex, smp), uv + vec2(0.0, ts.y)).rgb, vec3(0.3, 0.59, 0.11));
			n = perturb(gn, wpos, uv, normalize(vec3((h0 - hx) * 2.5, (h0 - hy) * 2.5, 1.0)));
			float sat = max(base.r, max(base.g, base.b)) - min(base.r, min(base.g, base.b));
			float metallic = (1.0 - smoothstep(0.06, 0.16, sat)) * smoothstep(0.03, 0.12, h0) * (1.0 - smoothstep(0.55, 0.8, h0));
			metal = metallic * 0.8;
			rough = mix(mix(0.85, 0.6, smoothstep(0.2, 0.6, h0)), 0.32 + (1.0 - h0) * 0.15, metallic);
			// metal shows its color in its reflection: a little brighter base
			albedo = mix(albedo, albedo * 2.5 + 0.02, metallic * 0.6);
		}
	}
	float shadow = shadow_at(wpos, n, eyedepth);
	vec3 c = shade(albedo, rough, metal, ao, n, v, shadow);
	c = apply_fog(c, wpos);
	frag_color = vec4(c, base.a * color.a);
}
@end

@program lit lit_vs lit_fs

//---------------------------------------------------------------- shadow maps

@vs shadow_vs
layout(binding=0) uniform shadow_vs_params {
	mat4 mvp;
};

in vec3 position;

void main() {
	gl_Position = mvp * vec4(position, 1.0);
}
@end

@fs shadow_fs
void main() {
}
@end

@program shadow shadow_vs shadow_fs

//---------------------------------------------------------------- sky

@vs sky_vs
layout(binding=0) uniform sky_vs_params {
	mat4 ndctodir;      // clip (far plane) to a world direction
};

out vec3 dir;

void main() {
	vec2 p = vec2(float((gl_VertexIndex << 1) & 2), float(gl_VertexIndex & 2)) * 2.0 - 1.0;
	gl_Position = vec4(p, 0.0, 1.0);
	vec4 d = ndctodir * vec4(p, 1.0, 1.0);
	dir = d.xyz / d.w;
}
@end

@fs sky_fs
@include_block common
@include_block scene_ub
layout(binding=1) uniform texture2D skylut;
layout(binding=1) uniform sampler lutsmp;
layout(binding=15) uniform texture2D cloudtex;
layout(binding=11) uniform sampler cloudsmp;
layout(binding=1) uniform sky_fs_params {
	vec4 screen;        // xy: 1/size of the frame, z: volumetric clouds on
};
@include_block sky_fn

in vec3 dir;
out vec4 frag_color;

void main() {
	vec3 d = normalize(dir);
	vec3 c = sky_color(d);
	if (screen.z > 0.5) {
		// the clouds (a quarter of the size, filtered up) over the sky, the sun behind them
		vec4 cl = texture(sampler2D(cloudtex, cloudsmp), gl_FragCoord.xy * screen.xy);
		float cs0 = dot(d, sundir.xyz);
		float disk0 = smoothstep(0.99995, 0.99999, cs0) * 60.0 * sundir.w;
		c += suncolor.rgb * disk0 * (cl.a - 1.0);
		c = c * cl.a + cl.rgb;
		frag_color = vec4(groundcolor.w > 0.5 ? watercolor.rgb * 0.3 : c, 1.0);
		return;
	}
	float cs = dot(d, sundir.xyz);
	// the sun's glow in the haze, narrower than the LUT can hold
	c += suncolor.rgb * skyzenith.w * hg_phase(cs, 0.92) * 0.05;
	// the disk, darker at its edge
	float disk = smoothstep(0.99995, 0.99999, cs);
	float limb = sqrt(max(1.0 - (1.0 - cs) / (1.0 - 0.99995), 0.0));
	c += suncolor.rgb * disk * (0.4 + 0.6 * limb) * 60.0 * sundir.w;
	if (groundcolor.w > 0.5) c = watercolor.rgb * 0.3;
	frag_color = vec4(c, 1.0);
}
@end

@program sky sky_vs sky_fs

//---------------------------------------------------------------- water

@vs water_vs
layout(binding=0) uniform water_vs_params {
	mat4 viewproj;      // world to clip
	vec4 cam;           // xz: grid center, y: water height, w: time
	vec4 waves;         // x: amplitude scale, y: fade start, z: fade end, w: -
};

in vec2 position;       // the grid, around the camera

out vec3 wpos;
out vec3 wnormal;
out float crest;

const float PI = 3.14159265;

// Gerstner waves: direction xy, wavelength, steepness
vec3 gerstner(vec2 p, vec4 w, float t, inout vec3 tn, inout vec3 bn) {
	float k = 2.0 * PI / w.z;
	float c = sqrt(9.8 / k) * 0.6;
	vec2 d = normalize(w.xy);
	float f = k * (dot(d, p) - c * t);
	float a = w.w / k;
	tn += vec3(-d.x * d.x * (w.w * sin(f)), d.x * (w.w * cos(f)), -d.x * d.y * (w.w * sin(f)));
	bn += vec3(-d.x * d.y * (w.w * sin(f)), d.y * (w.w * cos(f)), -d.y * d.y * (w.w * sin(f)));
	return vec3(d.x * (a * cos(f)), a * sin(f), d.y * (a * cos(f)));
}

void main() {
	vec2 p = position + cam.xz;
	float dist = length(position);
	float amp = waves.x * (1.0 - smoothstep(waves.y, waves.z, dist));
	vec3 tn = vec3(1.0, 0.0, 0.0);
	vec3 bn = vec3(0.0, 0.0, 1.0);
	vec3 o = vec3(0.0);
	float t = cam.w;
	vec3 t0 = vec3(0.0);
	vec3 b0 = vec3(0.0);
	o += gerstner(p, vec4(1.0, 0.3, 9.0, 0.16), t, t0, b0);
	o += gerstner(p, vec4(0.6, 1.0, 5.3, 0.14), t, t0, b0);
	o += gerstner(p, vec4(-0.7, 0.5, 3.1, 0.12), t, t0, b0);
	o += gerstner(p, vec4(0.2, -1.0, 1.9, 0.10), t, t0, b0);
	o *= amp;
	tn += t0 * amp;
	bn += b0 * amp;
	wpos = vec3(p.x + o.x, cam.y + o.y, p.y + o.z);
	wnormal = normalize(cross(bn, tn));
	crest = o.y / max(waves.x * 0.25, 1e-3);
	gl_Position = viewproj * vec4(wpos, 1.0);
}
@end

@fs water_fs
@include_block common
@include_block scene_ub
layout(binding=8) uniform texture2D scenecolor;
layout(binding=9) uniform texture2D scenedepth;
@image_sample_type scenedepth unfilterable_float
layout(binding=10) uniform texture2D refl;
layout(binding=1) uniform texture2D skylut;
layout(binding=11) uniform texture2D wnormals;
layout(binding=4) uniform texture2DArray hdalb;
layout(binding=7) uniform texture2D shadowmap;
layout(binding=5) uniform sampler clampsmp;
layout(binding=6) uniform sampler pointsmp;
@sampler_type pointsmp nonfiltering
layout(binding=1) uniform sampler lutsmp;
layout(binding=7) uniform sampler repsmp;
layout(binding=4) uniform sampler shadowsmp;
layout(binding=1) uniform water_fs_params {
	vec4 screen;        // xy: 1/size of the frame, z: planar reflection on, w: -
	vec4 view;          // the camera's forward direction (eye depth of a point), w: -
};
@include_block sky_fn
@include_block shadow_fn

in vec3 wpos;
in vec3 wnormal;
in float crest;

out vec4 frag_color;

void main() {
	vec2 suv = gl_FragCoord.xy * screen.xy;
	vec3 toeye = eyepos.xyz - wpos;
	float dist = length(toeye);
	vec3 v = toeye / dist;
	float t = suncolor.w;
	// small waves: two scrolling normal maps over the Gerstner normal
	vec2 p = wpos.xz;
	vec3 n1 = texture(sampler2D(wnormals, repsmp), p * 0.11 + vec2(t * 0.021, t * 0.013)).xzy * 2.0 - 1.0;
	vec3 n2 = texture(sampler2D(wnormals, repsmp), p * 0.047 - vec2(t * 0.011, -t * 0.017)).xzy * 2.0 - 1.0;
	float detail = 1.0 - smoothstep(30.0, 160.0, dist) * 0.7;
	vec3 n = normalize(wnormal + (vec3(n1.x, 0.0, n1.z) * 0.55 + vec3(n2.x, 0.0, n2.z) * 0.45) * detail);
	bool below = groundcolor.w > 0.5;
	if (below) n = -n;
	float eyed = dot(-toeye, view.xyz);
	// what is under the surface, and how much water the light crossed
	vec2 off = n.xz * 0.035 / max(eyed * 0.08, 1.0);
	float sd = textureLod(sampler2D(scenedepth, pointsmp), suv + off, 0.0).r;
	if (sd < eyed) {
		off = vec2(0.0);
		sd = textureLod(sampler2D(scenedepth, pointsmp), suv, 0.0).r;
	}
	vec3 under = textureLod(sampler2D(scenecolor, clampsmp), suv + off, 0.0).rgb;
	float thick = max(sd - eyed, 0.0);
	// Beer-Lambert: red goes first; and the water's own color, lit by the sky
	vec3 absorb = exp(-thick * vec3(0.46, 0.09, 0.06) * watercolor.w);
	vec3 deep = watercolor.rgb * (suncolor.rgb * max(sundir.y, 0.0) * 0.2 + skyzenith.rgb * 0.6);
	vec3 refr = under * absorb + deep * (1.0 - exp(-thick * 0.35 * watercolor.w));
	float nv = max(dot(n, v), 0.0);
	float fres = 0.02 + 0.98 * pow(1.0 - nv, 5.0);
	vec3 r = reflect(-v, n);
	vec3 refl_c;
	if (below) {
		// from below: the sky through Snell's window, the water's color around it
		refl_c = deep * 0.6;
		fres = 1.0 - smoothstep(0.55, 0.75, nv);
		refr = sky_color(vec3(r.x, abs(r.y), r.z)) * 0.6;
		vec3 c = mix(refr, refl_c, fres);
		frag_color = vec4(apply_fog(c, wpos), 1.0);
		return;
	}
	if (screen.z > 0.5) {
		vec2 roff = n.xz * 0.06 / max(eyed * 0.05, 1.0);
		vec4 rp = textureLod(sampler2D(refl, clampsmp), suv + roff, 0.0);
		refl_c = mix(sky_color(vec3(r.x, max(r.y, 0.0), r.z)), rp.rgb, rp.a);
	}
	else
		refl_c = sky_color(vec3(r.x, max(r.y, 0.0), r.z));
	float shadow = shadow_at(wpos, vec3(0.0, 1.0, 0.0), eyed);
	// the sun's glint: a sharp GGX lobe
	vec3 h = normalize(sundir.xyz + v);
	float nh = max(dot(n, h), 0.0);
	float a = 0.012;
	float d = a * a / (PI * pow(nh * nh * (a * a - 1.0) + 1.0, 2.0));
	vec3 glint = suncolor.rgb * d * fres * 0.25 * max(dot(n, sundir.xyz), 0.0) * shadow;
	vec3 c = mix(refr, refl_c, fres) + glint;
	// foam: along the shore (thin water) and on the crests
	float foamtex = texture(sampler2DArray(hdalb, repsmp), vec3(p * 0.21 + vec2(t * 0.01, 0.0), 5.0)).r;
	float shore = 1.0 - smoothstep(0.0, 0.9, thick);
	float pulse = 0.6 + 0.4 * sin(t * 1.3 - thick * 6.0 + vnoise(p * 0.3) * 4.0);
	float foam = clamp(shore * pulse * 1.4 + max(crest - 0.75, 0.0) * 1.5, 0.0, 1.0) * smoothstep(0.35, 0.75, foamtex);
	foam *= 1.0 - smoothstep(60.0, 140.0, dist);
	vec3 foamc = (suncolor.rgb * max(sundir.y, 0.1) * shadow + hemi_sky()) * 0.75;
	c = mix(c, foamc, foam);
	// the water meets the shore softly
	c = mix(under, c, smoothstep(0.0, 0.08, thick));
	frag_color = vec4(apply_fog(c, wpos), 1.0);
}
@end

@program water water_vs water_fs

//---------------------------------------------------------------- grass

@vs grass_vs
@include_block common
layout(binding=0) uniform grass_vs_params {
	mat4 viewproj;
	vec4 cam;           // xyz: camera, w: the tile's size (an instance repeats every tile)
	vec4 params;        // x: time, y: outer radius, z: inner radius (the denser ring inside), w: blade size
	vec4 wind;          // xy: direction, z: strength, w: land size
	vec4 land;          // x: water height, y: -, z: -, w: -
};
layout(binding=12) uniform texture2D heights;
@image_sample_type heights unfilterable_float
layout(binding=13) uniform texture2D vsplat;
layout(binding=8) uniform sampler hsmp;
@sampler_type hsmp nonfiltering
layout(binding=9) uniform sampler vsmp;

in vec2 shape;          // x: across -1..1, y: up 0..1
in vec4 inst;           // xy: position in the tile, z: angle, w: random

out vec3 wpos;
out vec3 bnormal;
out float along;
out float rnd;

float height_at(vec2 p) {
	// the land as the near rings draw it: every 2 units, interpolated
	float size = wind.w;
	vec2 g = p * 0.5;
	vec2 i = floor(g);
	vec2 f = g - i;
	ivec2 a = ivec2(int(mod(i.x * 2.0, size)), int(mod(i.y * 2.0, size)));
	ivec2 b = ivec2(int(mod(i.x * 2.0 + 2.0, size)), int(mod(i.y * 2.0 + 2.0, size)));
	float h00 = texelFetch(sampler2D(heights, hsmp), ivec2(a.x, a.y), 0).r;
	float h10 = texelFetch(sampler2D(heights, hsmp), ivec2(b.x, a.y), 0).r;
	float h01 = texelFetch(sampler2D(heights, hsmp), ivec2(a.x, b.y), 0).r;
	float h11 = texelFetch(sampler2D(heights, hsmp), ivec2(b.x, b.y), 0).r;
	return mix(mix(h00, h10, f.x), mix(h01, h11, f.x), f.y);
}

void main() {
	float tile = cam.w;
	vec2 base = inst.xy + tile * floor((cam.xz - inst.xy) / tile + 0.5);
	float dist = length(base - cam.xz);
	float outer = params.y;
	float inner = params.z;
	// how much grass: the land's splat, thinning out with distance
	vec4 sp = textureLod(sampler2D(vsplat, vsmp), (base + 0.5) / wind.w, 0.0);
	float h = height_at(base);
	float dens = sp.r * (1.0 - smoothstep(outer * 0.7, outer, dist)) * (inner > 0.0 ? smoothstep(inner * 0.8, inner, dist) : 1.0);
	dens *= smoothstep(land.x + 0.4, land.x + 0.9, h);
	float r = inst.w;
	float size = params.w * (0.6 + 0.8 * r) * step(r * 0.85 + 0.1, dens);
	// the blade: tapered, bending with the wind (gusts travel along it)
	float t = params.x;
	float ang = inst.z;
	vec2 side = vec2(cos(ang), sin(ang));
	vec2 wdir = wind.xy;
	float gust = vnoise(base * 0.06 - wdir * t * 0.9) * 1.6;
	float sway = (sin(t * 2.1 + dot(base, wdir) * 0.6 + r * 6.28) * 0.35 + gust) * wind.z;
	float y = shape.y;
	float bend = sway * y * y;
	float width = 0.011 * (1.0 - y * 0.85) * (size / params.w);
	vec2 lean = wdir * bend * size * 0.6 + vec2(-side.y, side.x) * y * y * (r - 0.5) * 0.25 * size;
	vec3 p = vec3(base.x + side.x * shape.x * width + lean.x,
		h - 0.05 + y * size * (1.0 - 0.3 * abs(bend)),
		base.y + side.y * shape.x * width + lean.y);
	wpos = p;
	// facing the blade's side, tilted with the bend
	bnormal = normalize(vec3(-side.y, 0.6, side.x) + vec3(wdir.x, 0.0, wdir.y) * bend * 0.5);
	along = y;
	rnd = r;
	gl_Position = viewproj * vec4(p, 1.0);
	if (size <= 0.0) gl_Position = vec4(2.0, 2.0, 2.0, 1.0);
}
@end

@fs grass_fs
@include_block common
@include_block scene_ub
layout(binding=1) uniform texture2D skylut;
layout(binding=4) uniform texture2DArray hdalb;
layout(binding=7) uniform texture2D shadowmap;
layout(binding=1) uniform sampler lutsmp;
layout(binding=3) uniform sampler hdsmp;
layout(binding=4) uniform sampler shadowsmp;
layout(binding=1) uniform grass_fs_params {
	vec4 tint;          // rgb: the map's ground hue
	vec4 view;          // the camera's forward direction
};
@include_block sky_fn
@include_block shadow_fn

in vec3 wpos;
in vec3 bnormal;
in float along;
in float rnd;

out vec4 frag_color;

void main() {
	// the ground's own grass color under the blade, brighter towards the tip
	vec3 g = srgb_to_linear(textureLod(sampler2DArray(hdalb, hdsmp), vec3(wpos.xz * 0.17, 0.0), 5.0).rgb);
	g = grass_tint(g, tint.rgb);
	vec3 albedo = g * mix(0.55, 1.25, along) * mix(0.85, 1.15, rnd);
	albedo = mix(albedo, albedo * vec3(1.25, 1.15, 0.6), smoothstep(0.75, 1.0, along) * rnd);
	vec3 v = normalize(eyepos.xyz - wpos);
	vec3 n = normalize(bnormal);
	if (dot(n, v) < 0.0) n = -n;
	float eyed = dot(wpos - eyepos.xyz, view.xyz);
	float shadow = shadow_at(wpos, vec3(0.0, 1.0, 0.0), eyed);
	float nl = max(dot(n, sundir.xyz), 0.0) * 0.6 + 0.4 * max(sundir.y, 0.0);
	// light through the blade when the sun is behind it
	float trans = pow(max(dot(-v, sundir.xyz), 0.0), 4.0) * 0.6;
	vec3 ambient = hemi_sky() * albedo * mix(0.35, 1.0, along);
	vec3 c = albedo / PI * suncolor.rgb * (nl + trans) * shadow + ambient;
	// a little gloss along the blade
	vec3 h = normalize(sundir.xyz + v);
	c += suncolor.rgb * pow(max(dot(n, h), 0.0), 40.0) * 0.04 * shadow * along;
	frag_color = vec4(apply_fog(c, wpos), 1.0);
}
@end

@program grass grass_vs grass_fs

//---------------------------------------------------------------- post-processing

// a triangle over the target; the shaders read at gl_FragCoord / size
@vs post_vs
void main() {
	vec2 p = vec2(float((gl_VertexIndex << 1) & 2), float(gl_VertexIndex & 2)) * 2.0 - 1.0;
	gl_Position = vec4(p, 0.0, 1.0);
}
@end

// the scene so far into images the water reads: its color, and its depth as eye depth
@fs copy_fs
layout(binding=0) uniform texture2D srccolor;
layout(binding=1) uniform texture2D depth;
@image_sample_type depth unfilterable_float
@image_sample_type srccolor unfilterable_float
layout(binding=6) uniform sampler pointsmp;
layout(binding=0) uniform copy_params {
	vec4 screen;        // xy: 1/size, z: flip y (origin top left), w: clip z from -1 (GL)
	mat4 invproj;       // the scene's (API) projection's inverse
};

layout(location=0) out vec4 color;
layout(location=1) out vec4 lindepth;

void main() {
	vec2 uv = gl_FragCoord.xy * screen.xy;
	color = textureLod(sampler2D(srccolor, pointsmp), uv, 0.0);
	float d = textureLod(sampler2D(depth, pointsmp), uv, 0.0).r;
	vec2 ndc = vec2(uv.x * 2.0 - 1.0, screen.z > 0.5 ? 1.0 - uv.y * 2.0 : uv.y * 2.0 - 1.0);
	float z = screen.w > 0.5 ? d * 2.0 - 1.0 : d;
	vec4 e = invproj * vec4(ndc, z, 1.0);
	float ed = d >= 1.0 ? 1e6 : -e.z / e.w;
	lindepth = vec4(ed, 0.0, 0.0, 1.0);
}
@end

@program copy post_vs copy_fs

// bloom: a 13-tap box downsample (Jimenez, CoD AW), the first one weighted against fireflies
@fs down_fs
layout(binding=0) uniform texture2D src;
layout(binding=0) uniform sampler linsmp;
layout(binding=0) uniform down_params {
	vec4 texel;         // xy: 1/target size, zw: 1/source size
	vec4 opts;          // x: first pass (Karis average), y: brightness limit
};

out vec4 frag_color;

vec3 s(vec2 uv) {
	return min(textureLod(sampler2D(src, linsmp), uv, 0.0).rgb, vec3(opts.y));
}

float karis(vec3 c) {
	return 1.0 / (1.0 + dot(c, vec3(0.2126, 0.7152, 0.0722)) * 0.25);
}

void main() {
	vec2 uv = gl_FragCoord.xy * texel.xy;
	vec2 t = texel.zw;
	vec3 a = s(uv + t * vec2(-2.0, -2.0));
	vec3 b = s(uv + t * vec2(0.0, -2.0));
	vec3 c = s(uv + t * vec2(2.0, -2.0));
	vec3 d = s(uv + t * vec2(-2.0, 0.0));
	vec3 e = s(uv);
	vec3 f = s(uv + t * vec2(2.0, 0.0));
	vec3 g = s(uv + t * vec2(-2.0, 2.0));
	vec3 h = s(uv + t * vec2(0.0, 2.0));
	vec3 i = s(uv + t * vec2(2.0, 2.0));
	vec3 j = s(uv + t * vec2(-1.0, -1.0));
	vec3 k = s(uv + t * vec2(1.0, -1.0));
	vec3 l = s(uv + t * vec2(-1.0, 1.0));
	vec3 m = s(uv + t * vec2(1.0, 1.0));
	vec3 r;
	if (opts.x > 0.5) {
		vec3 g0 = (a + b + d + e) * 0.25;
		vec3 g1 = (b + c + e + f) * 0.25;
		vec3 g2 = (d + e + g + h) * 0.25;
		vec3 g3 = (e + f + h + i) * 0.25;
		vec3 g4 = (j + k + l + m) * 0.25;
		float w0 = karis(g0), w1 = karis(g1), w2 = karis(g2), w3 = karis(g3), w4 = karis(g4);
		r = (g0 * w0 * 0.125 + g1 * w1 * 0.125 + g2 * w2 * 0.125 + g3 * w3 * 0.125 + g4 * w4 * 0.5) /
			(w0 * 0.125 + w1 * 0.125 + w2 * 0.125 + w3 * 0.125 + w4 * 0.5);
	}
	else {
		r = e * 0.125 + (a + c + g + i) * 0.03125 + (b + d + f + h) * 0.0625 + (j + k + l + m) * 0.125;
	}
	frag_color = vec4(r, 1.0);
}
@end

@program down post_vs down_fs

// bloom: a 3x3 tent upsample, added to the bigger level (blending ONE, ONE)
@fs up_fs
layout(binding=0) uniform texture2D src;
layout(binding=0) uniform sampler linsmp;
layout(binding=0) uniform up_params {
	vec4 texel;         // xy: 1/target size, zw: 1/source size
	vec4 opts;          // x: radius in source texels, y: weight
};

out vec4 frag_color;

void main() {
	vec2 uv = gl_FragCoord.xy * texel.xy;
	vec2 t = texel.zw * opts.x;
	vec3 c = textureLod(sampler2D(src, linsmp), uv, 0.0).rgb * 4.0;
	c += (textureLod(sampler2D(src, linsmp), uv + vec2(-t.x, 0.0), 0.0).rgb +
		textureLod(sampler2D(src, linsmp), uv + vec2(t.x, 0.0), 0.0).rgb +
		textureLod(sampler2D(src, linsmp), uv + vec2(0.0, -t.y), 0.0).rgb +
		textureLod(sampler2D(src, linsmp), uv + vec2(0.0, t.y), 0.0).rgb) * 2.0;
	c += textureLod(sampler2D(src, linsmp), uv + vec2(-t.x, -t.y), 0.0).rgb +
		textureLod(sampler2D(src, linsmp), uv + vec2(t.x, -t.y), 0.0).rgb +
		textureLod(sampler2D(src, linsmp), uv + vec2(-t.x, t.y), 0.0).rgb +
		textureLod(sampler2D(src, linsmp), uv + vec2(t.x, t.y), 0.0).rgb;
	frag_color = vec4(c / 16.0 * opts.y, 1.0);
}
@end

@program up post_vs up_fs

// god rays: the sky's light around the sun, blurred towards the sun on the screen
// (where something hides the sky, nothing shines)
@fs rays_fs
layout(binding=0) uniform texture2D src;
layout(binding=1) uniform texture2D depth;
layout(binding=0) uniform sampler linsmp;
layout(binding=6) uniform sampler pointsmp;
layout(binding=0) uniform rays_params {
	vec4 texel;         // xy: 1/target size
	vec4 sun;           // xy: the sun on the screen (uv), z: strength, w: aspect (width/height)
	vec4 color;         // the sun's color
};

out vec4 frag_color;

void main() {
	vec2 uv = gl_FragCoord.xy * texel.xy;
	vec2 delta = (sun.xy - uv) / 48.0;
	float decay = 1.0;
	vec3 acc = vec3(0.0);
	vec2 p = uv + delta * fract(sin(dot(gl_FragCoord.xy, vec2(12.9898, 78.233))) * 43758.5453);
	for (int i = 0; i < 48; i++) {
		float d = textureLod(sampler2D(depth, pointsmp), p, 0.0).r;
		vec2 q = (p - sun.xy) * vec2(sun.w, 1.0);
		float around = exp(-dot(q, q) * 9.0);
		if (d > 9e5 && p.x > 0.0 && p.x < 1.0 && p.y > 0.0 && p.y < 1.0) {
			vec3 c = min(textureLod(sampler2D(src, linsmp), p, 0.0).rgb, vec3(8.0));
			acc += c * around * decay;
		}
		decay *= 0.965;
		p += delta;
	}
	frag_color = vec4(acc / 48.0 * sun.z * color.rgb, 1.0);
}
@end

@program rays post_vs rays_fs

// exposure, bloom, god rays, a little lens flare, ACES, to sRGB; the luma goes to
// alpha for FXAA
@fs composite_fs
layout(binding=0) uniform texture2D hdr;
layout(binding=1) uniform texture2D bloom;
layout(binding=2) uniform texture2D rays;
layout(binding=0) uniform sampler linsmp;
layout(binding=0) uniform composite_params {
	vec4 texel;         // xy: 1/size
	vec4 params;        // x: exposure, y: bloom, z: god rays on, w: underwater
	vec4 sun;           // xy: the sun on the screen, z: flare strength, w: aspect
	vec4 grade;         // rgb: white balance, w: vignette
};

out vec4 frag_color;

vec3 aces(vec3 x) {
	// Narkowicz's fit of the ACES filmic curve
	return clamp((x * (2.51 * x + 0.03)) / (x * (2.43 * x + 0.59) + 0.14), 0.0, 1.0);
}

void main() {
	vec2 uv = gl_FragCoord.xy * texel.xy;
	vec3 c = textureLod(sampler2D(hdr, linsmp), uv, 0.0).rgb;
	vec3 b = textureLod(sampler2D(bloom, linsmp), uv, 0.0).rgb;
	c = mix(c, b, params.y);
	if (params.z > 0.5) c += textureLod(sampler2D(rays, linsmp), uv, 0.0).rgb;
	// ghosts of the bright spots, mirrored through the center
	if (sun.z > 0.0) {
		vec2 g = vec2(1.0) - uv;
		vec2 gv = (vec2(0.5) - g) * 0.45;
		vec3 fl = vec3(0.0);
		for (int i = 0; i < 4; i++) {
			vec2 q = g + gv * float(i);
			float w = pow(1.0 - length(vec2(0.5) - q) / 0.7071, 6.0);
			vec3 tint = i == 1 ? vec3(0.3, 0.6, 1.0) : (i == 2 ? vec3(1.0, 0.6, 0.3) : vec3(0.7, 1.0, 0.6));
			fl += max(textureLod(sampler2D(bloom, linsmp), q, 0.0).rgb - 1.5, 0.0) * w * tint;
		}
		c += fl * sun.z;
	}
	c *= params.x * grade.rgb;
	if (params.w > 0.5) c *= vec3(0.75, 0.95, 1.05);
	vec2 vq = uv - 0.5;
	c *= 1.0 - grade.w * dot(vq, vq) * 1.6;
	c = aces(c);
	c = pow(c, vec3(1.0 / 2.2));
	frag_color = vec4(c, dot(c, vec3(0.299, 0.587, 0.114)));
}
@end

@program composite post_vs composite_fs

// FXAA 3.11 (Lottes), the quality preset's idea in fewer steps
@fs fxaa_fs
layout(binding=0) uniform texture2D src;
layout(binding=0) uniform sampler linsmp;
layout(binding=0) uniform fxaa_params {
	vec4 texel;         // xy: 1/size
};

out vec4 frag_color;

float luma(vec2 uv) {
	return textureLod(sampler2D(src, linsmp), uv, 0.0).a;
}

void main() {
	vec2 uv = gl_FragCoord.xy * texel.xy;
	vec2 t = texel.xy;
	vec4 center = textureLod(sampler2D(src, linsmp), uv, 0.0);
	float m = center.a;
	float n = luma(uv + vec2(0.0, -t.y));
	float s = luma(uv + vec2(0.0, t.y));
	float e = luma(uv + vec2(t.x, 0.0));
	float w = luma(uv + vec2(-t.x, 0.0));
	float lo = min(m, min(min(n, s), min(e, w)));
	float hi = max(m, max(max(n, s), max(e, w)));
	float range = hi - lo;
	if (range < max(0.0312, hi * 0.125)) {
		frag_color = vec4(center.rgb, 1.0);
		return;
	}
	float nw = luma(uv + vec2(-t.x, -t.y));
	float ne = luma(uv + vec2(t.x, -t.y));
	float sw = luma(uv + vec2(-t.x, t.y));
	float se = luma(uv + vec2(t.x, t.y));
	float edgeh = abs(nw + ne - 2.0 * n) + 2.0 * abs(w + e - 2.0 * m) + abs(sw + se - 2.0 * s);
	float edgev = abs(nw + sw - 2.0 * w) + 2.0 * abs(n + s - 2.0 * m) + abs(ne + se - 2.0 * e);
	bool horz = edgeh >= edgev;
	float l1 = horz ? n : w;
	float l2 = horz ? s : e;
	float g1 = abs(l1 - m);
	float g2 = abs(l2 - m);
	float step_ = horz ? t.y : t.x;
	float lavg;
	float grad;
	if (g1 >= g2) {
		step_ = -step_;
		lavg = (l1 + m) * 0.5;
		grad = g1;
	}
	else {
		lavg = (l2 + m) * 0.5;
		grad = g2;
	}
	vec2 p = uv;
	if (horz) p.y += step_ * 0.5; else p.x += step_ * 0.5;
	vec2 dir = horz ? vec2(t.x, 0.0) : vec2(0.0, t.y);
	vec2 p1 = p - dir;
	vec2 p2 = p + dir;
	float e1 = luma(p1) - lavg;
	float e2 = luma(p2) - lavg;
	bool d1 = abs(e1) >= grad * 0.25;
	bool d2 = abs(e2) >= grad * 0.25;
	const float steps[8] = float[8](1.5, 2.0, 2.0, 2.0, 4.0, 8.0, 8.0, 12.0);
	for (int i = 0; i < 8; i++) {
		if (d1 && d2) break;
		if (!d1) { p1 -= dir * steps[i]; e1 = luma(p1) - lavg; d1 = abs(e1) >= grad * 0.25; }
		if (!d2) { p2 += dir * steps[i]; e2 = luma(p2) - lavg; d2 = abs(e2) >= grad * 0.25; }
	}
	float dist1 = horz ? uv.x - p1.x : uv.y - p1.y;
	float dist2 = horz ? p2.x - uv.x : p2.y - uv.y;
	bool left = dist1 < dist2;
	float dmin = min(dist1, dist2);
	float len = dist1 + dist2;
	bool good = ((left ? e1 : e2) < 0.0) != (m - lavg < 0.0);
	float off = good ? 0.5 - dmin / len : 0.0;
	// subpixel aliasing
	float avg = (2.0 * (n + s + e + w) + nw + ne + sw + se) / 12.0;
	float sub = clamp(abs(avg - m) / range, 0.0, 1.0);
	sub = (-2.0 * sub + 3.0) * sub * sub;
	off = max(off, sub * sub * 0.75);
	vec2 f = uv;
	if (horz) f.y += off * step_; else f.x += off * step_;
	frag_color = vec4(textureLod(sampler2D(src, linsmp), f, 0.0).rgb, 1.0);
}
@end

@program fxaa post_vs fxaa_fs

//---------------------------------------------------------------- trees, bushes, rocks

// instanced meshes made by renderer_ultra.cpp, placed from the land (the same on
// every client); the wind is the grass's
@block veg_vs_body
layout(binding=0) uniform veg_vs_params {
	mat4 viewproj;
	vec4 cam;           // xyz: camera, w: time
	vec4 wind;          // xy: direction, z: strength
};

in vec3 position;
in vec3 normal0;
in vec3 attr;           // x: part (0 trunk, 1 leaves, 2 rock), y: occlusion, z: how much it sways
in vec4 inst0;          // xyz: where it stands, w: scale
in vec4 inst1;          // x: turn, y: tint, z: -, w: phase

vec3 veg_world(out vec3 n) {
	float c = cos(inst1.x);
	float s = sin(inst1.x);
	vec3 p = position * inst0.w;
	p = vec3(c * p.x - s * p.z, p.y, s * p.x + c * p.z);
	n = vec3(c * normal0.x - s * normal0.z, normal0.y, s * normal0.x + c * normal0.z);
	vec3 w = inst0.xyz + p;
	float t = cam.w;
	vec2 wd = wind.xy;
	float gust = 0.6 + 0.4 * sin(t * 0.9 + dot(inst0.xz, wd) * 0.05) * sin(t * 0.37 + inst0.x * 0.02);
	float sway = (sin(t * 1.4 + inst1.w) * 0.35 + gust) * wind.z * attr.z * attr.z * inst0.w * 0.12;
	// leaves flutter a little on their own
	float flutter = sin(t * 7.0 + dot(w, vec3(1.7, 2.3, 1.1))) * 0.025 * attr.z * step(0.5, attr.x) * step(attr.x, 1.5);
	w.xz += wd * sway + flutter;
	w.y += flutter;
	return w;
}
@end

@vs veg_vs
@include_block veg_vs_body
out vec3 wpos;
out vec3 wnormal;
out vec3 vattr;
out float tint;

void main() {
	vec3 n;
	vec3 w = veg_world(n);
	wpos = w;
	wnormal = n;
	vattr = attr;
	tint = inst1.y;
	gl_Position = viewproj * vec4(w, 1.0);
}
@end

@fs veg_fs
@include_block common
@include_block scene_ub
layout(binding=1) uniform texture2D skylut;
layout(binding=1) uniform sampler lutsmp;
layout(binding=4) uniform texture2DArray hdalb;
layout(binding=5) uniform texture2DArray hdnrm;
layout(binding=6) uniform texture2DArray hdorm;
layout(binding=3) uniform sampler hdsmp;
layout(binding=7) uniform texture2D shadowmap;
layout(binding=4) uniform sampler shadowsmp;
layout(binding=1) uniform veg_fs_params {
	vec4 view;          // the camera's forward direction; w: clip below the water (reflection)
};
@include_block sky_fn
@include_block pbr_fn
@include_block shadow_fn

in vec3 wpos;
in vec3 wnormal;
in vec3 vattr;
in float tint;

out vec4 frag_color;

vec3 tri_rock(vec3 p, vec3 n, out vec3 tn, out float rough, out float ao) {
	vec3 bw = pow(abs(n), vec3(4.0));
	bw /= bw.x + bw.y + bw.z;
	vec3 a = texture(sampler2DArray(hdalb, hdsmp), vec3(p.zy * 0.3, 1.0)).rgb * bw.x +
		texture(sampler2DArray(hdalb, hdsmp), vec3(p.xz * 0.3, 1.0)).rgb * bw.y +
		texture(sampler2DArray(hdalb, hdsmp), vec3(p.xy * 0.3, 1.0)).rgb * bw.z;
	vec3 nx = texture(sampler2DArray(hdnrm, hdsmp), vec3(p.zy * 0.3, 1.0)).rgb * 2.0 - 1.0;
	vec3 ny = texture(sampler2DArray(hdnrm, hdsmp), vec3(p.xz * 0.3, 1.0)).rgb * 2.0 - 1.0;
	vec3 nz = texture(sampler2DArray(hdnrm, hdsmp), vec3(p.xy * 0.3, 1.0)).rgb * 2.0 - 1.0;
	// whiteout blend of the three planes' normals
	vec3 n1 = vec3(nx.xy + n.zy, abs(nx.z) * n.x).zyx;
	vec3 n2 = vec3(ny.xy + n.xz, abs(ny.z) * n.y).xzy;
	vec3 n3 = vec3(nz.xy + n.xy, abs(nz.z) * n.z);
	tn = normalize(n1 * bw.x + n2 * bw.y + n3 * bw.z);
	vec3 orm = texture(sampler2DArray(hdorm, hdsmp), vec3(p.xz * 0.3, 1.0)).rgb;
	rough = orm.g;
	ao = orm.r;
	return a;
}

void main() {
	if (view.w > 0.5 && wpos.y < skyhorizon.w - 0.05) discard;
	vec3 v = normalize(eyepos.xyz - wpos);
	vec3 n = normalize(wnormal);
	if (dot(n, v) < 0.0 && vattr.x > 0.5 && vattr.x < 1.5) n = -n;
	float part = vattr.x;
	float ao = vattr.y;
	vec3 albedo;
	float rough;
	float trans = 0.0;
	if (part > 1.5) {
		// rocks: the land's HD rock, triplanar, with its normal map; moss on top
		vec3 tn;
		float tao;
		albedo = srgb_to_linear(tri_rock(wpos, n, tn, rough, tao));
		albedo = mix(albedo, vec3(dot(albedo, vec3(0.3, 0.59, 0.11))), 0.3) * mix(0.55, 0.8, tint);
		float moss = smoothstep(0.6, 0.9, n.y) * smoothstep(0.4, 0.7, vnoise(wpos.xz * 1.3));
		albedo = mix(albedo, vec3(0.05, 0.08, 0.025), moss * 0.7);
		n = tn;
		ao *= tao;
	}
	else if (part > 0.5) {
		// leaves: green, varied per plant and in patches, light through them
		float k = vnoise(wpos.xz * 4.3 + wpos.y * 3.7) * 0.6 + vnoise(wpos.xz * 1.1 - wpos.y * 0.9) * 0.4;
		vec3 g = mix(vec3(0.035, 0.07, 0.02), vec3(0.09, 0.13, 0.035), tint);
		albedo = g * mix(0.45, 1.5, k * k);
		rough = 0.65;
		trans = 0.35;
	}
	else {
		// bark: brown, in stripes along the trunk
		float k = vnoise(vec2(atan(wnormal.z, wnormal.x) * 4.0, wpos.y * 9.0));
		albedo = mix(vec3(0.05, 0.035, 0.025), vec3(0.11, 0.08, 0.055), k);
		rough = 0.9;
	}
	float eyed = dot(wpos - eyepos.xyz, view.xyz);
	float shadow = shadow_at(wpos, n, eyed);
	vec3 c = shade(albedo, rough, 0.0, ao, n, v, shadow);
	c += albedo * suncolor.rgb * trans * pow(max(dot(-v, sundir.xyz), 0.0), 3.0) * shadow;
	frag_color = vec4(apply_fog(c, wpos), 1.0);
}
@end

@program veg veg_vs veg_fs

@vs vegshadow_vs
@include_block veg_vs_body
void main() {
	vec3 n;
	vec3 w = veg_world(n);
	gl_Position = viewproj * vec4(w, 1.0);
}
@end

@program vegshadow vegshadow_vs shadow_fs

//---------------------------------------------------------------- volumetric clouds

// a layer of clouds, raymarched at a quarter of the frame's size: density from a
// tiling 3D noise (two octaves) carved by a coverage that drifts with the wind, lit
// by the sun through a few steps towards it and by the sky; the sky pass reads it
@fs clouds_fs
@include_block common
layout(binding=14) uniform texture3D noise3d;
layout(binding=10) uniform sampler noisesmp;
layout(binding=0) uniform clouds_params {
	mat4 ndctodir;      // clip (far plane) to a world direction
	vec4 texel;         // xy: 1/target size, zw: -
	vec4 eye;           // xyz: camera, w: time
	vec4 layer;         // x: base height, y: top height, z: coverage, w: density
	vec4 sun;           // xyz: towards the sun, w: -
	vec4 suncol;        // rgb: the sun's light
	vec4 ambient;       // rgb: the sky's light
	vec4 wind;          // xy: drift per second
};

out vec4 frag_color;

float cloud_density(vec3 p) {
	vec3 q = p + vec3(wind.x, 0.0, wind.y) * eye.w;
	float base = texture(sampler3D(noise3d, noisesmp), q * 0.0018).r;
	float detail = texture(sampler3D(noise3d, noisesmp), q * 0.0071 + vec3(0.3, 0.1, 0.7)).r;
	float h = clamp((p.y - layer.x) / (layer.y - layer.x), 0.0, 1.0);
	// rounder at the bottom, thinner at the top
	float shape = smoothstep(0.0, 0.15, h) * smoothstep(1.0, 0.55, h);
	float d = base * shape - (1.0 - layer.z);
	d = d - (1.0 - detail) * 0.18;
	return max(d, 0.0) * layer.w;
}

void main() {
	vec2 uv = gl_FragCoord.xy * texel.xy;
	vec2 ndc = uv * 2.0 - 1.0;
	vec4 d4 = ndctodir * vec4(ndc.x, texel.z > 0.5 ? -ndc.y : ndc.y, 1.0, 1.0);
	vec3 rd = normalize(d4.xyz / d4.w);
	vec3 ro = eye.xyz;
	if (rd.y < 0.01 && ro.y < layer.x) {
		frag_color = vec4(0.0, 0.0, 0.0, 1.0);
		return;
	}
	// the slab between base and top along the ray
	float t0 = (layer.x - ro.y) / rd.y;
	float t1 = (layer.y - ro.y) / rd.y;
	if (t0 > t1) { float tt = t0; t0 = t1; t1 = tt; }
	t0 = max(t0, 0.0);
	t1 = min(t1, 6000.0);
	if (t1 <= t0) {
		frag_color = vec4(0.0, 0.0, 0.0, 1.0);
		return;
	}
	const int steps = 40;
	float dt = (t1 - t0) / float(steps);
	float t = t0 + dt * hash12(gl_FragCoord.xy);
	float trans = 1.0;
	vec3 light = vec3(0.0);
	float cs = dot(rd, sun.xyz);
	// forward scattering towards the sun, and some back
	float g1 = 0.75;
	float g2 = -0.25;
	float phase = 0.7 * (1.0 - g1 * g1) / pow(1.0 + g1 * g1 - 2.0 * g1 * cs, 1.5) +
		0.3 * (1.0 - g2 * g2) / pow(1.0 + g2 * g2 - 2.0 * g2 * cs, 1.5);
	phase /= 4.0 * PI;
	for (int i = 0; i < steps; i++) {
		vec3 p = ro + rd * t;
		float dens = cloud_density(p);
		if (dens > 0.001) {
			// light reaching here: a few steps towards the sun
			float od = 0.0;
			for (int j = 1; j <= 4; j++) od += cloud_density(p + sun.xyz * (float(j) * 28.0));
			float tl = exp(-od * 28.0 * 1.2);
			float powder = 1.0 - exp(-dens * 60.0);
			float hfrac = clamp((p.y - layer.x) / (layer.y - layer.x), 0.0, 1.0);
			vec3 s = suncol.rgb * tl * phase * mix(1.0, powder * 2.0, 0.5) * 6.0 + ambient.rgb * mix(0.45, 1.0, hfrac);
			float a = exp(-dens * dt);
			light += trans * s * (1.0 - a);
			trans *= a;
			if (trans < 0.02) break;
		}
		t += dt;
	}
	// far clouds fade into the haze
	float fade = exp(-t0 * 0.00025);
	frag_color = vec4(light * fade, mix(1.0, trans, fade));
}
@end

@program clouds post_vs clouds_fs
