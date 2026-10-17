#pragma once

#include "Effects.h"
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
    int prop = -1;
};

struct PropPart {
    strace::GameObject* node = nullptr;
    strace::Transform rest;
    DirectX::XMFLOAT4X4 toLocal;
    float x = 0.0f, y = 0.0f, z = 0.0f;
    float vx = 0.0f, vy = 0.0f, vz = 0.0f;
    float rx = 0.0f, ry = 0.0f, rz = 0.0f;
    float spinX = 0.0f, spinY = 0.0f, spinZ = 0.0f;
    float half = 0.5f;
    float mass = 1.0f;
    bool wet = false;
    bool gone = false;
};

struct Prop {
    strace::GameObject* root = nullptr;
    std::vector<PropPart> parts;
    bool alive = true;
};

struct Wake {
    strace::GameObject* node = nullptr;
    float age = 99.0f;
    float life = 2.4f;
    float grow = 1.0f;
    bool line = false;
    V2 pos;
    V2 drift;
};

class WakeTrail {
public:
    strace::GameObject* node = nullptr;

    void Attach(strace::GameObject* n) {
        node = n;
        if (node) {
            node->active = false;
            node->castShadows = false;
        }
        m_count = 0;
    }

    void Reset(V2 stern) {
        for (V2& p : m_pts) p = stern;
        m_count = 1;
        m_intensity = 0.0f;
    }

    void Update(float dt, V2 stern, float speed, float halfBeam, float shipLength, float odometer, V2 viewMin, V2 viewMax) {
        if (!node) return;
        if (m_count == 0) Reset(stern);
        float spacing = shipLength * 1.25f / static_cast<float>(kPoints - 1);
        if (Len(stern - m_pts[1]) >= spacing || m_count == 1) {
            for (int i = kPoints - 1; i > 1; --i) m_pts[i] = m_pts[i - 1];
            m_pts[1] = stern;
            m_count = std::min(m_count + 1, kPoints);
        }
        m_pts[0] = stern;
        m_intensity = Damp(m_intensity, Clamp(std::fabs(speed) / 3.0f, 0.0f, 1.0f), 1.5f, dt);
        node->active = m_intensity > 0.02f;
        if (!node->active) return;
        strace::Material* m = strace::SceneService::UniqueMaterial(*node);
        if (!m) return;
        float minX = 1e9f, maxX = -1e9f, minZ = 1e9f, maxZ = -1e9f;
        for (int i = 0; i < kPoints; ++i) {
            minX = std::min(minX, m_pts[i].x);
            maxX = std::max(maxX, m_pts[i].x);
            minZ = std::min(minZ, m_pts[i].z);
            maxZ = std::max(maxZ, m_pts[i].z);
        }
        // Los brazos de Estela.hlsl se abren kArmTan (0,3) por metro de
        // recorrido: el rectangulo tiene que alcanzar el brazo al final de la
        // estela, o la V se corta en recto contra el borde de la malla.
        const float kArmTan = 0.3f;
        float margin = halfBeam + shipLength * 1.25f * kArmTan + 2.0f;
        minX = std::max(minX - margin, viewMin.x);
        maxX = std::min(maxX + margin, viewMax.x);
        minZ = std::max(minZ - margin, viewMin.z);
        maxZ = std::min(maxZ + margin, viewMax.z);
        if (minX >= maxX || minZ >= maxZ) {
            node->active = false;
            return;
        }
        node->transform.position[0] = (minX + maxX) * 0.5f;
        node->transform.position[1] = 0.09f;
        node->transform.position[2] = (minZ + maxZ) * 0.5f;
        node->transform.rotation[1] = 0.0f;
        node->transform.scale[0] = maxX - minX;
        node->transform.scale[2] = maxZ - minZ;
        for (int k = 0; k < 7; ++k)
            m->userParams[k] = {m_pts[k * 2].x, m_pts[k * 2].z, m_pts[k * 2 + 1].x, m_pts[k * 2 + 1].z};
        m->userParams[7] = {halfBeam, shipLength, m_intensity, odometer};
    }

private:
    static constexpr int kPoints = 14;
    V2 m_pts[kPoints];
    int m_count = 0;
    float m_intensity = 0.0f;
};

class SceneLink {
public:
    strace::GameObject* shipNode = nullptr;
    strace::GameObject* water = nullptr;
    strace::GameObject* camera = nullptr;
    std::vector<strace::GameObject*> levels;
    std::vector<House> houses;
    std::vector<SolidCollider> solids;
    std::vector<V2> route;
    std::vector<float> routeWidths;
    std::vector<std::pair<V2, float>> basins;
    std::vector<std::pair<V2, V2>> seas;
    FillLayer fill;
    WakeTrail trail;
    Effects fx;
    std::vector<Prop> props;
    bool houseShadows = true;
    std::vector<Wake> wakes;
    Obb goal;
    V2 startPos;
    float startYaw = 0.0f;
    int current = -1;
    float shipLength = 26.0f;
    float shipBeam = 3.6f;

    bool Link() {
        shipNode = FindRoot("Barco");
        water = FindRoot("Agua");
        trail.Attach(FindRoot("Estela"));
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
        fx.Link();
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
        props.clear();
        route.clear();
        routeWidths.clear();
        basins.clear();
        seas.clear();
        strace::GameObject& lvl = *levels[static_cast<size_t>(index)];
        startPos = {lvl.transform.position[0], lvl.transform.position[2]};
        startYaw = 0.0f;
        std::vector<std::unique_ptr<strace::GameObject>>& kids = lvl.children;
        std::unordered_map<const strace::GameObject*, int> propOf;
        ForEachNode(kids, [&](strace::GameObject& g) {
            if (g.parent && g.parent->name == "Obstaculos") {
                Prop pr;
                pr.root = &g;
                if (g.children.empty()) pr.parts.push_back({&g, g.transform});
                for (auto& c : g.children)
                    if (c) pr.parts.push_back({c.get(), c->transform});
                propOf[&g] = static_cast<int>(props.size());
                props.push_back(std::move(pr));
            }
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
                for (auto& c : g.children)
                    if (c && c->name == "Islote") c->castShadows = false;
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
            int prop = -1;
            for (const strace::GameObject* a = &g; a && prop < 0; a = a->parent) {
                auto it = propOf.find(a);
                if (it != propOf.end()) prop = it->second;
            }
            solids.push_back({o, -1, prop});
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
        fx.Clear();
        for (Prop& p : props) {
            p.alive = true;
            for (PropPart& part : p.parts) {
                part.node->transform = part.rest;
                part.node->active = true;
                part.wet = false;
                part.gone = false;
            }
        }
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
        fx.Burst(BlastFor(*h.node, h.obb, h.push, 1.0f));
    }

    void CollapseFill(long long key, V2 push, const Obb& obb) {
        strace::GameObject* node = fill.NodeFor(key);
        fill.Collapse(key, push);
        if (node) fx.Burst(BlastFor(*node, obb, Norm(push), 1.0f));
    }

    void CollapseProp(int index, V2 push, V2 at) {
        Prop& pr = props[static_cast<size_t>(index)];
        if (!pr.alive) return;
        pr.alive = false;
        push = Norm(push);
        V2 side{-push.z, push.x};
        Blast b;
        b.at = at;
        b.push = push;
        b.radius = 2.5f;
        b.power = 1.4f;
        b.height = 1.0f;
        for (PropPart& part : pr.parts) {
            strace::GameObject& n = *part.node;
            DirectX::XMFLOAT3 w = strace::ComputeWorldPosition(n);
            DirectX::XMMATRIX parent = n.parent ? strace::ComputeWorldMatrix(*n.parent) : DirectX::XMMatrixIdentity();
            DirectX::XMStoreFloat4x4(&part.toLocal, DirectX::XMMatrixInverse(nullptr, parent));
            part.x = w.x;
            part.y = w.y;
            part.z = w.z;
            part.rx = part.ry = part.rz = 0.0f;
            part.half = 0.5f * n.transform.scale[1];
            part.mass = Clamp(std::cbrt(std::fabs(n.transform.scale[0] * n.transform.scale[1] * n.transform.scale[2])), 0.3f, 6.0f);
            part.wet = false;
            part.gone = false;
            float dist = Len(V2{w.x, w.z} - at);
            float k = std::exp(-dist / 5.0f);
            float inv = 1.0f / std::sqrt(part.mass);
            V2 v = push * ((2.0f + 9.0f * k) * inv) + side * (Signed() * 2.5f * inv);
            part.vx = v.x;
            part.vz = v.z;
            part.vy = (1.0f + 5.0f * k) * inv + Signed() * 0.8f;
            DirectX::XMFLOAT3 lp;
            DirectX::XMVECTOR pv = DirectX::XMVectorSet(push.x, 0.0f, push.z, 0.0f);
            DirectX::XMStoreFloat3(&lp, DirectX::XMVector3TransformNormal(pv, DirectX::XMLoadFloat4x4(&part.toLocal)));
            float spin = (60.0f + 260.0f * k) * inv;
            part.spinX = lp.z * spin + Signed() * 40.0f * inv;
            part.spinZ = -lp.x * spin + Signed() * 40.0f * inv;
            part.spinY = Signed() * 90.0f * inv;
            b.height = std::max(b.height, w.y + part.half);
            AddPalette(n, b.palette);
        }
        fx.Burst(b);
    }

    Blast BlastFor(strace::GameObject& node, const Obb& obb, V2 push, float power) {
        Blast b;
        b.at = obb.center;
        b.push = push;
        b.radius = std::max(1.5f, std::min(obb.hx, obb.hz));
        b.power = power;
        b.height = 5.0f;
        if (strace::MeshCache* cache = strace::SceneService::Meshes()) {
            auto r = cache->Resolve(strace::SceneService::ProjectRoot(), node.meshPath);
            if (r.mesh) b.height = std::max(2.0f, r.mesh->boundsMax.y);
        }
        AddPalette(node, b.palette);
        return b;
    }

    static void AddPalette(strace::GameObject& g, std::vector<Tint>& out) {
        strace::MaterialCache* mats = strace::SceneService::Materials();
        if (!g.meshPath.empty()) {
            if (strace::MeshCache* cache = strace::SceneService::Meshes()) {
                auto r = cache->Resolve(strace::SceneService::ProjectRoot(), g.meshPath);
                if (r.mesh)
                    for (const auto& sub : r.mesh->submeshes) {
                        if (sub.materialName == "Halo" || sub.indexCount == 0) continue;
                        DirectX::XMFLOAT4 c = sub.baseColor;
                        std::wstring path;
                        for (const auto& sm : g.submeshMaterials)
                            if (sm.materialName == sub.materialName) path = sm.materialPath;
                        if (path.empty()) path = L"Assets/Materiales/Casas/" + std::wstring(sub.materialName.begin(), sub.materialName.end()) + L".strmat";
                        if (mats)
                            if (strace::Material* m = mats->GetMutable(path)) c = m->albedoColor;
                        // Pesa por triangulos, pero lo que tiene COLOR vale el
                        // triple: el blanco de las paredes tiene muchos mas
                        // triangulos que el ladrillo o el tejado, y la casa
                        // reventaba en una lluvia de cubos blancos que no
                        // decia de que casa salian.
                        float chroma = std::max({c.x, c.y, c.z}) - std::min({c.x, c.y, c.z});
                        float weight = static_cast<float>(sub.indexCount) * (chroma > 0.05f ? 3.0f : 1.0f);
                        out.push_back({c.x, c.y, c.z, weight});
                    }
            }
        } else if (g.primitive != strace::PrimitiveType::Empty) {
            DirectX::XMFLOAT4 c{0.8f, 0.8f, 0.8f, 1.0f};
            if (mats && !g.materialPath.empty())
                if (strace::Material* m = mats->GetMutable(g.materialPath)) c = m->albedoColor;
            float area = std::fabs(g.transform.scale[0] * g.transform.scale[1]) + std::fabs(g.transform.scale[1] * g.transform.scale[2]) +
                         std::fabs(g.transform.scale[0] * g.transform.scale[2]);
            out.push_back({c.x, c.y, c.z, area * 10.0f});
        }
        for (auto& c : g.children)
            if (c && c->active) AddPalette(*c, out);
    }

    void UpdateProps(float dt) {
        for (Prop& pr : props) {
            if (pr.alive) continue;
            for (PropPart& p : pr.parts) {
                if (p.gone) continue;
                if (!p.wet) {
                    p.vy -= 20.0f * dt;
                    if (p.y - p.half < 0.0f && p.vy < 0.0f) {
                        p.wet = true;
                        fx.Splash(p.x, p.z, Clamp(p.mass * 0.9f, 0.4f, 3.0f));
                        p.vx *= 0.3f;
                        p.vz *= 0.3f;
                        p.vy = std::max(p.vy * 0.15f, -2.0f);
                        p.spinX *= 0.3f;
                        p.spinY *= 0.3f;
                        p.spinZ *= 0.3f;
                    }
                } else {
                    p.vy = Damp(p.vy, -1.3f, 3.0f, dt);
                    float drag = std::exp(-1.5f * dt);
                    p.vx *= drag;
                    p.vz *= drag;
                }
                p.x += p.vx * dt;
                p.y += p.vy * dt;
                p.z += p.vz * dt;
                p.rx += p.spinX * dt;
                p.ry += p.spinY * dt;
                p.rz += p.spinZ * dt;
                if (p.y < -p.half * 2.0f - 6.0f) {
                    p.gone = true;
                    p.node->active = false;
                    continue;
                }
                DirectX::XMFLOAT3 local;
                DirectX::XMStoreFloat3(&local, DirectX::XMVector3TransformCoord(DirectX::XMVectorSet(p.x, p.y, p.z, 1.0f),
                                                                                DirectX::XMLoadFloat4x4(&p.toLocal)));
                p.node->transform.position[0] = local.x;
                p.node->transform.position[1] = local.y;
                p.node->transform.position[2] = local.z;
                p.node->transform.rotation[0] = p.rest.rotation[0] + p.rx;
                p.node->transform.rotation[1] = p.rest.rotation[1] + p.ry;
                p.node->transform.rotation[2] = p.rest.rotation[2] + p.rz;
            }
        }
    }

    int Destroyed() const {
        int n = 0;
        for (const House& h : houses) n += h.alive ? 0 : 1;
        return n + fill.Destroyed();
    }

    void Update(float dt) {
        fx.Update(dt);
        UpdateProps(dt);
        for (House& h : houses) {
            if (h.fall >= 0.0f) {
                h.fall += dt;
                float t = Clamp(h.fall / kToppleTime, 0.0f, 1.0f);
                float yawR = h.rest.rotation[1] / kDeg;
                Topple tp = ToppleAt(h.fall, h.push, yawR);
                float lx = Dot(h.push, Right(yawR)), lz = Dot(h.push, Forward(yawR));
                h.node->transform.position[0] = h.rest.position[0] + tp.dx;
                h.node->transform.position[2] = h.rest.position[2] + tp.dz;
                h.node->transform.position[1] = h.rest.position[1] + tp.dy;
                h.node->transform.rotation[0] = h.rest.rotation[0] + tp.rx;
                h.node->transform.rotation[2] = h.rest.rotation[2] + tp.rz;
                if (h.top) {
                    float pop = std::sin(Clamp(h.fall * 3.2f, 0.0f, kPi)) * 1.4f;
                    float slide = 2.4f * tp.tip;
                    h.top->transform.position[1] = h.topRest.position[1] + pop - 1.2f * tp.tip;
                    h.top->transform.position[0] = h.topRest.position[0] + lx * slide;
                    h.top->transform.position[2] = h.topRest.position[2] + lz * slide;
                    h.top->transform.rotation[0] = h.topRest.rotation[0] + lz * 35.0f * tp.tip;
                    h.top->transform.rotation[2] = h.topRest.rotation[2] - lx * 35.0f * tp.tip;
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
                if (show) {
                    // La sombra que vale aqui es la del SOL: la escena no tiene
                    // lamparas, asi que `castShadows` (la del atlas de lamparas)
                    // no hacia nada y las casas flotaban sobre el agua. Quien
                    // recorta es la caja del sol (SunBox), que cubre el encuadre:
                    // un radio aparte dejaria un borde donde la sombra se corta.
                    h.node->castSunShadows = houseShadows;
                    if (h.top) h.top->castSunShadows = houseShadows;
                }
                visible += show ? 1 : 0;
            }
        }
        return visible;
    }

    void EmitWakeLine(V2 p, float yaw, V2 drift, float life) {
        EmitWake(p, yaw, 1.0f, life);
        for (Wake& w : wakes)
            if (w.age == 0.0f && w.node->active) {
                w.line = true;
                w.drift = drift;
            }
    }

    void EmitWake(V2 p, float yaw, float size, float life) {
        Wake* best = nullptr;
        for (Wake& w : wakes)
            if (!best || w.age > best->age) best = &w;
        if (!best) return;
        best->age = 0.0f;
        best->life = life;
        best->grow = size;
        best->line = false;
        best->pos = p;
        best->drift = {};
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
            float alpha;
            if (w.line) {
                w.pos += w.drift * dt;
                w.node->transform.position[0] = w.pos.x;
                w.node->transform.position[2] = w.pos.z;
                w.node->transform.scale[0] = 0.22f + 0.35f * t;
                w.node->transform.scale[2] = 2.6f;
                alpha = 0.9f * (1.0f - t);
            } else {
                float s = w.grow * (0.6f + 1.8f * t);
                w.node->transform.scale[0] = s;
                w.node->transform.scale[2] = s * 1.6f;
                alpha = 0.75f * (1.0f - t) * (1.0f - t);
            }
            if (strace::Material* m = strace::SceneService::UniqueMaterial(*w.node)) m->albedoColor.w = alpha;
        }
    }

    void ViewBounds(V2& lo, V2& hi) const {
        lo = {1e9f, 1e9f};
        hi = {-1e9f, -1e9f};
        for (const V2& q : m_quad) {
            lo.x = std::min(lo.x, q.x);
            lo.z = std::min(lo.z, q.z);
            hi.x = std::max(hi.x, q.x);
            hi.z = std::max(hi.z, q.z);
        }
        if (lo.x >= hi.x) {
            lo = {-1e9f, -1e9f};
            hi = {1e9f, 1e9f};
        }
    }

    // La caja del mapa del sol: centrada en lo que la camara VE y del tamano
    // del encuadre. Antes iba centrada en el barco con 18 m de medio lado, que
    // alcanzaba para la sombra del barco y para nada mas - con las casas
    // proyectando, la sombra se cortaba en seco a 18 m del barco, en mitad de
    // la pantalla.
    //
    // El medio lado se redondea a escalones de 8 m: el motor ajusta el CENTRO a
    // la grilla de texeles, pero un medio lado que cambia cada frame (la camara
    // se aleja con la velocidad) cambia el tamano del texel y el borde de cada
    // sombra herviria. `margin` es lo que una casa fuera del cuadro alcanza a
    // tirar dentro con el sol bajo.
    void SunBox(V2& center, float& half) const {
        V2 lo, hi;
        ViewBounds(lo, hi);
        if (hi.x - lo.x > 1e8f) {
            center = m_focus;
            half = 40.0f;
            return;
        }
        center = (lo + hi) * 0.5f;
        const float margin = 10.0f;
        float raw = 0.5f * std::max(hi.x - lo.x, hi.z - lo.z) + margin;
        half = std::clamp(std::ceil(raw / 8.0f) * 8.0f, 24.0f, 96.0f);
    }

    void ShaderClock(float t) {
        if (water)
            if (strace::Material* m = strace::SceneService::UniqueMaterial(*water)) m->userParams[4].x = t;
        if (strace::MaterialCache* cache = strace::SceneService::Materials())
            if (strace::Material* m = cache->GetMutable(L"Assets/Materiales/Casas/Halo.strmat")) m->userParams[5].x = t;
    }

    void UpdateFill(float dt, V2 focus) {
        m_focus = focus;
        fill.Update(m_quad, focus, dt, houseShadows);
        if (!water) return;
        float yaw = camera ? camera->transform.rotation[1] / kDeg : 0.0f;
        V2 f = Forward(yaw), r = Right(yaw);
        float minR = 1e9f, maxR = -1e9f, minF = 1e9f, maxF = -1e9f;
        for (const V2& q : m_quad) {
            minR = std::min(minR, Dot(q, r));
            maxR = std::max(maxR, Dot(q, r));
            minF = std::min(minF, Dot(q, f));
            maxF = std::max(maxF, Dot(q, f));
        }
        const float margin = 3.0f;
        V2 center = r * ((minR + maxR) * 0.5f) + f * ((minF + maxF) * 0.5f);
        water->transform.position[0] = center.x;
        water->transform.position[2] = center.z;
        water->transform.rotation[1] = yaw * kDeg;
        water->transform.scale[0] = maxR - minR + margin * 2.0f;
        water->transform.scale[2] = maxF - minF + margin * 2.0f;
        const float side[4] = {-1.0f, 1.0f, 1.0f, -1.0f}, depth[4] = {-1.0f, -1.0f, 1.0f, 1.0f};
        V2 c[4];
        for (int i = 0; i < 4; ++i) c[i] = m_quad[i] + r * (side[i] * margin) + f * (depth[i] * margin);
        if (strace::Material* m = strace::SceneService::UniqueMaterial(*water)) {
            m->userParams[2] = {c[0].x, c[0].z, c[1].x, c[1].z};
            m->userParams[3] = {c[2].x, c[2].z, c[3].x, c[3].z};
        }
    }

private:
    static constexpr float kCell = 24.0f;
    V2 m_quad[4];
    V2 m_focus;
    std::unordered_map<long long, std::vector<int>> m_cells;
    std::mt19937 m_rng{5};

    static long long CellKey(V2 p) {
        long long cx = static_cast<long long>(std::floor(p.x / kCell));
        long long cz = static_cast<long long>(std::floor(p.z / kCell));
        return (cx << 32) | (cz & 0xffffffffLL);
    }

    std::uniform_real_distribution<float> m_dist{0.0f, 1.0f};

    float Signed() { return m_dist(m_rng) * 2.0f - 1.0f; }
};

}
