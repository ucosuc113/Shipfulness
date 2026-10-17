float4 PSMain(PSInput input) : SV_TARGET {
    float3 N = normalize(input.worldNormal);
    float3 V = normalize(CameraWorldPosPad.xyz - input.worldPos);
    float facing = saturate(dot(N, V));
    float soft = UserParams[0].y > 0.0f ? UserParams[0].y : 0.6f;
    float edge = smoothstep(0.0f, soft, facing);
    float dissolve = UserParams[0].x;
    float n = StraceValueNoise(input.worldPos.xz * 1.7f + input.worldPos.y * 0.9f + UserParams[0].z);
    float keep = smoothstep(dissolve - 0.15f, dissolve + 0.05f, n * 0.85f + facing * 0.3f);
    PSInput up = input;
    up.worldNormal = normalize(N + float3(0.0f, 0.8f, 0.0f));
    float3 lit = StraceLighting(AlbedoColor.rgb, up);
    float self = UserParams[0].w;
    float3 col = lerp(lit, AlbedoColor.rgb, self);
    float alpha = AlbedoColor.a * edge * keep;
    if (alpha < 0.01f) discard;
    return float4(col, alpha);
}
