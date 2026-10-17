// La caja de sombra del barco (ver BuildShip en GeneradorEscena): solo existe
// para el mapa de sombras del sol, que dibuja profundidad sin pasar por aqui.
// En la imagen no aporta nada: alfa 0 en un material transparente, y el color
// en cero por si alguna mezcla no multiplicara por el alfa.
float4 PSMain(PSInput input) : SV_TARGET {
    return float4(0.0f, 0.0f, 0.0f, 0.0f);
}
