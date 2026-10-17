#pragma once

// EL CASCO SE ROMPE DONDE CHOCA: la malla del barco, abollada y agujereada.
//
// Un choque deja una HERIDA en coordenadas del barco (no del mundo: la herida
// viaja con el barco). Una herida hace dos cosas a la malla:
//
//   abolla   los vertices dentro de un elipsoide alrededor del punto se corren
//            hacia ADENTRO, por la normal del choque, con una caida suave y un
//            ruido por vertice (chapa arrugada, no un hundimiento de goma).
//   agujerea las caras cuyo centro cae en el nucleo de la herida desaparecen.
//            El borde del nucleo tambien lleva ruido, por cara: un agujero
//            redondo se lee como un circulo dibujado, uno mellado como un golpe.
//
// Por eso el casco viene cortado en una grilla (Barco.strmesh se importo asi):
// las caras miden ~0,4 m y hay vertices donde doblar.
//
// TODO SE CALCULA DESDE LA MALLA PRISTINA, y no sobre lo ya abollado. Es lo que
// mantiene la malla cerrada: el casco tiene vertices repetidos (uno por cada
// normal o uv distinto en el mismo punto), y si el desplazamiento dependiera de
// la posicion ACTUAL, dos copias del mismo punto podrian divergir en un bit y
// abrir una grieta. Con la posicion original como entrada, la cuenta y el ruido
// dan exactamente lo mismo para todas las copias. Las heridas se SUMAN.
//
// COMO LLEGA AL RENDERER: MeshCache::Upload. La malla herida se arma en memoria
// y se sube a la cache bajo una clave que no es un archivo (kKey); el nodo
// apunta a esa clave mientras haya dano, y vuelve a la ruta del .strmesh al
// reiniciar. Cada herida vuelve a subir la MISMA clave y la cache la reemplaza
// en el sitio, devolviendo los buffers viejos.
//
// La primera version, cuando el motor no tenia Upload, escribia la malla a la
// carpeta temporal y apuntaba el nodo ahi (una ruta absoluta pisa a la carpeta
// del proyecto): un megabyte escrito y releido por choque, un nombre nuevo cada
// vez para que la cache no siguiera dibujando la version vieja, y archivos que
// limpiar al salir. Nada de eso hace falta ya.
//
// Lo subido es de runtime: "Recargar del disco" en el Editor lo suelta, pero el
// Editor no recarga durante Play, que es el unico momento en que hay dano.

#include "Shipfulness.h"
#include "Engine/Diagnostics/Log.h"
#include "Engine/Rendering/MeshCache.h"

#include <cstdint>

namespace ship {

struct HullWound {
    DirectX::XMFLOAT3 at;  // centro, en coordenadas del barco
    V2 inward;             // hacia donde se hundio la chapa (x, z del barco)
    float radius = 1.0f;
    float height = 0.0f;   // medio alto del elipsoide; 0 = radius * 1,35
    float severity = 0.0f; // la integridad que se llevo, sobre la que habia que perder para hundirse (gotea mas)
    bool holed = false;
    float leakTimer = 0.0f;
};

class HullDamage {
public:
    // Cuanto se rompio en la ultima herida, para los trozos que salen volando:
    // el color de lo que se rompio, pesado por cuantas caras de cada submalla
    // se fueron (o se abollaron, si no hubo agujero).
    struct Piece {
        float r, g, b, weight;
    };

    bool Attach(strace::GameObject* hull) {
        m_node = hull;
        m_ready = false;
        if (!m_node || m_node->meshPath.empty()) return false;
        m_basePath = m_node->meshPath;
        strace::MeshCache* cache = strace::SceneService::Meshes();
        if (!cache) return false;
        auto r = cache->Resolve(strace::SceneService::ProjectRoot(), m_basePath);
        if (!r.mesh) return false;
        m_base = *r.mesh;
        const size_t tris = m_base.indices.size() / 3;
        // El signo de cada cara: el producto cruz de sus vertices contra la
        // normal que trajo el archivo. Asi recalcular normales no depende de
        // saber de que lado gira el motor (zurdo, y con la V dada vuelta).
        m_sign.assign(tris, 1.0f);
        m_triSub.assign(tris, 0);
        for (size_t s = 0; s < m_base.submeshes.size(); ++s) {
            const strace::SubMesh& sub = m_base.submeshes[s];
            for (uint32_t k = sub.firstIndex; k + 2 < sub.firstIndex + sub.indexCount; k += 3) m_triSub[k / 3] = static_cast<uint16_t>(s);
        }
        for (size_t t = 0; t < tris; ++t) {
            const auto& a = m_base.vertices[m_base.indices[t * 3]];
            const auto& b = m_base.vertices[m_base.indices[t * 3 + 1]];
            const auto& c = m_base.vertices[m_base.indices[t * 3 + 2]];
            DirectX::XMFLOAT3 n = Cross(Sub(b.position, a.position), Sub(c.position, a.position));
            float d = n.x * (a.normal.x + b.normal.x + c.normal.x) + n.y * (a.normal.y + b.normal.y + c.normal.y) +
                      n.z * (a.normal.z + b.normal.z + c.normal.z);
            m_sign[t] = d < 0.0f ? -1.0f : 1.0f;
        }
        // El AREA de cada cara, de la malla pristina: la integridad es area
        // sana sobre area total, y no caras sobre caras - la grilla de corte
        // deja astillas en las esquinas y caras enteras en lo liso, y contarlas
        // igual haria que un golpe en la cabina (llena de astillas) pesara mas
        // que uno en la cubierta.
        m_triArea.assign(tris, 0.0f);
        m_totalArea = 0.0;
        for (size_t t = 0; t < tris; ++t) {
            DirectX::XMFLOAT3 n = Cross(Sub(m_base.vertices[m_base.indices[t * 3 + 1]].position, m_base.vertices[m_base.indices[t * 3]].position),
                                        Sub(m_base.vertices[m_base.indices[t * 3 + 2]].position, m_base.vertices[m_base.indices[t * 3]].position));
            m_triArea[t] = 0.5f * std::sqrt(n.x * n.x + n.y * n.y + n.z * n.z);
            m_totalArea += m_triArea[t];
        }
        // LA ALTURA DE LA BORDA a lo largo del casco, en franjas de kBand: el
        // punto mas alto de la chapa de los COSTADOS (lo que esta a mas del 80 %
        // de la manga de esa franja; asi la cabina, mas angosta, no cuenta). Es
        // para centrar las heridas: la proa del barco esta a 2,1 m y la popa a
        // 1,5, y una herida a altura fija rompia la proa por debajo, donde la
        // camara de arriba no la ve.
        const float z0 = m_base.boundsMin.z, z1 = m_base.boundsMax.z;
        m_bandZ0 = z0;
        const int bands = std::max(1, static_cast<int>(std::ceil((z1 - z0) / kBand)));
        std::vector<float> wide(bands, 0.0f);
        m_gunwale.assign(bands, 0.0f);
        auto bandOf = [&](float z) { return std::clamp(static_cast<int>((z - z0) / kBand), 0, bands - 1); };
        for (const auto& v : m_base.vertices) wide[bandOf(v.position.z)] = std::max(wide[bandOf(v.position.z)], std::fabs(v.position.x));
        for (const auto& v : m_base.vertices) {
            int b = bandOf(v.position.z);
            if (std::fabs(v.position.x) >= wide[b] * 0.8f) m_gunwale[b] = std::max(m_gunwale[b], v.position.y);
        }
        m_ready = true;
        Reset();
        return true;
    }

    bool Ready() const { return m_ready; }

    // La altura de la borda en este z del barco (ver Attach). 1,2 m sin malla.
    float GunwaleAt(float z) const {
        if (m_gunwale.empty()) return 1.2f;
        int b = std::clamp(static_cast<int>((z - m_bandZ0) / kBand), 0, static_cast<int>(m_gunwale.size()) - 1);
        return m_gunwale[b];
    }

    void Reset() {
        m_wounds.clear();
        if (!m_ready) return;
        m_work = m_base.vertices;
        m_alive.assign(m_base.indices.size() / 3, 1);
        m_damage.assign(m_base.indices.size() / 3, 0.0f);
        m_lostArea = 0.0;
        m_touched.assign(m_base.vertices.size(), 0);
        Publish(true);
    }

    const std::vector<HullWound>& Wounds() const { return m_wounds; }
    std::vector<HullWound>& Wounds() { return m_wounds; }
    const std::vector<Piece>& LastPieces() const { return m_pieces; }

    // EL CASCO EN PIE, 1 = sano: el area de chapa que sigue en pie sobre el area
    // total, con cada cara pesada por su dano (1 si se cayo; lo abollado, una
    // parte, ver kDentWeight). ShipHealth la convierte en la integridad que ve
    // el jugador.
    float StandingArea() const { return m_totalArea > 0.0 ? static_cast<float>(1.0 - m_lostArea / m_totalArea) : 1.0f; }

    // La herida que haya a menos de `reach` metros de este punto (del barco),
    // o -1: volver a pegar donde ya hay un agujero lo agranda, y hunde mas.
    int WoundNear(DirectX::XMFLOAT3 at, float reach) const {
        int best = -1;
        float bd = reach * reach;
        for (size_t i = 0; i < m_wounds.size(); ++i) {
            float dx = m_wounds[i].at.x - at.x, dz = m_wounds[i].at.z - at.z;
            if (dx * dx + dz * dz < bd) {
                bd = dx * dx + dz * dz;
                best = static_cast<int>(i);
            }
        }
        return best;
    }

    // radius: medio ancho de la zona abollada. depth: cuanto se hunde la chapa
    // en el centro. hole: el radio del nucleo que se lleva las caras, como
    // fraccion de radius (0 = solo abolla). Devuelve las caras que se fueron.
    int Wound(const HullWound& w, float depth, float hole) {
        m_wounds.push_back(w);
        m_pieces.clear();
        if (!m_ready) return 0;
        const float r = std::max(w.radius, 0.1f);
        // Mas alto que ancho: la chapa del costado se rompe de la flotacion a la
        // borda, y un golpe a la altura del agua igual muerde la cubierta.
        const float ry = w.height > 0.0f ? w.height : r * 1.35f;
        V2 in = Norm(w.inward);
        std::vector<float> subWeight(m_base.submeshes.size(), 0.0f);
        for (size_t i = 0; i < m_base.vertices.size(); ++i) {
            const DirectX::XMFLOAT3& p = m_base.vertices[i].position;
            float dx = (p.x - w.at.x) / r, dy = (p.y - w.at.y) / ry, dz = (p.z - w.at.z) / r;
            float e = dx * dx + dy * dy + dz * dz;
            if (e >= 1.0f) continue;
            float f = (1.0f - e) * (1.0f - e);
            float n = Noise(p.x * 3.1f, p.y * 2.7f, p.z * 3.3f);
            float push = depth * f * (0.55f + 0.9f * n);
            // Ademas de hundirse, la chapa se arruga: un poco de corrimiento
            // vertical por vertice, con el mismo ruido (o sea, igual en las copias).
            float wrinkle = depth * f * 0.35f * (Noise(p.z * 4.1f, p.x * 3.7f, p.y * 2.9f) - 0.5f);
            m_work[i].position.x += in.x * push;
            m_work[i].position.z += in.z * push;
            m_work[i].position.y += wrinkle - depth * f * 0.15f;
            m_touched[i] = 1;
        }
        int removed = 0;
        const size_t tris = m_alive.size();
        for (size_t t = 0; t < tris; ++t) {
            if (!m_alive[t]) continue;
            const auto& a = m_base.vertices[m_base.indices[t * 3]].position;
            const auto& b = m_base.vertices[m_base.indices[t * 3 + 1]].position;
            const auto& c = m_base.vertices[m_base.indices[t * 3 + 2]].position;
            float cx = (a.x + b.x + c.x) / 3.0f, cy = (a.y + b.y + c.y) / 3.0f, cz = (a.z + b.z + c.z) / 3.0f;
            float dx = (cx - w.at.x) / r, dy = (cy - w.at.y) / ry, dz = (cz - w.at.z) / r;
            float e = std::sqrt(dx * dx + dy * dy + dz * dz);
            if (e >= 1.0f) continue;
            // El borde: la mitad un ruido suave (la forma general del desgarro) y
            // la mitad un numero suelto por cara. Con la grilla de corte, cada
            // celda son dos triangulos en diagonal, y el numero por cara es lo
            // que convierte un borde de escalones en dientes de sierra.
            float jag = 0.5f * Noise(cx * 1.7f, cy * 1.9f, cz * 1.3f) +
                        0.5f * Hash(static_cast<int>(std::floor(cx * 9.0f)), static_cast<int>(std::floor(cy * 9.0f)),
                                    static_cast<int>(std::floor(cz * 9.0f)));
            float dmg;
            if (hole > 0.0f && e < hole * (0.65f + 0.7f * jag)) {
                m_alive[t] = 0;
                ++removed;
                subWeight[m_triSub[t]] += 1.0f;
                dmg = 1.0f;
            } else {
                subWeight[m_triSub[t]] += 0.15f * (1.0f - e);
                dmg = kDentWeight * std::min(1.0f, depth / kDeepDent) * (1.0f - e);
            }
            // El dano de una cara es el PEOR que recibio, no la suma: volver a
            // pegar donde ya hay un agujero solo resta lo que se rompe de nuevo
            // alrededor (el borde que se agranda), que es lo que pasa con una
            // chapa que ya no esta.
            if (dmg > m_damage[t]) {
                m_lostArea += static_cast<double>(m_triArea[t]) * (dmg - m_damage[t]);
                m_damage[t] = dmg;
            }
        }
        for (size_t s = 0; s < subWeight.size(); ++s)
            if (subWeight[s] > 0.0f) {
                const auto& c = m_base.submeshes[s].baseColor;
                m_pieces.push_back({c.x, c.y, c.z, subWeight[s]});
            }
        Publish(false);
        return removed;
    }

    // CON LO QUE CHOCA EL BARCO: circulos INSCRITOS en el casco que sigue en
    // pie, en coordenadas del barco (ver ShipSim::SetHullShape). Vacio sin
    // malla.
    //
    // De la malla viva y deformada, no de las heridas: una herida dice donde
    // se pego, pero lo que queda depende de cuantas se solaparon, del ruido
    // del borde y de lo que abollaron. Contar caras vivas es contar lo que se
    // ve. Solo la chapa de flotacion a borda (el centro de la cara, en la
    // malla PRISTINA, entre el agua y un poco sobre la borda): lo de debajo
    // del agua no lo ve nadie y la herida no lo alcanza, y lo que sobresale
    // arriba no llega a la altura de una orilla.
    //
    // Tres pasos, sobre una grilla vista desde arriba de kCell:
    //
    //   1. que celdas tienen chapa (cada cara marca su caja en x, z).
    //   2. que celdas son AFUERA: las vacias a las que se llega desde el borde
    //      de la grilla sin cruzar chapa. Lo solido es todo lo demas. Es lo que
    //      hace falta, porque el casco es una CASCARA: con la cubierta rota y
    //      los costados enteros, el hueco del medio sigue siendo barco (no se
    //      llega a el desde afuera); con la proa abierta, el boquete se conecta
    //      con el agua y deja de serlo, aunque las dos esquinas sigan en pie.
    //      Medir solo el ancho de cada franja (el borde de babor y el de
    //      estribor) no alcanza: un boquete en el CENTRO de la proa no se ve,
    //      el circulo sigue en el medio, el choque cae en el agujero y la proa
    //      deja de romperse - el defecto que esto arregla.
    //   3. circulos: de cada celda solida, la distancia a lo de afuera es el
    //      radio del circulo mas grande que cabe centrado ahi. Primero una
    //      cadena por el eje del casco, despues lo que le quede afuera (ver
    //      abajo).
    //
    // Inscritos y no circunscritos A PROPOSITO: un circulo que asoma de la
    // chapa vuelve a chocar en el aire. Lo que se pierde son las puntas mas
    // finas que kMinCircle, que meten unos centimetros en una pared antes de
    // tocarla.
    std::vector<HullCircle> Outline() const {
        std::vector<HullCircle> out;
        if (!m_ready) return out;
        const float gx0 = m_base.boundsMin.x - 2.0f * kCell, gz0 = m_base.boundsMin.z - 2.0f * kCell;
        const int nx = static_cast<int>(std::ceil((m_base.boundsMax.x - m_base.boundsMin.x) / kCell)) + 4;
        const int nz = static_cast<int>(std::ceil((m_base.boundsMax.z - m_base.boundsMin.z) / kCell)) + 4;
        auto cellX = [&](float x) { return std::clamp(static_cast<int>(std::floor((x - gx0) / kCell)), 1, nx - 2); };
        auto cellZ = [&](float z) { return std::clamp(static_cast<int>(std::floor((z - gz0) / kCell)), 1, nz - 2); };
        // 1. La chapa. El anillo exterior de la grilla queda siempre vacio: es
        // por donde entra el relleno de afuera.
        std::vector<uint8_t> cell(static_cast<size_t>(nx) * nz, 0);  // 0 vacio, 1 chapa, 2 afuera
        const size_t tris = m_alive.size();
        for (size_t t = 0; t < tris; ++t) {
            if (!m_alive[t]) continue;
            const uint32_t i0 = m_base.indices[t * 3], i1 = m_base.indices[t * 3 + 1], i2 = m_base.indices[t * 3 + 2];
            const float cy = (m_base.vertices[i0].position.y + m_base.vertices[i1].position.y + m_base.vertices[i2].position.y) / 3.0f;
            const float cz = (m_base.vertices[i0].position.z + m_base.vertices[i1].position.z + m_base.vertices[i2].position.z) / 3.0f;
            if (cy < kWaterline || cy > GunwaleAt(cz) + 0.3f) continue;
            const auto& a = m_work[i0].position;
            const auto& b = m_work[i1].position;
            const auto& c = m_work[i2].position;
            const int x0 = cellX(std::min({a.x, b.x, c.x})), x1 = cellX(std::max({a.x, b.x, c.x}) - 1e-3f);
            const int z0 = cellZ(std::min({a.z, b.z, c.z})), z1 = cellZ(std::max({a.z, b.z, c.z}) - 1e-3f);
            for (int z = z0; z <= z1; ++z)
                for (int x = x0; x <= x1; ++x) cell[static_cast<size_t>(z) * nx + x] = 1;
        }
        // 2. Afuera: relleno desde la esquina, de a cuatro vecinos (en
        // diagonal no: dos caras que se tocan por la punta siguen cerrando).
        std::vector<int> stack{0};
        cell[0] = 2;
        while (!stack.empty()) {
            const int i = stack.back();
            stack.pop_back();
            const int x = i % nx, z = i / nx;
            const int next[4][2] = {{x - 1, z}, {x + 1, z}, {x, z - 1}, {x, z + 1}};
            for (const auto& n : next) {
                if (n[0] < 0 || n[0] >= nx || n[1] < 0 || n[1] >= nz) continue;
                const int j = n[1] * nx + n[0];
                if (cell[static_cast<size_t>(j)] != 0) continue;
                cell[static_cast<size_t>(j)] = 2;
                stack.push_back(j);
            }
        }
        // 3. El radio de cada celda solida: hasta el BORDE de la celda de
        // afuera mas cercana (centro a centro menos media celda). A fuerza
        // bruta: son unas 2.000 celdas y solo se cuenta al recibir un golpe.
        std::vector<int> outside;
        for (int i = 0; i < nx * nz; ++i)
            if (cell[static_cast<size_t>(i)] == 2) outside.push_back(i);
        std::vector<float> radius(static_cast<size_t>(nx) * nz, -1.0f);
        for (int z = 0; z < nz; ++z)
            for (int x = 0; x < nx; ++x) {
                if (cell[static_cast<size_t>(z) * nx + x] == 2) continue;
                int best = 1 << 30;
                for (int o : outside) {
                    const int dx = o % nx - x, dz = o / nx - z;
                    best = std::min(best, dx * dx + dz * dz);
                }
                radius[static_cast<size_t>(z) * nx + x] = (std::sqrt(static_cast<float>(best)) - 0.5f) * kCell;
            }
        std::vector<uint8_t> covered(static_cast<size_t>(nx) * nz, 0);
        auto add = [&](int cx, int cz) {
            const float r = radius[static_cast<size_t>(cz) * nx + cx];
            out.push_back({gx0 + (cx + 0.5f) * kCell, gz0 + (cz + 0.5f) * kCell, r});
            const int reach = static_cast<int>(std::ceil(r / kCell)) + 1;
            const float lim = (r + 0.5f * kCell) * (r + 0.5f * kCell);
            for (int z = std::max(0, cz - reach); z <= std::min(nz - 1, cz + reach); ++z)
                for (int x = std::max(0, cx - reach); x <= std::min(nx - 1, cx + reach); ++x) {
                    const float dx = (x - cx) * kCell, dz = (z - cz) * kCell;
                    if (dx * dx + dz * dz <= lim) covered[static_cast<size_t>(z) * nx + x] = 1;
                }
        };
        // 3a. LA CADENA: una fila cada kRowStep celdas, y en cada tramo
        // seguido de casco de esa fila, la celda de radio mayor (el eje del
        // casco; con la proa partida en dos puntas, una por punta). Elegir de
        // mayor a menor en toda la grilla dejaba los circulos grandes a un
        // radio uno del otro - 2,2 m, con el costado hundido 40 cm entre dos -
        // y despues gastaba el tope en circulos chicos que no sobresalian de
        // nada. A 0,8 m entre filas el costado se hunde 4 cm.
        for (int z = 0; z < nz && out.size() < kMaxCircles; z += kRowStep) {
            int x = 0;
            while (x < nx && out.size() < kMaxCircles) {
                if (radius[static_cast<size_t>(z) * nx + x] < 0.0f) {
                    ++x;
                    continue;
                }
                int bestX = x;
                for (; x < nx && radius[static_cast<size_t>(z) * nx + x] >= 0.0f; ++x)
                    if (radius[static_cast<size_t>(z) * nx + x] > radius[static_cast<size_t>(z) * nx + bestX]) bestX = x;
                if (radius[static_cast<size_t>(z) * nx + bestX] >= kMinCircle) add(bestX, z);
            }
        }
        // 3b. Lo que la cadena no alcanzo (las puntas entre dos filas): de
        // mayor a menor, solo celdas que ningun circulo cubre todavia.
        std::vector<int> rest;
        for (int i = 0; i < nx * nz; ++i)
            if (radius[static_cast<size_t>(i)] >= kMinCircle && !covered[static_cast<size_t>(i)]) rest.push_back(i);
        // Estable, y la grilla se recorre siempre igual: el mismo casco da
        // los mismos circulos.
        std::stable_sort(rest.begin(), rest.end(), [&](int p, int q) { return radius[static_cast<size_t>(p)] > radius[static_cast<size_t>(q)]; });
        for (int i : rest) {
            if (out.size() >= kMaxCircles) break;
            if (!covered[static_cast<size_t>(i)]) add(i % nx, i / nx);
        }
        return out;
    }

    // Del barco al mundo, con la misma cuenta que dibuja el nodo (incluida la
    // escora y el hundimiento que ShipSim::Apply le puso este frame).
    DirectX::XMFLOAT3 ToWorld(DirectX::XMFLOAT3 local) const {
        if (!m_node) return local;
        DirectX::XMFLOAT3 o;
        DirectX::XMStoreFloat3(&o, DirectX::XMVector3TransformCoord(DirectX::XMLoadFloat3(&local), strace::ComputeWorldMatrix(*m_node)));
        return o;
    }

private:
    // La clave de la malla herida en la MeshCache. Una sola: cada partida del
    // multi-play es otro proceso, con su propia cache.
    static constexpr const wchar_t* kKey = L"memoria:/Shipfulness/casco";

    // Cuanto de una cara cuenta como perdido si solo se abollo: la chapa sigue
    // ahi, pero ya no aguanta como antes. Con una abolladura de kDeepDent metros
    // en el centro, una cara abollada vale kDentWeight de una caida.
    static constexpr float kDentWeight = 0.3f;
    static constexpr float kBand = 0.25f;
    std::vector<float> m_gunwale;
    float m_bandZ0 = 0.0f;
    static constexpr float kDeepDent = 0.4f;
    // La colision (Outline): la flotacion en coordenadas del barco, el lado
    // de una celda de la grilla, el circulo mas chico que vale la pena (por
    // debajo, un jiron de chapa que no para nada) y un tope de circulos para
    // que un casco hecho trizas no multiplique el costo del choque.
    static constexpr float kWaterline = 0.0f;
    static constexpr float kCell = 0.2f;
    static constexpr float kMinCircle = 0.25f;
    static constexpr size_t kMaxCircles = 64;
    static constexpr int kRowStep = 4;

    strace::GameObject* m_node = nullptr;
    std::wstring m_basePath;
    bool m_uploaded = false;
    bool m_ready = false;
    strace::MeshData m_base;
    std::vector<strace::MeshVertex> m_work;
    std::vector<uint8_t> m_alive;
    std::vector<float> m_damage;
    std::vector<float> m_triArea;
    double m_totalArea = 0.0;
    double m_lostArea = 0.0;
    std::vector<uint8_t> m_touched;
    std::vector<float> m_sign;
    std::vector<uint16_t> m_triSub;
    std::vector<HullWound> m_wounds;
    std::vector<Piece> m_pieces;

    static DirectX::XMFLOAT3 Sub(DirectX::XMFLOAT3 a, DirectX::XMFLOAT3 b) { return {a.x - b.x, a.y - b.y, a.z - b.z}; }
    static DirectX::XMFLOAT3 Cross(DirectX::XMFLOAT3 a, DirectX::XMFLOAT3 b) {
        return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x};
    }

    // Ruido de valor en [0, 1], de la posicion y nada mas (ver arriba: las
    // copias de un vertice tienen que dar el mismo numero).
    static float Hash(int x, int y, int z) {
        uint32_t h = static_cast<uint32_t>(x) * 73856093u ^ static_cast<uint32_t>(y) * 19349663u ^ static_cast<uint32_t>(z) * 83492791u;
        h ^= h >> 13;
        h *= 0x5bd1e995u;
        h ^= h >> 15;
        return static_cast<float>(h & 0xffffff) / static_cast<float>(0xffffff);
    }
    static float Noise(float x, float y, float z) {
        int ix = static_cast<int>(std::floor(x)), iy = static_cast<int>(std::floor(y)), iz = static_cast<int>(std::floor(z));
        float fx = x - ix, fy = y - iy, fz = z - iz;
        fx = fx * fx * (3.0f - 2.0f * fx);
        fy = fy * fy * (3.0f - 2.0f * fy);
        fz = fz * fz * (3.0f - 2.0f * fz);
        float c[2][2][2];
        for (int a = 0; a < 2; ++a)
            for (int b = 0; b < 2; ++b)
                for (int d = 0; d < 2; ++d) c[a][b][d] = Hash(ix + a, iy + b, iz + d);
        auto L = [](float p, float q, float t) { return p + (q - p) * t; };
        return L(L(L(c[0][0][0], c[1][0][0], fx), L(c[0][1][0], c[1][1][0], fx), fy),
                 L(L(c[0][0][1], c[1][0][1], fx), L(c[0][1][1], c[1][1][1], fx), fy), fz);
    }

    // Las normales de lo que se movio, de las caras que siguen vivas. Solo de
    // lo tocado: el resto del casco conserva las normales del archivo, que
    // pueden venir suavizadas y una cuenta por caras las aplanaria.
    void RecomputeNormals() {
        std::vector<DirectX::XMFLOAT3> acc(m_work.size(), {0.0f, 0.0f, 0.0f});
        const size_t tris = m_alive.size();
        for (size_t t = 0; t < tris; ++t) {
            if (!m_alive[t]) continue;
            uint32_t i0 = m_base.indices[t * 3], i1 = m_base.indices[t * 3 + 1], i2 = m_base.indices[t * 3 + 2];
            if (!m_touched[i0] && !m_touched[i1] && !m_touched[i2]) continue;
            DirectX::XMFLOAT3 n = Cross(Sub(m_work[i1].position, m_work[i0].position), Sub(m_work[i2].position, m_work[i0].position));
            float s = m_sign[t];
            for (uint32_t i : {i0, i1, i2}) {
                acc[i].x += n.x * s;
                acc[i].y += n.y * s;
                acc[i].z += n.z * s;
            }
        }
        for (size_t i = 0; i < m_work.size(); ++i) {
            if (!m_touched[i]) continue;
            float l = std::sqrt(acc[i].x * acc[i].x + acc[i].y * acc[i].y + acc[i].z * acc[i].z);
            if (l > 1e-12f) m_work[i].normal = {acc[i].x / l, acc[i].y / l, acc[i].z / l};
        }
    }

    void Publish(bool pristine) {
        if (!m_node) return;
        strace::MeshCache* cache = strace::SceneService::Meshes();
        if (pristine) {
            m_node->meshPath = m_basePath;
            // Devolver los buffers del casco herido: sano, no los usa nadie.
            if (m_uploaded && cache) cache->Invalidate(kKey);
            m_uploaded = false;
            return;
        }
        if (!cache) return;
        RecomputeNormals();
        strace::MeshData out;
        out.name = m_base.name;
        out.vertices = m_work;
        out.boundsMin = m_base.boundsMin;
        out.boundsMax = m_base.boundsMax;
        out.indices.reserve(m_base.indices.size());
        for (const strace::SubMesh& sub : m_base.submeshes) {
            strace::SubMesh o = sub;
            o.firstIndex = static_cast<uint32_t>(out.indices.size());
            for (uint32_t k = sub.firstIndex; k + 2 < sub.firstIndex + sub.indexCount; k += 3)
                if (m_alive[k / 3]) out.indices.insert(out.indices.end(), {m_base.indices[k], m_base.indices[k + 1], m_base.indices[k + 2]});
            o.indexCount = static_cast<uint32_t>(out.indices.size()) - o.firstIndex;
            out.submeshes.push_back(o);
        }
        if (!cache->Upload(kKey, std::move(out)).Valid()) {
            // Sin buffers el casco sigue sano a la vista; el agua que entra
            // (ShipHealth) no depende de esto, asi que el juego sigue.
            strace::Log::Warn(L"Casco: no se pudo subir la malla herida.");
            return;
        }
        m_uploaded = true;
        m_node->meshPath = kKey;
    }
};

// LA VIDA DEL BARCO: la INTEGRIDAD del casco, en porcentaje.
//
// No cuenta choques: cuenta casco. HullDamage mide cuanta chapa sigue en pie
// (StandingArea), asi que un roce que abolla resta poco, un boquete resta lo que
// mide, y volver a pegar en el mismo agujero resta solo lo nuevo que se rompe
// alrededor.
//
// La integridad NO es esa area tal cual: toda la chapa cuenta (fondo y cabina
// incluidos) y un choque fuerte se lleva entre el 2 y el 5 % de ella, asi
// que "HULL 90%" ya seria un barco grave - un numero que no se lee. Se escala:
// 100 % es el casco sano y 0 % es haber perdido kStructuralShare del area. El
// barco se hunde al bajar de kSinkIntegrity. Medido chocando: un choque
// mediano resta ~10 %, uno fuerte 20-30 %, un roce que solo abolla casi nada.
//
// Los escalones, como fraccion del camino hasta hundirse (Flood: 0 sano, 1
// hundido):
//
//   Flood < kLeaking   SANO o con abolladuras.
//   Flood < kSinking   HACE AGUA: mas bajo y escorado, los agujeros gotean.
//   Flood < 1          SE HUNDE: la cubierta casi al ras, crujidos seguidos.
//   integridad <= kSinkIntegrity   HUNDIDO. Fin de la partida.
//
// El agua que se ve (cuanto baja, de que lado se escora, de que punta se
// clava) sale de Flood y de DONDE se perdio la integridad: lo que se ve y lo
// que mata son el mismo numero.
//
// Sin motor, como Core/ en AN0MALY: es una cuenta, y se puede probar con un for.
struct ShipHealth {
    // El area de chapa perdida que deja la integridad en 0 %: el 18 % del casco.
    // Era el 6 % con heridas a 0,55 m del agua; centradas hacia la borda (ver
    // DamageHull) cada choque rompe unas tres veces mas chapa.
    static constexpr float kStructuralShare = 0.18f;
    // Por debajo de esta integridad, el barco se hunde.
    static constexpr float kSinkIntegrity = 0.30f;
    static constexpr float kLeaking = 0.18f;
    static constexpr float kSinking = 0.7f;

    float integrity = 1.0f;
    float heel = 0.0f;  // -1 (banda de babor) .. 1 (estribor): de que lado se rompio
    float trim = 0.0f;  // -1 (popa) .. 1 (proa)

    void Reset() { *this = ShipHealth{}; }

    float Flood() const { return Clamp((1.0f - integrity) / (1.0f - kSinkIntegrity), 0.0f, 1.0f); }
    bool Dead() const { return integrity <= kSinkIntegrity; }
    int Stage() const {
        float f = Flood();
        return Dead() ? 3 : (f >= kSinking ? 2 : (f >= kLeaking ? 1 : 0));
    }
    int Percent() const { return static_cast<int>(std::floor(integrity * 100.0f + 1e-4f)); }

    static float FromStanding(float standingArea) {
        return Clamp(1.0f - (1.0f - standingArea) / kStructuralShare, 0.0f, 1.0f);
    }

    // El casco en pie (HullDamage::StandingArea) y donde fue el golpe que lo
    // bajo: side, x del barco sobre la media manga (-1..1); along, z sobre el
    // medio largo (-1..1). La escora y el asiento son el promedio de donde se
    // perdio integridad, pesado por cuanto se perdio en cada sitio.
    void Set(float standingArea, float side, float along) {
        float now = FromStanding(standingArea);
        float lost = integrity - now;
        if (lost <= 0.0f) return;
        float before = 1.0f - integrity, total = 1.0f - now;
        heel = (heel * before + Clamp(side, -1.0f, 1.0f) * lost) / total;
        trim = (trim * before + Clamp(along, -1.0f, 1.0f) * lost) / total;
        integrity = now;
    }
};
}
