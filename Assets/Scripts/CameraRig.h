#pragma once

#include "Shipfulness.h"

namespace ship {

class CameraRig {
public:
    strace::GameObject* cam = nullptr;
    bool follow = true;
    bool shakeEnabled = true;
    float zoom = 1.0f;

    void Attach(strace::GameObject* node) { cam = node && node->camera ? node : nullptr; }

    void Snap() { m_snap = true; }

    void Update(float dt, const ShipState& s, float fixedYaw, float menuOrbit) {
        if (!cam) return;
        V2 f = Forward(s.yaw);
        float speed = s.ForwardSpeed();
        float desiredYaw = follow ? s.yaw : fixedYaw;
        // LA DISTANCIA SALE DEL LARGO DEL BARCO: 14 m + 2 por metro de casco.
        // Con el barco de cubos (26 m) da los 66 de antes; con el de Blender
        // (8 m) da 30 y el barco se ve a su tamano en vez de un punto entre
        // las casas. Un numero fijo dejaba de servir con el primer barco nuevo.
        float desiredDist = (14.0f + 2.0f * s.length + std::fabs(speed) * 1.5f) * zoom;
        float pitchDeg = 57.0f;
        // EL BARCO JUSTO ENCIMA DE LA CONSOLA: el punto que mira la camara va
        // ADELANTE del barco, lo justo para que la popa quede a kSternMargin
        // metros de la fila de pantalla kSternScreen - el borde de arriba de la
        // consola, que esta centrada abajo (ver Console3D::Anchor). Antes la
        // popa iba al borde inferior de la imagen, con la consola en el rincon.
        //
        // La cuenta es la de esa fila sobre el agua: su rayo baja a pitch + a,
        // con tan(a) = (1 - 2 * kSternScreen) * tan(fov/2), asi que corta el
        // suelo a dist * (cos(pitch) - sin(pitch) / tan(pitch + a)) por detras
        // del punto mirado. Se calcula con la distancia y el fov de VERDAD, no con
        // un numero afinado a ojo: la distancia crece con la velocidad y un
        // desplazamiento fijo dejaria la popa fuera de cuadro a toda maquina.
        // Por eso tampoco hay mirada adelante por velocidad (el speed * 0.7 de
        // antes): con el barco abajo, toda la pantalla ya es lo que viene.
        //
        // A lo largo de la direccion de la CAMARA, no del barco: con el
        // seguimiento apagado el barco puede ir de costado, y empujar el punto
        // por su proa lo sacaria de cuadro por un lado.
        const float pitch0 = pitchDeg / kDeg;
        const float halfFov = (cam->camera->fovYDegrees * 0.5f) / kDeg;
        const float below = std::atan((1.0f - 2.0f * kSternScreen) * std::tan(halfFov));
        const float bottom = desiredDist * (std::cos(pitch0) - std::sin(pitch0) / std::tan(pitch0 + below));
        // Puede ser NEGATIVO (mirar detras del centro del barco): con la popa
        // por encima de la consola, el barco largo no entra de otra forma. El
        // tope en 0 que habia aqui dejaba la popa debajo del volante.
        const float ahead = bottom - s.length * 0.5f - kSternMargin;
        // Y ADELANTADO POR LA VELOCIDAD, lo que el suavizado de abajo lo atrasa:
        // Damp con lambda kFollow persigue a un punto que va a v m/s con
        // v / kFollow metros de retraso. Sin esto, la cuenta de arriba ponia la
        // popa sobre la consola con el barco QUIETO, y en marcha el barco subia
        // por la pantalla hasta la mitad (2-3 m de retraso a velocidad de
        // crucero son un 10 % de la imagen).
        V2 desired = s.pos + Forward(desiredYaw) * ahead + s.vel * (1.0f / kFollow);
        if (menuOrbit >= 0.0f) {
            desiredYaw = s.yaw + 0.5f + 0.25f * std::sin(menuOrbit * 0.07f);
            // El menu tambien a la medida del barco. El punto mirado iba 1,15
            // esloras por delante de la proa y el barco quedaba cortado contra
            // el rincon de abajo a la derecha: en la portada, el protagonista
            // sin popa. A 0,45 esloras y un poco mas lejos entra entero en la
            // mitad derecha, que es la que la columna del menu deja libre.
            V2 look = s.pos + f * (s.length * 0.45f);
            desired = look - Right(desiredYaw) * (s.length * 0.77f);
            desiredDist = 30.0f + 2.0f * s.length;
            pitchDeg = 46.0f;
        }
        float k = m_snap ? 1000.0f : 1.0f;
        m_target.x = Damp(m_target.x, desired.x, kFollow * k, dt);
        m_target.z = Damp(m_target.z, desired.z, kFollow * k, dt);
        m_yaw = DampAngle(m_yaw, desiredYaw, 0.9f * k, dt);
        m_dist = Damp(m_dist, desiredDist, 1.5f * k, dt);
        m_pitch = Damp(m_pitch, pitchDeg, 1.5f * k, dt);
        m_snap = false;
        float pitch = m_pitch / kDeg;
        V2 back = Forward(m_yaw) * (-std::cos(pitch) * m_dist);
        float shake = shakeEnabled ? m_shake * m_shake : 0.0f;
        m_shake = std::max(0.0f, m_shake - dt * 2.0f);
        m_time += dt;
        cam->transform.position[0] = m_target.x + back.x + shake * std::sin(m_time * 47.0f) * 0.8f;
        cam->transform.position[1] = std::sin(pitch) * m_dist + shake * std::sin(m_time * 53.0f) * 0.5f;
        cam->transform.position[2] = m_target.z + back.z + shake * std::cos(m_time * 41.0f) * 0.5f;
        cam->transform.rotation[0] = m_pitch;
        cam->transform.rotation[1] = m_yaw * kDeg;
        cam->transform.rotation[2] = 0.0f;
    }

    void Shake(float amount) { m_shake = std::min(1.0f, m_shake + amount); }

private:
    // Metros entre la popa y la fila kSternScreen de la imagen (sobre el agua).
    // Uno: la popa justo encima de la consola, y el resto de la pantalla es lo
    // que viene (eran 3, y con el retraso de la camara el barco iba al centro).
    static constexpr float kSternMargin = 1.0f;
    // Con que rapidez la camara alcanza su punto (lambda de Damp).
    static constexpr float kFollow = 2.2f;
    // La fila de pantalla (0 = borde de abajo, 1 = arriba) desde la que se mide
    // kSternMargin: el borde de arriba de la consola centrada. Con fov 38, la
    // consola a 9 m y escala 0,7, su tapa queda al 26% de la altura; un poco
    // mas para que el volante, que asoma, no le muerda la popa.
    static constexpr float kSternScreen = 0.30f;

    V2 m_target;
    float m_yaw = 0.0f;
    float m_dist = 60.0f;
    float m_pitch = 57.0f;
    float m_shake = 0.0f;
    float m_time = 0.0f;
    bool m_snap = true;
};

}
