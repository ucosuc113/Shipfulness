float2 EstelaPoint(int i) {
    float4 v = UserParams[i / 2];
    return (i % 2 == 0) ? v.xy : v.zw;
}

float EstelaHash(float2 p) {
    p = frac(p * float2(123.34f, 456.21f));
    p += dot(p, p + 45.32f);
    return frac(p.x * p.y);
}

float EstelaStroke(float d, float width, float aa) {
    return (1.0f - smoothstep(width - aa, width + aa, d)) * saturate(width / aa);
}

float EstelaDots(float u, float off, float width, float k, float aa, float id, float cellLen) {
    float cu = u / cellLen;
    float cell = floor(cu);
    float h0 = EstelaHash(float2(cell, id));
    float h1 = EstelaHash(float2(cell + 5.3f, id + 2.1f));
    float h2 = EstelaHash(float2(cell + 9.7f, id + 4.6f));
    float h3 = EstelaHash(float2(cell + 1.9f, id + 8.2f));
    float along = (frac(cu) - (0.3f + 0.4f * h0)) * cellLen;
    float spray = 0.04f + 0.3f * k;
    float side = h3 > 0.5f ? 1.0f : -1.0f;
    float lateral = side * (width * 0.9f + h1 * h1 * spray);
    float rad = max(0.03f + 0.06f * h2 * (0.5f + k), aa * 0.7f);
    float present = step(0.95f - 0.75f * smoothstep(0.2f, 0.8f, k), EstelaHash(float2(cell + 3.3f, id + 6.4f)));
    return EstelaStroke(length(float2(along, off - lateral)), rad, aa) * present * (0.6f + 0.4f * h2);
}

float4 PSMain(PSInput input) : SV_TARGET {
    float2 p = input.worldPos.xz;
    float halfBeam = UserParams[7].x;
    float intensity = UserParams[7].z;
    float odometer = UserParams[7].w;

    float bestDist = 1e9f;
    float bestS = 0.0f;
    float bestSide = 1.0f;
    float run = 0.0f;
    float front = 1.0f;
    bool haveFront = false;
    float2 a = EstelaPoint(0);
    [unroll] for (int i = 0; i < 13; ++i) {
        float2 b = EstelaPoint(i + 1);
        float2 ab = b - a;
        float len = length(ab);
        if (len > 1e-3f) {
            float2 dir = ab / len;
            float raw = dot(p - a, dir);
            if (!haveFront) {
                front = raw;
                haveFront = true;
            }
            float along = clamp(raw, 0.0f, len);
            float2 q = a + dir * along;
            float dist = length(p - q);
            if (dist < bestDist) {
                bestDist = dist;
                bestS = run + along;
                float cross = dir.x * (p.y - a.y) - dir.y * (p.x - a.x);
                bestSide = cross >= 0.0f ? 1.0f : -1.0f;
            }
        }
        run += len;
        a = b;
    }
    float total = max(run, 1e-3f);
    float s = bestS;
    float k = s / total;
    float lat = bestDist * bestSide;
    float u = odometer - s;
    float aa = max(fwidth(lat), 1e-4f) * 1.1f;

    // Los brazos se ABREN: una estela de barco es una V (Kelvin, ~19,5 grados
    // por lado en aguas profundas). Antes se abrian 0,025 por metro - 1,4
    // grados -, o sea dos rieles paralelos detras del casco, y con la linea
    // del centro eran tres: se leia como esquis, no como agua apartada. Aqui
    // 0,3 (unos 17 grados): un poco menos que Kelvin, porque con la camara
    // cenital y la estela de 1,25 esloras los brazos a 19,5 se salen de la
    // pantalla antes de verse terminar. El script repite el numero
    // (WakeTrail::Update) para que el rectangulo de la malla los cubra.
    const float kArmTan = 0.3f;
    float spread = halfBeam * 0.85f + s * kArmTan;
    float growth = smoothstep(0.0f, 4.0f, s);
    float thin = 1.0f - smoothstep(0.55f, 0.97f, k);

    float trail = 0.0f;
    [unroll] for (int j = -1; j <= 1; j += 2) {
        float fj = (float)j;
        float off = lat - fj * spread;
        float ragged = 0.7f + 0.6f * StraceValueNoise(float2(u * 1.3f, fj * 5.1f + 3.0f));
        // Grueso y espumoso junto a la popa, fino y roto al final: el brazo se
        // gasta al abrirse, que es lo que lo hace leerse como una ola y no
        // como una raya pintada.
        float width = lerp(0.2f, 0.34f, growth) * lerp(1.0f, 0.45f, k) * thin *
                      lerp(1.0f, ragged, 0.6f + 0.4f * smoothstep(0.2f, 0.6f, k));
        // Junto a la popa el brazo no se corta: LATE en opacidad, y recien hacia
        // el final se rompe en trozos. Un brazo liso se leia como una raya
        // pintada; cortado desde el principio, como la linea discontinua de una
        // carretera (los cortes a frecuencia 1,7 salian casi regulares).
        float gaps = StraceValueNoise(float2(u * 0.7f, fj * 9.3f + 1.0f));
        float pulse = 0.55f + 0.45f * smoothstep(0.2f, 0.65f, gaps);
        float broken = smoothstep(0.2f + 0.45f * k, 0.3f + 0.45f * k, gaps);
        float cut = lerp(pulse, broken, smoothstep(0.35f, 0.85f, k));
        float stroke = EstelaStroke(abs(off), width, aa) * cut * 0.9f;
        float dotWidth = max(width, 0.08f);
        float dots = max(EstelaDots(u, off, dotWidth, k, aa, fj + 7.0f, 0.45f),
                         EstelaDots(u, off, dotWidth, k, aa, fj + 19.0f, 0.31f));
        trail = max(trail, max(stroke, dots));
    }

    // El centro ya no es una linea: es el agua que revuelve la helice, una
    // franja de espuma ROTA del ancho del casco que se abre un poco y se
    // deshace en manchas antes de la mitad de la estela. Las manchas salen de
    // ruido en (recorrido, lateral), asi quedan quietas en el agua mientras el
    // barco se aleja, como la espuma de verdad.
    float washHalf = halfBeam * 0.55f + s * 0.06f;
    float washFade = 1.0f - smoothstep(0.05f, 0.5f, k);
    // Frecuencias altas a proposito: con manchas del ancho del casco la franja
    // salia moteada como un dalmata.
    float churn = StraceValueNoise(float2(u * 1.8f, lat * 3.2f)) * 0.6f +
                  StraceValueNoise(float2(u * 4.1f, lat * 6.5f + 11.0f)) * 0.4f;
    float inWash = 1.0f - smoothstep(washHalf * 0.6f, washHalf + aa, abs(lat));
    float foamCut = lerp(0.35f, 0.75f, smoothstep(0.0f, 0.5f, k));
    float wash = smoothstep(foamCut, foamCut + 0.08f, churn) * inWash * washFade;
    trail = max(trail, wash * 0.75f);
    // Debajo de la espuma, un velo claro y suave del agua revuelta: la espuma
    // sola son manchas sueltas; con el velo se lee como una sola estela.
    trail = max(trail, inWash * washFade * 0.12f);

    float tail = 1.0f - smoothstep(0.9f, 1.0f, k);
    float ahead = smoothstep(-0.15f, 0.1f, front);
    float strength = saturate(intensity * 1.6f);
    return float4(0.97f, 0.99f, 1.0f, saturate(trail * tail * ahead * strength));
}
