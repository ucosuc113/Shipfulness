#define STRACE_CUSTOM_VS

PSInput VSMain(VSInput input) {
    PSInput output;
    float3 P = mul(float4(input.position, 1.0f), World).xyz;
    float3 C = CameraWorldPosPad.xyz;
    float depthFrac = saturate(input.uv.x);
    float surface = 0.02f + 0.05f * (1.0f - depthFrac);
    float drop = max(C.y - P.y, 1e-3f);
    float t = (C.y - surface) / drop;
    float3 S = C + (P - C) * t;
    S.y = surface;
    output.position = mul(float4(S, 1.0f), ViewProj);
    output.worldPos = S;
    output.worldNormal = float3(0.0f, 1.0f, 0.0f);
    output.uv = input.uv;
    output.bakedIrradiance = float3(0.0f, 0.0f, 0.0f);
    return output;
}

float HaloStroke(float d, float center, float width, float aa) {
    return 1.0f - smoothstep(width, width + aa, abs(d - center));
}

float HaloHash(float2 p) {
    p = frac(p * float2(123.34f, 456.21f));
    p += dot(p, p + 45.32f);
    return frac(p.x * p.y);
}

float HaloBubbles(float2 p, float cell, float t, float aa) {
    float2 g = floor(p / cell);
    float best = 0.0f;
    [unroll] for (int y = -1; y <= 1; ++y) {
        [unroll] for (int x = -1; x <= 1; ++x) {
            float2 c = g + float2((float)x, (float)y);
            float h0 = HaloHash(c);
            float h1 = HaloHash(c + 17.3f);
            float h2 = HaloHash(c + 41.9f);
            if (h2 > 0.6f) continue;
            float2 q = (c + 0.15f + 0.7f * float2(h0, h1)) * cell;
            float rad = cell * lerp(0.12f, 0.3f, h2 / 0.6f);
            float pop = 0.5f + 0.5f * sin(t * (1.0f + 1.5f * h0) + h1 * 6.283f);
            rad *= 0.5f + 0.5f * pop;
            float d = length(p - q);
            best = max(best, (1.0f - smoothstep(rad - aa, rad + aa, d)) * lerp(0.5f, 1.0f, h1));
        }
    }
    return best;
}

// La misma luz que el agua (AguaLight en Agua.hlsl, con los mismos mandos en
// UserParams[4].y/.z): sin ella el halo no se enteraba de la sombra del sol, y
// con el agua poco profunda ya visible, el anillo turquesa brillaba igual
// dentro de la sombra de su propia casa - una mancha clara pegada al islote
// justo donde el agua de al lado se oscurece.
float HaloLight(PSInput input) {
    PSInput up = input;
    up.worldNormal = float3(0.0f, 1.0f, 0.0f);
    float3 l = StraceLighting(float3(1.0f, 1.0f, 1.0f), up);
    float reference = UserParams[4].y > 0.0f ? UserParams[4].y : 1.0f;
    float floorLight = UserParams[4].z > 0.0f ? UserParams[4].z : 0.62f;
    return lerp(floorLight, 1.0f, saturate(dot(l, float3(0.333f, 0.333f, 0.334f)) / reference));
}

float HaloSurge(float ph) {
    return smoothstep(0.0f, 0.06f, ph) * (1.0f - smoothstep(0.06f, 0.55f, ph));
}

float4 PSMain(PSInput input) : SV_TARGET {
    float d = saturate(input.uv.x);
    float2 p = input.worldPos.xz;
    float t = max(Time.x, UserParams[5].x);
    float3 water = UserParams[0].rgb;
    float3 top = UserParams[1].rgb;
    float3 foamColor = UserParams[2].rgb;
    float foamWidth = UserParams[2].w;
    float k = 1.0f - d;
    float fade = k * k * (3.0f - 2.0f * k);
    float3 c = lerp(water, top, saturate(fade * 1.15f));
    float opacity = UserParams[3].x > 0.0f ? UserParams[3].x : 1.0f;
    float alpha = pow(fade, 1.3f) * opacity;
    float travel = UserParams[3].y > 0.0f ? UserParams[3].y : 0.2f;
    float speed = UserParams[3].z > 0.0f ? UserParams[3].z : 0.3f;
    float px = max(fwidth(d), 1e-4f);
    float aa = px * 1.2f;
    float wpx = max(max(fwidth(p.x), fwidth(p.y)), 1e-4f);

    float n = StraceValueNoise(p * 0.25f + t * 0.02f);
    float surge = 0.0f;
    float rings = 0.0f;
    [unroll] for (int i = 0; i < 3; ++i) {
        float ph = frac(t * speed + (float)i / 3.0f + n * 0.7f);
        surge = max(surge, HaloSurge(ph));
        float r = 1.0f - ph;
        float center = foamWidth + travel * r * r;
        float width = max(foamWidth * lerp(0.9f, 0.45f, r), px * 1.1f);
        float gaps = StraceValueNoise(p * 0.9f + float2((float)i * 13.7f, t * 0.15f));
        float broken = smoothstep(0.15f + 0.4f * r, 0.3f + 0.4f * r, gaps);
        float life = smoothstep(1.0f, 0.75f, r) * smoothstep(0.0f, 0.12f, r);
        rings = max(rings, HaloStroke(d, center, width, aa) * broken * life);
    }

    float lace = StraceValueNoise(p * 2.2f + float2(t * 0.35f, -t * 0.27f));
    float reach = foamWidth * (0.6f + 2.4f * surge) * (0.7f + 0.6f * lace);
    float edgeWidth = max(reach, px * 1.5f);
    float foam = HaloStroke(d, 0.0f, edgeWidth, aa);

    float zone = 1.0f - smoothstep(edgeWidth, edgeWidth + foamWidth * (1.5f + 2.0f * surge), d);
    float2 bp = p + float2(sin(t * 0.7f + n * 6.283f), cos(t * 0.6f + n * 6.283f)) * 0.04f;
    float bubbles = HaloBubbles(bp, 0.22f, t, wpx * 1.2f) * zone * (0.5f + 0.5f * surge);
    foam = max(foam, bubbles);
    foam = max(foam, rings * 0.85f);

    c = lerp(c, foamColor, foam);
    alpha = max(alpha, foam);
    return float4(c * HaloLight(input), alpha);
}
