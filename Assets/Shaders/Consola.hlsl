// La consola: un objeto del mundo que se lee como interfaz.
//
// Con StraceLighting la consola recibia el sol Y el ambiente de la escena, y
// la suma pasaba de 1: el naranja del timon salia amarillo palido y el azul de
// la tabla gris, que es exactamente como se ve un plastico con brillo aunque
// el especular este en cero. Y dependia de la escena: otro sol, otra consola.
// Aqui no entra ninguna luz de la escena. Las caras que miran a la camara (la
// tapa de cada pieza) dan el color del modelo TAL CUAL - el que el artista
// eligio, el de la referencia - y los costados bajan hacia UserParams[0].x, en
// planos: la normal es la de la cara (ver la nota del signo abajo), asi que un
// bisel de pocas caras es una escalera de tonos y no un degradado.
//
// UserParams[0]: x = brillo de lo que no mira a la luz (0..1),
//                y = cuanto viene la luz de ARRIBA en vez de la camara (0..1).
// Sin parametros (todo 0) cae a 0,55 y luz desde la camara.
float4 PSMain(PSInput input) : SV_TARGET {
    // La normal de la cara por derivadas; el signo del producto cruz depende
    // de la orientacion de pantalla, asi que se alinea con la interpolada.
    float3 face = normalize(cross(ddx(input.worldPos), ddy(input.worldPos)));
    float3 N = dot(face, input.worldNormal) < 0.0f ? -face : face;

    float3 V = normalize(CameraWorldPosPad.xyz - input.worldPos);
    float3 key = normalize(lerp(V, float3(0.0f, 1.0f, 0.0f), UserParams[0].y));
    float low = UserParams[0].x > 0.0f ? UserParams[0].x : 0.55f;

    float shade = lerp(low, 1.0f, saturate(dot(N, key)));
    return float4(AlbedoColor.rgb * shade + EmissiveSelected.rgb, AlbedoColor.a);
}
