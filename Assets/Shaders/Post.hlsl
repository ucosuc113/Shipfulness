// El etalonado de Shipfulness: el ultimo toque sobre la imagen del MUNDO.
//
// Va con `uiOverPost` encendido en la escena (escena -> POST -> UI): la
// interfaz ya tiene su paleta exacta (Ui.h) y la consola la copia, y un
// vinyeteado encima de la UI oscureceria justo el panel del rincon, que es lo
// que hay que leer de un vistazo. Lo que este pase unifica es la ciudad, el
// agua y los efectos entre si, no la interfaz.
//
// Tres cosas, y ninguna cambia de que color es algo - solo como conviven:
//
//   1. SATURACION y CONTRASTE suaves alrededor del gris medio. El agua y las
//      casas salian lavadas (el render es casi todo blancos y un solo azul).
//   2. VIRADO por luminancia: las sombras hacia el azul del agua y las luces
//      hacia el calor del sol. Es lo que hace que la sombra de una casa sobre
//      el canal se lea como parte del agua y no como gris encima.
//   3. VINYETEADO ovalado y corto: solo las esquinas. Centra la mirada en el
//      barco sin que se note como un efecto.
//
//   UserParams[0]  x saturacion (1,1)  y contraste (1,06)
//                  z fuerza del virado (0,5)  w fuerza del vinyeteado (0,25)
//   UserParams[1]  rgb color de las sombras  (0,86 0,95 1,08)
//   UserParams[2]  rgb color de las luces    (1,04 1,0 0,94)
//
// Sin parametros (todo 0) cae a esos valores, asi que el .strmat puede quedar
// vacio y el pase hace lo mismo.

float3 PostOr(float3 v, float3 fallback) {
    return any(v != 0.0f) ? v : fallback;
}

float PostOr1(float v, float fallback) {
    return v > 0.0f ? v : fallback;
}

float4 PSMain(PSInput input) : SV_TARGET {
    float3 c = AlbedoTex.Sample(AlbedoSampler, input.uv).rgb;

    float saturation = PostOr1(UserParams[0].x, 1.1f);
    float contrast = PostOr1(UserParams[0].y, 1.06f);
    float toning = PostOr1(UserParams[0].z, 0.5f);
    float vignette = PostOr1(UserParams[0].w, 0.25f);
    float3 shadowTint = PostOr(UserParams[1].rgb, float3(0.86f, 0.95f, 1.08f));
    float3 lightTint = PostOr(UserParams[2].rgb, float3(1.04f, 1.0f, 0.94f));

    float lum = dot(c, float3(0.299f, 0.587f, 0.114f));
    c = lerp(lum.xxx, c, saturation);
    c = (c - 0.5f) * contrast + 0.5f;

    // El virado reparte por luminancia con una transicion ancha: con un corte
    // estrecho, el borde de una sombra mostraria la costura entre los dos
    // tintes.
    float split = smoothstep(0.15f, 0.75f, lum);
    float3 tint = lerp(shadowTint, lightTint, split);
    c = lerp(c, c * tint, toning);

    // El ovalo sigue la forma de la pantalla (la UV ya es 0..1 en los dos
    // ejes) y empieza tarde: a 0,55 del centro no hay nada.
    float2 d = input.uv - 0.5f;
    float r = length(d * 1.41421f);
    float v = smoothstep(0.55f, 1.15f, r);
    c *= 1.0f - vignette * v;

    return float4(saturate(c), 1.0f);
}
