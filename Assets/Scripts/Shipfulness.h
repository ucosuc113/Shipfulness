#pragma once

#include "Engine/Scene/GameObject.h"
#include "Engine/Scene/SceneService.h"
#include "Engine/Scene/SceneTransform.h"
#include "Engine/Components/Colliders/BoxCollider3D.h"
#include "Engine/Rendering/Material.h"
#include "Engine/Platform/Input.h"

#include <DirectXMath.h>
#include <algorithm>
#include <cmath>
#include <memory>
#include <string>
#include <vector>

namespace ship {

constexpr float kPi = 3.14159265358979f;
constexpr float kDeg = 180.0f / kPi;

struct V2 {
    float x = 0.0f, z = 0.0f;
    V2() = default;
    V2(float ax, float az) : x(ax), z(az) {}
    V2 operator+(V2 o) const { return {x + o.x, z + o.z}; }
    V2 operator-(V2 o) const { return {x - o.x, z - o.z}; }
    V2 operator*(float s) const { return {x * s, z * s}; }
    V2& operator+=(V2 o) { x += o.x; z += o.z; return *this; }
    V2& operator-=(V2 o) { x -= o.x; z -= o.z; return *this; }
};

inline float Dot(V2 a, V2 b) { return a.x * b.x + a.z * b.z; }
inline float Len(V2 a) { return std::sqrt(Dot(a, a)); }
inline V2 Norm(V2 a) { float l = Len(a); return l > 1e-6f ? a * (1.0f / l) : V2{0.0f, 1.0f}; }
inline V2 Forward(float yaw) { return {std::sin(yaw), std::cos(yaw)}; }
inline V2 Right(float yaw) { return {std::cos(yaw), -std::sin(yaw)}; }
inline float Clamp(float v, float a, float b) { return v < a ? a : (v > b ? b : v); }
inline float Lerp(float a, float b, float t) { return a + (b - a) * t; }
inline float Approach(float v, float target, float rate) {
    return v < target ? std::min(v + rate, target) : std::max(v - rate, target);
}
inline float Damp(float a, float b, float lambda, float dt) { return Lerp(a, b, 1.0f - std::exp(-lambda * dt)); }
inline float WrapAngle(float a) {
    while (a > kPi) a -= 2.0f * kPi;
    while (a < -kPi) a += 2.0f * kPi;
    return a;
}
inline float DampAngle(float a, float b, float lambda, float dt) {
    return a + WrapAngle(b - a) * (1.0f - std::exp(-lambda * dt));
}

struct Obb {
    V2 center;
    V2 axisX{1.0f, 0.0f};
    V2 axisZ{0.0f, 1.0f};
    float hx = 0.5f, hz = 0.5f;

    bool Contains(V2 p, float margin = 0.0f) const {
        V2 d = p - center;
        return std::fabs(Dot(d, axisX)) <= hx - margin && std::fabs(Dot(d, axisZ)) <= hz - margin;
    }

    bool Push(V2 p, float r, V2& outNormal, float& outDepth) const {
        V2 d = p - center;
        float lx = Dot(d, axisX), lz = Dot(d, axisZ);
        float cx = Clamp(lx, -hx, hx), cz = Clamp(lz, -hz, hz);
        if (cx == lx && cz == lz) {
            float px = hx - std::fabs(lx), pz = hz - std::fabs(lz);
            if (px < pz) {
                outNormal = axisX * (lx >= 0.0f ? 1.0f : -1.0f);
                outDepth = px + r;
            } else {
                outNormal = axisZ * (lz >= 0.0f ? 1.0f : -1.0f);
                outDepth = pz + r;
            }
            return true;
        }
        V2 closest = center + axisX * cx + axisZ * cz;
        V2 delta = p - closest;
        float dist = Len(delta);
        if (dist >= r) return false;
        outNormal = dist > 1e-5f ? delta * (1.0f / dist) : V2{0.0f, 1.0f};
        outDepth = r - dist;
        return true;
    }
};

inline Obb ObbFromCollider(const strace::GameObject& go, const strace::BoxCollider3D& box) {
    using namespace DirectX;
    XMMATRIX m = strace::ComputeWorldMatrix(go);
    XMVECTOR c = XMVector3TransformCoord(XMLoadFloat3(&box.center), m);
    XMVECTOR ax = XMVector3TransformNormal(XMVectorSet(box.size.x * 0.5f, 0, 0, 0), m);
    XMVECTOR az = XMVector3TransformNormal(XMVectorSet(0, 0, box.size.z * 0.5f, 0), m);
    XMFLOAT3 fc, fx, fz;
    XMStoreFloat3(&fc, c);
    XMStoreFloat3(&fx, ax);
    XMStoreFloat3(&fz, az);
    Obb o;
    o.center = {fc.x, fc.z};
    V2 vx{fx.x, fx.z}, vz{fz.x, fz.z};
    o.hx = Len(vx);
    o.hz = Len(vz);
    o.axisX = Norm(vx);
    o.axisZ = Norm(vz);
    return o;
}

template <class T>
T* FindComponent(strace::GameObject& go) {
    for (auto& c : go.components)
        if (c && c->type == T::kType) return static_cast<T*>(c.get());
    return nullptr;
}

template <class F>
void ForEachNode(std::vector<std::unique_ptr<strace::GameObject>>& nodes, F&& f) {
    for (auto& n : nodes) {
        if (!n) continue;
        f(*n);
        ForEachNode(n->children, f);
    }
}

inline strace::GameObject* FindRoot(const std::string& name) {
    for (auto& n : strace::SceneService::MutableRoots())
        if (n && n->name == name) return n.get();
    return nullptr;
}

inline strace::GameObject* FindChild(strace::GameObject& parent, const std::string& name) {
    for (auto& c : parent.children)
        if (c && c->name == name) return c.get();
    return nullptr;
}

inline bool StartsWith(const std::string& s, const char* prefix) { return s.rfind(prefix, 0) == 0; }

class Keys {
public:
    void Update() {
        for (int vk : kWatched) {
            m_prev[vk] = m_now[vk];
            m_now[vk] = strace::Input::IsKeyDown(vk);
        }
    }
    bool Held(int vk) const { return m_now[vk & 0xFF]; }
    bool Pressed(int vk) const { return m_now[vk & 0xFF] && !m_prev[vk & 0xFF]; }

private:
    static constexpr int kWatched[] = {'W', 'A', 'S', 'D', 'H', 'R', 'C', 'M', 'P', '1', '2', '3',
                                       0x20, 0x0D, 0x1B, 0x25, 0x26, 0x27, 0x28, 0x77};
    bool m_now[256] = {};
    bool m_prev[256] = {};
};

struct ShipState {
    strace::GameObject* node = nullptr;
    V2 pos;
    float yaw = 0.0f;
    V2 vel;
    float yawRate = 0.0f;
    float length = 26.0f;
    float beam = 3.6f;
    float throttle = 0.0f;
    float rudder = 0.0f;
    float radius = 1.8f;
    float scrape = 0.0f;
    std::vector<float> circleOffsets;

    float ForwardSpeed() const { return Dot(vel, Forward(yaw)); }
    V2 CirclePos(size_t i) const { return pos + Forward(yaw) * circleOffsets[i]; }
};

}
