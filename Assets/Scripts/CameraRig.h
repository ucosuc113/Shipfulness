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
        V2 desired = s.pos + f * (speed * 0.9f - 4.0f);
        float desiredYaw = follow ? s.yaw : fixedYaw;
        float desiredDist = (62.0f + std::fabs(speed) * 2.0f) * zoom;
        float pitchDeg = 57.0f;
        if (menuOrbit >= 0.0f) {
            desiredYaw = s.yaw + 0.5f + 0.25f * std::sin(menuOrbit * 0.07f);
            V2 look = s.pos + f * 30.0f;
            desired = look - Right(desiredYaw) * 20.0f;
            desiredDist = 74.0f;
            pitchDeg = 46.0f;
        }
        float k = m_snap ? 1000.0f : 1.0f;
        m_target.x = Damp(m_target.x, desired.x, 2.2f * k, dt);
        m_target.z = Damp(m_target.z, desired.z, 2.2f * k, dt);
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
    V2 m_target;
    float m_yaw = 0.0f;
    float m_dist = 60.0f;
    float m_pitch = 57.0f;
    float m_shake = 0.0f;
    float m_time = 0.0f;
    bool m_snap = true;
};

}
