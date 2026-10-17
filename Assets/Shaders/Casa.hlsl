// El pastel de un color es ESE color aclarado: se mezcla con blanco, que
// conserva el tono (la proporcion entre canales se acerca a 1 por igual) y
// solo baja la saturacion y sube la luz. Antes cada casa sacaba un pastel al
// azar de una paleta de siete (y su techo otro de cuatro), segun la posicion:
// la ciudad salia de colores que el modelo no tenia. Ahora una casa gris es
// un gris claro y una de ladrillo es un ladrillo claro, y nada mas.
float3 CasaPastel(float3 c, float amount) {
    return lerp(c, float3(1.0f, 1.0f, 1.0f), amount);
}

float4 PSMain(PSInput input) : SV_TARGET {
    float3 base = AlbedoColor.rgb;
    float3 tint = UserParams[0].rgb;
    float tintAmount = UserParams[0].w;
    float lift = UserParams[1].x;
    float desat = UserParams[1].y;
    float rimAmount = UserParams[1].z;
    float coolShadow = UserParams[1].w;
    float pastel = UserParams[2].x;  // 0 = el color tal cual, 1 = blanco
    float topDim = UserParams[2].y;
    float3 rimColor = UserParams[3].rgb;

    base = CasaPastel(base, pastel);
    // La normal de la CARA, no la interpolada (ver Consola.hlsl): con la suave,
    // los techos curvos de pocas caras llevaban un degradado que los barria
    // como un reflejo de metal. Todo lo de abajo (luz, techo aplanado, borde)
    // usa esta, asi cada triangulo es un solo plano de color.
    float3 faceN = normalize(cross(ddx(input.worldPos), ddy(input.worldPos)));
    float3 N = dot(faceN, input.worldNormal) < 0.0f ? -faceN : faceN;
    input.worldNormal = N;

    float lum = dot(base, float3(0.299f, 0.587f, 0.114f));
    base = lerp(base, lum.xxx, desat);
    base = lerp(base, float3(1.0f, 1.0f, 1.0f), lift);
    base = lerp(base, base * tint * 1.15f, tintAmount);

    float3 lit = StraceLighting(base, input);
    PSInput flat = input;
    flat.worldNormal = normalize(lerp(N, float3(0.0f, 1.0f, 0.0f), 0.85f));
    float3 litFlat = StraceLighting(base, flat);
    lit = lerp(lit, litFlat, UserParams[4].x * smoothstep(0.2f, 0.6f, N.y));

    float baseLum = max(dot(base, float3(0.299f, 0.587f, 0.114f)), 1e-3f);
    float shade = saturate(dot(lit, float3(0.299f, 0.587f, 0.114f)) / baseLum);
    lit = lerp(lit, lit * tint * 1.25f + tint * base * 0.08f, (1.0f - shade) * coolShadow);

    lit *= 1.0f - topDim * saturate(N.y);
    float3 V = normalize(CameraWorldPosPad.xyz - input.worldPos);
    float rim = pow(1.0f - saturate(dot(N, V)), 3.0f) * (1.0f - saturate(N.y));
    lit += rimColor * rim * rimAmount;

    return float4(lit, AlbedoColor.a);
}
