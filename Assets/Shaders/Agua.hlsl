float AguaHash(float2 p) {
    p = frac(p * float2(123.34f, 456.21f));
    p += dot(p, p + 45.32f);
    return frac(p.x * p.y);
}

float AguaSpecks(float2 p, float cell, float radius, float seed, float t) {
    float2 g = floor(p / cell);
    float best = 0.0f;
    [unroll] for (int y = -1; y <= 1; ++y) {
        [unroll] for (int x = -1; x <= 1; ++x) {
            float2 c = g + float2((float)x, (float)y);
            float h0 = AguaHash(c + seed);
            float h1 = AguaHash(c + seed + 17.3f);
            float h2 = AguaHash(c + seed + 41.9f);
            float2 q = (c + 0.15f + 0.7f * float2(h0, h1)) * cell;
            float d = length(p - q);
            float twinkle = 0.5f + 0.5f * sin(t * (0.8f + 2.2f * h2) + h0 * 6.283f);
            float on = step(0.42f, h2);
            best = max(best, on * twinkle * saturate(1.0f - d / radius));
        }
    }
    return best;
}

float4 PSMain(PSInput input) : SV_TARGET {
    float t = Time.x;
    float2 p = input.worldPos.xz;
    float3 V = normalize(input.worldPos - CameraWorldPosPad.xyz);
    float slope = max(-V.y, 0.25f);
    float drift = StraceValueNoise(p * 0.03f + float2(t * 0.015f, t * 0.011f));
    float swell = StraceValueNoise(p * 0.11f + float2(-t * 0.05f, t * 0.04f));
    float3 deep = AlbedoColor.rgb;
    float3 col = deep * (0.95f + 0.07f * drift + 0.03f * swell);
    float3 lit = StraceLighting(col, input);
    float3 speckTint = UserParams[0].w > 0.0f ? UserParams[0].rgb : float3(0.85f, 0.95f, 1.0f);
    float strength = UserParams[0].w > 0.0f ? UserParams[0].w : 0.9f;
    float sparkle = 0.0f;
    [unroll] for (int k = 0; k < 3; ++k) {
        float depth = 0.4f + 1.3f * (float)k;
        float2 q = p + V.xz * (depth / slope);
        q += float2(t * 0.12f, t * 0.07f) * (k == 0 ? 1.0f : 0.35f);
        float layer = AguaSpecks(q, 2.4f + 0.8f * (float)k, 0.10f + 0.035f * (float)k, 13.1f * (float)k, t);
        sparkle += layer * (1.0f - 0.3f * (float)k);
    }
    float3 shade = lit / max(col, 1e-3f);
    float3 result = lit + speckTint * sparkle * strength * saturate(shade);
    return float4(result, 1.0f);
}
