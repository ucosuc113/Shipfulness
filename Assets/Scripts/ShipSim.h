#pragma once

#include "SceneLink.h"

namespace ship {

struct StepResult {
    float impact = 0.0f;
    int collapsed = -1;
    long long collapsedFill = -1;
    int collapsedProp = -1;
    V2 collapsedAt;
    bool contact = false;
    V2 contactAt;
    V2 contactNormal;
    // El golpe mas fuerte del paso, y donde fue: es el que rompe el casco (ver
    // HullDamage). contactAt es el ULTIMO contacto, que con el barco apoyado en
    // una orilla suele ser otro circulo que el que pego.
    V2 hitAt;
    V2 hitNormal;
};

class ShipSim {
public:
    static constexpr int kMinOrder = -2;
    static constexpr int kMaxOrder = 3;
    static constexpr float kBreakSpeed = 2.4f;

    ShipState s;
    float order = 0.0f;
    float baseY = 0.0f;
    int bumps = 0;
    // Lo que el agua embarcada le hace a la flotacion (ver ShipHealth): metros
    // que baja, y grados de escora (roll) y de asiento (pitch, + = proa abajo).
    // Los pone el juego; aqui solo se aplican.
    float settle = 0.0f;
    float heelDeg = 0.0f;
    float trimDeg = 0.0f;
    // 0 a flote, 1 hundido: apaga el cabeceo de las olas, que en un casco
    // yendose al fondo se ve como que flota.
    float sunk = 0.0f;

    void Init(SceneLink& link) {
        m_link = &link;
        s.node = link.shipNode;
        s.length = link.shipLength;
        s.beam = link.shipBeam;
        if (s.node) baseY = s.node->transform.position[1];
        s.radius = s.beam * 0.5f;
        s.circleOffsets.clear();
        float half = s.length * 0.5f - s.radius;
        int n = std::max(3, static_cast<int>(std::ceil(s.length / s.radius)));
        for (int i = 0; i < n; ++i) s.circleOffsets.push_back(Lerp(-half, half, static_cast<float>(i) / (n - 1)));
        SetHullShape({});
    }

    // La forma con la que choca el barco, de HullDamage::Outline. Vacia = la
    // fila de circulos del barco sano (sin malla de casco no hay otra).
    //
    // POR QUE NO ALCANZA CON LA FILA FIJA: al romper la proa, los circulos
    // seguian ocupando el lugar de la chapa que ya no estaba. El choque caia
    // en el aire, la herida no encontraba caras que llevarse y la proa dejaba
    // de romperse y de restar integridad - justo donde mas se pegaba.
    void SetHullShape(std::vector<HullCircle> shape) {
        if (shape.empty())
            for (float z : s.circleOffsets) shape.push_back({0.0f, z, s.radius});
        s.circles = std::move(shape);
    }

    void Reset() {
        s.pos = m_link->startPos;
        s.yaw = m_link->startYaw;
        s.vel = {};
        s.yawRate = 0.0f;
        s.throttle = 0.0f;
        s.rudder = 0.0f;
        s.scrape = 0.0f;
        order = 0.0f;
        bumps = 0;
        settle = heelDeg = trimDeg = sunk = 0.0f;
        m_cooldown = 0.0f;
        Apply(0.0f);
    }

    float Throttle() const {
        float o = std::round(order);
        return o >= 0 ? o / kMaxOrder : 0.6f * o / -kMinOrder;
    }

    StepResult Step(float dt, float steer, bool controllable) {
        s.throttle = Approach(s.throttle, controllable ? Throttle() : 0.0f, dt * 0.5f);
        s.rudder = Approach(s.rudder, controllable ? Clamp(steer, -1.0f, 1.0f) : 0.0f, 1.8f * dt);
        StepResult total;
        const int sub = 4;
        for (int k = 0; k < sub; ++k) {
            Integrate(dt / sub);
            Collide(total);
        }
        m_cooldown -= dt;
        s.scrape = Damp(s.scrape, total.contact ? Clamp(Len(s.vel) / 2.5f, 0.1f, 1.0f) : 0.0f, 10.0f, dt);
        if (total.collapsed < 0 && total.collapsedFill < 0 && total.collapsedProp < 0 && total.impact > 0.7f && m_cooldown <= 0.0f) {
            bumps++;
            m_cooldown = 0.35f;
        } else if (total.impact <= 0.7f) {
            total.impact = 0.0f;
        }
        return total;
    }

    void Apply(float time) {
        if (!s.node) return;
        float calm = 1.0f - sunk;
        float bob = (0.06f * std::sin(time * 1.3f) + 0.03f * std::sin(time * 2.1f + 1.0f)) * calm;
        s.node->transform.position[0] = s.pos.x;
        s.node->transform.position[1] = baseY + bob - settle;
        s.node->transform.position[2] = s.pos.z;
        s.node->transform.rotation[1] = s.yaw * kDeg;
        s.node->transform.rotation[2] =
            Clamp(-s.yawRate * s.ForwardSpeed() * 1.6f + 0.6f * std::sin(time * 0.9f) * calm, -8.0f, 8.0f) + heelDeg;
        s.node->transform.rotation[0] = 0.4f * std::sin(time * 0.7f + 0.5f) * calm - m_pitchKick + trimDeg;
        m_pitchKick = Damp(m_pitchKick, 0.0f, 4.0f, 1.0f / 60.0f);
    }

    bool InsideGoal() const {
        for (size_t i = 0; i < s.circleOffsets.size(); ++i)
            if (!m_link->goal.Contains(s.CirclePos(i), s.radius * 0.5f)) return false;
        return true;
    }

private:
    SceneLink* m_link = nullptr;
    float m_cooldown = 0.0f;
    float m_pitchKick = 0.0f;

    void Integrate(float h) {
        V2 f = Forward(s.yaw), r = Right(s.yaw);
        float u = Dot(s.vel, f);
        float v = Dot(s.vel, r);
        float thrust = s.throttle * 2.3f;
        u += (thrust - 0.16f * u - 0.035f * u * std::fabs(u)) * h;
        v *= std::exp(-2.6f * h);
        float flow = u + thrust * 0.35f;
        float yawAcc = s.rudder * flow * 0.075f - 0.95f * s.yawRate - 0.6f * s.yawRate * std::fabs(s.yawRate);
        s.yawRate += yawAcc * h;
        v -= s.yawRate * u * 0.12f * h;
        s.yaw = WrapAngle(s.yaw + s.yawRate * h);
        s.vel = Forward(s.yaw) * u + Right(s.yaw) * v;
        s.pos += s.vel * h;
    }

    struct Near {
        Obb obb;
        int house = -1;
        long long fill = -1;
        int prop = -1;
    };
    std::vector<Near> m_list;

    void Collide(StepResult& out) {
        float reach = s.length * 0.5f + 5.0f;
        m_link->Touch(s.pos, reach + 6.0f);
        m_list.clear();
        for (const SolidCollider& c : m_link->solids) {
            if (c.house >= 0 && !m_link->houses[static_cast<size_t>(c.house)].alive) continue;
            if (c.prop >= 0 && !m_link->props[static_cast<size_t>(c.prop)].alive) continue;
            float rr = reach + c.obb.hx + c.obb.hz;
            V2 d = c.obb.center - s.pos;
            if (Dot(d, d) < rr * rr) m_list.push_back({c.obb, c.house, -1, c.prop});
        }
        m_link->fill.ForEachSolid(s.pos, reach + 4.0f, [&](const FillSlot& f) { m_list.push_back({f.obb, -1, f.key}); });
        float inertia = (s.length * s.length + s.beam * s.beam) / 12.0f;
        for (const HullCircle& hc : s.circles) {
            for (Near& col : m_list) {
                if (col.house == -2) continue;
                if (col.prop >= 0 && !m_link->props[static_cast<size_t>(col.prop)].alive) continue;
                V2 p = s.HullPoint(hc.x, hc.z);
                V2 n;
                float depth;
                if (!col.obb.Push(p, hc.r, n, depth)) continue;
                out.contact = true;
                out.contactAt = p - n * hc.r;
                out.contactNormal = n;
                V2 rp = p - n * hc.r - s.pos;
                V2 arm{rp.z, -rp.x};
                V2 vp = s.vel + arm * s.yawRate;
                float vn = Dot(vp, n);
                bool breakable = col.house >= 0 || col.fill >= 0 || col.prop >= 0;
                if (breakable && -vn >= kBreakSpeed && out.collapsed < 0 && out.collapsedFill < 0 && out.collapsedProp < 0) {
                    if (col.house >= 0) {
                        m_link->Collapse(col.house, n * -1.0f);
                        out.collapsed = col.house;
                        out.collapsedAt = col.obb.center;
                    } else if (col.fill >= 0) {
                        m_link->CollapseFill(col.fill, n * -1.0f, col.obb);
                        out.collapsedFill = col.fill;
                        out.collapsedAt = col.obb.center;
                    } else {
                        m_link->CollapseProp(col.prop, n * -1.0f, out.contactAt);
                        out.collapsedProp = col.prop;
                        out.collapsedAt = out.contactAt;
                    }
                    if (-vn > out.impact) {
                        out.hitAt = out.contactAt;
                        out.hitNormal = n;
                    }
                    out.impact = std::max(out.impact, -vn);
                    col.house = -2;
                    s.vel = s.vel * 0.08f;
                    s.yawRate *= 0.25f;
                    s.throttle *= 0.3f;
                    m_pitchKick = 4.0f;
                    continue;
                }
                s.pos += n * (depth * 0.85f);
                if (vn < 0.0f) {
                    float cross = Dot(arm, n);
                    float j = -1.2f * vn / (1.0f + cross * cross / inertia);
                    s.vel += n * j;
                    s.yawRate += j * cross / inertia;
                    V2 t{-n.z, n.x};
                    float vt = Dot(vp, t);
                    float crossT = Dot(arm, t);
                    float jt = Clamp(-vt / (1.0f + crossT * crossT / inertia), -0.4f * j, 0.4f * j);
                    s.vel += t * jt;
                    s.yawRate += jt * crossT / inertia;
                    if (-vn > out.impact) {
                        out.hitAt = out.contactAt;
                        out.hitNormal = n;
                    }
                    out.impact = std::max(out.impact, -vn);
                }
            }
        }
    }
};

}
