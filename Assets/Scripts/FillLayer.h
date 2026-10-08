#pragma once

#include "Shipfulness.h"

#include <cstdint>
#include <filesystem>
#include <unordered_map>
#include <unordered_set>

namespace ship {

struct FillSlot {
    long long key = 0;
    V2 pos;
    float yaw = 0.0f;
    Obb obb;
    int mesh = 0;
};

class FillLayer {
public:
    struct HouseMesh {
        float w, d;
        std::wstring path;
        std::wstring island;
    };

    void Setup(strace::GameObject* pool, uint64_t seed, std::vector<V2> route, std::vector<float> widths,
               std::vector<std::pair<V2, float>> basins, std::vector<std::pair<V2, V2>> seas) {
        m_nodes.clear();
        m_entries.clear();
        m_assigned.clear();
        m_blocks.clear();
        m_raw.clear();
        m_destroyed.clear();
        m_occupied.clear();
        m_seed = seed;
        m_route = std::move(route);
        m_widths = std::move(widths);
        m_basins = std::move(basins);
        m_seas = std::move(seas);
        if (pool)
            for (auto& c : pool->children) {
                c->active = false;
                m_nodes.push_back(c.get());
                m_entries.push_back({});
            }
        if (m_meshes.empty()) ScanMeshes();
    }

    void Occupy(const Obb& o, float grow) {
        float hx = o.hx + grow, hz = o.hz + grow;
        float ext = std::sqrt(hx * hx + hz * hz);
        int x0 = static_cast<int>(std::floor((o.center.x - ext) / kOcc)), x1 = static_cast<int>(std::floor((o.center.x + ext) / kOcc));
        int z0 = static_cast<int>(std::floor((o.center.z - ext) / kOcc)), z1 = static_cast<int>(std::floor((o.center.z + ext) / kOcc));
        for (int x = x0; x <= x1; ++x)
            for (int z = z0; z <= z1; ++z) {
                V2 c{(x + 0.5f) * kOcc, (z + 0.5f) * kOcc};
                V2 d = c - o.center;
                if (std::fabs(Dot(d, o.axisX)) <= hx + kOcc * 0.5f && std::fabs(Dot(d, o.axisZ)) <= hz + kOcc * 0.5f)
                    m_occupied.insert(Pack(x, z));
            }
    }

    void Restore() {
        for (size_t i = 0; i < m_nodes.size(); ++i) {
            m_nodes[i]->active = false;
            m_entries[i] = {};
        }
        m_assigned.clear();
        m_destroyed.clear();
    }

    void Update(const V2 quad[4], V2 focus, float dt) {
        for (size_t i = 0; i < m_entries.size(); ++i) {
            Entry& e = m_entries[i];
            if (e.fall < 0.0f) continue;
            e.fall += dt;
            float t = Clamp(e.fall / 2.2f, 0.0f, 1.0f);
            float k = t * t * (3.0f - 2.0f * t);
            strace::GameObject* n = m_nodes[i];
            float lx = Dot(e.push, Right(e.slot.yaw)), lz = Dot(e.push, Forward(e.slot.yaw));
            n->transform.position[0] = e.slot.pos.x + e.push.x * k * 2.0f;
            n->transform.position[2] = e.slot.pos.z + e.push.z * k * 2.0f;
            n->transform.position[1] = -5.5f * k * k;
            n->transform.rotation[0] = lz * 38.0f * k;
            n->transform.rotation[2] = -lx * 38.0f * k;
            if (t >= 1.0f) {
                m_assigned.erase(e.slot.key);
                e = {};
                n->active = false;
            }
        }
        m_timer -= dt;
        if (m_timer > 0.0f) return;
        m_timer = 0.1f;
        float minX = 1e9f, maxX = -1e9f, minZ = 1e9f, maxZ = -1e9f;
        for (int i = 0; i < 4; ++i) {
            minX = std::min(minX, quad[i].x);
            maxX = std::max(maxX, quad[i].x);
            minZ = std::min(minZ, quad[i].z);
            maxZ = std::max(maxZ, quad[i].z);
        }
        float area = 0.0f;
        for (int i = 0; i < 4; ++i) area += quad[i].x * quad[(i + 1) % 4].z - quad[(i + 1) % 4].x * quad[i].z;
        float orient = area >= 0.0f ? 1.0f : -1.0f;
        m_wanted.clear();
        int bx0 = static_cast<int>(std::floor(minX / kBlock)) - 1, bx1 = static_cast<int>(std::floor(maxX / kBlock)) + 1;
        int bz0 = static_cast<int>(std::floor(minZ / kBlock)) - 1, bz1 = static_cast<int>(std::floor(maxZ / kBlock)) + 1;
        for (int bx = bx0; bx <= bx1; ++bx)
            for (int bz = bz0; bz <= bz1; ++bz)
                for (const FillSlot& s : Block(bx, bz)) {
                    if (m_destroyed.count(s.key)) continue;
                    bool in = true;
                    for (int i = 0; i < 4 && in; ++i) {
                        V2 a = quad[i], b = quad[(i + 1) % 4], e = Norm(b - a);
                        if ((e.x * (s.pos.z - a.z) - e.z * (s.pos.x - a.x)) * orient < -10.0f) in = false;
                    }
                    if (in) m_wanted.push_back({Dot(s.pos - focus, s.pos - focus), &s});
                }
        std::sort(m_wanted.begin(), m_wanted.end(), [](const auto& a, const auto& b) { return a.first < b.first; });
        if (m_wanted.size() > m_nodes.size()) m_wanted.resize(m_nodes.size());
        m_keep.clear();
        for (const auto& w : m_wanted) m_keep.insert(w.second->key);
        for (size_t i = 0; i < m_entries.size(); ++i) {
            Entry& e = m_entries[i];
            if (e.used && e.fall < 0.0f && !m_keep.count(e.slot.key)) {
                m_assigned.erase(e.slot.key);
                e = {};
                m_nodes[i]->active = false;
            }
        }
        size_t free = 0;
        for (const auto& w : m_wanted) {
            if (m_assigned.count(w.second->key)) continue;
            while (free < m_entries.size() && m_entries[free].used) ++free;
            if (free >= m_entries.size()) break;
            Entry& e = m_entries[free];
            e.used = true;
            e.slot = *w.second;
            e.fall = -1.0f;
            strace::GameObject* n = m_nodes[free];
            const HouseMesh& hm = m_meshes[static_cast<size_t>(e.slot.mesh)];
            n->meshPath = hm.path;
            for (auto& c : n->children)
                if (c && c->name == "Islote") {
                    c->meshPath = hm.island;
                    c->active = !hm.island.empty();
                }
            n->transform.position[0] = e.slot.pos.x;
            n->transform.position[1] = 0.0f;
            n->transform.position[2] = e.slot.pos.z;
            n->transform.rotation[0] = 0.0f;
            n->transform.rotation[1] = e.slot.yaw * kDeg;
            n->transform.rotation[2] = 0.0f;
            n->active = true;
            m_assigned[e.slot.key] = static_cast<int>(free);
        }
    }

    template <class F>
    void ForEachSolid(V2 at, float reach, F&& f) {
        int bx0 = static_cast<int>(std::floor((at.x - reach) / kBlock)), bx1 = static_cast<int>(std::floor((at.x + reach) / kBlock));
        int bz0 = static_cast<int>(std::floor((at.z - reach) / kBlock)), bz1 = static_cast<int>(std::floor((at.z + reach) / kBlock));
        for (int bx = bx0; bx <= bx1; ++bx)
            for (int bz = bz0; bz <= bz1; ++bz)
                for (const FillSlot& s : Block(bx, bz))
                    if (!m_destroyed.count(s.key) && Len(s.pos - at) < reach) f(s);
    }

    void Collapse(long long key, V2 push) {
        if (m_destroyed.count(key)) return;
        m_destroyed.insert(key);
        auto it = m_assigned.find(key);
        if (it == m_assigned.end()) return;
        Entry& e = m_entries[static_cast<size_t>(it->second)];
        e.fall = 0.0f;
        e.push = Norm(push);
    }

    int Destroyed() const { return static_cast<int>(m_destroyed.size()); }
    int Visible() const { return static_cast<int>(m_assigned.size()); }
    bool Ready() const { return !m_meshes.empty() && !m_nodes.empty(); }

private:
    static constexpr float kBlock = 14.0f;
    static constexpr float kOcc = 1.5f;

    struct Entry {
        bool used = false;
        FillSlot slot;
        float fall = -1.0f;
        V2 push;
    };

    std::vector<strace::GameObject*> m_nodes;
    std::vector<Entry> m_entries;
    std::vector<HouseMesh> m_meshes;
    std::unordered_map<long long, std::vector<FillSlot>> m_blocks;
    std::unordered_map<long long, std::vector<FillSlot>> m_raw;
    std::unordered_map<long long, int> m_assigned;
    std::unordered_set<long long> m_destroyed;
    std::unordered_set<long long> m_occupied;
    std::unordered_set<long long> m_keep;
    std::vector<std::pair<float, const FillSlot*>> m_wanted;
    std::vector<V2> m_route;
    std::vector<float> m_widths;
    std::vector<std::pair<V2, float>> m_basins;
    std::vector<std::pair<V2, V2>> m_seas;
    uint64_t m_seed = 1;
    float m_timer = 0.0f;

    static long long Pack(int x, int z) {
        return (static_cast<long long>(x) << 32) | (static_cast<long long>(z) & 0xffffffffLL);
    }

    static uint64_t Mix(uint64_t x) {
        x += 0x9e3779b97f4a7c15ULL;
        x = (x ^ (x >> 30)) * 0xbf58476d1ce4e5b9ULL;
        x = (x ^ (x >> 27)) * 0x94d049bb133111ebULL;
        return x ^ (x >> 31);
    }

    void ScanMeshes() {
        namespace fs = std::filesystem;
        std::error_code ec;
        fs::path dir = fs::path(strace::SceneService::ProjectRoot()) / L"Assets" / L"Modelos";
        for (auto& e : fs::directory_iterator(dir, ec)) {
            std::wstring n = e.path().filename().wstring();
            int w = 0, d = 0;
            wchar_t tag[64] = {};
            if (e.path().extension() != L".strmesh" || std::swscanf(n.c_str(), L"Casa_%d_%d_%63[^.]", &w, &d, tag) != 3) continue;
            std::wstring island = L"Islote_" + std::wstring(tag) + L".strmesh";
            m_meshes.push_back({w / 10.0f, d / 10.0f, L"Assets/Modelos/" + n,
                                fs::exists(dir / island, ec) ? L"Assets/Modelos/" + island : std::wstring()});
        }
        std::sort(m_meshes.begin(), m_meshes.end(), [](const HouseMesh& a, const HouseMesh& b) { return a.path < b.path; });
    }

    float Clearance(V2 p) const {
        float best = 1e9f;
        for (size_t i = 0; i + 1 < m_route.size(); ++i) {
            V2 a = m_route[i], b = m_route[i + 1], ab = b - a;
            float t = Clamp(Dot(p - a, ab) / std::max(Dot(ab, ab), 1e-6f), 0.0f, 1.0f);
            best = std::min(best, Len(p - (a + ab * t)) - Lerp(m_widths[i], m_widths[i + 1], t));
        }
        for (const auto& b : m_basins) best = std::min(best, Len(p - b.first) - b.second);
        for (const auto& m : m_seas) best = std::min(best, Dot(m.first - p, m.second));
        return best;
    }

    float FlowYaw(V2 p) const {
        if (m_route.size() < 2) return 0.0f;
        size_t best = 0;
        float bd = 1e9f;
        for (size_t i = 0; i + 1 < m_route.size(); ++i) {
            float d = Len(m_route[i] - p);
            if (d < bd) {
                bd = d;
                best = i;
            }
        }
        V2 dir = Norm(m_route[best + 1] - m_route[best]);
        return std::atan2(dir.x, dir.z);
    }

    bool Free(const Obb& o) const {
        for (int cx = -1; cx <= 1; ++cx)
            for (int cz = -1; cz <= 1; ++cz) {
                V2 p = o.center + o.axisX * (cx * o.hx) + o.axisZ * (cz * o.hz);
                if (m_occupied.count(Pack(static_cast<int>(std::floor(p.x / kOcc)), static_cast<int>(std::floor(p.z / kOcc)))))
                    return false;
                if ((cx != 0 || cz != 0) && Clearance(p) < 1.0f) return false;
            }
        return true;
    }

    static bool Overlap(const Obb& a, const Obb& b, float shrink) {
        V2 d = b.center - a.center;
        if (Len(d) > a.hx + a.hz + b.hx + b.hz) return false;
        const V2 axes[4] = {a.axisX, a.axisZ, b.axisX, b.axisZ};
        for (V2 ax : axes) {
            float ra = a.hx * std::fabs(Dot(a.axisX, ax)) + a.hz * std::fabs(Dot(a.axisZ, ax));
            float rb = b.hx * std::fabs(Dot(b.axisX, ax)) + b.hz * std::fabs(Dot(b.axisZ, ax));
            if (std::fabs(Dot(d, ax)) > ra + rb - shrink) return false;
        }
        return true;
    }

    const std::vector<FillSlot>& Raw(int bx, int bz) {
        long long bkey = Pack(bx, bz);
        auto it = m_raw.find(bkey);
        if (it != m_raw.end()) return it->second;
        std::vector<FillSlot>& out = m_raw[bkey];
        if (m_meshes.empty()) return out;
        uint64_t h = Mix(m_seed ^ Mix(static_cast<uint64_t>(bkey)));
        auto rnd = [&]() {
            h = Mix(h);
            return static_cast<float>(h >> 40) / static_cast<float>(1ull << 24);
        };
        V2 center{(bx + 0.5f) * kBlock, (bz + 0.5f) * kBlock};
        if (Clearance(center) < -2.0f) return out;
        float yaw = FlowYaw(center) + (rnd() - 0.5f) * 0.9f;
        if (rnd() < 0.2f) yaw = rnd() * 2.0f * kPi;
        V2 ax = Right(yaw), az = Forward(yaw);
        const float sub = kBlock * 0.25f;
        for (int k = 0; k < 4; ++k) {
            if (rnd() < 0.06f) continue;
            const HouseMesh* m = &m_meshes[static_cast<size_t>(rnd() * m_meshes.size()) % m_meshes.size()];
            float lx = (k % 2 == 0 ? -sub : sub) + (rnd() - 0.5f) * 2.4f;
            float lz = (k < 2 ? -sub : sub) + (rnd() - 0.5f) * 2.4f;
            float turn = rnd();
            float houseYaw = yaw + (turn < 0.35f ? kPi : 0.0f) + (turn > 0.7f ? kPi * 0.5f : 0.0f) + (rnd() - 0.5f) * 0.5f;
            FillSlot s;
            s.key = static_cast<long long>((Mix(static_cast<uint64_t>(bkey)) & 0x03ffffffffffffffULL) * 16ULL) + k;
            s.pos = center + ax * lx + az * lz;
            s.yaw = houseYaw;
            s.mesh = static_cast<int>(m - m_meshes.data());
            s.obb.center = s.pos;
            s.obb.axisX = Right(houseYaw);
            s.obb.axisZ = Forward(houseYaw);
            s.obb.hx = m->w * 0.5f + 0.05f;
            s.obb.hz = m->d * 0.5f + 0.05f;
            bool ok = Free(s.obb);
            for (const FillSlot& o : out)
                if (ok && Overlap(o.obb, s.obb, 1.1f)) ok = false;
            if (ok) out.push_back(s);
        }
        return out;
    }

    const std::vector<FillSlot>& Block(int bx, int bz) {
        long long bkey = Pack(bx, bz);
        auto it = m_blocks.find(bkey);
        if (it != m_blocks.end()) return it->second;
        std::vector<FillSlot> mine = Raw(bx, bz);
        std::vector<FillSlot>& out = m_blocks[bkey];
        for (const FillSlot& s : mine) {
            bool ok = true;
            for (int nx = bx - 1; nx <= bx + 1 && ok; ++nx)
                for (int nz = bz - 1; nz <= bz + 1 && ok; ++nz) {
                    if (nx > bx || (nx == bx && nz >= bz)) continue;
                    for (const FillSlot& o : Raw(nx, nz))
                        if (Overlap(o.obb, s.obb, 1.1f)) {
                            ok = false;
                            break;
                        }
                }
            if (ok) out.push_back(s);
        }
        return out;
    }
};

}
