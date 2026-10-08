#pragma once

#include "FillLayer.h"

#include <random>
#include <unordered_map>

namespace ship {

struct House {
    strace::GameObject* node = nullptr;
    strace::GameObject* top = nullptr;
    strace::Transform rest;
    strace::Transform topRest;
    Obb obb;
    bool alive = true;
    bool solid = false;
    float fall = -1.0f;
    V2 push;
};

struct SolidCollider {
    Obb obb;
    int house = -1;
};

struct Wake {
    strace::GameObject* node = nullptr;
    float age = 99.0f;
    float life = 2.4f;
    float grow = 1.0f;
};

class SceneLink {
public:
    strace::GameObject* shipNode = nullptr;
    strace::GameObject* camera = nullptr;
    std::vector<strace::GameObject*> levels;
    std::vector<House> houses;
    std::vector<SolidCollider> solids;
    std::vector<V2> route;
    std::vector<float> routeWidths;
    std::vector<std::pair<V2, float>> basins;
    std::vector<std::pair<V2, V2>> seas;
    FillLayer fill;
    std::vector<Wake> wakes;
    Obb goal;
    V2 startPos;
    float startYaw = 0.0f;
    int current = -1;
    float shipLength = 26.0f;
    float shipBeam = 3.6f;

    bool Link() {
        shipNode = FindRoot("Barco");
        camera = FindRoot("Camara");
        levels.clear();
        for (int i = 1;; ++i) {
            strace::GameObject* l = FindRoot("Nivel_" + std::to_string(i));
            if (!l) break;
            levels.push_back(l);
        }
        if (shipNode) {
            if (auto* box = FindComponent<strace::BoxCollider3D>(*shipNode)) {
                shipBeam = box->size.x * shipNode->transform.scale[0];
                shipLength = box->size.z * shipNode->transform.scale[2];
            }
        }
        wakes.clear();
        if (strace::GameObject* w = FindRoot("Estelas"))
            for (auto& c : w->children) wakes.push_back({c.get()});
        return shipNode && !levels.empty();
    }

    void Bind(int index, uint64_t seed = 1) {
        if (levels.empty()) return;
        index = std::clamp(index, 0, static_cast<int>(levels.size()) - 1);
        RestoreHouses();
        current = index;
        for (size_t i = 0; i < levels.size(); ++i) levels[i]->active = static_cast<int>(i) == index;
        houses.clear();
        solids.clear();
        route.clear();
        routeWidths.clear();
        basins.clear();
        seas.clear();
        strace::GameObject& lvl = *levels[static_cast<size_t>(index)];
        startPos = {lvl.transform.position[0], lvl.transform.position[2]};
        startYaw = 0.0f;
        std::vector<std::unique_ptr<strace::GameObject>>& kids = lvl.children;
        ForEachNode(kids, [&](strace::GameObject& g) {
            if (g.name == "Inicio") {
                DirectX::XMFLOAT3 p = strace::ComputeWorldPosition(g);
                DirectX::XMFLOAT3 f = strace::ComputeWorldForward(g);
                startPos = {p.x, p.z};
                startYaw = std::atan2(f.x, f.z);
            }
            if (g.parent && g.parent->name == "Ruta") {
                DirectX::XMFLOAT3 p = strace::ComputeWorldPosition(g);
                route.push_back({p.x, p.z});
                routeWidths.push_back(g.transform.scale[0]);
            }
            if (g.parent && g.parent->name == "Mares") {
                DirectX::XMFLOAT3 p = strace::ComputeWorldPosition(g);
                DirectX::XMFLOAT3 f = strace::ComputeWorldForward(g);
                seas.push_back({{p.x, p.z}, Norm({f.x, f.z})});
            }
            if (g.parent && g.parent->name == "Darsenas") {
                DirectX::XMFLOAT3 p = strace::ComputeWorldPosition(g);
                basins.push_back({{p.x, p.z}, g.transform.scale[0]});
            }
            auto* box = FindComponent<strace::BoxCollider3D>(g);
            if (StartsWith(g.name, "Casa_") && (box || !g.meshPath.empty())) {
                House h;
                h.node = &g;
                h.rest = g.transform;
                h.obb = box ? ObbFromCollider(g, *box) : ObbFromMesh(g);
                for (auto& c : g.children)
                    if (c && (c->name == "Tejado" || c->name == "Azotea")) h.top = c.get();
                if (h.top) h.topRest = h.top->transform;
                houses.push_back(h);
                if (box && !box->isTrigger) MakeSolid(static_cast<int>(houses.size()) - 1);
                return;
            }
            if (!box) return;
            Obb o = ObbFromCollider(g, *box);
            if (box->isTrigger) {
                if (g.name == "Meta") goal = o;
                return;
            }
            solids.push_back({o, -1});
        });
        BuildCells();
        fill.Setup(FindRoot("Relleno"), seed, route, routeWidths, basins, seas);
        for (const House& h : houses) fill.Occupy(h.obb, 0.6f);
        for (const SolidCollider& c : solids)
            if (c.house < 0) fill.Occupy(c.obb, 1.0f);
    }

    static Obb ObbFromMesh(strace::GameObject& g) {
        strace::BoxCollider3D b;
        b.size = {4.0f, 1.0f, 5.0f};
        std::wstring path = g.meshPath;
        for (auto& c : g.children)
            if (c && c->name == "Islote" && !c->meshPath.empty()) path = c->meshPath;
        if (strace::MeshCache* cache = strace::SceneService::Meshes()) {
            auto r = cache->Resolve(strace::SceneService::ProjectRoot(), path);
            if (r.mesh) {
                DirectX::XMFLOAT3 mn{1e9f, 1e9f, 1e9f}, mx{-1e9f, -1e9f, -1e9f};
                for (const auto& sub : r.mesh->submeshes) {
                    if (sub.materialName == "Halo") continue;
                    for (uint32_t k = sub.firstIndex; k < sub.firstIndex + sub.indexCount; ++k) {
                        const auto& v = r.mesh->vertices[r.mesh->indices[k]].position;
                        mn = {std::min(mn.x, v.x), std::min(mn.y, v.y), std::min(mn.z, v.z)};
                        mx = {std::max(mx.x, v.x), std::max(mx.y, v.y), std::max(mx.z, v.z)};
                    }
                }
                if (mx.x > mn.x) {
                    b.center = {(mn.x + mx.x) * 0.5f, (mn.y + mx.y) * 0.5f, (mn.z + mx.z) * 0.5f};
                    b.size = {mx.x - mn.x, mx.y - mn.y, mx.z - mn.z};
                }
            }
        }
        return ObbFromCollider(g, b);
    }

    void Touch(V2 at, float reach) {
        int r = static_cast<int>(std::ceil(reach / kCell));
        long long cx = static_cast<long long>(std::floor(at.x / kCell));
        long long cz = static_cast<long long>(std::floor(at.z / kCell));
        for (long long x = cx - r; x <= cx + r; ++x)
            for (long long z = cz - r; z <= cz + r; ++z) {
                auto it = m_cells.find((x << 32) | (z & 0xffffffffLL));
                if (it == m_cells.end()) continue;
                for (int idx : it->second) {
                    House& h = houses[static_cast<size_t>(idx)];
                    if (h.alive && !h.solid && Len(h.obb.center - at) < reach) MakeSolid(idx);
                }
            }
    }

    void MakeSolid(int index) {
        House& h = houses[static_cast<size_t>(index)];
        if (h.solid) return;
        h.solid = true;
        solids.push_back({h.obb, index});
    }

    void ExposeAround(V2 at, float radius) {
        for (size_t i = 0; i < houses.size(); ++i)
            if (houses[i].alive && Len(houses[i].obb.center - at) < radius) MakeSolid(static_cast<int>(i));
    }

    void RestoreHouses() {
        fill.Restore();
        for (House& h : houses) {
            h.node->transform = h.rest;
            h.node->active = true;
            if (h.top) h.top->transform = h.topRest;
            h.alive = true;
            h.fall = -1.0f;
        }
    }

    void Collapse(int index, V2 push) {
        House& h = houses[static_cast<size_t>(index)];
        if (!h.alive) return;
        h.alive = false;
        h.fall = 0.0f;
        h.push = Norm(push);
        ExposeAround(h.obb.center, 10.0f);
    }

    int Destroyed() const {
        int n = 0;
        for (const House& h : houses) n += h.alive ? 0 : 1;
        return n + fill.Destroyed();
    }

    void Update(float dt) {
        for (House& h : houses) {
            if (h.fall >= 0.0f) {
                h.fall += dt;
                float t = Clamp(h.fall / 2.2f, 0.0f, 1.0f);
                float e = t * t * (3.0f - 2.0f * t);
                float yawR = h.rest.rotation[1] / kDeg;
                float lx = Dot(h.push, Right(yawR)), lz = Dot(h.push, Forward(yawR));
                h.node->transform.position[0] = h.rest.position[0] + h.push.x * e * 2.0f;
                h.node->transform.position[2] = h.rest.position[2] + h.push.z * e * 2.0f;
                h.node->transform.position[1] = h.rest.position[1] - 5.5f * e * e;
                h.node->transform.rotation[0] = h.rest.rotation[0] + lz * 38.0f * e;
                h.node->transform.rotation[2] = h.rest.rotation[2] - lx * 38.0f * e;
                if (h.top) {
                    h.top->transform.position[1] = h.topRest.position[1] - 0.9f * std::min(1.0f, t * 3.0f);
                    h.top->transform.position[0] = h.topRest.position[0] + lx * 0.25f * e;
                    h.top->transform.position[2] = h.topRest.position[2] + lz * 0.25f * e;
                }
                if (t >= 1.0f) {
                    h.fall = -1.0f;
                    h.node->active = false;
                }
            }
            if (!h.alive) h.node->active = false;
        }
    }

    void BuildCells() {
        m_cells.clear();
        for (size_t i = 0; i < houses.size(); ++i) m_cells[CellKey(houses[i].obb.center)].push_back(static_cast<int>(i));
    }

    int Cull(const strace::GameObject& cam, float aspect) {
        if (!cam.camera) return 0;
        float pitch = cam.transform.rotation[0] / kDeg, yaw = cam.transform.rotation[1] / kDeg;
        float ty = std::tan(cam.camera->fovYDegrees * 0.5f / kDeg), tx = ty * aspect;
        float cp = std::cos(pitch), sp = std::sin(pitch);
        float fx = std::sin(yaw) * cp, fy = -sp, fz = std::cos(yaw) * cp;
        float rx = std::cos(yaw), rz = -std::sin(yaw);
        float ux = std::sin(yaw) * sp, uy = cp, uz = std::cos(yaw) * sp;
        float px = cam.transform.position[0], py = cam.transform.position[1], pz = cam.transform.position[2];
        V2 quad[4];
        const float sx[4] = {-1, 1, 1, -1}, sy[4] = {-1, -1, 1, 1};
        for (int i = 0; i < 4; ++i) {
            float dx = fx + rx * tx * sx[i] + ux * ty * sy[i];
            float dy = fy + uy * ty * sy[i];
            float dz = fz + rz * tx * sx[i] + uz * ty * sy[i];
            float t = dy < -1e-3f ? std::min(py / -dy, 420.0f) : 420.0f;
            quad[i] = {px + dx * t, pz + dz * t};
            m_quad[i] = quad[i];
        }
        float area = 0.0f;
        for (int i = 0; i < 4; ++i) area += quad[i].x * quad[(i + 1) % 4].z - quad[(i + 1) % 4].x * quad[i].z;
        float orient = area >= 0.0f ? 1.0f : -1.0f;
        int visible = 0;
        const float margin = kCell * 0.71f + 9.0f;
        for (auto& [key, list] : m_cells) {
            V2 c{(static_cast<float>(static_cast<int>(key >> 32)) + 0.5f) * kCell,
                 (static_cast<float>(static_cast<int>(key & 0xffffffff)) + 0.5f) * kCell};
            bool in = true;
            for (int i = 0; i < 4 && in; ++i) {
                V2 a = quad[i], b = quad[(i + 1) % 4], e = Norm(b - a);
                float side = (e.x * (c.z - a.z) - e.z * (c.x - a.x)) * orient;
                if (side < -margin) in = false;
            }
            for (int idx : list) {
                House& h = houses[static_cast<size_t>(idx)];
                bool show = in && (h.alive || h.fall >= 0.0f);
                h.node->active = show;
                visible += show ? 1 : 0;
            }
        }
        return visible;
    }

    void EmitWake(V2 p, float yaw, float size, float life) {
        Wake* best = nullptr;
        for (Wake& w : wakes)
            if (!best || w.age > best->age) best = &w;
        if (!best) return;
        best->age = 0.0f;
        best->life = life;
        best->grow = size;
        best->node->active = true;
        best->node->transform.position[0] = p.x;
        best->node->transform.position[2] = p.z;
        best->node->transform.rotation[1] = yaw * kDeg;
    }

    void UpdateWakes(float dt) {
        for (Wake& w : wakes) {
            if (w.age > w.life) {
                w.node->active = false;
                continue;
            }
            w.age += dt;
            float t = Clamp(w.age / w.life, 0.0f, 1.0f);
            float s = w.grow * (0.6f + 1.8f * t);
            w.node->transform.scale[0] = s;
            w.node->transform.scale[2] = s * 1.6f;
            if (strace::Material* m = strace::SceneService::UniqueMaterial(*w.node))
                m->albedoColor.w = 0.75f * (1.0f - t) * (1.0f - t);
        }
    }

    void Debris(V2 at, float yaw) {
        for (int i = 0; i < 6; ++i) {
            float a = yaw + i * kPi / 3.0f + m_dist(m_rng) * 0.4f;
            EmitWake(at + Forward(a) * (2.0f + m_dist(m_rng)), a, 1.8f, 2.8f);
        }
    }

    void UpdateFill(float dt, V2 focus) { fill.Update(m_quad, focus, dt); }

    void Shutdown() {
        RestoreHouses();
        for (strace::GameObject* l : levels) l->active = true;
        for (Wake& w : wakes) w.node->active = false;
    }

private:
    static constexpr float kCell = 24.0f;
    V2 m_quad[4];
    std::unordered_map<long long, std::vector<int>> m_cells;
    std::mt19937 m_rng{5};

    static long long CellKey(V2 p) {
        long long cx = static_cast<long long>(std::floor(p.x / kCell));
        long long cz = static_cast<long long>(std::floor(p.z / kCell));
        return (cx << 32) | (cz & 0xffffffffLL);
    }

    std::uniform_real_distribution<float> m_dist{0.0f, 1.0f};
};

}
