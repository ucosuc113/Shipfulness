#include "Engine/Scripting/ScriptRegistry.h"
#include "Engine/Gameplay/GameTime.h"

#include "AudioDirector.h"
#include "CameraRig.h"
#include "Levels.h"
#include "Options.h"
#include "SceneLink.h"
#include "ShipSim.h"
#include "Ui.h"

#include <cstdlib>

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
        m_camera.Attach(m_link.camera);
        m_camera.Snap();
        m_opts.Load(Levels().size());
        m_audio.Start();
        ApplyOptions();
        m_ui.sound = [this](const char* what) {
            if (std::string(what) == "hover") m_audio.Play("ui_click", -16);
            else m_audio.Play("ui_confirm", -6);
        };
        m_screen = Screen::MainMenu;
        if (const char* ap = std::getenv("SHIPFULNESS_AUTOPLAY")) {
            m_autopilot = std::atoi(ap);
            StartRun();
        }
    }

    void OnDestroy() override {
        if (!m_ok) return;
        m_opts.Save();
        m_audio.Stop();
        m_link.Shutdown();
    }

    void OnUpdate(float dt) override {
        if (!m_ok) return;
        dt = Clamp(dt, 0.0f, 0.05f);
        m_time += dt;
        m_fps = Lerp(m_fps, 1.0f / std::max(strace::GameTime::RawDelta(), 1e-4f), 0.05f);
        m_keys.Update();
        m_ui.Begin();
        bool sailing = m_screen == Screen::Playing && m_phaseTime > kCountdown;
        float steer = 0.0f;
        switch (m_screen) {
        case Screen::Playing: steer = UpdatePlaying(dt); break;
        default: break;
        }
        if (m_screen != Screen::Paused) {
            StepResult r = m_sim.Step(m_screen == Screen::Paused ? 0.0f : dt, steer, sailing);
            if (sailing) HandleImpacts(r);
            m_link.Update(dt);
            m_link.UpdateWakes(dt);
            EmitWake(dt);
        }
        m_sim.Apply(m_time);
        bool menuView = m_screen == Screen::MainMenu || m_screen == Screen::HowTo ||
                        (m_screen == Screen::Options && m_optionsReturn == Screen::MainMenu);
        m_camera.follow = m_opts.cameraFollow;
        m_camera.shakeEnabled = m_opts.cameraShake;
        m_camera.Update(dt, m_sim.s, m_link.startYaw, menuView ? m_time : -1.0f);
        m_visible = m_link.Cull(*m_link.camera, m_ui.W() / std::max(1.0f, m_ui.H()));
        m_link.UpdateFill(dt, m_sim.s.pos);
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
            if (m_quitTimer <= 0.0f) ExitProcess(0);
        }
    }

private:
    static constexpr float kCountdown = 2.4f;
    static constexpr float kWheelMax = kPi;

    SceneLink m_link;
    ShipSim m_sim;
    CameraRig m_camera;
    AudioDirector m_audio;
    Ui m_ui;
    Options m_opts;
    Keys m_keys;
    Screen m_screen = Screen::MainMenu;
    Screen m_optionsReturn = Screen::MainMenu;
    bool m_ok = false;
    int m_levelIndex = 0;
    int m_autopilot = 0;
    float m_time = 0.0f;
    float m_phaseTime = 0.0f;
    float m_raceTime = 0.0f;
    float m_fps = 60.0f;
    float m_wheel = 0.0f;
    float m_wheelGrab = 0.0f;
    float m_leverDrag = 0.0f;
    float m_hornLit = 0.0f;
    float m_wakeTimer = 0.0f;
    float m_toast = 0.0f;
    float m_quitTimer = 0.0f;
    std::string m_toastText;
    int m_visible = 0;
    int m_stars = 0;
    bool m_newBest = false;

    const LevelDef& Level() const { return Levels()[static_cast<size_t>(m_levelIndex)]; }

    void ApplyOptions() { m_audio.SetVolumes(m_opts.music, m_opts.sfx); }

    void Toast(const std::string& t) {
        m_toastText = t;
        m_toast = 2.5f;
    }

    void StartRun() {
        m_link.Bind(0, Level().seed);
        m_sim.Reset();
        m_camera.Snap();
        m_wheel = 0.0f;
        m_raceTime = 0.0f;
        m_phaseTime = 0.0f;
        m_screen = Screen::Playing;
        m_audio.Play("horn_long", -2);
    }

    void Quit() {
        m_opts.Save();
        wchar_t path[MAX_PATH] = {};
        GetModuleFileNameW(nullptr, path, MAX_PATH);
        std::wstring exe(path);
        if (exe.find(L"Editor") != std::wstring::npos) {
            Toast("QUIT ONLY WORKS IN THE EXPORTED GAME");
            return;
        }
        PostQuitMessage(0);
        m_quitTimer = 0.6f;
    }

    float UpdatePlaying(float dt) {
        m_phaseTime += dt;
        if (m_phaseTime > kCountdown) m_raceTime += dt;
        if (m_keys.Pressed(0x1B) || m_keys.Pressed('P')) {
            m_screen = Screen::Paused;
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
        m_wheel = Clamp(m_wheel + key * kWheelMax * 1.1f * dt, -kWheelMax, kWheelMax);
        if (m_ui.activeId != 9001 && key == 0.0f && m_opts.wheelAutoCenter) m_wheel = Approach(m_wheel, 0.0f, kWheelMax * 0.45f * dt);
        if (m_ui.mouse.wheel != 0.0f) m_camera.zoom = Clamp(m_camera.zoom - m_ui.mouse.wheel * 0.08f, 0.6f, 1.6f);
        m_hornLit = std::max(0.0f, m_hornLit - dt);
        m_toast = std::max(0.0f, m_toast - dt);
        if (m_phaseTime > kCountdown) {
            if (m_sim.InsideGoal()) Finish();
        }
        if (m_autopilot) return Autopilot();
        return m_wheel / kWheelMax;
    }

    void StepOrder(int d) {
        float o = Clamp(std::round(m_sim.order) + d, static_cast<float>(ShipSim::kMinOrder), static_cast<float>(ShipSim::kMaxOrder));
        if (o != m_sim.order) m_audio.Play("ui_click", -6);
        m_sim.order = o;
    }

    void Horn() {
        m_audio.Play("horn_short", -3);
        m_hornLit = 0.5f;
    }

    void HandleImpacts(const StepResult& r) {
        static int lastBumps = 0;
        if (r.collapsed >= 0 || r.collapsedFill >= 0) {
            m_audio.Play("crash", 0);
            m_audio.Play("splash", -4);
            m_camera.Shake(1.0f);
            m_link.Debris(r.collapsedAt, m_sim.s.yaw);
        } else if (m_sim.bumps != lastBumps && r.impact > 0.0f) {
            m_audio.PlayOneOf("bump_", 3, Clamp(-14.0f + r.impact * 5.0f, -14.0f, -1.0f));
            m_camera.Shake(Clamp(r.impact * 0.15f, 0.05f, 0.4f));
        }
        lastBumps = m_sim.bumps;
    }

    void Finish() {
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

    float Autopilot() {
        if (m_autopilot == 3) {
            m_sim.order = std::fmod(m_raceTime, 6.0f) < 4.5f ? 3.0f : -2.0f;
            if (m_raceTime > 15.0f) StartRun();
            return std::sin(m_raceTime * 0.9f) > 0.0f ? 1.0f : -1.0f;
        }
        m_sim.order = m_autopilot == 2 ? 3.0f : 2.0f;
        const auto& c = m_link.route;
        if (c.empty()) return 0.0f;
        if (m_autopilot == 2 && m_raceTime > 2.0f && m_raceTime < 6.0f) return 1.0f;
        size_t best = 0;
        float bd = 1e9f;
        for (size_t i = 0; i < c.size(); ++i) {
            float d = Len(c[i] - m_sim.s.pos);
            if (d < bd) {
                bd = d;
                best = i;
            }
        }
        if (best + 2 >= c.size()) return Clamp(-m_sim.s.yawRate * 2.0f, -1.0f, 1.0f);
        V2 aim = c[std::min(c.size() - 1, best + 3)] - m_sim.s.pos;
        float err = WrapAngle(std::atan2(aim.x, aim.z) - m_sim.s.yaw);
        return Clamp(err * 2.5f - m_sim.s.yawRate * 2.0f, -1.0f, 1.0f);
    }

    void EmitWake(float dt) {
        const ShipState& s = m_sim.s;
        float speed = s.ForwardSpeed();
        if (std::fabs(speed) < 0.35f) return;
        m_wakeTimer -= dt;
        if (m_wakeTimer > 0.0f) return;
        m_wakeTimer = 0.11f;
        V2 f = Forward(s.yaw), r = Right(s.yaw);
        float size = Clamp(std::fabs(speed) / 6.0f, 0.3f, 1.0f);
        V2 stern = s.pos - f * (s.length * 0.5f - 0.5f);
        m_link.EmitWake(stern + r * (s.beam * 0.35f), s.yaw, 0.9f * size, 2.4f);
        m_link.EmitWake(stern - r * (s.beam * 0.35f), s.yaw, 0.9f * size, 2.4f);
    }

    void DrawScreen(float dt) {
        switch (m_screen) {
        case Screen::MainMenu: DrawMainMenu(); break;
        case Screen::Options: DrawOptions(); break;
        case Screen::HowTo: DrawHowTo(); break;
        case Screen::Playing: DrawPlaying(); break;
        case Screen::Paused:
            DrawHudStats();
            DrawPause();
            break;
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

    void Dim(float a) { m_ui.Rect({0, 0, m_ui.W(), m_ui.H()}, UiColor{0.06f, 0.14f, 0.22f, a}); }

    void DrawMainMenu() {
        float s = m_ui.S(), W = m_ui.W(), H = m_ui.H();
        const int strips = 32;
        float fadeW = std::min(W * 0.62f, 760 * s);
        for (int i = 0; i < strips; ++i) {
            float t = static_cast<float>(i) / strips;
            float a = 0.78f * (1.0f - t) * (1.0f - t * 0.4f);
            float x0 = std::round(fadeW * t), x1 = std::round(fadeW * static_cast<float>(i + 1) / strips);
            m_ui.Rect({x0, 0, x1 - x0, H}, UiColor{0.07f, 0.11f, 0.22f, a});
        }
        float cw = std::min(420 * s, W * 0.42f);
        float x = std::max(40 * s, W * 0.05f);
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
        float wy = play.y + bh * 0.5f, wx = play.x + 34 * s;
        m_ui.Octagon(wx, wy, 13 * s, 4 * s, kInk, m_time * 0.6f);
        for (int k = 0; k < 3; ++k) {
            float a = m_time * 0.6f + k * kPi * 2.0f / 3.0f;
            m_ui.Draw().AddLine(wx, wy, wx + std::cos(a) * 13 * s, wy + std::sin(a) * 13 * s, kInk, 3 * s);
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
        m_ui.TextRight(W - 24 * s, H - 30 * s, "PROTOTYPE 0.3", kSteel, small);
    }

    void DrawOptions() {
        if (m_optionsReturn == Screen::Paused) DrawHudStats();
        Dim(0.45f);
        float s = m_ui.S(), W = m_ui.W(), H = m_ui.H();
        UiRect p{W * 0.5f - 290 * s, H * 0.5f - 270 * s, 580 * s, 540 * s};
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
        y += h + g * 2;
        if (m_ui.Button(16, {W * 0.5f - 130 * s, y, 260 * s, 50 * s}, "BACK", true) || m_keys.Pressed(0x1B)) {
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
            "...BUT YOUR SHIP STOPS DEAD.",
            "TO KEEP GOING, BACK UP AND BUILD SPEED.",
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
        if (m_ui.Button(20, {W * 0.5f - 130 * s, p.Bottom() - 70 * s, 260 * s, 50 * s}, "BACK", true) || m_keys.Pressed(0x1B))
            m_screen = Screen::MainMenu;
    }

    void DrawPause() {
        Dim(0.5f);
        float s = m_ui.S(), W = m_ui.W(), H = m_ui.H();
        UiRect p{W * 0.5f - 200 * s, H * 0.5f - 200 * s, 400 * s, 400 * s};
        m_ui.Panel(p);
        m_ui.TextCentered(W * 0.5f, p.y + 22 * s, "PAUSED", kOrange, m_ui.T() * 2);
        float x = p.x + 40 * s, y = p.y + 90 * s, w = p.width - 80 * s, h = 52 * s, g = 16 * s;
        if (m_ui.Button(30, {x, y, w, h}, "RESUME", true) || m_keys.Pressed(0x1B)) m_screen = Screen::Playing;
        y += h + g;
        if (m_ui.Button(31, {x, y, w, h}, "RETRY")) StartRun();
        y += h + g;
        if (m_ui.Button(32, {x, y, w, h}, "OPTIONS")) {
            m_optionsReturn = Screen::Paused;
            m_screen = Screen::Options;
        }
        y += h + g;
        if (m_ui.Button(33, {x, y, w, h}, "MAIN MENU")) GoMenu();
    }

    void GoMenu() {
        m_link.Bind(0, Level().seed);
        m_sim.Reset();
        m_camera.Snap();
        m_screen = Screen::MainMenu;
    }

    void DrawResults() {
        float s = m_ui.S(), W = m_ui.W(), H = m_ui.H();
        UiRect p{W * 0.5f - 260 * s, H * 0.5f - 230 * s, 520 * s, 460 * s};
        m_ui.Panel(p);
        m_ui.TextCentered(W * 0.5f, p.y + 22 * s, "YOU MADE IT!", kGreen, m_ui.T() * 3);
        for (int i = 0; i < 3; ++i) {
            float cx = W * 0.5f + (i - 1) * 74 * s, cy = p.y + 120 * s;
            m_ui.FilledOctagon(cx + 3 * s, cy + 4 * s, 26 * s, kNavyDark);
            m_ui.FilledOctagon(cx, cy, 26 * s, i < m_stars ? kOrange : kSteelDark);
        }
        int houses = m_link.Destroyed();
        char buf[96];
        float y = p.y + 170 * s;
        std::snprintf(buf, sizeof(buf), "TIME  %d:%05.2f%s", static_cast<int>(m_raceTime) / 60, std::fmod(m_raceTime, 60.0f),
                      m_newBest ? "  NEW RECORD!" : "");
        m_ui.TextCentered(W * 0.5f, y, buf, kPaper);
        std::snprintf(buf, sizeof(buf), "HOUSES WRECKED  %d", houses);
        m_ui.TextCentered(W * 0.5f, y + 30 * s, buf, kPaper);
        std::snprintf(buf, sizeof(buf), "BUMPS  %d", m_sim.bumps);
        m_ui.TextCentered(W * 0.5f, y + 60 * s, buf, kPaper);
        std::snprintf(buf, sizeof(buf), "CITY HALL SENDS A BILL: %d,500 COINS", houses * 12);
        m_ui.TextCentered(W * 0.5f, y + 90 * s, houses > 0 ? buf : "CITY HALL APPLAUDS YOU", houses > 0 ? kRed : kGreen);
        float bw = 200 * s, bh = 52 * s;
        if (m_ui.Button(40, {W * 0.5f - bw - 10 * s, p.Bottom() - 80 * s, bw, bh}, "AGAIN", true)) StartRun();
        if (m_ui.Button(41, {W * 0.5f + 10 * s, p.Bottom() - 80 * s, bw, bh}, "MENU")) GoMenu();
    }

    void DrawHudStats() {
        float s = m_ui.S();
        m_ui.Panel({16 * s, 16 * s, 270 * s, 84 * s});
        m_ui.Text(30 * s, 26 * s, Level().name, kOrange);
        char buf[64];
        std::snprintf(buf, sizeof(buf), "%d:%05.2f / %d:%02d", static_cast<int>(m_raceTime) / 60, std::fmod(m_raceTime, 60.0f),
                      static_cast<int>(Level().parTime) / 60, static_cast<int>(Level().parTime) % 60);
        m_ui.Text(30 * s, 48 * s, buf, kPaper);
        std::snprintf(buf, sizeof(buf), "HOUSES %d   BUMPS %d", m_link.Destroyed(), m_sim.bumps);
        m_ui.Text(30 * s, 70 * s, buf, kPaper);
    }

    void DrawPlaying() {
        DrawHudStats();
        DrawConsole();
        if (m_phaseTime < kCountdown + 0.6f) {
            int n = 3 - static_cast<int>(m_phaseTime / 0.8f);
            float s = m_ui.S();
            UiRect r{m_ui.W() * 0.5f - 230 * s, m_ui.H() * 0.2f, 460 * s, 90 * s};
            m_ui.Panel(r);
            m_ui.TextCentered(m_ui.W() * 0.5f, r.y + 14 * s, Level().name, kOrange, m_ui.T() * 2);
            m_ui.TextCentered(m_ui.W() * 0.5f, r.y + 56 * s, n > 0 ? "CASTING OFF... " + std::to_string(n) : "FULL STEAM AHEAD!",
                              kPaper);
        }
    }

    void DrawConsole() {
        float s = m_ui.S();
        auto& d = m_ui.Draw();
        UiRect panel{m_ui.W() * 0.5f - 210 * s, m_ui.H() - 160 * s, 420 * s, 148 * s};
        m_ui.Panel(panel);
        UiRect lever{panel.x + 18 * s, panel.y + 14 * s, 74 * s, panel.height - 28 * s};
        m_ui.Chamfer(lever, 6 * s, kSteelDark);
        const int minO = ShipSim::kMinOrder, maxO = ShipSim::kMaxOrder, steps = maxO - minO;
        float top = lever.y + 16 * s, bottom = lever.Bottom() - 16 * s;
        auto yOf = [&](float o) { return Lerp(bottom, top, (o - minO) / steps); };
        bool leverHover = m_ui.Hover(lever);
        if (leverHover && m_ui.mouse.pressed) m_ui.activeId = 9000;
        if (m_ui.activeId == 9000 && m_ui.mouse.down) {
            m_leverDrag = Clamp(minO + (bottom - m_ui.mouse.y) / (bottom - top) * steps, static_cast<float>(minO), static_cast<float>(maxO));
            float snapped = std::round(m_leverDrag);
            if (snapped != m_sim.order) {
                m_sim.order = snapped;
                m_audio.Play("ui_click", -8);
            }
        }
        float shown = m_ui.activeId == 9000 && m_ui.mouse.down ? m_leverDrag : m_sim.order;
        float slotX = lever.x + 26 * s;
        d.AddRect({slotX, top - 4 * s, 8 * s, bottom - top + 8 * s}, kInk);
        for (int o = minO; o <= maxO; ++o) {
            float y = yOf(static_cast<float>(o));
            UiColor tick = o == 0 ? kPaper : (o > 0 ? kSteel : kRed);
            d.AddRect({lever.x + 46 * s, y - 1.5f * s, (o == 0 ? 18 : 12) * s, 3 * s}, tick);
        }
        float hy = yOf(shown);
        UiColor handle = leverHover || m_ui.activeId == 9000 ? kOrangeLight : kOrange;
        m_ui.Chamfer({lever.x + 8 * s, hy - 8 * s, 40 * s, 16 * s}, 3 * s, handle);
        d.AddRect({lever.x + 8 * s, hy + 4 * s, 40 * s, 4 * s}, kOrangeDark);
        float cx = panel.x + 205 * s, cy = panel.y + panel.height * 0.5f, rad = 54 * s;
        float dx = m_ui.mouse.x - cx, dy = m_ui.mouse.y - cy;
        bool wheelHover = dx * dx + dy * dy < (rad + 14 * s) * (rad + 14 * s);
        if (wheelHover && m_ui.mouse.pressed) {
            m_ui.activeId = 9001;
            m_wheelGrab = std::atan2(dy, dx);
        }
        if (m_ui.activeId == 9001 && m_ui.mouse.down) {
            float a = std::atan2(dy, dx);
            m_wheel = Clamp(m_wheel + WrapAngle(a - m_wheelGrab), -kWheelMax, kWheelMax);
            m_wheelGrab = a;
        }
        float rot = m_wheel;
        for (int k = 0; k < 3; ++k) {
            float a = rot - kPi * 0.5f + k * kPi * 2.0f / 3.0f;
            d.AddLine(cx, cy, cx + std::cos(a) * rad, cy + std::sin(a) * rad, kSteel, 7 * s);
        }
        UiColor ring = wheelHover || m_ui.activeId == 9001 ? kOrangeLight : kOrange;
        m_ui.Octagon(cx + 3 * s, cy + 4 * s, rad, 13 * s, kOrangeDark, rot);
        m_ui.Octagon(cx, cy, rad, 13 * s, ring, rot);
        m_ui.FilledOctagon(cx, cy, 10 * s, kInk);
        float arcR = rad + 15 * s;
        for (int i = -6; i <= 6; ++i) {
            float a = kPi * 0.5f + i * 0.13f;
            d.AddRect({cx + std::cos(a) * arcR - 1.5f * s, cy + std::sin(a) * arcR - 1.5f * s, 3 * s, 3 * s},
                      i == 0 ? kPaper : kSteelDark);
        }
        float ma = kPi * 0.5f - m_sim.s.rudder * 6 * 0.13f;
        m_ui.FilledOctagon(cx + std::cos(ma) * arcR, cy + std::sin(ma) * arcR, 5 * s, kOrange);
        float bx = panel.Right() - 110 * s;
        UiRect camB{bx, panel.y + 18 * s, 36 * s, 32 * s}, pauseB{bx + 46 * s, panel.y + 18 * s, 36 * s, 32 * s};
        bool camH = m_ui.Hover(camB), pauseH = m_ui.Hover(pauseB);
        m_ui.Chamfer(camB, 5 * s, m_opts.cameraFollow ? (camH ? kOrangeLight : kOrange) : kSteelDark);
        m_ui.Chamfer(pauseB, 5 * s, pauseH ? kOrangeLight : kOrange);
        d.AddTextCentered(camB, "C", kInk, m_ui.T());
        d.AddTextCentered(pauseB, "II", kInk, m_ui.T());
        if (m_ui.mouse.pressed && camH) m_ui.activeId = 9002;
        if (m_ui.mouse.released && camH && m_ui.activeId == 9002) m_opts.cameraFollow = !m_opts.cameraFollow;
        if (m_ui.mouse.pressed && pauseH) m_ui.activeId = 9003;
        if (m_ui.mouse.released && pauseH && m_ui.activeId == 9003) m_screen = Screen::Paused;
        float hx = bx + 41 * s, hy2 = panel.y + 104 * s;
        float hdx = m_ui.mouse.x - hx, hdy = m_ui.mouse.y - hy2;
        bool hornH = hdx * hdx + hdy * hdy < 26 * 26 * s * s;
        if (hornH && m_ui.mouse.pressed) {
            m_ui.activeId = 9004;
            Horn();
        }
        m_ui.FilledOctagon(hx + 2 * s, hy2 + 4 * s, 26 * s, kNavyDark);
        m_ui.FilledOctagon(hx, hy2, 26 * s, m_hornLit > 0.0f ? kPaper : (hornH ? kOrangeLight : kOrange));
    }
};

STRACE_REGISTER_SCRIPT(ShipfulnessGame)
