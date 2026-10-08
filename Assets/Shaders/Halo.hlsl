float4 PSMain(PSInput input) : SV_TARGET {
    float u = saturate(input.uv.x);
    float t = Time.x;
    float3 water = UserParams[0].rgb;
    float3 shallow = UserParams[1].rgb;
    float shallowAmount = UserParams[1].w;
    float3 foamColor = UserParams[2].rgb;
    float foamWidth = UserParams[2].w;
    float breathe = 0.5f + 0.5f * sin(t * 1.4f + input.worldPos.x * 0.45f + input.worldPos.z * 0.31f);
    float edge = foamWidth * (0.75f + 0.5f * breathe);
    float foam = 1.0f - smoothstep(edge * 0.55f, edge, u);
    float ripple = smoothstep(0.02f, 0.0f, abs(u - (0.28f + 0.1f * breathe))) * 0.35f;
    float3 c = lerp(shallow, water, smoothstep(0.08f, 1.0f, u));
    c = lerp(c, foamColor, saturate(foam + ripple));
    float alpha = saturate(max(foam, shallowAmount * (1.0f - smoothstep(0.1f, 1.0f, u))));
    return float4(StraceLighting(c, input), alpha);
}
