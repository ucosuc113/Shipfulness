#pragma once

#include "Shipfulness.h"

#include <random>

namespace ship {

struct Tint {
    float r = 1.0f, g = 1.0f, b = 1.0f;
    float weight = 1.0f;
};

struct Blast {
    V2 at;
    V2 push;
    float height = 6.0f;
    float radius = 3.0f;
    float power = 1.0f;
    std::vector<Tint> palette;
};

class Effects {
public:
    void Link() {
        m_chunks.clear();
        m_puffs.clear();
        m_drops.clear();
        m_rings.clear();
        m_sinks.clear();
        strace::GameObject* root = FindRoot("Efectos");
        if (!root) return;
        for (auto& c : root->children) {
            if (!c) continue;
            c->active = false;
            Particle p;
            p.node = c.get();
            p.mat = strace::SceneService::UniqueMaterial(*c);
            if (c->name == "Trozo") m_chunks.push_back(p);
            else if (c->name == "Polvo") m_puffs.push_back(p);
            else if (c->name == "Gota") m_drops.push_back(p);
            else if (c->name == "Onda") m_rings.push_back(p);
        }
    }

    void Clear() {
        for (auto* pool : {&m_chunks, &m_puffs, &m_drops, &m_rings})
            for (Particle& p : *pool) Kill(p);
        m_sinks.clear();
    }

    void Burst(const Blast& b) {
        float power = Clamp(b.power, 0.3f, 2.5f);
        V2 push = Norm(b.push);
        V2 side{-push.z, push.x};
        int chunks = static_cast<int>(10 + 10 * power);
        for (int i = 0; i < chunks; ++i) {
            Particle& p = Grab(m_chunks);
            // Grab devuelve el dummy estatico (node = nullptr) si el pool esta
            // vacio - sin el arbol "Efectos" en la escena, siempre. Spawn y
            // Color ya se protegen contra eso; las escrituras de nodo de abajo
            // eran las unicas sin guarda (crash: escritura en 0x48).
            if (!p.node) continue;
            float h = Rand(0.25f, 1.0f) * b.height;
            V2 o = b.at + push * Rand(-0.6f, 0.4f) * b.radius + side * Rand(-1.0f, 1.0f) * b.radius * 0.8f;
            V2 fling = push * Rand(3.0f, 9.0f) * power + side * Rand(-4.0f, 4.0f) + Norm(o - b.at) * Rand(0.0f, 3.0f);
            float size = Rand(0.25f, 0.9f) * (Rand(0.0f, 1.0f) < 0.15f ? 1.8f : 1.0f);
            Tint t = Pick(b.palette);
            // Mas abanico de tono que antes (0,82-1,08): con la luz de la
            // escena encima, trozos del mismo color salian iguales y se leian
            // como cubos de plastico, no como pedazos de pared.
            float shade = Rand(0.7f, 1.08f);
            Spawn(p, o.x, h, o.z, fling.x, Rand(4.0f, 11.0f) * std::sqrt(power), fling.z, 99.0f);
            p.sx = size * Rand(0.6f, 1.4f);
            p.sy = size * Rand(0.4f, 1.0f);
            p.sz = size * Rand(0.6f, 1.4f);
            p.spinX = Rand(-500.0f, 500.0f);
            p.spinY = Rand(-400.0f, 400.0f);
            p.spinZ = Rand(-500.0f, 500.0f);
            p.rx = Rand(0.0f, 360.0f);
            p.ry = Rand(0.0f, 360.0f);
            p.rz = Rand(0.0f, 360.0f);
            p.floats = Rand(0.0f, 1.0f) < 0.4f;
            Color(p, t.r * shade, t.g * shade, t.b * shade, 1.0f);
            p.node->transform.scale[0] = p.sx;
            p.node->transform.scale[1] = p.sy;
            p.node->transform.scale[2] = p.sz;
        }
        Tint dust = Average(b.palette);
        int puffs = static_cast<int>(8 + 6 * power);
        for (int i = 0; i < puffs; ++i) {
            float a = Rand(0.0f, 2.0f * kPi);
            V2 dir = Forward(a);
            V2 o = b.at + dir * b.radius * Rand(0.5f, 1.1f);
            V2 v = dir * Rand(1.5f, 4.5f) + push * Rand(0.5f, 2.5f);
            Puff(o.x, Rand(0.3f, 1.2f), o.z, v.x, Rand(0.4f, 1.6f), v.z, Rand(1.2f, 2.0f), Rand(3.2f, 5.5f) * std::sqrt(power),
                 Rand(1.6f, 2.6f), Mix(dust, {0.9f, 0.88f, 0.84f}, 0.6f), Rand(0.55f, 0.8f));
        }
        for (int i = 0; i < puffs / 2; ++i) {
            V2 o = b.at + V2{Rand(-1.0f, 1.0f), Rand(-1.0f, 1.0f)} * b.radius * 0.6f;
            V2 v = push * Rand(1.0f, 3.0f);
            Puff(o.x, b.height * Rand(0.4f, 0.9f), o.z, v.x, Rand(1.5f, 3.0f), v.z, Rand(1.0f, 1.8f), Rand(3.0f, 4.5f),
                 Rand(1.8f, 3.0f), Mix(dust, {0.8f, 0.78f, 0.75f}, 0.4f), Rand(0.5f, 0.75f));
        }
        Spray(b.at, b.radius, push, static_cast<int>(14 + 10 * power), 1.0f + 0.4f * power);
        Ring(b.at, b.radius * 0.8f, b.radius + 5.0f * power, 1.6f, 1.0f, 0.22f);
        Ring(b.at + push * 1.5f, b.radius * 0.5f, b.radius + 9.0f * power, 2.6f, 0.7f, 0.12f);
        m_sinks.push_back({b.at, b.radius, 2.6f, 0.0f});
    }

    void Splash(float x, float z, float size) {
        size = Clamp(size, 0.2f, 3.0f);
        Spray({x, z}, size * 0.6f, {0.0f, 1.0f}, static_cast<int>(3 + size * 6), 0.5f + size * 0.35f);
        Ring({x, z}, size * 0.3f, size * 2.2f + 0.8f, 1.1f + size * 0.3f, 0.9f, 0.25f);
        if (size > 1.2f) Puff(x, 0.3f, z, 0.0f, 1.2f, 0.0f, 0.8f, size * 2.0f, 1.2f, {0.95f, 0.97f, 1.0f}, 0.45f);
    }

    void Scrape(V2 at, V2 normal, float strength) {
        strength = Clamp(strength, 0.0f, 1.5f);
        V2 side{-normal.z, normal.x};
        for (int i = 0; i < static_cast<int>(2 + 4 * strength); ++i) {
            Particle& p = Grab(m_drops);
            V2 v = normal * Rand(1.0f, 3.0f) + side * Rand(-2.5f, 2.5f);
            Spawn(p, at.x, 0.2f, at.z, v.x, Rand(2.0f, 5.0f) * (0.6f + strength * 0.5f), v.z, 2.0f);
            p.size = Rand(0.08f, 0.2f);
            p.alpha = 0.9f;
            Color(p, 0.95f, 0.98f, 1.0f, p.alpha);
            Self(p, 0.6f);
        }
        if (strength > 0.4f)
            Puff(at.x, 0.8f, at.z, normal.x, 0.8f, normal.z, 0.5f, 1.6f + strength, 0.9f, {0.86f, 0.82f, 0.76f}, 0.45f);
    }

    // El casco del barco que se rompe (ver HullDamage): chapas que saltan hacia
    // afuera de la banda rota, una nubecita y agua. NO es Burst: Burst es el
    // derrumbe de una casa entera (nubes de 5 m, dos ondas, un hundimiento que
    // gotea dos segundos), y la casa ya lo trae - sumarle otro igual por el barco
    // tapaba el golpe que se queria ver. Lo que rompe el barco son chapas chicas.
    void Shatter(const Blast& b) {
        float power = Clamp(b.power, 0.2f, 1.5f);
        V2 push = Norm(b.push);
        V2 side{-push.z, push.x};
        int chunks = static_cast<int>(5 + 8 * power);
        for (int i = 0; i < chunks; ++i) {
            Particle& p = Grab(m_chunks);
            if (!p.node) continue;
            V2 o = b.at + side * Rand(-1.0f, 1.0f) * b.radius + push * Rand(-0.2f, 0.3f);
            V2 fling = push * Rand(1.5f, 5.0f) * power + side * Rand(-2.0f, 2.0f);
            float size = Rand(0.15f, 0.5f);
            Tint t = Pick(b.palette);
            float shade = Rand(0.85f, 1.05f);
            Spawn(p, o.x, Rand(0.3f, 1.0f) * b.height, o.z, fling.x, Rand(3.0f, 7.5f) * std::sqrt(power), fling.z, 99.0f);
            // Chapa: ancha y fina.
            p.sx = size * Rand(0.8f, 1.5f);
            p.sy = size * Rand(0.12f, 0.3f);
            p.sz = size * Rand(0.8f, 1.5f);
            p.spinX = Rand(-600.0f, 600.0f);
            p.spinY = Rand(-400.0f, 400.0f);
            p.spinZ = Rand(-600.0f, 600.0f);
            p.rx = Rand(0.0f, 360.0f);
            p.ry = Rand(0.0f, 360.0f);
            p.rz = Rand(0.0f, 360.0f);
            p.floats = Rand(0.0f, 1.0f) < 0.5f;
            Color(p, t.r * shade, t.g * shade, t.b * shade, 1.0f);
            p.node->transform.scale[0] = p.sx;
            p.node->transform.scale[1] = p.sy;
            p.node->transform.scale[2] = p.sz;
        }
        for (int i = 0; i < 2 + static_cast<int>(power * 2.0f); ++i) {
            V2 o = b.at + side * Rand(-1.0f, 1.0f) * b.radius;
            V2 v = push * Rand(0.6f, 1.6f);
            Puff(o.x, Rand(0.4f, 1.0f) * b.height, o.z, v.x, Rand(0.4f, 1.0f), v.z, Rand(0.5f, 0.8f), Rand(1.2f, 2.0f) * std::sqrt(power),
                 Rand(0.9f, 1.4f), Mix(Average(b.palette), {0.85f, 0.85f, 0.85f}, 0.5f), Rand(0.45f, 0.6f));
        }
        Spray(b.at, b.radius * 0.6f, push, static_cast<int>(5 + 6 * power), 0.6f + 0.3f * power);
        Ring(b.at, 0.3f, 1.5f + 2.5f * power, 1.0f, 0.8f, 0.25f);
    }

    // Un agujero del casco que hace agua (ver HullDamage): un chorrito que sale
    // por la banda rota, y un anillo donde cae. Lo llama el juego con un
    // temporizador por herida, en coordenadas de MUNDO de este frame, porque la
    // herida viaja con el barco y la particula ya no: el chorro se queda atras
    // cuando el barco avanza, como el agua de verdad.
    void Leak(float x, float y, float z, V2 outward, float strength) {
        strength = Clamp(strength, 0.1f, 1.5f);
        outward = Norm(outward);
        V2 side{-outward.z, outward.x};
        for (int i = 0; i < 1 + static_cast<int>(strength * 2.0f); ++i) {
            Particle& p = Grab(m_drops);
            if (!p.node) return;
            V2 v = outward * Rand(0.8f, 2.2f) * (0.6f + strength) + side * Rand(-0.6f, 0.6f);
            Spawn(p, x, std::max(y, 0.15f), z, v.x, Rand(0.5f, 2.0f), v.z, 2.0f);
            p.size = Rand(0.07f, 0.16f) * (0.7f + strength * 0.5f);
            p.alpha = 0.85f;
            Color(p, 0.95f, 0.98f, 1.0f, p.alpha);
            Self(p, 0.6f);
        }
        if (Rand(0.0f, 1.0f) < 0.45f) {
            V2 at = V2{x, z} + outward * Rand(0.3f, 1.0f);
            Ring(at, 0.1f, Rand(0.5f, 1.1f) * (0.6f + strength * 0.5f), Rand(0.5f, 0.8f), 0.7f, 0.3f);
        }
    }

    // Lo que sube de un casco que se va al fondo: anillos y gotas alrededor,
    // y alguna nube baja. Mismo trato que Leak: mundo, cada frame que haga falta.
    void Bubbles(V2 at, float radius, float strength) {
        V2 o = at + V2{Rand(-1.0f, 1.0f), Rand(-1.0f, 1.0f)} * radius;
        Ring(o, 0.1f, Rand(0.6f, 1.8f) * (0.5f + strength), Rand(0.6f, 1.1f), 0.8f, 0.3f);
        Particle& p = Grab(m_drops);
        if (p.node) {
            Spawn(p, o.x, 0.05f, o.z, Rand(-0.5f, 0.5f), Rand(1.5f, 3.5f) * strength + 0.5f, Rand(-0.5f, 0.5f), 2.0f);
            p.size = Rand(0.1f, 0.25f);
            p.alpha = 0.85f;
            Color(p, 0.95f, 0.98f, 1.0f, p.alpha);
            Self(p, 0.6f);
        }
        if (Rand(0.0f, 1.0f) < 0.12f * strength)
            Puff(o.x, 0.3f, o.z, 0.0f, 0.9f, 0.0f, 0.8f, 2.4f, 1.4f, {0.92f, 0.95f, 1.0f}, 0.45f);
    }

    void Update(float dt) {
        for (Particle& p : m_chunks) {
            if (!p.node->active) continue;
            p.age += dt;
            if (p.state == 0) {
                p.vy -= 22.0f * dt;
                p.x += p.vx * dt;
                p.y += p.vy * dt;
                p.z += p.vz * dt;
                p.rx += p.spinX * dt;
                p.ry += p.spinY * dt;
                p.rz += p.spinZ * dt;
                if (p.y < 0.05f && p.vy < 0.0f) {
                    if (p.vy < -5.0f) Splash(p.x, p.z, (p.sx + p.sy + p.sz) * 0.5f);
                    p.state = p.floats ? 2 : 1;
                    p.age = 0.0f;
                    p.life = p.floats ? Rand(4.0f, 7.0f) : Rand(0.8f, 1.4f);
                    p.vx *= 0.15f;
                    p.vz *= 0.15f;
                    p.vy = -1.2f;
                }
            } else if (p.state == 1) {
                p.y -= 1.6f * dt;
                p.x += p.vx * dt;
                p.z += p.vz * dt;
                float k = 1.0f - p.age / p.life;
                p.node->transform.scale[0] = p.sx * k;
                p.node->transform.scale[1] = p.sy * k;
                p.node->transform.scale[2] = p.sz * k;
                if (p.age >= p.life) Kill(p);
            } else {
                p.vx *= std::exp(-1.2f * dt);
                p.vz *= std::exp(-1.2f * dt);
                p.x += p.vx * dt;
                p.z += p.vz * dt;
                p.rx = Damp(p.rx, std::round(p.rx / 180.0f) * 180.0f, 3.0f, dt);
                p.rz = Damp(p.rz, std::round(p.rz / 180.0f) * 180.0f, 3.0f, dt);
                p.ry += p.spinY * 0.05f * dt;
                float bob = 0.08f * std::sin(p.age * 2.2f + p.seed * 6.0f);
                float sinkIn = Clamp((p.age - p.life + 1.2f) / 1.2f, 0.0f, 1.0f);
                p.y = -p.sy * 0.25f + bob - sinkIn * 1.5f;
                if (p.age >= p.life) Kill(p);
            }
            if (!p.node->active) continue;
            Place(p);
            p.node->transform.rotation[0] = p.rx;
            p.node->transform.rotation[1] = p.ry;
            p.node->transform.rotation[2] = p.rz;
        }
        for (Particle& p : m_puffs) {
            if (!p.node->active) continue;
            p.age += dt;
            float t = p.age / p.life;
            if (t >= 1.0f) {
                Kill(p);
                continue;
            }
            float drag = std::exp(-2.2f * dt);
            p.vx *= drag;
            p.vz *= drag;
            p.vy = p.vy * drag + 0.6f * dt;
            p.x += p.vx * dt;
            p.y += p.vy * dt;
            p.z += p.vz * dt;
            float s = Lerp(p.size, p.size1, 1.0f - std::exp(-3.5f * t));
            p.node->transform.scale[0] = s;
            p.node->transform.scale[1] = s * 0.85f;
            p.node->transform.scale[2] = s;
            Place(p);
            if (p.mat) {
                p.mat->albedoColor.w = p.alpha * (1.0f - t * t);
                p.mat->userParams[0].x = t * 0.9f;
            }
        }
        for (Particle& p : m_drops) {
            if (!p.node->active) continue;
            p.age += dt;
            p.vy -= 24.0f * dt;
            p.x += p.vx * dt;
            p.y += p.vy * dt;
            p.z += p.vz * dt;
            if (p.y < 0.0f && p.vy < 0.0f) {
                Kill(p);
                continue;
            }
            float speed = std::sqrt(p.vx * p.vx + p.vy * p.vy + p.vz * p.vz);
            float horiz = std::sqrt(p.vx * p.vx + p.vz * p.vz);
            p.node->transform.rotation[0] = -std::atan2(p.vy, std::max(horiz, 1e-3f)) * kDeg;
            p.node->transform.rotation[1] = std::atan2(p.vx, p.vz) * kDeg;
            p.node->transform.rotation[2] = 0.0f;
            p.node->transform.scale[0] = p.size;
            p.node->transform.scale[1] = p.size;
            p.node->transform.scale[2] = p.size * (1.0f + speed * 0.12f);
            Place(p);
        }
        for (Particle& p : m_rings) {
            if (!p.node->active) continue;
            p.age += dt;
            float t = p.age / p.life;
            if (t >= 1.0f) {
                Kill(p);
                continue;
            }
            float r = Lerp(p.size, p.size1, 1.0f - std::pow(1.0f - t, 2.6f));
            p.node->transform.scale[0] = r * 2.0f + 1.0f;
            p.node->transform.scale[2] = r * 2.0f + 1.0f;
            if (p.mat) {
                p.mat->userParams[0] = {t, p.alpha * (1.0f - t * t), p.sx, p.seed * 17.0f};
                p.mat->userParams[1] = {p.x, p.z, r, 0.0f};
            }
        }
        for (Sink& s : m_sinks) {
            s.age += dt;
            s.timer -= dt;
            if (s.timer > 0.0f || s.age > s.life) continue;
            s.timer = 0.07f;
            float k = 1.0f - s.age / s.life;
            V2 o = s.at + V2{Rand(-1.0f, 1.0f), Rand(-1.0f, 1.0f)} * s.radius;
            if (Rand(0.0f, 1.0f) < 0.5f) Ring(o, 0.1f, Rand(0.6f, 1.6f) * (0.5f + k), Rand(0.5f, 0.9f), 0.8f * k + 0.2f, 0.35f);
            Particle& p = Grab(m_drops);
            Spawn(p, o.x, 0.05f, o.z, Rand(-0.6f, 0.6f), Rand(1.5f, 3.5f) * k + 0.5f, Rand(-0.6f, 0.6f), 2.0f);
            p.size = Rand(0.1f, 0.25f);
            p.alpha = 0.85f;
            Color(p, 0.95f, 0.98f, 1.0f, p.alpha);
            Self(p, 0.6f);
        }
        m_sinks.erase(std::remove_if(m_sinks.begin(), m_sinks.end(), [](const Sink& s) { return s.age > s.life; }), m_sinks.end());
    }

private:
    struct Particle {
        strace::GameObject* node = nullptr;
        strace::Material* mat = nullptr;
        float x = 0.0f, y = 0.0f, z = 0.0f;
        float vx = 0.0f, vy = 0.0f, vz = 0.0f;
        float rx = 0.0f, ry = 0.0f, rz = 0.0f;
        float spinX = 0.0f, spinY = 0.0f, spinZ = 0.0f;
        float sx = 1.0f, sy = 1.0f, sz = 1.0f;
        float size = 1.0f, size1 = 1.0f;
        float age = 0.0f, life = 1.0f;
        float alpha = 1.0f;
        float seed = 0.0f;
        int state = 0;
        bool floats = false;
    };

    struct Sink {
        V2 at;
        float radius;
        float life;
        float age;
        float timer = 0.0f;
    };

    std::vector<Particle> m_chunks, m_puffs, m_drops, m_rings;
    std::vector<Sink> m_sinks;
    std::mt19937 m_rng{1234};

    float Rand(float a, float b) { return std::uniform_real_distribution<float>(a, b)(m_rng); }

    static Tint Mix(Tint a, Tint b, float t) { return {Lerp(a.r, b.r, t), Lerp(a.g, b.g, t), Lerp(a.b, b.b, t), 1.0f}; }

    Tint Pick(const std::vector<Tint>& palette) {
        if (palette.empty()) return {0.85f, 0.8f, 0.75f, 1.0f};
        float total = 0.0f;
        for (const Tint& t : palette) total += t.weight;
        float r = Rand(0.0f, total);
        for (const Tint& t : palette) {
            r -= t.weight;
            if (r <= 0.0f) return t;
        }
        return palette.back();
    }

    static Tint Average(const std::vector<Tint>& palette) {
        Tint out{0.0f, 0.0f, 0.0f, 1.0f};
        float total = 0.0f;
        for (const Tint& t : palette) {
            out.r += t.r * t.weight;
            out.g += t.g * t.weight;
            out.b += t.b * t.weight;
            total += t.weight;
        }
        if (total <= 0.0f) return {0.85f, 0.8f, 0.75f, 1.0f};
        return {out.r / total, out.g / total, out.b / total, 1.0f};
    }

    Particle& Grab(std::vector<Particle>& pool) {
        static Particle none;
        if (pool.empty()) return none;
        Particle* best = &pool.front();
        float oldest = -1.0f;
        for (Particle& p : pool) {
            if (!p.node->active) return p;
            float k = p.age / std::max(p.life, 1e-3f);
            if (k > oldest) {
                oldest = k;
                best = &p;
            }
        }
        return *best;
    }

    void Spawn(Particle& p, float x, float y, float z, float vx, float vy, float vz, float life) {
        if (!p.node) return;
        p.x = x;
        p.y = y;
        p.z = z;
        p.vx = vx;
        p.vy = vy;
        p.vz = vz;
        p.age = 0.0f;
        p.life = life;
        p.state = 0;
        p.seed = Rand(0.0f, 1.0f);
        p.node->active = true;
        p.node->transform.rotation[0] = 0.0f;
        p.node->transform.rotation[1] = 0.0f;
        p.node->transform.rotation[2] = 0.0f;
        Place(p);
    }

    static void Place(Particle& p) {
        p.node->transform.position[0] = p.x;
        p.node->transform.position[1] = p.y;
        p.node->transform.position[2] = p.z;
    }

    static void Kill(Particle& p) {
        if (!p.node) return;
        p.node->active = false;
        p.node->transform.position[1] = -50.0f;
    }

    static void Color(Particle& p, float r, float g, float b, float a) {
        if (p.mat) p.mat->albedoColor = {r, g, b, a};
    }

    static void Self(Particle& p, float self) {
        if (p.mat) p.mat->userParams[0] = {0.0f, 0.5f, p.seed * 9.0f, self};
    }

    void Puff(float x, float y, float z, float vx, float vy, float vz, float size0, float size1, float life, Tint c, float alpha) {
        Particle& p = Grab(m_puffs);
        if (!p.node) return;
        Spawn(p, x, y, z, vx, vy, vz, life);
        p.size = size0;
        p.size1 = size1;
        p.alpha = alpha;
        p.node->transform.rotation[1] = Rand(0.0f, 360.0f);
        Color(p, c.r, c.g, c.b, alpha);
        if (p.mat) p.mat->userParams[0] = {0.0f, 0.65f, p.seed * 9.0f, 0.3f};
    }

    void Spray(V2 at, float radius, V2 push, int count, float power) {
        for (int i = 0; i < count; ++i) {
            Particle& p = Grab(m_drops);
            if (!p.node) return;
            float a = Rand(0.0f, 2.0f * kPi);
            V2 dir = Forward(a);
            V2 o = at + dir * radius * Rand(0.6f, 1.1f);
            V2 v = dir * Rand(1.5f, 5.0f) * power + push * Rand(0.0f, 3.0f);
            Spawn(p, o.x, 0.1f, o.z, v.x, Rand(5.0f, 11.0f) * power, v.z, 3.0f);
            p.size = Rand(0.12f, 0.35f) * std::sqrt(power);
            p.alpha = Rand(0.75f, 0.95f);
            Color(p, 0.95f, 0.98f, 1.0f, p.alpha);
            Self(p, 0.6f);
        }
    }

    void Ring(V2 at, float r0, float r1, float life, float alpha, float width) {
        Particle& p = Grab(m_rings);
        if (!p.node) return;
        Spawn(p, at.x, 0.08f, at.z, 0.0f, 0.0f, 0.0f, life);
        p.size = r0;
        p.size1 = r1;
        p.alpha = alpha;
        p.sx = width;
        p.node->transform.scale[0] = r0 * 2.0f + 1.0f;
        p.node->transform.scale[1] = 1.0f;
        p.node->transform.scale[2] = r0 * 2.0f + 1.0f;
        if (p.mat) {
            p.mat->userParams[0] = {0.0f, alpha, width, p.seed * 17.0f};
            p.mat->userParams[1] = {at.x, at.z, std::max(r0, 0.05f), 0.0f};
            p.mat->userParams[2] = {0.95f, 0.98f, 1.0f, 0.0f};
        }
    }
};

}
