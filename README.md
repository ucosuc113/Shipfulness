# Shipfulness

Un barco demasiado grande por canales demasiado angostos. Llega a la meta antes
de que se acabe el tiempo y con el casco entero: cada choque lo rompe donde
pega, y un casco roto hace agua, se escora y termina hundiéndose.

Hecho con **S.T.R.AC.E**, un motor propio (Direct3D 11, Windows).

## Controles

| | |
|---|---|
| `W` / `S` (o flechas) | subir / bajar la marcha |
| `A` / `D` (o flechas) | timón |
| ratón | la consola de abajo: palanca, timón, bocina, cámara y pausa |
| rueda | acercar / alejar la cámara |
| `H` | bocina |
| `C` | cámara que sigue al barco / fija |
| `R` | reiniciar |
| `P` / `Esc` | pausa |

## Abrir y compilar

El proyecto se abre desde el Hub de S.T.R.AC.E (`Shipfulness.strproj`). El
motor no está en este repositorio.

- **Jugar en el Editor**: el botón Play. Los scripts (`Assets/Scripts/`) se
  compilan solos al entrar.
- **Compilar el juego**: Archivo > Build Settings > Compilar. Sale a
  `Build/Windows/`: `Shipfulness.exe`, `EngineCore.dll`, `Scripts.dll` y
  `Shipfulness.pak` (los datos del juego). Esa carpeta entera es lo que se
  reparte.

`Build/` e `Intermediate/` los genera el motor y no van al repositorio.

## Qué hay dónde

```
Assets/Scripts/   el juego (C++): ShipfulnessGame.cpp es el punto de entrada
  ShipSim.h       el barco: maniobra y choques
  HullDamage.h    el casco que se rompe, la colision que lo sigue y la integridad
  SceneLink.h     la escena: niveles, casas, obstaculos, estelas
  FillLayer.h     la ciudad que se genera alrededor del canal
  Console3D.h     la consola de mando 3D
  AudioDirector.h el sonido
Assets/Shaders/   agua, casco, casas, estela, efectos y el post-proceso
Assets/Modelos/   mallas del motor (.strmesh) y sus fuentes (.fbx)
Scenes/Main.strscene
```
