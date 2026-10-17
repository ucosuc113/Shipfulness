#include "Engine/Scripting/ScriptRegistry.h"
#include "Engine/Gameplay/GameTime.h"
#include "Engine/Rendering/RenderSettings.h"

#include "AudioDirector.h"
#include "CameraRig.h"
#include "Console3D.h"
#include "HullDamage.h"
#include "Levels.h"
#include "Options.h"
#include "SceneLink.h"
#include "ShipSim.h"
#include "Ui.h"

#include <chrono>
#include <cstdlib>
#include <cstring>

#ifdef __EMSCRIPTEN__
#include <emscripten.h>
#endif

using namespace ship;

enum class Screen { MainMenu, Options, HowTo, Playing, Paused, Results };

class ShipfulnessGame : public strace::IScript {
public:
    void OnStart() override {
        m_ok = m_link.Link();
        if (!m_ok) return;
        m_levelIndex = 0;
        const std::string& lname = m_link.levels.front()->name;
        if (StartsWith(lname, "Nivel_")) m_levelIndex = std::max(0, std::atoi(lname.c_str() + 6) - 1);
        m_levelIndex = std::min(m_levelIndex, static_cast<int>(Levels().size()) - 1);
        m_link.Bind(0, Level().seed);
        m_sim.Init(m_link);
        m_sim.Reset();
        // El casco que se rompe (ver HullDamage.h): el hijo "Casco" del barco.
        // Sin el, el barco no se abolla pero el agua igual entra y hunde.
        if (m_link.shipNode)
            if (strace::GameObject* casco = FindChild(*m_link.shipNode, "Casco"))
                if (!m_hull.Attach(casco)) strace::Log::Warn(L"Casco: no se pudo leer la malla del barco; no se vera el dano.");
        m_sim.SetHullShape(m_hull.Outline());
        m_camera.Attach(m_link.camera);
        m_camera.Snap();
        // La consola 3D reemplaza al panel que DrawConsole() dibujaba: las
        // piezas viven en la escena (hijas de la camara) y este script las
        // anima. Los avisos son SU logica - audio, sim, pantallas - para que
        // la consola no sepa nada del juego mas alla del estado que le prestan.
        m_console.Link(m_link.camera);
        m_console.orderChanged = [this](float o) {
            m_sim.order = o;
            m_audio.Play("ui_click", -8);
        };
        m_console.hornPressed = [this]() { Horn(); };
        m_console.pausePressed = [this]() {
            if (m_screen == Screen::Playing) {
                m_screen = Screen::Paused;
                m_justPaused = true;
            }
        };
        m_console.cameraToggled = [this]() { m_opts.cameraFollow = !m_opts.cameraFollow; };
        m_console.hoverChanged = [this]() { m_audio.Play("ui_click", -16); };
        m_opts.Load(Levels().size());
        m_audio.Start();
        ApplyOptions();
        m_sun = FindRoot("Sol");
        m_ui.sound = [this](const char* what) {
            if (std::string(what) == "hover") m_audio.Play("ui_click", -16);
            else m_audio.Play("ui_confirm", -6);
        };
        m_screen = Screen::MainMenu;
    }

    void OnDestroy() override {
        if (!m_ok) return;
        // Devuelve el casco sano y suelta los buffers de la malla herida.
        m_hull.Reset();
        m_opts.Save();
        m_audio.Stop();
    }

    void OnUpdate(float dt) override {
        if (!m_ok) return;
        dt = Clamp(dt, 0.0f, 0.05f);
        m_time += dt;
        m_fps = Lerp(m_fps, 1.0f / std::max(strace::GameTime::RawDelta(), 1e-4f), 0.05f);
        m_keys.Update();
        m_escUsed = false;
        m_justPaused = false;
        m_ui.Begin();
        bool sailing = m_screen == Screen::Playing && m_phaseTime > kCountdown && m_sinkTime < 0.0f;
        float steer = 0.0f;
        switch (m_screen) {
        case Screen::Playing: steer = UpdatePlaying(dt); break;
        default: break;
        }
        if (m_screen != Screen::Paused) {
            float simDt = m_hitStop > 0.0f ? dt * 0.08f : dt;
            m_hitStop = std::max(0.0f, m_hitStop - dt);
            StepResult r = m_sim.Step(simDt, steer, sailing);
            if (sailing) HandleImpacts(r, dt);
            if (m_screen == Screen::Playing) UpdateHull(simDt);
            m_link.Update(simDt);
            m_link.UpdateWakes(simDt);
            EmitWake(simDt);
        }
        m_sim.Apply(m_time);
        // La caja del mapa del sol esta centrada en el nodo "Sol" y mide
        // range metros de medio lado: la mueve SunBox para que cubra lo que
        // se ve (las casas tambien proyectan). Usa el encuadre del frame
        // ANTERIOR (Cull corre mas abajo), que a esta escala es lo mismo. El
        // motor ajusta el centro a su grilla de texeles, asi que moverlo cada
        // frame no hace hervir el borde de la sombra.
        if (m_sun && m_sun->light) {
            V2 c;
            float half;
            m_link.SunBox(c, half);
            m_sun->transform.position[0] = c.x;
            m_sun->transform.position[1] = 1.0f;
            m_sun->transform.position[2] = c.z;
            m_sun->light->range = half;
        }
        bool menuView = m_screen == Screen::MainMenu || m_screen == Screen::HowTo ||
                        (m_screen == Screen::Options && m_optionsReturn == Screen::MainMenu);
        m_camera.follow = m_opts.cameraFollow;
        m_camera.shakeEnabled = m_opts.cameraShake;
        m_camera.Update(dt, m_sim.s, m_link.startYaw, menuView ? m_time : -1.0f);
        // El estado que la consola muestra: solo visible jugando (en pausa y
        // resultados manda el HUD 2D), y siempre despues de mover la camara,
        // porque el raycast del mouse usa la vista de ESTE frame.
        m_console.SetVisible(m_screen == Screen::Playing);
        m_console.SetOrder(m_sim.order);
        m_console.SetRudder(m_sim.s.rudder);
        m_console.SetCameraFollow(m_opts.cameraFollow);
        m_visible = m_link.Cull(*m_link.camera, m_ui.W() / std::max(1.0f, m_ui.H()));
        m_link.UpdateFill(dt, m_sim.s.pos);
        m_link.ShaderClock(m_time);
        m_visible += m_link.fill.Visible();
        float speed = m_sim.s.ForwardSpeed();
        float stress = std::fabs(m_sim.s.yawRate) * 1.5f + std::fabs(m_sim.s.rudder) * std::fabs(speed) / 8.0f;
        m_audio.Update(dt, m_screen == Screen::Playing, speed, m_sim.s.throttle, m_sim.s.scrape, stress);
        DrawScreen(dt);
        if (m_opts.showFps) {
            char buf[48];
            std::snprintf(buf, sizeof(buf), "%d FPS  %d HOUSES", static_cast<int>(m_fps + 0.5f), m_visible);
            m_ui.TextRight(m_ui.W() - 14 * m_ui.S(), 14 * m_ui.S(), buf, kPaper);
        }
        m_ui.End();
        if (m_quitTimer > 0.0f) {
            m_quitTimer -= dt;
#ifndef __EMSCRIPTEN__
            if (m_quitTimer <= 0.0f) ExitProcess(0);
#endif
        }
    }

private:
    static constexpr float kCountdown = 2.4f;

    SceneLink m_link;
    ShipSim m_sim;
    CameraRig m_camera;
    Console3D m_console;
    strace::GameObject* m_sun = nullptr;
    AudioDirector m_audio;
    HullDamage m_hull;
    ShipHealth m_health;
    // -1 a flote; desde que el agua llega a 1, los segundos del naufragio.
    float m_sinkTime = -1.0f;
    bool m_sunk = false;
    float m_groanTimer = 0.0f;
    float m_bubbleTimer = 0.0f;
    // El aviso de lo que le pasa al casco: va DENTRO del panel de arriba a la
    // izquierda, al lado del porcentaje (ver DrawHudStats). Fue un cartel al
    // centro de la pantalla, y tapaba justo lo que viene por delante del barco.
    std::string m_alert;
    float m_alertTime = 0.0f;
    UiColor m_alertColor = kOrange;
    Ui m_ui;
    Options m_opts;
    Keys m_keys;
    bool m_escUsed = false;
    // El frame en que se entro en pausa: DrawPause no lee teclas en el (la
    // tecla que pauso - Esc o P - sigue "recien apretada" hasta el proximo).
    bool m_justPaused = false;
    Screen m_screen = Screen::MainMenu;
    Screen m_optionsReturn = Screen::MainMenu;
    bool m_ok = false;
    int m_levelIndex = 0;
    float m_time = 0.0f;
    float m_phaseTime = 0.0f;
    float m_raceTime = 0.0f;
    float m_fps = 60.0f;
    float m_wakeTimer = 0.0f;
    float m_odometer = 0.0f;
    float m_hitStop = 0.0f;
    float m_scrapeTimer = 0.0f;
    float m_toast = 0.0f;
    float m_quitTimer = 0.0f;
    std::string m_toastText;
    int m_visible = 0;
    int m_stars = 0;
    bool m_newBest = false;

    const LevelDef& Level() const { return Levels()[static_cast<size_t>(m_levelIndex)]; }

    void ApplyOptions() {
        m_audio.SetVolumes(m_opts.music, m_opts.sfx);
        int q = std::clamp(m_opts.quality, 0, 2);
        const strace::AntialiasMode aa[3] = {strace::AntialiasMode::Off, strace::AntialiasMode::X2, strace::AntialiasMode::X4};
        const strace::ShadowQuality sq[3] = {strace::ShadowQuality::Low, strace::ShadowQuality::Medium, strace::ShadowQuality::High};
        strace::RenderSettings::SetAntialias(aa[q]);
        strace::RenderSettings::SetShadowQuality(sq[q]);
        const int limits[3] = {120, 220, 320};
        m_link.fill.SetLimit(limits[q]);
        // En Baja las casas no tiran sombra del sol: queda la del barco, que
        // es la que dice donde esta, y el mapa no redibuja cientos de casas
        // cada vez que la camara avanza.
        m_link.houseShadows = q > 0;
        if (m_link.water)
            if (strace::Material* m = strace::SceneService::UniqueMaterial(*m_link.water)) m->userParams[0].w = q == 0 ? 0.0001f : 0.85f;
    }

    void Toast(const std::string& t) {
        m_toastText = t;
        m_toast = 2.5f;
    }

    void ResetHull() {
        m_hull.Reset();
        m_sim.SetHullShape(m_hull.Outline());
        m_health.Reset();
        m_sinkTime = -1.0f;
        m_sunk = false;
        m_alertTime = 0.0f;
        m_alert.clear();
    }

    void StartRun() {
        m_link.Bind(0, Level().seed);
        m_sim.Reset();
        ResetHull();
        m_link.trail.Reset(m_sim.s.pos - Forward(m_sim.s.yaw) * (m_sim.s.length * 0.5f + 0.4f));
        m_camera.Snap();
        m_console.SetWheel(0.0f);
        m_raceTime = 0.0f;
        m_phaseTime = 0.0f;
        m_screen = Screen::Playing;
        m_audio.Play("horn_long", -2);
    }

    void Quit() {
        m_opts.Save();
#ifndef __EMSCRIPTEN__
        wchar_t path[MAX_PATH] = {};
        GetModuleFileNameW(nullptr, path, MAX_PATH);
        std::wstring exe(path);
        if (exe.find(L"Editor") != std::wstring::npos) {
            Toast("QUIT ONLY WORKS IN THE EXPORTED GAME");
            return;
        }
        PostQuitMessage(0);
        m_quitTimer = 0.6f;
#else
        // En web el juego ES la pagina: no hay ventana propia que cerrar.
        // Detener el runtime deja la pagina quieta con lo ultimo dibujado.
        emscripten_force_exit(0);
#endif
    }

    float UpdatePlaying(float dt) {
        m_phaseTime += dt;
        if (m_phaseTime > kCountdown) m_raceTime += dt;
        if (EscPressed() || m_keys.Pressed('P')) {
            m_screen = Screen::Paused;
            m_justPaused = true;
            return 0.0f;
        }
        if (m_keys.Pressed('R')) {
            StartRun();
            return 0.0f;
        }
        if (m_keys.Pressed('C')) m_opts.cameraFollow = !m_opts.cameraFollow;
        if (m_keys.Pressed('H')) Horn();
        if (m_keys.Pressed('W') || m_keys.Pressed(0x26)) StepOrder(1);
        if (m_keys.Pressed('S') || m_keys.Pressed(0x28)) StepOrder(-1);
        float key = 0.0f;
        if (m_keys.Held('A') || m_keys.Held(0x25)) key -= 1.0f;
        if (m_keys.Held('D') || m_keys.Held(0x27)) key += 1.0f;
        m_console.AddWheel(key * Console3D::kWheelMax * 1.1f * dt);
        if (!m_console.draggingWheel && key == 0.0f && m_opts.wheelAutoCenter) m_console.AutoCenter(dt);
        if (m_ui.mouse.wheel != 0.0f) m_camera.zoom = Clamp(m_camera.zoom - m_ui.mouse.wheel * 0.08f, 0.6f, 1.6f);
        m_toast = std::max(0.0f, m_toast - dt);
        if (m_sinkTime >= 0.0f) return 0.0f;
        if (m_phaseTime > kCountdown) {
            if (m_sim.InsideGoal()) Finish();
        }
        return m_console.Wheel() / Console3D::kWheelMax;
    }

    void StepOrder(int d) {
        float o = Clamp(std::round(m_sim.order) + d, static_cast<float>(ShipSim::kMinOrder), static_cast<float>(ShipSim::kMaxOrder));
        if (o != m_sim.order) m_audio.Play("ui_click", -6);
        m_sim.order = o;
    }

    void Horn() {
        m_audio.Play("horn_short", -3);
        m_console.PulseHorn();
    }

    void HandleImpacts(const StepResult& r, float dt) {
        static int lastBumps = 0;
        if (r.collapsed >= 0 || r.collapsedFill >= 0 || r.collapsedProp >= 0) {
            m_audio.Play("crash", 0);
            m_audio.Play("splash", -4);
            m_camera.Shake(r.collapsedProp >= 0 ? 1.0f : 0.8f);
            m_hitStop = r.collapsedProp >= 0 ? 0.11f : 0.07f;
            DamageHull(r, true);
        } else if (m_sim.bumps != lastBumps && r.impact > 0.0f) {
            m_audio.PlayOneOf("bump_", 3, Clamp(-14.0f + r.impact * 5.0f, -14.0f, -1.0f));
            m_camera.Shake(Clamp(r.impact * 0.15f, 0.05f, 0.4f));
            m_link.fx.Scrape(r.contactAt, r.contactNormal, r.impact / 2.0f);
            DamageHull(r, false);
        }
        m_scrapeTimer -= dt;
        if (r.contact && m_scrapeTimer <= 0.0f && std::fabs(m_sim.s.ForwardSpeed()) > 0.6f) {
            m_scrapeTimer = 0.09f;
            m_link.fx.Scrape(r.contactAt, r.contactNormal, Clamp(std::fabs(m_sim.s.ForwardSpeed()) / 4.0f, 0.1f, 0.6f));
        }
        lastBumps = m_sim.bumps;
    }

    void Alert(const std::string& text, UiColor color, float seconds = 2.8f) {
        m_alert = text;
        m_alertColor = color;
        m_alertTime = seconds;
    }

    // Un choque rompe el casco DONDE fue: el punto y la normal del golpe mas
    // fuerte del paso, pasados a coordenadas del barco (ver HullDamage.h para
    // la malla y ShipHealth para la integridad). Un roce (menos de 1 m/s contra
    // la orilla) no deja nada: rozar es pilotar.
    void DamageHull(const StepResult& r, bool crashed) {
        const ShipState& s = m_sim.s;
        float v = r.impact;
        if (!crashed && v < 1.0f) return;
        V2 d = r.hitAt - s.pos;
        float halfL = s.length * 0.5f, halfB = s.beam * 0.5f;
        float lx = Clamp(Dot(d, Right(s.yaw)), -halfB, halfB);
        float lz = Clamp(Dot(d, Forward(s.yaw)), -halfL + 0.3f, halfL - 0.3f);
        // La normal del choque apunta del obstaculo AL barco: es hacia donde se
        // hunde la chapa.
        V2 inward{Dot(r.hitNormal, Right(s.yaw)), Dot(r.hitNormal, Forward(s.yaw))};
        HullWound w;
        // CENTRADA HACIA LA BORDA, no a una altura fija: la borda va de 1,5 m en
        // la popa a 2,1 en la proa (GunwaleAt), y con el centro a 0,55 m un
        // choque de proa rompia el casco por debajo, donde la camara de arriba
        // casi no lo ve. Ahora el centro va a 0,6 de la borda y el elipsoide
        // alcanza de la flotacion a pasada la cubierta: el boquete muerde la
        // borda y entra en la cubierta, que es lo que se ve.
        const float gunwale = m_hull.GunwaleAt(lz);
        w.at = {lx, gunwale * (crashed ? 0.6f : 0.65f), lz};
        w.inward = inward;
        // Con tope: a toda maquina (7 m/s) una herida sin tope se comia medio
        // casco de 15 m. Un choque fuerte abre un boquete de unos 4 m de ancho.
        w.radius = crashed ? std::min(1.0f + 0.16f * v, 2.0f) : 0.5f + 0.2f * v;
        w.height = std::max(w.radius * 1.35f, gunwale * (crashed ? 0.8f : 0.6f));
        float depth = crashed ? std::min(0.22f + 0.05f * v, 0.5f) : 0.06f * v;
        // Sin derribo es un golpe contra algo que no cede: abolla, y solo pasado
        // ~1,8 m/s abre algo.
        float hole = crashed ? std::min(0.6f + 0.04f * std::max(0.0f, v - 2.4f), 0.72f) : (v > 1.8f ? 0.38f : 0.0f);
        w.holed = hole > 0.0f;
        int before = m_health.Stage();
        float integrityBefore = m_health.integrity;
        auto t0 = std::chrono::steady_clock::now();
        int removed = m_hull.Wound(w, depth, hole);
        // La colision sigue al casco: lo que se acaba de caer ya no choca, y
        // el proximo golpe en esta punta cae en la chapa que queda detras.
        m_sim.SetHullShape(m_hull.Outline());
        m_health.Set(m_hull.StandingArea(), lx / halfB, lz / halfL);
        float lost = integrityBefore - m_health.integrity;
        // Lo que gotea cada agujero: la parte del camino al hundimiento que se
        // llevo este golpe.
        m_hull.Wounds().back().severity = lost / (1.0f - ShipHealth::kSinkIntegrity);
        double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
        // Hasta donde llega ahora la colision por cada punta: es como se ve en
        // el log que una proa rota dejo de chocar donde ya no hay chapa.
        float bow = -1e9f, stern = 1e9f;
        for (const HullCircle& c : m_sim.s.circles) {
            bow = std::max(bow, c.z + c.r);
            stern = std::min(stern, c.z - c.r);
        }
        char line[260];
        std::snprintf(line, sizeof(line),
                      "Casco: %s a %.1f m/s en (%.1f, %.1f): %d caras menos, chapa en pie %.2f%%, integridad -%.1f%% -> %.1f%% (%.1f ms); "
                      "colision: %d circulos, de %.2f a %.2f m",
                      crashed ? "choque" : "golpe", v, lx, lz, removed, m_hull.StandingArea() * 100.0f, lost * 100.0f,
                      m_health.integrity * 100.0f, ms, static_cast<int>(m_sim.s.circles.size()), stern, bow);
        strace::Log::Info(std::wstring(line, line + std::strlen(line)));
        // Los trozos del BARCO, con el color de lo que se rompio (no el de la
        // casa: esos ya salen del derrumbe), hacia afuera de la banda rota.
        DirectX::XMFLOAT3 wp = m_hull.ToWorld(w.at);
        if (crashed || removed > 0) {
            Blast b;
            b.at = {wp.x, wp.z};
            b.push = r.hitNormal * -1.0f;
            b.radius = 0.8f;
            b.height = 1.6f;
            b.power = crashed ? 0.5f + 0.1f * v : 0.3f;
            for (const HullDamage::Piece& p : m_hull.LastPieces()) b.palette.push_back({p.r, p.g, p.b, p.weight});
            m_link.fx.Shatter(b);
            m_audio.PlayOneOf("creak_", 3, crashed ? -1.0f : -7.0f);
        }
        // Cada golpe que baja la integridad hace parpadear su linea del panel;
        // al cruzar un escalon, ademas, una palabra al lado que se queda. Un
        // roce que no baja nada no avisa.
        int after = m_health.Stage();
        if (lost > 0.0005f && after < 3) {
            if (after == 2 && before < 2) Alert("SINKING!", kRed, 1.2f);
            else if (after == 1 && before < 1) Alert("LEAKING", kOrange, 1.2f);
            else Alert(m_alert, m_alertColor, 1.2f);
        }
    }

    // Lo que la integridad perdida se ve por fuera, cada frame: el barco baja,
    // se escora hacia la banda rota y se clava de la punta golpeada - despacio,
    // como entra el agua -; los agujeros gotean; y con el barco hundiendose,
    // cruje.
    void UpdateHull(float dt) {
        const float w = m_health.Flood();
        float settle = 0.55f * w;
        float heel = -m_health.heel * 9.0f * w;
        float trim = m_health.trim * 3.5f * w;
        if (m_sinkTime < 0.0f && m_health.Dead() && m_phaseTime > kCountdown) Shipwreck();
        if (m_sinkTime >= 0.0f) {
            m_sinkTime += dt;
            float k = Clamp(m_sinkTime / 5.0f, 0.0f, 1.0f);
            k = k * k * (3.0f - 2.0f * k);
            // Se va de la punta rota: la otra sale del agua.
            float end = m_health.trim >= 0.0f ? 1.0f : -1.0f;
            float side = m_health.heel >= 0.0f ? 1.0f : -1.0f;
            settle += 7.0f * k * k;
            trim = end * (3.5f + 20.0f * k);
            heel = -side * (6.0f + 12.0f * k);
            m_sim.sunk = Clamp(m_sinkTime, 0.0f, 1.0f);
            m_bubbleTimer -= dt;
            if (m_bubbleTimer <= 0.0f && m_sinkTime < 6.0f) {
                m_bubbleTimer = 0.04f;
                // A lo largo del casco, saltando de punta a punta.
                V2 along = Forward(m_sim.s.yaw) * (m_sim.s.length * 0.42f * std::sin(m_sinkTime * 7.3f));
                m_link.fx.Bubbles(m_sim.s.pos + along, 2.0f, 1.0f - 0.6f * k);
            }
            if (m_sinkTime > 6.5f && m_screen == Screen::Playing) {
                m_audio.Play("fail_jingle", -2);
                m_screen = Screen::Results;
            }
        }
        m_sim.settle = Damp(m_sim.settle, settle, m_sinkTime >= 0.0f ? 6.0f : 1.2f, dt);
        m_sim.heelDeg = Damp(m_sim.heelDeg, heel, m_sinkTime >= 0.0f ? 6.0f : 1.0f, dt);
        m_sim.trimDeg = Damp(m_sim.trimDeg, trim, m_sinkTime >= 0.0f ? 6.0f : 1.0f, dt);
        // Los agujeros gotean, mas cuanta mas agua lleva el barco (va mas bajo,
        // y por el agujero entra y sale a borbotones).
        for (HullWound& wd : m_hull.Wounds()) {
            if (!wd.holed || m_sinkTime > 2.0f) continue;
            wd.leakTimer -= dt;
            if (wd.leakTimer > 0.0f) continue;
            wd.leakTimer = Lerp(0.45f, 0.12f, Clamp(wd.severity * 2.0f + w * 0.5f, 0.0f, 1.0f));
            DirectX::XMFLOAT3 p = m_hull.ToWorld({wd.at.x, 0.35f, wd.at.z});
            V2 out = Right(m_sim.s.yaw) * -wd.inward.x + Forward(m_sim.s.yaw) * -wd.inward.z;
            m_link.fx.Leak(p.x, p.y, p.z, out, wd.severity * 2.0f + w);
        }
        // Hundiendose: crujidos cada vez mas seguidos. Antes, ninguno extra: los
        // de los giros (AudioDirector) ya estan.
        if (m_health.Stage() >= 2 && m_sinkTime < 0.0f) {
            m_groanTimer -= dt;
            if (m_groanTimer <= 0.0f) {
                m_audio.PlayOneOf("creak_", 3, -5.0f);
                m_groanTimer = Lerp(3.5f, 1.4f, Clamp((w - ShipHealth::kSinking) / (1.0f - ShipHealth::kSinking), 0.0f, 1.0f));
            }
        }
        m_alertTime = std::max(0.0f, m_alertTime - dt);
    }

    void Shipwreck() {
        m_sinkTime = 0.0f;
        m_sunk = true;
        m_sim.order = 0.0f;
        m_bubbleTimer = 0.0f;
        m_audio.Play("crash", -3);
        m_audio.Play("splash", 0);
        m_audio.PlayOneOf("creak_", 3, 0.0f);
        m_camera.Shake(0.6f);
        Alert("ABANDON SHIP!", kRed, 6.5f);
    }

    void Finish() {
        m_sunk = false;
        int houses = m_link.Destroyed();
        float par = Level().parTime;
        m_stars = 1;
        if (houses <= 2 && m_raceTime <= par * 1.3f) m_stars = 2;
        if (houses == 0 && m_raceTime <= par) m_stars = 3;
        float& best = m_opts.best[static_cast<size_t>(m_levelIndex)];
        m_newBest = best <= 0.0f || m_raceTime < best;
        if (m_newBest) best = m_raceTime;
        m_opts.Save();
        m_sim.order = 0.0f;
        m_audio.Play("win_jingle", -2);
        m_screen = Screen::Results;
    }

    void EmitWake(float dt) {
        const ShipState& s = m_sim.s;
        V2 stern = s.pos - Forward(s.yaw) * (s.length * 0.5f - 1.2f);
        m_odometer += std::fabs(s.ForwardSpeed()) * dt;
        V2 lo, hi;
        m_link.ViewBounds(lo, hi);
        m_link.trail.Update(dt, stern, s.ForwardSpeed(), s.beam * 0.5f, s.length, m_odometer, lo, hi);
    }

    void DrawScreen(float dt) {
        switch (m_screen) {
        case Screen::MainMenu: DrawMainMenu(); break;
        case Screen::Options: DrawOptions(); break;
        case Screen::HowTo: DrawHowTo(); break;
        case Screen::Playing: DrawPlaying(dt); break;
        case Screen::Paused: DrawPause(); break;
        case Screen::Results:
            DrawHudStats();
            DrawResults();
            break;
        }
        if (m_toast > 0.0f && m_screen != Screen::Playing) m_toast = std::max(0.0f, m_toast - dt);
        if (m_toast > 0.0f) {
            float s = m_ui.S();
            float w = static_cast<float>(strace::MeasureTextWidth(m_toastText, m_ui.T())) + 40 * s;
            UiRect r{m_ui.W() * 0.5f - w * 0.5f, m_ui.H() - 60 * s, w, 36 * s};
            m_ui.Panel(r, kNavyDark);
            m_ui.TextCentered(m_ui.W() * 0.5f, r.y + 10 * s, m_toastText, kOrange);
        }
    }

    // ESCAPE SE GASTA UNA VEZ POR FRAME. Pausar con Esc no andaba: el juego
    // pasaba a Paused en UpdatePlaying y, en el MISMO frame, DrawPause leia
    // la misma pulsacion y reanudaba - pausa y despausa sin que se viera
    // nada (con P si andaba, porque DrawPause no la miraba). Quien la usa la
    // consume, y el resto del frame ya no la ve.
    bool EscPressed() {
        if (m_escUsed || !m_keys.Pressed(0x1B)) return false;
        m_escUsed = true;
        return true;
    }

    void Dim(float a) { m_ui.Rect({0, 0, m_ui.W(), m_ui.H()}, UiColor{0.06f, 0.14f, 0.22f, a}); }

    void DrawMainMenu() {
        float s = m_ui.S(), W = m_ui.W(), H = m_ui.H();
        float cw = std::min(420 * s, W * 0.42f);
        float x = std::max(40 * s, W * 0.05f);
        // EL FONDO DE LA COLUMNA: casi opaco detras del menu y un borde en
        // cuatro escalones, como las esquinas de los paneles. Era un degradado
        // de 32 tiras de 0,78 a 0, y en su mitad (alfa 0,3-0,6) las casas
        // blancas de detras se veian como fantasmas: parecia un fallo de
        // render y no una decision. Un velo casi opaco tapa del todo, y el
        // borde corto deja la ciudad entera a la vista del otro lado.
        {
            const UiColor veil{0.07f, 0.10f, 0.21f, 1.0f};
            float solid = std::round(x + cw + 36 * s);
            m_ui.Rect({0, 0, solid, H}, UiColor{veil.r, veil.g, veil.b, 0.93f});
            const float steps[4] = {0.72f, 0.5f, 0.3f, 0.12f};
            float step = std::round(14 * s);
            for (int i = 0; i < 4; ++i)
                m_ui.Rect({solid + step * i, 0, step, H}, UiColor{veil.r, veil.g, veil.b, steps[i]});
        }
        int titleScale = std::max(2, static_cast<int>(cw / static_cast<float>(strace::MeasureTextWidth("SHIPFULNESS", 1))));
        float titleH = 7.0f * titleScale;
        float subH = 7.0f * m_ui.T();
        float cardH = 72 * s, bh = 52 * s, gap = 12 * s;
        float total = titleH + 18 * s + 6 * s + 14 * s + subH * 2 + 10 * s + 34 * s + cardH + 24 * s + 4 * bh + 3 * gap;
        float y = std::max(24 * s, (H - total) * 0.45f);
        float bob = std::sin(m_time * 1.2f) * 2 * s;
        m_ui.Text(x + 4 * s, y + bob + 5 * s, "SHIPFULNESS", kNavyDark, titleScale);
        m_ui.Text(x, y + bob, "SHIPFULNESS", kPaper, titleScale);
        y += titleH + 18 * s;
        float titleW = static_cast<float>(strace::MeasureTextWidth("SHIPFULNESS", titleScale));
        m_ui.Rect({x, y, titleW, 6 * s}, kOrange);
        m_ui.Rect({x, y + 6 * s, titleW, 3 * s}, kOrangeDark);
        y += 6 * s + 14 * s;
        m_ui.Text(x, y, "A SHIP FAR TOO LONG.", kPaper);
        y += subH + 10 * s;
        m_ui.Text(x, y, "A CANAL FAR TOO NARROW.", kOrange);
        y += subH + 34 * s;
        UiRect card{x, y, cw, cardH};
        m_ui.Panel(card, kNavyDark);
        m_ui.Rect({card.x + 10 * s, card.y + 12 * s, 6 * s, card.height - 24 * s}, kOrange);
        m_ui.Text(card.x + 30 * s, card.y + 14 * s, Level().name, kPaper);
        int small = std::max(1, m_ui.T() - 1);
        m_ui.Draw().AddText(card.x + 30 * s, card.y + 42 * s, Level().subtitle, kSteel, small);
        float best = m_opts.best[static_cast<size_t>(m_levelIndex)];
        if (best > 0.0f) {
            char buf[32];
            std::snprintf(buf, sizeof(buf), "%d:%05.2f", static_cast<int>(best) / 60, std::fmod(best, 60.0f));
            m_ui.TextRight(card.Right() - 18 * s, card.y + 14 * s, buf, kGreen);
        }
        y += cardH + 24 * s;
        UiRect play{x, y, cw, bh};
        if (m_ui.Button(1, play, "SET SAIL", true)) StartRun();
        // El timon en PIXELES, como la fuente: un aro octogonal en escalera
        // (dos FilledOctagon, el de adentro del color del boton) y radios de
        // cuadritos pegados a una grilla. Eran lineas suavizadas que giraban -
        // el unico trazo vectorial de una interfaz hecha de pixeles, justo en
        // el boton principal. Sigue girando: los cuadritos saltan de celda en
        // celda, que es como gira un sprite.
        float wy = std::round(play.y + bh * 0.5f), wx = std::round(play.x + 34 * s);
        {
            // Seis radios (tres diametros) que asoman del aro como las cabillas
            // de un timon de verdad: con tres radios gruesos desde el centro
            // el icono se leia como el triangulo de "play".
            float px = std::max(2.0f, std::round(2 * s));
            UiColor face = m_ui.Hover(play) ? kOrangeLight : kOrange;
            m_ui.FilledOctagon(wx, wy, 12 * s, kInk);
            m_ui.FilledOctagon(wx, wy, 12 * s - px * 1.6f, face);
            for (int k = 0; k < 3; ++k) {
                float a = m_time * 0.6f + k * kPi / 3.0f;
                for (float r = -16 * s; r <= 16 * s; r += px) {
                    float cx = std::round((wx + std::cos(a) * r) / px) * px, cy = std::round((wy + std::sin(a) * r) / px) * px;
                    m_ui.Rect({cx - px * 0.5f, cy - px * 0.5f, px, px}, kInk);
                }
            }
            m_ui.Rect({wx - px, wy - px, px * 2, px * 2}, kInk);
        }
        y += bh + gap;
        if (m_ui.Button(2, {x, y, cw, bh}, "OPTIONS")) {
            m_optionsReturn = Screen::MainMenu;
            m_screen = Screen::Options;
        }
        y += bh + gap;
        if (m_ui.Button(3, {x, y, cw, bh}, "HOW TO PLAY")) m_screen = Screen::HowTo;
        y += bh + gap;
        if (m_ui.Button(4, {x, y, cw, bh}, "QUIT")) Quit();
        if (m_keys.Pressed(0x0D) || m_keys.Pressed(0x20)) StartRun();
        m_ui.Draw().AddText(x, H - 30 * s, "MOUSE: THROTTLE, WHEEL AND HORN   -   ENTER: SET SAIL", kSteel, small);
        // La version a secas, la misma de BuildSettings ("product.version"):
        // "PROTOTYPE" en la portada era lo primero que hacia leer el juego como
        // una prueba, y el menu es su escaparate.
        m_ui.TextRight(W - 24 * s, H - 30 * s, "V1.0", kSteel, small);
    }

    void DrawOptions() {
        if (m_optionsReturn == Screen::Paused) DrawHudStats();
        Dim(0.45f);
        float s = m_ui.S(), W = m_ui.W(), H = m_ui.H();
        UiRect p{W * 0.5f - 290 * s, H * 0.5f - 300 * s, 580 * s, 600 * s};
        m_ui.Panel(p);
        m_ui.TextCentered(W * 0.5f, p.y + 22 * s, "OPTIONS", kOrange, m_ui.T() * 2);
        float x = p.x + 30 * s, y = p.y + 82 * s, w = p.width - 60 * s, h = 50 * s, g = 10 * s;
        if (m_ui.Slider(10, {x, y, w, h}, "MUSIC", m_opts.music)) ApplyOptions();
        y += h + g;
        if (m_ui.Slider(11, {x, y, w, h}, "SOUND EFFECTS", m_opts.sfx)) ApplyOptions();
        y += h + g;
        m_ui.Toggle(12, {x, y, w, h}, "SHOW FPS", m_opts.showFps);
        y += h + g;
        m_ui.Toggle(13, {x, y, w, h}, "CAMERA FOLLOWS SHIP", m_opts.cameraFollow);
        y += h + g;
        m_ui.Toggle(14, {x, y, w, h}, "WHEEL SELF-CENTERS", m_opts.wheelAutoCenter);
        y += h + g;
        m_ui.Toggle(15, {x, y, w, h}, "CAMERA SHAKE", m_opts.cameraShake);
        y += h + g;
        static const char* const kQuality[3] = {"LOW", "MEDIUM", "HIGH"};
        if (m_ui.Cycle(17, {x, y, w, h}, "GRAPHICS", m_opts.quality, kQuality, 3)) ApplyOptions();
        y += h + g * 2;
        if (m_ui.Button(16, {W * 0.5f - 130 * s, y, 260 * s, 50 * s}, "BACK", true) || EscPressed()) {
            m_opts.Save();
            m_screen = m_optionsReturn;
        }
    }

    void DrawHowTo() {
        Dim(0.45f);
        float s = m_ui.S(), W = m_ui.W(), H = m_ui.H();
        UiRect p{W * 0.5f - 380 * s, H * 0.5f - 280 * s, 760 * s, 560 * s};
        m_ui.Panel(p);
        m_ui.TextCentered(W * 0.5f, p.y + 22 * s, "HOW TO PLAY", kOrange, m_ui.T() * 2);
        const char* lines[] = {
            "DRAG THE LEVER: ENGINES AHEAD OR ASTERN",
            "DRAG THE WHEEL IN A CIRCLE TO STEER",
            "CLICK THE ROUND BUTTON: HORN",
            "MOUSE SCROLL: ZOOM THE CAMERA IN OR OUT",
            "",
            "RAM A HOUSE HARD ENOUGH AND IT COLLAPSES...",
            "...BUT YOUR SHIP STOPS DEAD, AND BREAKS WHERE IT HIT.",
            "EVERY HIT COSTS HULL. BELOW THE LIMIT, SHE SINKS.",
            "",
            "GET THE WHOLE SHIP PAST THE FINISH LINE",
            "AND OUT INTO THE OPEN SEA.",
            "",
            "KEYBOARD: W S ENGINES  A D WHEEL  H HORN  ESC PAUSE",
        };
        float y = p.y + 80 * s;
        for (const char* l : lines) {
            m_ui.Text(p.x + 36 * s, y, l, l[0] == 'K' && l[1] == 'E' ? kSteel : kPaper);
            y += 28 * s;
        }
        if (m_ui.Button(20, {W * 0.5f - 130 * s, p.Bottom() - 70 * s, 260 * s, 50 * s}, "BACK", true) || EscPressed())
            m_screen = Screen::MainMenu;
    }

    // LA PAUSA. Era un panel con un titulo y cuatro botones sobre el HUD
    // oscurecido: no decia como iba la partida (habia que leerlo a traves del
    // velo) ni con que tecla se volvia. Ahora el panel trae el titulo con la
    // misma barra naranja de la portada, el estado de la partida en una
    // tarjeta (el reloj contra el par, el casco en bloques como el HUD, las
    // casas), los botones, y abajo los atajos. El HUD no se dibuja debajo: lo
    // que decia ya esta en la tarjeta.
    void DrawPause() {
        Dim(0.62f);
        float s = m_ui.S(), W = m_ui.W(), H = m_ui.H();
        int t = m_ui.T(), small = std::max(1, t - 1);
        float bh = 50 * s, gap = 12 * s;
        float pw = 460 * s, ph = 152 * s + 4 * bh + 3 * gap + 74 * s;
        UiRect p{std::round(W * 0.5f - pw * 0.5f), std::round(H * 0.5f - ph * 0.5f), pw, ph};
        m_ui.Panel(p, kNavyDark);

        // Titulo y su barra, como "SHIPFULNESS" en el menu.
        float y = p.y + 22 * s;
        m_ui.TextCentered(W * 0.5f, y, "PAUSED", kPaper, t * 3);
        y += 21.0f * t + 10 * s;
        float tw = static_cast<float>(strace::MeasureTextWidth("PAUSED", t * 3));
        m_ui.Rect({std::round(W * 0.5f - tw * 0.5f), y, tw, 5 * s}, kOrange);
        m_ui.Rect({std::round(W * 0.5f - tw * 0.5f), y + 5 * s, tw, 2 * s}, kOrangeDark);
        y += 20 * s;

        // La tarjeta de la partida.
        float x = p.x + 30 * s, w = p.width - 60 * s;
        UiRect card{x, y, w, 62 * s};
        m_ui.Chamfer(card, 8 * s, kNavy);
        m_ui.Rect({card.x + 10 * s, card.y + 12 * s, 5 * s, card.height - 24 * s}, kOrange);
        m_ui.Draw().AddText(card.x + 26 * s, card.y + 12 * s, Level().name, kOrange, small);
        char buf[64];
        std::snprintf(buf, sizeof(buf), "%d:%05.2f / %d:%02d", static_cast<int>(m_raceTime) / 60, std::fmod(m_raceTime, 60.0f),
                      static_cast<int>(Level().parTime) / 60, static_cast<int>(Level().parTime) % 60);
        m_ui.Text(card.x + 26 * s, card.y + 32 * s, buf, m_raceTime > Level().parTime ? kOrange : kPaper);
        int stage = m_health.Stage();
        UiColor hc = stage >= 2 ? kRed : (stage == 1 ? kOrange : kPaper);
        float blk = std::round(9 * s), bgap = std::max(1.0f, std::round(2 * s));
        float bx = card.Right() - 16 * s - 10 * (blk + bgap);
        int lit = static_cast<int>(std::ceil(m_health.Percent() / 10.0f));
        m_ui.Draw().AddText(bx, card.y + 12 * s, "HULL", kSteel, small);
        for (int i = 0; i < 10; ++i) m_ui.Rect({bx + i * (blk + bgap), card.y + 34 * s, blk, blk}, i < lit ? hc : kNavyDark);
        std::snprintf(buf, sizeof(buf), "HOUSES %d", m_link.Destroyed());
        m_ui.Draw().AddText(card.Right() - 16 * s - static_cast<float>(strace::MeasureTextWidth(buf, small)), card.y + 12 * s, buf,
                            kSteel, small);
        y += card.height + 20 * s;

        if (m_ui.Button(30, {x, y, w, bh}, "RESUME", true) || (!m_justPaused && (EscPressed() || m_keys.Pressed('P')))) m_screen = Screen::Playing;
        y += bh + gap;
        if (m_ui.Button(31, {x, y, w, bh}, "RETRY") || (!m_justPaused && m_keys.Pressed('R'))) StartRun();
        y += bh + gap;
        if (m_ui.Button(32, {x, y, w, bh}, "OPTIONS")) {
            m_optionsReturn = Screen::Paused;
            m_screen = Screen::Options;
        }
        y += bh + gap;
        if (m_ui.Button(33, {x, y, w, bh}, "MAIN MENU")) GoMenu();
        y += bh + 22 * s;
        m_ui.Draw().AddText(W * 0.5f - static_cast<float>(strace::MeasureTextWidth("ESC / P: RESUME     R: RETRY", small)) * 0.5f, y,
                            "ESC / P: RESUME     R: RETRY", kSteel, small);
    }

    void GoMenu() {
        m_link.Bind(0, Level().seed);
        m_sim.Reset();
        ResetHull();
        m_link.trail.Reset(m_sim.s.pos - Forward(m_sim.s.yaw) * (m_sim.s.length * 0.5f + 0.4f));
        m_camera.Snap();
        m_screen = Screen::MainMenu;
    }

    void DrawResults() {
        float s = m_ui.S(), W = m_ui.W(), H = m_ui.H();
        UiRect p{W * 0.5f - 260 * s, H * 0.5f - 230 * s, 520 * s, 460 * s};
        m_ui.Panel(p);
        m_ui.TextCentered(W * 0.5f, p.y + 22 * s, m_sunk ? "SHIPWRECKED!" : "YOU MADE IT!", m_sunk ? kRed : kGreen, m_ui.T() * 3);
        for (int i = 0; i < 3; ++i) {
            float cx = W * 0.5f + (i - 1) * 74 * s, cy = p.y + 120 * s;
            m_ui.FilledOctagon(cx + 3 * s, cy + 4 * s, 26 * s, kNavyDark);
            m_ui.FilledOctagon(cx, cy, 26 * s, !m_sunk && i < m_stars ? kOrange : kSteelDark);
        }
        int houses = m_link.Destroyed();
        char buf[96];
        float y = p.y + 170 * s;
        std::snprintf(buf, sizeof(buf), "TIME  %d:%05.2f%s", static_cast<int>(m_raceTime) / 60, std::fmod(m_raceTime, 60.0f),
                      m_newBest && !m_sunk ? "  NEW RECORD!" : "");
        m_ui.TextCentered(W * 0.5f, y, buf, kPaper);
        std::snprintf(buf, sizeof(buf), "HOUSES WRECKED  %d", houses);
        m_ui.TextCentered(W * 0.5f, y + 30 * s, buf, kPaper);
        std::snprintf(buf, sizeof(buf), "BUMPS  %d   HULL  %d%%", m_sim.bumps, m_health.Percent());
        m_ui.TextCentered(W * 0.5f, y + 60 * s, buf, kPaper);
        std::snprintf(buf, sizeof(buf), "CITY HALL SENDS A BILL: %d,500 COINS", houses * 12);
        if (m_sunk) m_ui.TextCentered(W * 0.5f, y + 90 * s, "THE CANAL KEEPS YOUR SHIP", kRed);
        else m_ui.TextCentered(W * 0.5f, y + 90 * s, houses > 0 ? buf : "CITY HALL APPLAUDS YOU", houses > 0 ? kRed : kGreen);
        float bw = 200 * s, bh = 52 * s;
        if (m_ui.Button(40, {W * 0.5f - bw - 10 * s, p.Bottom() - 80 * s, bw, bh}, "AGAIN", true)) StartRun();
        if (m_ui.Button(41, {W * 0.5f + 10 * s, p.Bottom() - 80 * s, bw, bh}, "MENU")) GoMenu();
    }

    // EL HUD, por jerarquia: lo que se mira en carrera (el reloj y el casco)
    // grande o como forma, lo demas chico. Eran cuatro renglones del mismo
    // tamano y el mismo blanco, y para saber cuanto casco quedaba habia que
    // LEER un numero en mitad de una curva.
    void DrawHudStats() {
        float s = m_ui.S();
        int t = m_ui.T();
        int small = std::max(1, t - 1);
        UiRect panel{16 * s, 16 * s, 360 * s, 132 * s};
        m_ui.Panel(panel);
        float x = 30 * s;
        m_ui.Draw().AddText(x, 26 * s, Level().name, kOrange, small);
        char buf[64];
        std::snprintf(buf, sizeof(buf), "%d:%05.2f", static_cast<int>(m_raceTime) / 60, std::fmod(m_raceTime, 60.0f));
        // El reloj al doble; el tiempo par al lado, chico y apagado: es la
        // referencia, no el dato.
        m_ui.Text(x, 42 * s, buf, m_raceTime > Level().parTime ? kOrange : kPaper, t * 2);
        float tw = static_cast<float>(strace::MeasureTextWidth(buf, t * 2));
        std::snprintf(buf, sizeof(buf), "/ %d:%02d", static_cast<int>(Level().parTime) / 60, static_cast<int>(Level().parTime) % 60);
        m_ui.Draw().AddText(x + tw + 10 * s, 42 * s + 7.0f * (t * 2 - small), buf, kSteel, small);

        // El casco como BARRA de diez bloques (ver ShipHealth): el color avisa
        // el escalon antes que el numero - blanco sano, naranja hace agua,
        // rojo se hunde. Recien golpeado parpadea (m_alertTime), y la palabra
        // del escalon (m_alert) va a la derecha de la barra hasta reiniciar.
        int stage = m_health.Stage();
        UiColor c = stage >= 2 ? kRed : (stage == 1 ? kOrange : kPaper);
        if (m_alertTime > 0.0f && std::fmod(m_alertTime, 0.24f) < 0.12f) c = kPaper;
        float by = 90 * s;
        m_ui.Draw().AddText(x, by + 2 * s, "HULL", kSteel, small);
        float bx = x + static_cast<float>(strace::MeasureTextWidth("HULL", small)) + 10 * s;
        const int blocks = 10;
        float bw = std::round(14 * s), bgap = std::max(1.0f, std::round(3 * s)), bh = std::round(12 * s);
        int lit = static_cast<int>(std::ceil(m_health.Percent() / 10.0f));
        for (int i = 0; i < blocks; ++i)
            m_ui.Rect({bx + i * (bw + bgap), by, bw, bh}, i < lit ? c : kNavyDark);
        float after = bx + blocks * (bw + bgap) + 6 * s;
        if (!m_alert.empty()) m_ui.Draw().AddText(after, by + 2 * s, m_alert, m_alertColor, small);
        else {
            std::snprintf(buf, sizeof(buf), "%d%%", m_health.Percent());
            m_ui.Draw().AddText(after, by + 2 * s, buf, c, small);
        }

        std::snprintf(buf, sizeof(buf), "HOUSES %d   BUMPS %d", m_link.Destroyed(), m_sim.bumps);
        m_ui.Draw().AddText(x, 116 * s, buf, kSteel, small);
    }

    void DrawPlaying(float dt) {
        DrawHudStats();
        // La consola de mando son los modelos 3D de la escena; este es el
        // frame en que se les presta el mouse y se animan (ver Console3D.h).
        m_console.Update(dt, m_ui, true);
        m_console.DrawIcons(m_ui);
        if (m_phaseTime < kCountdown + 0.6f) {
            int n = 3 - static_cast<int>(m_phaseTime / 0.8f);
            float s = m_ui.S();
            // Arriba del todo: al 20% de la altura tapaba la proa justo cuando
            // el barco arranca, que es lo unico que hay que mirar en la salida.
            UiRect r{m_ui.W() * 0.5f - 230 * s, 20 * s, 460 * s, 90 * s};
            m_ui.Panel(r);
            m_ui.TextCentered(m_ui.W() * 0.5f, r.y + 14 * s, Level().name, kOrange, m_ui.T() * 2);
            m_ui.TextCentered(m_ui.W() * 0.5f, r.y + 56 * s, n > 0 ? "CASTING OFF... " + std::to_string(n) : "FULL STEAM AHEAD!",
                              kPaper);
        }
    }

    // DrawConsole() se retiro: la consola de mando ahora son los modelos 3D de
    // la escena (Console3D.h). Todo lo que dibujaba aqui -la palanca de
    // velocidades, el timon con su arco de marcas, los botones de camara,
    // pausa y bocina, el señalizador- existe en madera de verdad, y el mouse
    // los toca con raycast en vez de rectangulos de pantalla.
};

STRACE_REGISTER_SCRIPT(ShipfulnessGame)
