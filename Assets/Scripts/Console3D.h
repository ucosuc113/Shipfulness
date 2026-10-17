#pragma once

#include "Shipfulness.h"
#include "Ui.h"

#include "Engine/Rendering/Camera.h"
#include "Engine/Scene/Raycast.h"

#include <DirectXMath.h>

#include <cmath>
#include <functional>

namespace ship {

// La consola de mando 3D: los modelos de "Assets/Modelos/Consola" que
// GeneradorEscena cuelga de la camara (ver BuildConsole ahi). Esta es la
// mitad de juego: encontrar las piezas en la escena, animarlas con el estado
// de la simulacion y dejar que el mouse las TOQUE. Es el reemplazo del panel
// que DrawConsole() dibujaba con UiDrawList.
//
// EL MOUSE ES UN RAYCAST DE VERDAD, no rectangulos de pantalla: el cursor se
// desproyecta con la misma vista/proyeccion con la que se dibujo el frame
// (ComputeGameCameraView/Projection + ScreenPointToRay) y el rayo acierta la
// MALLA de la pieza (SceneService::Raycast, precision de triangulo). El
// filtro limita el rayo al subarbol de la consola: ni el barco ni las casas
// le quitan el clic a una perilla.
//
// ARRASTRAR SIN RAYCAST POR FRAME: una vez agarrada la palanca o el manubrio,
// el puntero deja de importar - se intersecta el rayo del mouse con el PLANO
// de la pieza (en el espacio local de la consola) y se usa esa coordenada.
// Es lo que el panel 2D hacia con atan2 alrededor del timon, traducido a 3D:
// agarrar un radio del manubrio y que el radio siga al cursor.
class Console3D {
public:
    static constexpr float kWheelMax = kPi;  // +-media vuelta, como el panel 2D

    // Avisos que el juego atiende con SU logica (audio, simulacion, pantallas).
    std::function<void(float)> orderChanged;  // la palanca cambio de marcha (int como float)
    std::function<void()> hornPressed;        // bocina, al presionar (como el panel 2D)
    std::function<void()> pausePressed;       // pausa, al soltar sobre el boton
    std::function<void()> cameraToggled;      // seguimiento de camara, al soltar
    std::function<void()> hoverChanged;       // el cursor entro en una pieza nueva

    bool ok = false;
    bool draggingWheel = false;

    bool Link(strace::GameObject* camera) {
        m_camera = camera;
        ok = false;
        if (!camera) return false;
        m_root = FindChild(*camera, "Consola");
        if (!m_root) return false;
        m_manubrio = FindChild(*m_root, "Manubrio");
        m_palanca = FindChild(*m_root, "Palanca");
        m_bocina = FindChild(*m_root, "Bocina");
        m_pausa = FindChild(*m_root, "Pausa");
        m_seguimiento = FindChild(*m_root, "Seguimiento");
        m_giro = FindChild(*m_root, "Giro");
        ok = m_manubrio && m_palanca && m_bocina && m_pausa && m_seguimiento && m_giro;
        if (!ok) return false;
        // Alturas de reposo de los botones: de aqui se hunden al presionar.
        m_bocinaY = m_bocina->transform.position[1];
        m_pausaY = m_pausa->transform.position[1];
        m_segY = m_seguimiento->transform.position[1];
        m_shownZ = ZOfOrder(m_order);
        m_rectBocina = RestRect(*m_bocina);
        m_rectPausa = RestRect(*m_pausa);
        m_rectSeg = RestRect(*m_seguimiento);
        return true;
    }

    // Los iconos de los botones, en pixeles sobre la consola (ver DrawIcons
    // abajo). Va despues de Update, en el mismo frame: la consola ya esta
    // anclada y la camara ya se movio.
    void DrawIcons(Ui& ui);

    void SetVisible(bool visible) {
        if (m_root) m_root->active = visible;
    }

    // El estado que la simulacion le presta a la consola, cada frame.
    void SetOrder(float order) { m_order = order; }
    void SetRudder(float rudder) { m_rudder = rudder; }
    void SetCameraFollow(bool follow) { m_follow = follow; }

    // El timon que el jugador deja girado (radianes, +-kWheelMax).
    float Wheel() const { return m_wheel; }
    void SetWheel(float w) { m_wheel = Clamp(w, -kWheelMax, kWheelMax); }
    void AddWheel(float d) { SetWheel(m_wheel + d); }
    void AutoCenter(float dt) { m_wheel = Approach(m_wheel, 0.0f, kWheelMax * 0.45f * dt); }
    void PulseHorn() { m_hornLit = 0.5f; }

    void Update(float dt, const Ui& ui, bool interactable);

private:
    enum class Part { None, Base, Lever, Wheel, Horn, Pause, Camera, Giro };
    enum class Drag { None, Lever, Wheel };

    // Geometria de la consola, en su espacio local (las mismas unidades con
    // las que se modelo).
    static constexpr float kLeverPlaneY = 0.14f;  // altura de la perilla: plano de arrastre
    static constexpr float kWheelPlaneY = 0.26f;  // plano del volante horizontal
    static constexpr float kAsternZ = -0.64f;     // orden -2 (todo atras): fondo del riel
    static constexpr float kAheadZ = 0.66f;        // orden +3 (todo adelante): tope del riel
    static constexpr float kLeverSteps = 5.0f;    // de -2 a 3 (ShipSim::kMin/kMaxOrder)
    static constexpr float kPressDepth = 0.03f;   // cuanto se hunde un boton
    // Barrido del señalizador con el timon a tope: el mismo +-45 grados que
    // marcaba el arco alrededor del timon en el panel 2D.
    static constexpr float kGiroBarridoDeg = 45.0f;

    // El anclaje abajo (ver Anchor). La base mide 3,61 x 2,12 m
    // (bounds de Consola_Base.strmesh); inclinada -65,7 grados, su largo se ve
    // en pantalla como 2,12 * sin(65,7) = 1,93 m, mas el grosor de la tabla y
    // el volante que asoma: 1,0 de media altura deja todo dentro.
    static constexpr float kAnchorZ = 9.0f;          // la profundidad de siempre
    static constexpr float kAnchorMargin = 0.06f;    // del borde, en fraccion de media altura
    static constexpr float kBaseHalfScreenH = 1.0f;

    // Los mismos realces del panel 2D (ver Ui.h), para que la consola no
    // cambie el idioma visual del juego.
    static constexpr float kNaranja[3] = {0.93f, 0.56f, 0.21f};
    static constexpr float kNaranjaClaro[3] = {1.0f, 0.78f, 0.45f};
    static constexpr float kPapel[3] = {0.98f, 0.97f, 0.93f};
    static constexpr float kAceroOscuro[3] = {0.36f, 0.39f, 0.47f};

    static float ZOfOrder(float order) { return Lerp(kAsternZ, kAheadZ, (order + 2.0f) / kLeverSteps); }
    static float OrderOfZ(float z) { return (z - kAsternZ) / (kAheadZ - kAsternZ) * kLeverSteps - 2.0f; }

    // Es `g` la consola o algo que cuelga de ella? Es el filtro del raycast:
    // lo que no cuelga de la consola no le quita el clic a una perilla.
    static bool BajoDe(const strace::GameObject* g, const strace::GameObject* root) {
        for (; g; g = g->parent)
            if (g == root) return true;
        return false;
    }

    Part De(const strace::GameObject* g) const {
        if (g == m_palanca) return Part::Lever;
        if (g == m_manubrio) return Part::Wheel;
        if (g == m_bocina) return Part::Horn;
        if (g == m_pausa) return Part::Pause;
        if (g == m_seguimiento) return Part::Camera;
        if (g == m_giro) return Part::Giro;
        return BajoDe(g, m_root) ? Part::Base : Part::None;
    }

    // LA CONSOLA CENTRADA ABAJO, como el puente de un barco visto desde atras.
    // Estuvo en la esquina inferior derecha porque centrada tapaba la popa;
    // ahora es al reves: CameraRig deja la popa por ENCIMA de la consola
    // (kSternScreen), asi que el barco sale justo delante del timon. Se ancla
    // cada frame desde el borde de abajo visible a la profundidad kAnchorZ:
    // la altura de ese borde depende del fov, y el anclaje no se afina a ojo.
    //
    // Es la hija de la camara (ver BuildConsole en GeneradorEscena): su
    // posicion local ES su posicion en la vista, y el borde visible a una
    // profundidad z esta a z * tan(fov/2) del centro. La inclinacion y la
    // escala son las de la escena; aqui solo se mueve.
    void Anchor() {
        if (!m_root || !m_camera || !m_camera->camera) return;
        const float halfH = kAnchorZ * std::tan(m_camera->camera->fovYDegrees * 0.5f / kDeg);
        const float s = m_root->transform.scale[0];
        const float margin = halfH * kAnchorMargin;
        m_root->transform.position[0] = 0.0f;
        m_root->transform.position[1] = -halfH + kBaseHalfScreenH * s + margin;
        m_root->transform.position[2] = kAnchorZ;
    }

    // Del cursor al rayo del mundo, con la MISMA vista/proyeccion con la que
    // se dibujo el frame - o el clic sale del pixel de al lado (ver
    // Raycast.h, que lo pide con todas las letras).
    bool MouseRay(const Ui& ui, strace::Ray& out) const {
        if (!m_camera || !m_camera->camera) return false;
        float w = ui.W(), h = ui.H();
        if (w <= 0.0f || h <= 0.0f) return false;
        DirectX::XMMATRIX view = strace::ComputeGameCameraView(*m_camera);
        DirectX::XMMATRIX proj =
            strace::ComputeGameCameraProjection(*m_camera->camera, strace::SceneService::Type(), w / h);
        return strace::ScreenPointToRay(ui.mouse.x, ui.mouse.y, w, h, view * proj, out);
    }

    // El punto donde el rayo del mouse corta el plano y=altura del espacio
    // LOCAL de la consola: es la coordenada con la que se arrastra una pieza
    // aunque el cursor ya no este encima de su malla.
    bool LocalPlanePoint(const strace::Ray& ray, float y, DirectX::XMFLOAT3& out) const {
        if (!m_root) return false;
        DirectX::XMMATRIX inv = DirectX::XMMatrixInverse(nullptr, strace::ComputeWorldMatrix(*m_root));
        DirectX::XMFLOAT3 o, d;
        DirectX::XMStoreFloat3(&o, DirectX::XMVector3TransformCoord(DirectX::XMLoadFloat3(&ray.origin), inv));
        DirectX::XMStoreFloat3(&d, DirectX::XMVector3TransformNormal(DirectX::XMLoadFloat3(&ray.direction), inv));
        if (std::fabs(d.y) < 1e-5f) return false;
        float t = (y - o.y) / d.y;
        if (t < 0.0f) return false;
        out = {o.x + d.x * t, y, o.z + d.z * t};
        return true;
    }

    // EL AREA DE CLIC DE UN BOTON ES SU RECTANGULO EN REPOSO, no su malla.
    //
    // Con el raycast contra la malla, apretar un boton lo rompia: la tapa
    // sobresale 0,028 de la tabla y se hunde 0,03 (kPressDepth), asi que
    // mientras estaba apretada quedaba DEBAJO de la superficie, el rayo
    // acertaba la tabla, y al soltar "el cursor ya no estaba sobre el boton" -
    // pausa y camara se cancelaban casi siempre. Ademas la tapa es mas chica
    // que su hueco, y un clic en el borde gris tampoco contaba.
    //
    // Asi que cada boton es el rectangulo XZ de su malla (en el espacio de la
    // consola, con la posicion de REPOSO) mas kHitMargin, a la altura de su
    // tapa: el rayo se corta con ese plano y se pregunta si cae adentro. No
    // depende de cuanto este hundido, y el margen cubre el hueco.
    struct ButtonRect {
        float x0 = 0, x1 = 0, z0 = 0, z1 = 0, top = 0;
        bool valid = false;
    };
    static constexpr float kHitMargin = 0.04f;

    static ButtonRect RestRect(const strace::GameObject& n) {
        ButtonRect r;
        strace::MeshCache* cache = strace::SceneService::Meshes();
        if (!cache || n.meshPath.empty()) return r;
        auto res = cache->Resolve(strace::SceneService::ProjectRoot(), n.meshPath);
        if (!res.mesh) return r;
        const float* p = n.transform.position;
        r.x0 = p[0] + res.mesh->boundsMin.x - kHitMargin;
        r.x1 = p[0] + res.mesh->boundsMax.x + kHitMargin;
        r.z0 = p[2] + res.mesh->boundsMin.z - kHitMargin;
        r.z1 = p[2] + res.mesh->boundsMax.z + kHitMargin;
        r.top = p[1] + res.mesh->boundsMax.y;
        r.valid = true;
        return r;
    }

    bool Inside(const strace::Ray& ray, const ButtonRect& r) const {
        DirectX::XMFLOAT3 q;
        return r.valid && LocalPlanePoint(ray, r.top, q) && q.x >= r.x0 && q.x <= r.x1 && q.z >= r.z0 && q.z <= r.z1;
    }

    // Un punto del espacio de la consola a pixeles de la UI, con la misma
    // vista/proyeccion que MouseRay: el icono cae donde el clic responde.
    bool ToScreen(const Ui& ui, float x, float y, float z, float& sx, float& sy) const {
        if (!m_root || !m_camera || !m_camera->camera) return false;
        float w = ui.W(), h = ui.H();
        DirectX::XMMATRIX view = strace::ComputeGameCameraView(*m_camera);
        DirectX::XMMATRIX proj =
            strace::ComputeGameCameraProjection(*m_camera->camera, strace::SceneService::Type(), w / h);
        DirectX::XMVECTOR pw = DirectX::XMVector3TransformCoord(DirectX::XMVectorSet(x, y, z, 1.0f), strace::ComputeWorldMatrix(*m_root));
        DirectX::XMVECTOR clip = DirectX::XMVector4Transform(DirectX::XMVectorSetW(pw, 1.0f), view * proj);
        float cw = DirectX::XMVectorGetW(clip);
        if (cw <= 1e-4f) return false;
        sx = (DirectX::XMVectorGetX(clip) / cw * 0.5f + 0.5f) * w;
        sy = (0.5f - DirectX::XMVectorGetY(clip) / cw * 0.5f) * h;
        return true;
    }

    // Realce por instancia: UniqueMaterial copia el color con el que el
    // artista pinto la submalla y a partir de ahi es de ESTE boton (ver
    // SceneService.h) - la escena guardada no se entera.
    static void Tint(strace::GameObject& n, const float c[3]) {
        if (strace::Material* m = strace::SceneService::UniqueMaterial(n)) {
            m->albedoColor.x = c[0];
            m->albedoColor.y = c[1];
            m->albedoColor.z = c[2];
        }
    }

    strace::GameObject* m_root = nullptr;
    strace::GameObject* m_manubrio = nullptr;
    strace::GameObject* m_palanca = nullptr;
    strace::GameObject* m_bocina = nullptr;
    strace::GameObject* m_pausa = nullptr;
    strace::GameObject* m_seguimiento = nullptr;
    strace::GameObject* m_giro = nullptr;
    strace::GameObject* m_camera = nullptr;

    float m_wheel = 0.0f;
    float m_order = 0.0f;
    float m_rudder = 0.0f;
    float m_hornLit = 0.0f;
    bool m_follow = true;
    Part m_hover = Part::None;
    Drag m_drag = Drag::None;
    float m_grab = 0.0f;
    float m_dragOrder = 0.0f;
    bool m_pressBocina = false, m_pressPausa = false, m_pressSeg = false;
    float m_shownZ = 0.0f;
    float m_bocinaY = 0.0f, m_pausaY = 0.0f, m_segY = 0.0f;
    ButtonRect m_rectBocina, m_rectPausa, m_rectSeg;
};

// Un frame de la consola: que pieza esta bajo el cursor, que se agarra,
// arrastra y suelta, y como se mueve cada pieza con el resultado.
inline void Console3D::Update(float dt, const Ui& ui, bool interactable) {
    if (!ok || !m_root->active) return;
    Anchor();
    m_hornLit = std::max(0.0f, m_hornLit - dt);

    // --- Que pieza esta bajo el cursor -----------------------------------
    strace::Ray ray{};
    Part hover = Part::None;
    bool haveRay = interactable && MouseRay(ui, ray);
    if (haveRay) {
        strace::RaycastHit hit;
        strace::RaycastOptions opt;
        opt.maxDistance = 80.0f;
        opt.filter = [this](const strace::GameObject& g) { return BajoDe(&g, m_root); };
        if (strace::SceneService::Raycast(ray, hit, opt)) hover = De(hit.object);
        // Los botones por su rectangulo de reposo (ver ButtonRect), y solo
        // cuando el rayo no esta sobre una pieza que se arrastra: el volante y
        // la palanca no se pisan con los botones, pero si algun dia se
        // tocaran, agarrar gana.
        if (hover == Part::None || hover == Part::Base || hover == Part::Horn || hover == Part::Pause ||
            hover == Part::Camera) {
            if (Inside(ray, m_rectPausa)) hover = Part::Pause;
            else if (Inside(ray, m_rectSeg)) hover = Part::Camera;
            else if (Inside(ray, m_rectBocina)) hover = Part::Horn;
            else if (hover != Part::None) hover = Part::Base;
        }
    }
    if (hover != m_hover) {
        if (hover != Part::None && hoverChanged) hoverChanged();
        m_hover = hover;
    }

    // --- Agarrar, arrastrar, soltar ---------------------------------------
    if (interactable) {
        if (ui.mouse.pressed) {
            switch (m_hover) {
            case Part::Lever: m_drag = Drag::Lever; break;
            case Part::Wheel: {
                m_drag = Drag::Wheel;
                DirectX::XMFLOAT3 p;
                if (LocalPlanePoint(ray, kWheelPlaneY, p))
                    m_grab = std::atan2(p.x, p.z - m_manubrio->transform.position[2]);
                break;
            }
            case Part::Horn:
                m_pressBocina = true;
                m_hornLit = 0.5f;
                if (hornPressed) hornPressed();
                break;
            case Part::Pause: m_pressPausa = true; break;
            case Part::Camera: m_pressSeg = true; break;
            default: break;
            }
        }
        if (m_drag == Drag::Lever) {
            DirectX::XMFLOAT3 p;
            if (LocalPlanePoint(ray, kLeverPlaneY, p)) {
                // El riel de la base mide de kAsternZ a kAheadZ; empujar la
                // palanca hacia adelante acelera, como en una maquina de
                // verdad. La marcha se asienta en la muesca mas cercana.
                m_dragOrder = OrderOfZ(Clamp(p.z, kAsternZ, kAheadZ));
                float snapped = std::round(m_dragOrder);
                if (snapped != m_order) {
                    m_order = snapped;
                    if (orderChanged) orderChanged(snapped);
                }
            }
        } else if (m_drag == Drag::Wheel) {
            DirectX::XMFLOAT3 p;
            if (LocalPlanePoint(ray, kWheelPlaneY, p)) {
                // El radio agarrado tiene que seguir al cursor: el angulo del
                // hit alrededor del eje, menos el de agarre, es cuanto giro
                // el manubrio desde la ultima vez.
                float a = std::atan2(p.x, p.z - m_manubrio->transform.position[2]);
                SetWheel(m_wheel + WrapAngle(a - m_grab));
                m_grab = a;
            }
        }
        if (ui.mouse.released) {
            // Como el panel 2D: pausa y camara se confirman al SOLTAR, y solo
            // si el cursor sigue encima del boton.
            if (m_pressPausa && m_hover == Part::Pause && pausePressed) pausePressed();
            if (m_pressSeg && m_hover == Part::Camera && cameraToggled) cameraToggled();
            m_drag = Drag::None;
            m_pressBocina = m_pressPausa = m_pressSeg = false;
        }
        draggingWheel = m_drag == Drag::Wheel;
    } else {
        m_drag = Drag::None;
        draggingWheel = false;
        m_pressBocina = m_pressPausa = m_pressSeg = false;
    }

    // --- Animar las piezas con el resultado -------------------------------
    // El volante gira con el timon que el jugador deja.
    m_manubrio->transform.rotation[1] = m_wheel * kDeg;
    // La palanca sigue la marcha de la sim - o la mano mientras se arrastra,
    // que se mueve continua, sin saltar de muesca en muesca.
    float targetZ = ZOfOrder(m_drag == Drag::Lever ? m_dragOrder : m_order);
    m_shownZ = m_drag == Drag::Lever ? targetZ : Damp(m_shownZ, targetZ, 14.0f, dt);
    m_palanca->transform.position[2] = m_shownZ;
    // El señalizador barre en ARCO alrededor de su articulacion -el pivote
    // trasero con el que fue modelado, ver ImportarControles- apuntando el
    // angulo REAL del timon (m_sim.s.rudder, que persigue al manubrio con su
    // inercia). El signo lleva la aguja a estribor (+x en pantalla) con
    // rudder positivo, como el marcador del arco del panel 2D.
    m_giro->transform.rotation[1] = -m_rudder * kGiroBarridoDeg;
    // Los botones se hunden mientras estan apretados.
    m_bocina->transform.position[1] =
        Damp(m_bocina->transform.position[1], m_bocinaY - (m_pressBocina ? kPressDepth : 0.0f), 30.0f, dt);
    m_pausa->transform.position[1] =
        Damp(m_pausa->transform.position[1], m_pausaY - (m_pressPausa ? kPressDepth : 0.0f), 30.0f, dt);
    m_seguimiento->transform.position[1] =
        Damp(m_seguimiento->transform.position[1], m_segY - (m_pressSeg ? kPressDepth : 0.0f), 30.0f, dt);

    // --- Colores de realce -------------------------------------------------
    Tint(*m_bocina, m_hornLit > 0.0f ? kPapel : (m_hover == Part::Horn ? kNaranjaClaro : kNaranja));
    Tint(*m_pausa, m_pressPausa || m_hover == Part::Pause ? kNaranjaClaro : kNaranja);
    Tint(*m_seguimiento, m_follow ? (m_hover == Part::Camera ? kNaranjaClaro : kNaranja)
                                  : (m_hover == Part::Camera ? kNaranjaClaro : kAceroOscuro));
}

// LOS ICONOS: los dos cuadrados naranjas eran iguales y mudos - nada decia
// que uno pausa y el otro suelta la camara, y la unica forma de saberlo era
// apretarlos. Se dibujan en pixeles de la UI (la misma grilla que la fuente)
// sobre el centro de cada tapa, proyectado con la vista del frame, y bajan con
// la tapa cuando se aprieta. El tamano sale del ancho de la tapa en pantalla,
// asi que acompanan a la consola si la resolucion cambia.
inline void Console3D::DrawIcons(Ui& ui) {
    if (!ok || !m_root->active) return;
    auto cap = [&](const ButtonRect& r, const strace::GameObject& n, float restY, float& cx, float& cy, float& wpx) {
        if (!r.valid) return false;
        float top = r.top + (n.transform.position[1] - restY);
        float zc = (r.z0 + r.z1) * 0.5f, xc = (r.x0 + r.x1) * 0.5f;
        float ax, ay, bx, by;
        if (!ToScreen(ui, xc, top, zc, cx, cy)) return false;
        if (!ToScreen(ui, r.x0 + kHitMargin, top, zc, ax, ay) || !ToScreen(ui, r.x1 - kHitMargin, top, zc, bx, by)) return false;
        wpx = std::fabs(bx - ax);
        return wpx > 4.0f;
    };
    float cx, cy, w;
    if (cap(m_rectPausa, *m_pausa, m_pausaY, cx, cy, w)) {
        // Pausa: las dos barras de siempre.
        float u = std::max(2.0f, std::round(w / 7.0f));
        float bh = u * 4.0f, bw = u * 1.0f;
        cx = std::round(cx);
        cy = std::round(cy);
        ui.Rect({cx - u * 1.5f, cy - bh * 0.5f, bw, bh}, kInk);
        ui.Rect({cx + u * 0.5f, cy - bh * 0.5f, bw, bh}, kInk);
    }
    if (cap(m_rectSeg, *m_seguimiento, m_segY, cx, cy, w)) {
        // Camara: un cuerpo con su lente. Apagada (sin seguimiento) va sin
        // relleno en la lente, como un ojo cerrado.
        float u = std::max(2.0f, std::round(w / 7.0f));
        cx = std::round(cx);
        cy = std::round(cy);
        ui.Rect({cx - u * 2.5f, cy - u * 1.5f, u * 5.0f, u * 3.0f}, kInk);
        ui.Rect({cx - u * 1.5f, cy - u * 2.5f, u * 1.5f, u}, kInk);
        ui.Rect({cx - u * 0.5f, cy - u * 0.5f, u, u}, m_follow ? kPaper : kSteelDark);
    }
}

} // namespace ship
