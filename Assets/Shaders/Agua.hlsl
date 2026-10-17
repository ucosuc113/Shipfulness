#define STRACE_CUSTOM_VS

PSInput VSMain(VSInput input) {
    PSInput output;
    float u = saturate(input.position.x + 0.5f);
    float v = saturate(input.position.z + 0.5f);
    float2 nearEdge = lerp(UserParams[2].xy, UserParams[2].zw, u);
    float2 farEdge = lerp(UserParams[3].zw, UserParams[3].xy, u);
    float2 g = lerp(nearEdge, farEdge, v);
    bool fitted = any(UserParams[2] != 0.0f) || any(UserParams[3] != 0.0f);
    float3 P = fitted ? float3(g.x, 0.0f, g.y) : mul(float4(input.position, 1.0f), World).xyz;
    output.position = mul(float4(P, 1.0f), ViewProj);
    output.worldPos = P;
    output.worldNormal = float3(0.0f, 1.0f, 0.0f);
    output.uv = input.uv;
    output.bakedIrradiance = float3(0.0f, 0.0f, 0.0f);
    return output;
}

float AguaHash(float2 p) {
    p = frac(p * float2(123.34f, 456.21f));
    p += dot(p, p + 45.32f);
    return frac(p.x * p.y);
}

float AguaLight(PSInput input) {
    PSInput up = input;
    up.worldNormal = float3(0.0f, 1.0f, 0.0f);
    float3 l = StraceLighting(float3(1.0f, 1.0f, 1.0f), up);
    float reference = UserParams[1].x > 0.0f ? UserParams[1].x : 1.0f;
    float floorLight = UserParams[1].y > 0.0f ? UserParams[1].y : 0.62f;
    return lerp(floorLight, 1.0f, saturate(dot(l, float3(0.333f, 0.333f, 0.334f)) / reference));
}

// Las motas de una capa: una por celda (si su hash h2 pasa la densidad), con
// el centro en el 15-85% de la celda y el radio en `radius`.
//
// El agua cubre la pantalla entera, y este bucle era casi todo su costo: 3
// capas x 9 celdas x 4 hashes por pixel. Medido en el Player (Release,
// 1920x1080): el agua sola eran ~25 de los ~33 ms del frame.
//
// La poda de vecinas es EXACTA, no una aproximacion. Una mota de la celda
// vecina `o` tiene el centro dentro de la caja [o+0.15, o+0.85] (en celdas,
// relativo a la celda del pixel) y radio <= radius.y, y su cobertura es
// 1 - smoothstep(rad - aa, rad + aa, d): vale 0 en cuanto d >= rad + aa. Asi
// que si el pixel esta a mas de (radius.y + aa) de esa caja, la vecina aporta
// 0 a un max() que empieza en 0 - saltarla no cambia ni un bit. Lejos de la
// camara, donde aa crece, las vecinas vuelven a entrar solas. El margen de
// 0.01 celdas cubre el redondeo de (c - seed), que no es g + o exacto.
//
// [branch] y no un if suelto: si el compilador lo aplana, evalua las nueve
// igual y la poda no ahorra nada. Las derivadas (fwidth) se toman FUERA, en
// PSMain, para que el salto sea legal.
float AguaSpecks(float2 p, float cell, float density, float2 radius, float2 opacity, float seed, float aa, float t) {
    float2 g = floor(p / cell);
    float2 f = p / cell - g;
    float reach = (radius.y + aa) / cell + 0.01f;
    float reach2 = reach * reach;
    float best = 0.0f;
    [unroll] for (int y = -1; y <= 1; ++y) {
        [unroll] for (int x = -1; x <= 1; ++x) {
            float2 o = float2((float)x, (float)y);
            float2 gap = max(max(o + 0.15f - f, f - (o + 0.85f)), 0.0f);
            [branch] if (dot(gap, gap) < reach2) {
                float2 c = g + o + seed;
                float h2 = AguaHash(c + 41.9f);
                [branch] if (h2 <= density) {
                    float h0 = AguaHash(c);
                    float h1 = AguaHash(c + 17.3f);
                    float h3 = AguaHash(c + 73.1f);
                    float2 q = (c - seed + 0.15f + 0.7f * float2(h0, h1)) * cell;
                    float size = h3 * h3;
                    float rad = lerp(radius.x, radius.y, size);
                    float alpha = lerp(opacity.x, opacity.y, frac(h3 * 7.13f + h0 * 3.7f));
                    float life = 0.5f + 0.5f * sin(t * (0.25f + 0.5f * h1) + h0 * 6.283f);
                    alpha *= smoothstep(0.05f, 0.45f, life);
                    float d = length(p - q);
                    float cover = (1.0f - smoothstep(rad - aa, rad + aa, d)) * saturate(rad / aa);
                    best = max(best, cover * alpha);
                }
            }
        }
    }
    return best;
}

// La altura de las ondas para el brillo: dos capas de ruido que derivan en
// direcciones distintas, asi el patron no se lee como una textura que se
// arrastra. Solo se usa su PENDIENTE (AguaGlint), nunca se desplaza el plano.
float AguaRipple(float2 p, float t) {
    return StraceValueNoise(p * 0.45f + float2(t * 0.11f, t * 0.04f)) * 0.6f +
           StraceValueNoise(p * 1.3f + float2(-t * 0.07f, t * 0.13f)) * 0.4f;
}

// El brillo del sol sobre las ondas. Con la camara casi cenital y el sol a 40
// grados, el reflejo de espejo cae FUERA de la pantalla: lo que se ve son las
// facetas de las ondas inclinadas lo justo (unos 25 grados) para mandar el sol
// a la camara, o sea chispas sueltas - que es justo el aspecto que buscamos y
// no una mancha blanca. Exponente 700 y pendiente 1,1: con 260 y 1,6 el menu,
// que mira mas rasante, era un campo de confeti. La pendiente sale por diferencias finitas (tres
// evaluaciones de la altura) y no por ddx/ddy: con derivadas de pantalla las
// chispas cambiarian con la distancia a la camara.
//
// Devuelve el brillo ya multiplicado por el color del sol, y lo apaga `lit`
// (0 en la sombra de una casa): un destello dentro de la sombra delata que no
// es luz.
float3 AguaGlint(float2 p, float3 worldPos, float t, float lit) {
    int count = (int)LightCountPad.x;
    [loop] for (int i = 0; i < count; ++i) {
        GpuLight sun = Lights[i];
        if (sun.PositionType.w != 0.0f) continue;
        float3 L = -sun.DirectionRange.xyz;
        float3 V = normalize(CameraWorldPosPad.xyz - worldPos);
        const float e = 0.2f;
        float h0 = AguaRipple(p, t);
        float hx = AguaRipple(p + float2(e, 0.0f), t);
        float hz = AguaRipple(p + float2(0.0f, e), t);
        float slope = UserParams[5].x > 0.0f ? UserParams[5].x : 1.1f;
        float3 N = normalize(float3(-(hx - h0) / e * slope, 1.0f, -(hz - h0) / e * slope));
        float nh = saturate(dot(N, normalize(L + V)));
        float spark = pow(nh, 700.0f);
        float sheen = pow(nh, 12.0f) * 0.03f;
        float strength = UserParams[5].y > 0.0f ? UserParams[5].y : 1.4f;
        return sun.ColorIntensity.rgb * (spark * strength + sheen) * lit;
    }
    return float3(0.0f, 0.0f, 0.0f);
}

float4 PSMain(PSInput input) : SV_TARGET {
    float t = max(Time.x, UserParams[4].x);
    float2 p = input.worldPos.xz;
    float3 water = AlbedoColor.rgb;
    float drift = StraceValueNoise(p * 0.018f + float2(t * 0.01f, t * 0.007f));
    float3 col = water * (0.96f + 0.08f * drift);
    float3 V = normalize(input.worldPos - CameraWorldPosPad.xyz);
    float2 slant = V.xz / max(-V.y, 0.3f);
    float2 current = float2(t * 0.05f, t * 0.03f);
    float strength = UserParams[0].w > 0.0f ? UserParams[0].w : 0.85f;
    float speck = 0.0f;

    // Las motas, mas chicas y mas raras que antes (radios ~0,6x, densidad 0,35
    // -> 0,25): a ese tamano y todas redondas se leian como NIEVE cayendo
    // sobre el canal, y cerca de la camara salian como monedas. Ahora son
    // espuma suelta; el brillo de verdad lo pone AguaGlint.
    float2 q0 = p + current;
    float a0 = max(max(fwidth(q0.x), fwidth(q0.y)), 1e-4f);
    speck = max(speck, AguaSpecks(q0, 5.0f, 0.25f, float2(0.08f, 0.24f), float2(0.5f, 0.85f), 0.0f, a0, t));

    float2 q1 = p + slant * 0.9f + current * 0.6f;
    float a1 = max(max(fwidth(q1.x), fwidth(q1.y)), 1e-4f);
    speck = max(speck, AguaSpecks(q1, 4.0f, 0.25f, float2(0.06f, 0.15f), float2(0.25f, 0.5f), 31.7f, a1, t));

    float2 q2 = p + slant * 2.2f + current * 0.3f;
    float a2 = max(max(fwidth(q2.x), fwidth(q2.y)), 1e-4f);
    speck = max(speck, AguaSpecks(q2, 3.0f, 0.25f, float2(0.04f, 0.1f), float2(0.1f, 0.25f), 67.3f, a2, t));

    col = lerp(col, UserParams[0].rgb, saturate(speck * strength));
    float light = AguaLight(input);
    float floorLight = UserParams[1].y > 0.0f ? UserParams[1].y : 0.62f;
    float lit = saturate((light - floorLight) / max(1.0f - floorLight, 1e-3f));
    return float4(col * light + AguaGlint(p, input.worldPos, t, lit), 1.0f);
}
