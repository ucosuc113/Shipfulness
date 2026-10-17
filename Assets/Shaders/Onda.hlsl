float4 PSMain(PSInput input) : SV_TARGET {
    float2 center = UserParams[1].xy;
    float radius = max(UserParams[1].z, 0.01f);
    float prog = saturate(UserParams[0].x);
    float strength = UserParams[0].y;
    float width = UserParams[0].z > 0.0f ? UserParams[0].z : 0.18f;
    float seed = UserParams[0].w;
    float2 d = input.worldPos.xz - center;
    float r = length(d) / radius;
    float aa = max(fwidth(r), 1e-4f) * 1.2f;
    float band = smoothstep(1.0f - width - aa, 1.0f - width * 0.4f, r) * (1.0f - smoothstep(1.0f - aa, 1.0f + aa, r));
    float inner = (1.0f - smoothstep(0.0f, 0.95f, r)) * (1.0f - prog) * 0.55f;
    float ang = atan2(d.y, d.x);
    float n = StraceValueNoise(float2(ang * 3.0f + seed, r * 2.0f + seed * 0.37f));
    float m = StraceValueNoise(input.worldPos.xz * 1.3f + seed);
    float broken = smoothstep(prog * 0.75f - 0.05f, prog * 0.75f + 0.15f, n * 0.6f + m * 0.4f);
    float foam = max(band, inner * smoothstep(prog * 0.9f, prog * 0.9f + 0.3f, m)) * broken;
    float alpha = saturate(foam * strength);
    if (alpha < 0.01f) discard;
    return float4(UserParams[2].rgb, alpha);
}
