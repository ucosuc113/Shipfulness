// El casco del barco: la luz de siempre (la Suave, StraceLightingSoft) MENOS la
// sombra, y el adentro oscuro.
//
// POR QUE SIN SOMBRA. La sombra del barco la proyectan cajas invisibles (las
// "Sombra*" hijas del nodo Barco, en la escena), no
// los 27.000 triangulos del casco. Una caja que proyecta y que el casco RECIBE
// tiene que ir por dentro del casco - si no, oscurece la cubierta y la banda al
// sol -, y por dentro, con el sol a 55 grados, su sombra cae entera debajo del
// barco: no se ve ninguna. Asi que el casco no recibe, y las cajas pueden medir
// lo que mide el barco de verdad (hasta la borda, y la cabina entera). Lo que se
// pierde es la sombra de la cabina sobre la propia cubierta; nada mas proyecta
// al sol en este juego (las casas no: castSunShadows nace apagado).
//
// EL ADENTRO. El casco se dibuja sin descarte de caras para que por un agujero
// (ver HullDamage.h) se vea la otra banda por dentro y no el agua de atras. Esas
// caras se ven de espaldas: su normal apunta lejos de la camara. Se las da vuelta
// para iluminarlas, y se oscurecen (UserParams[0].x, 0,35 si es 0): un agujero
// tiene que leerse como un hueco, no como mas chapa.
//
// La normal es la del ARCHIVO (la interpolada), no la de la cara: el casco viene
// cortado en una grilla para poder romperlo (ver HullDamage.h), y con
// la normal de cada cara esos cortes se veian como astillas sobre la chapa lisa.
// La del archivo es la que Blender muestra, y donde el dano mueve vertices
// HullDamage la recalcula. La cara solo se usa para saber de que lado se mira.
float4 PSMain(PSInput input) : SV_TARGET {
    float3 N = normalize(input.worldNormal);
    float3 V = normalize(CameraWorldPosPad.xyz - input.worldPos);
    // De espaldas = la cara de adentro, vista por un agujero. Con la cara y no
    // con N: una normal interpolada en un borde puede apuntar a la camara
    // aunque la cara no.
    float3 face = normalize(cross(ddx(input.worldPos), ddy(input.worldPos)));
    face = dot(face, N) < 0.0f ? -face : face;
    float inside = dot(face, V) < 0.0f ? 1.0f : 0.0f;
    N = inside > 0.5f ? -N : N;
    float dark = UserParams[0].x > 0.0f ? UserParams[0].x : 0.35f;
    float3 albedo = AlbedoColor.rgb;

    // LA FRANJA (UserParams[1]: color, w = 1 encendida; UserParams[2].xy: de
    // que altura a que altura). En espacio LOCAL del casco, no del mundo: el
    // barco escora, cabecea y se hunde, y una franja a altura de mundo se
    // quedaria quieta mientras la chapa se mueve por debajo. Las filas de World
    // son los ejes del objeto (mul(p, World)); dividir por su largo deja la
    // altura en metros del modelo aunque el nodo tuviera escala. Solo en las
    // caras de COSTADO: en la cubierta o el fondo una franja horizontal no
    // tiene sentido, y con la normal casi vertical cortaria a traves.
    if (UserParams[1].w > 0.5f) {
        float3 rel = input.worldPos - World[3].xyz;
        float3 up = World[1].xyz;
        float ly = dot(rel, up) / max(dot(up, up), 1e-6f);
        float px = max(fwidth(ly), 1e-4f);
        float band = smoothstep(UserParams[2].x - px, UserParams[2].x + px, ly) *
                     (1.0f - smoothstep(UserParams[2].y - px, UserParams[2].y + px, ly));
        float flank = 1.0f - smoothstep(0.35f, 0.6f, abs(N.y));
        albedo = lerp(albedo, UserParams[1].rgb, band * flank);
    }

    // EL BORDE: las caras que la camara ve de canto se oscurecen
    // (UserParams[2].z, la fuerza). Con la camara casi cenital el costado
    // blanco del casco y la cubierta blanca eran el mismo blanco, y el barco
    // no tenia silueta contra la espuma de su propia estela.
    float rim = pow(1.0f - saturate(dot(N, V)), 3.0f);
    albedo *= 1.0f - UserParams[2].z * rim * (1.0f - inside);
    albedo *= lerp(1.0f, dark, inside);

    float3 result = albedo * StraceAmbient(N) + albedo * BakedLightPad.rgb + albedo * input.bakedIrradiance + EmissiveSelected.rgb;
    uint mask = LightMaskPad.x;
    int count = (int)LightCountPad.x;
    for (int i = 0; i < count; i++) {
        if ((mask & (1u << (uint)i)) == 0u) continue;
        GpuLight L = Lights[i];
        float3 lightColor = L.ColorIntensity.rgb * L.ColorIntensity.a;
        float3 toLight;
        float atten = StraceLightAtten(L, input.worldPos, toLight);
        float NdotL = saturate(dot(N, toLight));
        float3 H = normalize(toLight + V);
        float spec = NdotL > 0.0f ? pow(saturate(dot(N, H)), max(SpecularShininess.a, 1.0f)) : 0.0f;
        result += (albedo * lightColor * NdotL + SpecularShininess.rgb * lightColor * spec * (1.0f - inside)) * atten;
    }
    return float4(StraceTonemap(result * AmbientColorPad.a), AlbedoColor.a);
}
