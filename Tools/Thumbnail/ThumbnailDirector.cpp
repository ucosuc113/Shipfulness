// LA MINIATURA DEL JUEGO, DIBUJADA POR EL PROPIO JUEGO.
//
// No es parte de Shipfulness: este archivo NO vive en Assets/Scripts y el
// Build no lo ve. make-thumbnail.ps1 copia el proyecto a
// Intermediate/Thumbnail/, mete este script en la copia, se lo cuelga al nodo
// "Juego" EN LUGAR de ShipfulnessGame, y corre el Editor sin nadie mirando
// para capturar el panel Juego a 1920x1080.
//
// Por que el motor y no una imagen hecha aparte: la miniatura es lo primero
// que alguien ve del juego en itch.io, y lo honesto es que sea el juego. Asi
// que esto usa las MISMAS piezas que ShipfulnessGame - SceneLink (el canal, la
// ciudad generada, los derrumbes), ShipSim (el barco y sus choques),
// HullDamage (el casco que se rompe donde pega), Effects (los trozos y el
// polvo) - y solo decide lo que el juego decide con el jugador al timon: donde
// esta el barco, contra que choca, y desde donde se mira.
//
// TODO SE LEE DE thumbnail.cfg (en la raiz de la copia): encuadre, sol, el
// barco, cuanto corre la simulacion y el titulo. Cambiar el encuadre no
// recompila nada - la huella de ScriptCompiler ve el mismo .cpp.
//
// EL TIEMPO ES FIJO: 1/60 por frame, ignorando lo que tardo el frame de
// verdad. Asi "el choque a los 1,9 s" cae en el mismo sitio en cada corrida y
// en cualquier maquina; con el delta real, el primer frame de Play (2-4 s
// cargando mallas) adelantaria la escena entera.
//
// Y SE CONGELA: pasado `run`, ni la simulacion, ni los derrumbes, ni las
// particulas, ni el agua avanzan. El Editor captura el ULTIMO frame de la
// corrida (--frames), y con la escena quieta da igual cuantos frames sobren.

#include "Engine/Scripting/ScriptRegistry.h"
#include "Engine/Rendering/RenderSettings.h"
#include "Engine/Diagnostics/Log.h"

#include "../HullDamage.h"
#include "../SceneLink.h"
#include "../ShipSim.h"
#include "../Ui.h"

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <map>
#include <sstream>

using namespace ship;

namespace {

// key = v1 v2 v3, una por linea; '#' comenta. Lo que no esta, su defecto.
class Config {
public:
    void Load(const std::wstring& path) {
        std::ifstream f{std::filesystem::path(path)};
        if (!f) {
            strace::Log::Warn(L"Miniatura: no hay " + path + L"; van los valores por defecto.");
            return;
        }
        std::string line;
        while (std::getline(f, line)) {
            size_t hash = line.find('#');
            if (hash != std::string::npos) line.resize(hash);
            size_t eq = line.find('=');
            if (eq == std::string::npos) continue;
            std::string key = Trim(line.substr(0, eq));
            m_text[key] = Trim(line.substr(eq + 1));
            std::istringstream in(line.substr(eq + 1));
            std::vector<float> v;
            float x;
            while (in >> x) v.push_back(x);
            if (!key.empty()) m_values[key] = v;
        }
    }

    float F(const std::string& key, float def, size_t i = 0) const {
        auto it = m_values.find(key);
        return it != m_values.end() && it->second.size() > i ? it->second[i] : def;
    }

    bool Has(const std::string& key) const { return m_text.count(key) > 0; }

    // Las palabras de una linea (nombres de nodo, por ejemplo).
    std::vector<std::string> Words(const std::string& key) const {
        std::vector<std::string> out;
        auto it = m_text.find(key);
        if (it == m_text.end()) return out;
        std::istringstream in(it->second);
        std::string w;
        while (in >> w) out.push_back(w);
        return out;
    }

private:
    std::map<std::string, std::vector<float>> m_values;
    std::map<std::string, std::string> m_text;

    static std::string Trim(const std::string& s) {
        size_t a = s.find_first_not_of(" \t\r"), b = s.find_last_not_of(" \t\r");
        return a == std::string::npos ? std::string() : s.substr(a, b - a + 1);
    }
};

}  // namespace

class ThumbnailDirector : public strace::IScript {
public:
    void OnStart() override {
        m_cfg.Load(strace::SceneService::ProjectRoot() + L"/thumbnail.cfg");
        m_ok = m_link.Link();
        if (!m_ok) {
            strace::Log::Error(L"Miniatura: la escena no tiene Barco o Nivel_1.");
            return;
        }
        // La calidad del juego en ALTA, sin preguntar a las opciones del
        // jugador: una miniatura no tiene "graficos medios".
        strace::RenderSettings::SetAntialias(strace::AntialiasMode::X4);
        strace::RenderSettings::SetShadowAntialias(strace::AntialiasMode::X4);
        strace::RenderSettings::SetShadowQuality(strace::ShadowQuality::High);
        m_link.fill.SetLimit(static_cast<int>(m_cfg.F("houses", 400)));
        m_link.houseShadows = true;

        m_link.Bind(static_cast<int>(m_cfg.F("level", 0)), static_cast<uint64_t>(m_cfg.F("seed", 7)));
        m_sim.Init(m_link);
        m_sim.Reset();
        if (m_link.shipNode)
            if (strace::GameObject* casco = FindChild(*m_link.shipNode, "Casco")) m_hull.Attach(casco);
        m_sim.SetHullShape(m_hull.Outline());

        // El barco donde lo pide el encuadre, ya navegando: la velocidad de
        // arranque y la marcha. Sin `ship` arranca en "Inicio" como el juego.
        if (m_cfg.Has("ship")) {
            m_sim.s.pos = {m_cfg.F("ship", 0, 0), m_cfg.F("ship", 0, 1)};
            m_sim.s.yaw = m_cfg.F("ship", 0, 2) / kDeg;
        }
        m_sim.s.vel = Forward(m_sim.s.yaw) * m_cfg.F("speed", 0.0f);
        m_sim.s.yawRate = m_cfg.F("yawRate", 0.0f);
        m_sim.order = m_cfg.F("order", 3.0f);
        m_sim.s.throttle = m_sim.Throttle();
        m_link.trail.Reset(m_sim.s.pos - Forward(m_sim.s.yaw) * (m_sim.s.length * 0.5f + 0.4f));
        // Una estela ya hecha: el barco "venia" navegando por detras.
        for (int i = 0; i < 40; ++i) {
            V2 back = m_sim.s.pos - Forward(m_sim.s.yaw) * (m_sim.s.length * 0.5f - 1.2f + (40 - i) * 0.6f);
            m_link.trail.Update(1.0f / 30.0f, back, 4.0f, m_sim.s.beam * 0.5f, m_sim.s.length, i * 0.6f, {-1e9f, -1e9f}, {1e9f, 1e9f});
        }

        // La consola 3D cuelga de la camara: en la miniatura no hay jugador.
        if (m_link.camera)
            if (strace::GameObject* c = FindChild(*m_link.camera, "Consola")) c->active = false;
        if (m_link.camera && m_link.camera->camera) m_link.camera->camera->fovYDegrees = m_cfg.F("fov", 38.0f);

        m_sun = FindRoot("Sol");
        if (m_sun && m_sun->light) {
            m_sun->transform.rotation[0] = m_cfg.F("sun", 40.0f, 0);
            m_sun->transform.rotation[1] = m_cfg.F("sun", -35.0f, 1);
            m_sun->light->intensity = m_cfg.F("sunIntensity", m_sun->light->intensity);
            if (m_cfg.Has("sunColor"))
                m_sun->light->color = {m_cfg.F("sunColor", 1, 0), m_cfg.F("sunColor", 1, 1), m_cfg.F("sunColor", 1, 2)};
        }
        // El vinyeteado del etalonado, a gusto del encuadre: en la miniatura
        // las esquinas son las que se recortan al hacerla cuadrada.
        if (m_cfg.Has("vignette"))
            if (strace::MaterialCache* mats = strace::SceneService::Materials())
                if (strace::Material* post = mats->GetMutable(L"Assets/Materiales/Post.strmat"))
                    post->userParams[0].w = m_cfg.F("vignette", 0.25f);

        // Lo que estorba al encuadre (una colina, un puente) se apaga por
        // NOMBRE: todos los nodos que se llamen asi, en cualquier nivel. Con
        // Bind ya hecho, asi que los colisionadores de lo apagado siguen ahi -
        // apagar no es una forma de abrirle paso al barco.
        for (const std::string& name : m_cfg.Words("hide"))
            for (auto& root : strace::SceneService::MutableRoots())
                if (root) {
                    if (root->name == name) root->active = false;
                    ForEachNode(root->children, [&](strace::GameObject& g) {
                        if (g.name == name) g.active = false;
                    });
                }

        m_run = m_cfg.F("run", 2.0f);
        m_steer = m_cfg.F("steer", 0.0f);
        PlaceCamera();
    }

    void OnUpdate(float) override {
        if (!m_ok) return;
        const float dt = 1.0f / 60.0f;
        const bool live = m_time < m_run;
        if (live) {
            m_time += dt;
            StepResult r = m_sim.Step(dt, m_steer, true);
            if (r.collapsed >= 0 || r.collapsedFill >= 0 || r.collapsedProp >= 0) Wound(r, true);
            else if (r.impact > 1.0f) Wound(r, false);
            Settle(dt);
            m_link.Update(dt);
            m_link.UpdateWakes(dt);
            V2 stern = m_sim.s.pos - Forward(m_sim.s.yaw) * (m_sim.s.length * 0.5f - 1.2f);
            m_odometer += std::fabs(m_sim.s.ForwardSpeed()) * dt;
            V2 lo, hi;
            m_link.ViewBounds(lo, hi);
            // La estela con la intensidad de crucero: el choque frena el barco
            // en seco y la V se apagaria justo en la foto.
            m_link.trail.Update(dt, stern, std::max(3.0f, std::fabs(m_sim.s.ForwardSpeed())), m_sim.s.beam * 0.5f, m_sim.s.length,
                                m_odometer, lo, hi);
        }
        m_sim.Apply(m_time);
        PlaceCamera();
        if (m_sun && m_sun->light) {
            V2 c;
            float half;
            m_link.SunBox(c, half);
            m_sun->transform.position[0] = c.x;
            m_sun->transform.position[1] = 1.0f;
            m_sun->transform.position[2] = c.z;
            m_sun->light->range = half;
        }
        m_ui.Begin();
        m_link.Cull(*m_link.camera, m_ui.W() / std::max(1.0f, m_ui.H()));
        // La ciudad de relleno se arma alrededor del FOCO: el centro del
        // encuadre sobre el agua, no el barco (el barco puede ir de lado).
        m_link.UpdateFill(dt, m_focus);
        m_link.ShaderClock(m_time);
        DrawTitle();
        m_ui.End();
        if ((live && ++m_frames % 60 == 0) || (!live && !m_logged)) {
            if (!live) m_logged = true;
            char line[200];
            std::snprintf(line, sizeof(line), "Miniatura: t=%.2f barco (%.1f, %.1f) rumbo %.0f vel %.2f casas caidas %d integridad %d%%",
                          m_time, m_sim.s.pos.x, m_sim.s.pos.z, m_sim.s.yaw * kDeg, m_sim.s.ForwardSpeed(), m_link.Destroyed(),
                          m_health.Percent());
            strace::Log::Info(std::wstring(line, line + std::strlen(line)));
        }
    }

    void OnDestroy() override {
        if (m_ok) m_hull.Reset();
    }

private:
    Config m_cfg;
    SceneLink m_link;
    ShipSim m_sim;
    HullDamage m_hull;
    ShipHealth m_health;
    Ui m_ui;
    strace::GameObject* m_sun = nullptr;
    V2 m_focus;
    float m_time = 0.0f;
    float m_run = 2.0f;
    float m_steer = 0.0f;
    float m_odometer = 0.0f;
    int m_frames = 0;
    bool m_logged = false;
    bool m_ok = false;

    // La camara por POSICION y PUNTO MIRADO, no por angulos: es como se piensa
    // un encuadre ("desde ahi, mirando al barco"). El motor la quiere como
    // pitch (+ = hacia abajo) y yaw, igual que la deja CameraRig.
    void PlaceCamera() {
        strace::GameObject* cam = m_link.camera;
        if (!cam) return;
        float px = m_cfg.F("cam", 0, 0), py = m_cfg.F("cam", 60, 1), pz = m_cfg.F("cam", -40, 2);
        float lx = m_cfg.F("look", 0, 0), ly = m_cfg.F("look", 0, 1), lz = m_cfg.F("look", 0, 2);
        float dx = lx - px, dy = ly - py, dz = lz - pz;
        float flat = std::sqrt(dx * dx + dz * dz);
        cam->transform.position[0] = px;
        cam->transform.position[1] = py;
        cam->transform.position[2] = pz;
        cam->transform.rotation[0] = std::atan2(-dy, std::max(flat, 1e-3f)) * kDeg;
        cam->transform.rotation[1] = std::atan2(dx, dz) * kDeg;
        cam->transform.rotation[2] = m_cfg.F("roll", 0.0f);
        m_focus = {lx, lz};
    }

    // El mismo golpe que ShipfulnessGame::DamageHull: la herida donde pego,
    // centrada hacia la borda, con su tope de tamano. Sin el log ni los
    // avisos, que aqui no los lee nadie.
    void Wound(const StepResult& r, bool crashed) {
        const ShipState& s = m_sim.s;
        float v = r.impact;
        V2 d = r.hitAt - s.pos;
        float halfL = s.length * 0.5f, halfB = s.beam * 0.5f;
        float lx = Clamp(Dot(d, Right(s.yaw)), -halfB, halfB);
        float lz = Clamp(Dot(d, Forward(s.yaw)), -halfL + 0.3f, halfL - 0.3f);
        HullWound w;
        const float gunwale = m_hull.GunwaleAt(lz);
        w.at = {lx, gunwale * (crashed ? 0.6f : 0.65f), lz};
        w.inward = {Dot(r.hitNormal, Right(s.yaw)), Dot(r.hitNormal, Forward(s.yaw))};
        w.radius = crashed ? std::min(1.0f + 0.16f * v, 2.0f) : 0.5f + 0.2f * v;
        w.height = std::max(w.radius * 1.35f, gunwale * (crashed ? 0.8f : 0.6f));
        float depth = crashed ? std::min(0.22f + 0.05f * v, 0.5f) : 0.06f * v;
        float hole = crashed ? std::min(0.6f + 0.04f * std::max(0.0f, v - 2.4f), 0.72f) : (v > 1.8f ? 0.38f : 0.0f);
        w.holed = hole > 0.0f;
        float before = m_health.integrity;
        int removed = m_hull.Wound(w, depth, hole);
        m_sim.SetHullShape(m_hull.Outline());
        m_health.Set(m_hull.StandingArea(), lx / halfB, lz / halfL);
        if (!m_hull.Wounds().empty()) m_hull.Wounds().back().severity = (before - m_health.integrity) / (1.0f - ShipHealth::kSinkIntegrity);
        if (crashed || removed > 0) {
            DirectX::XMFLOAT3 wp = m_hull.ToWorld(w.at);
            Blast b;
            b.at = {wp.x, wp.z};
            b.push = r.hitNormal * -1.0f;
            b.radius = 0.8f;
            b.height = 1.6f;
            b.power = crashed ? 0.5f + 0.1f * v : 0.3f;
            for (const HullDamage::Piece& p : m_hull.LastPieces()) b.palette.push_back({p.r, p.g, p.b, p.weight});
            m_link.fx.Shatter(b);
        }
        char line[160];
        std::snprintf(line, sizeof(line), "Miniatura: %s a %.1f m/s, integridad %d%%", crashed ? "choque" : "golpe", v, m_health.Percent());
        strace::Log::Info(std::wstring(line, line + std::strlen(line)));
    }

    // Lo que el agua embarcada se ve por fuera (ShipfulnessGame::UpdateHull):
    // baja, se escora hacia la banda rota, y los agujeros gotean.
    void Settle(float dt) {
        const float w = m_health.Flood();
        m_sim.settle = Damp(m_sim.settle, 0.55f * w, 1.2f, dt);
        m_sim.heelDeg = Damp(m_sim.heelDeg, -m_health.heel * 9.0f * w, 1.0f, dt);
        m_sim.trimDeg = Damp(m_sim.trimDeg, m_health.trim * 3.5f * w, 1.0f, dt);
        for (HullWound& wd : m_hull.Wounds()) {
            if (!wd.holed) continue;
            wd.leakTimer -= dt;
            if (wd.leakTimer > 0.0f) continue;
            wd.leakTimer = Lerp(0.45f, 0.12f, Clamp(wd.severity * 2.0f + w * 0.5f, 0.0f, 1.0f));
            DirectX::XMFLOAT3 p = m_hull.ToWorld({wd.at.x, 0.35f, wd.at.z});
            V2 out = Right(m_sim.s.yaw) * -wd.inward.x + Forward(m_sim.s.yaw) * -wd.inward.z;
            m_link.fx.Leak(p.x, p.y, p.z, out, wd.severity * 2.0f + w);
        }
    }

    // EL TITULO, con el idioma de la portada (DrawMainMenu): la fuente de
    // pixeles, la sombra corrida en azul noche y la barra naranja debajo.
    //
    // CENTRADO, y todo dentro del CUADRADO central (1080 x 1080 de 1920 x
    // 1080): quien recorte la miniatura a cuadrado se queda con el titulo
    // entero. La escala es entera, como en el juego - medio pixel de una
    // fuente de 5x7 no existe.
    void DrawTitle() {
        if (m_cfg.F("title", 1.0f) <= 0.0f) return;
        const float W = m_ui.W(), H = m_ui.H();
        const float u = H / 1080.0f;
        const int scale = std::max(2, static_cast<int>(m_cfg.F("titleScale", 13.0f) * u + 0.5f));
        const char* title = "SHIPFULNESS";
        float tw = static_cast<float>(strace::MeasureTextWidth(title, scale));
        float x = std::round(W * 0.5f - tw * 0.5f);
        float y = std::round(m_cfg.F("titleY", 70.0f) * u);
        float drop = std::round(scale * 0.55f);
        strace::UiDrawList& ui = m_ui.Draw();
        // Un contorno de un pixel de fuente en azul noche alrededor, y la
        // sombra larga: sobre el agua clara y las casas blancas, el blanco del
        // titulo solo no se separa del fondo.
        const float o = static_cast<float>(scale) * 0.5f;
        for (int k = 0; k < 8; ++k) {
            float a = k * kPi / 4.0f;
            ui.AddText(x + std::round(std::cos(a) * o), y + std::round(std::sin(a) * o), title, kNavyDark, scale);
        }
        ui.AddText(x + drop, y + drop * 1.3f, title, kNavyDark, scale);
        ui.AddText(x, y, title, kPaper, scale);
        float by = y + 7.0f * scale + std::round(scale * 1.1f);
        float bh = std::round(scale * 0.55f);
        ui.AddRect({x - o, by - o * 0.5f, tw + 2 * o, bh * 1.6f + o}, kNavyDark);
        ui.AddRect({x, by, tw, bh}, kOrange);
        ui.AddRect({x, by + bh, tw, std::round(bh * 0.5f)}, kOrangeDark);

        if (m_cfg.F("tagline", 1.0f) <= 0.0f) return;
        // La frase de la portada, en DOS lineas como alli: entera en una sola
        // no cabe en el cuadrado a un tamano que se lea en una miniatura.
        const int ts = std::max(2, static_cast<int>(m_cfg.F("taglineScale", 5.0f) * u + 0.5f));
        float ty = std::round(H - m_cfg.F("taglineBottom", 150.0f) * u);
        const char* l1 = "A SHIP FAR TOO LONG.";
        const char* l2 = "A CANAL FAR TOO NARROW.";
        Plaque(l1, kPaper, ty, ts);
        Plaque(l2, kOrange, ty + 7.0f * ts + std::round(ts * 2.2f), ts);
    }

    // Una linea sobre su placa: los paneles del juego (Chamfer) en azul noche.
    void Plaque(const char* text, UiColor color, float y, int scale) {
        float w = static_cast<float>(strace::MeasureTextWidth(text, scale));
        float x = std::round(m_ui.W() * 0.5f - w * 0.5f);
        float pad = std::round(scale * 1.6f);
        UiRect r{x - pad, y - pad, w + 2 * pad, 7.0f * scale + 2 * pad};
        m_ui.Chamfer({r.x + scale, r.y + scale * 1.4f, r.width, r.height}, scale * 1.5f, kShadow);
        m_ui.Chamfer(r, scale * 1.5f, kNavyDark);
        m_ui.Draw().AddText(x, y, text, color, scale);
    }
};

STRACE_REGISTER_SCRIPT(ThumbnailDirector)
