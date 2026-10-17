#pragma once

#include "Shipfulness.h"
#include "Engine/UI/UiService.h"
#include "Engine/Input/InputService.h"

// El fallback de Win32 (leer el cursor del sistema antes de que el motor
// reparta su primer frame de entrada) no existe en web: alla la unica fuente
// de entrada es InputService. Se compila solo donde hay Win32.
#ifndef __EMSCRIPTEN__
#define WIN32_LEAN_AND_MEAN
#include <Windows.h>
#pragma comment(lib, "user32.lib")
#endif

#include <cstdio>
#include <functional>

namespace ship {

using strace::UiColor;
using strace::UiRect;

constexpr UiColor kNavy{0.15f, 0.19f, 0.36f, 0.96f};
constexpr UiColor kNavyLight{0.22f, 0.27f, 0.47f, 0.98f};
constexpr UiColor kNavyDark{0.10f, 0.13f, 0.26f, 1.0f};
constexpr UiColor kOrange{0.98f, 0.66f, 0.27f, 1.0f};
constexpr UiColor kOrangeLight{1.0f, 0.78f, 0.45f, 1.0f};
constexpr UiColor kOrangeDark{0.82f, 0.48f, 0.16f, 1.0f};
constexpr UiColor kSteel{0.55f, 0.58f, 0.66f, 1.0f};
constexpr UiColor kSteelDark{0.36f, 0.39f, 0.47f, 1.0f};
constexpr UiColor kInk{0.11f, 0.13f, 0.22f, 1.0f};
constexpr UiColor kPaper{0.98f, 0.97f, 0.93f, 1.0f};
constexpr UiColor kShadow{0.05f, 0.08f, 0.15f, 0.45f};
constexpr UiColor kGreen{0.50f, 0.86f, 0.52f, 1.0f};
constexpr UiColor kRed{0.92f, 0.36f, 0.33f, 1.0f};

class Mouse {
public:
    float x = 0.0f, y = 0.0f, wheel = 0.0f;
    bool down = false, pressed = false, released = false;

    void Update(float uiW, float uiH) {
        const strace::InputFrame& f = strace::InputService::Actions().CurrentFrame();
        if (f.cursorInside || f.cursorX != 0.0f || f.cursorY != 0.0f) m_engine = true;
        bool now = false;
        if (m_engine) {
            x = f.cursorX;
            y = f.cursorY;
            wheel = f.wheelDelta;
            now = f.MouseButtonDown(strace::MouseButton::Left);
        }
#ifndef __EMSCRIPTEN__
        else {
            POINT p{};
            GetCursorPos(&p);
            HWND h = GetForegroundWindow();
            RECT rc{};
            if (h && ScreenToClient(h, &p) && GetClientRect(h, &rc) && rc.right > 0 && rc.bottom > 0) {
                x = static_cast<float>(p.x) * uiW / static_cast<float>(rc.right);
                y = static_cast<float>(p.y) * uiH / static_cast<float>(rc.bottom);
            }
            wheel = 0.0f;
            now = (GetAsyncKeyState(VK_LBUTTON) & 0x8000) != 0 && h && GetForegroundWindow() == h;
        }
#endif
        pressed = now && !down;
        released = !now && down;
        down = now;
    }

private:
    bool m_engine = false;
};

class Ui {
public:
    Mouse mouse;
    std::function<void(const char*)> sound;
    int activeId = 0;

    void Begin() {
        m_ui = &strace::UiService::Frame();
        m_w = strace::UiService::Width();
        m_h = strace::UiService::Height();
        // LA ESCALA BASE sigue a la pantalla con un piso de 0,75, que es lo
        // mas chico en que la fuente de 5x7 va a escala 2. Mira el ancho
        // tambien (contra 960, no 1280): en apaisado manda el alto, pero en
        // una ventana angosta o vertical las columnas se salian por el costado.
        //
        // El piso NO garantiza que una pantalla entre: el diseno esta medido
        // contra 720 filas, y el embed de itch.io (640x360) tiene la mitad.
        // De eso se encarga Fit(): cada pantalla primero se APRIETA (aire) y
        // solo si ni asi entra se achica. Bajar el piso a 0,5 para todas las
        // pantallas las entraba, pero a la mitad de tamano en cualquier
        // ventana chica - con el texto a escala 1 aunque sobrara lugar.
        m_base = std::max(0.75f, std::min(m_h / 720.0f, m_w / 960.0f));
        Restore();
        mouse.Update(m_w, m_h);
        if (!mouse.down && !mouse.released) activeId = 0;
        m_hoverNow = 0;
    }

    void End() {
        if (m_hoverNow != m_hoverLast && m_hoverNow != 0 && sound) sound("hover");
        m_hoverLast = m_hoverNow;
        if (mouse.released) activeId = 0;
    }

    float W() const { return m_w; }
    float H() const { return m_h; }
    float S() const { return m_s; }
    int T() const { return m_text; }
    strace::UiDrawList& Draw() { return *m_ui; }

    // EL AJUSTE DE UNA PANTALLA A LA VENTANA. `need(k)` devuelve cuanto alto
    // pide la pantalla con un aire k (1 = el diseno de 720 filas, 0 = lo mas
    // apretado que se sigue leyendo), en pixeles y con la escala del momento:
    // tiene que leer S() y T() ADENTRO, porque Fit puede cambiarlas.
    //
    // El orden es la decision: primero se quita aire (margenes, huecos,
    // botones mas bajos) y el texto queda del mismo tamano; solo si la
    // pantalla no entra ni apretada se achica todo, y solo lo justo. Devuelve
    // el aire mas grande que entra. La pantalla tiene que dejar la escala
    // como estaba al terminar (UiFitScope).
    template <class Need>
    float Fit(float avail, Need&& need) {
        for (int i = 0; i < 3; ++i) {
            float tight = need(0.0f);
            if (tight <= avail) break;
            // T es entero: al achicar puede saltar de 2 a 1, por eso se mide
            // otra vez en vez de confiar en una sola regla de tres.
            Shrink(Shrunk() * avail / tight);
        }
        if (need(1.0f) <= avail) return 1.0f;
        float lo = 0.0f, hi = 1.0f;
        for (int i = 0; i < 12; ++i) {
            float mid = (lo + hi) * 0.5f;
            (need(mid) <= avail ? lo : hi) = mid;
        }
        return lo;
    }

    // Lo mismo de costado, para una pantalla que no puede apretar su ancho
    // (un panel con texto adentro): si no entra, se achica.
    void FitWidth(float need) {
        if (need > m_w) Shrink(Shrunk() * m_w / need);
    }

    void Shrink(float f) {
        m_s = m_base * std::min(1.0f, f);
        m_text = std::max(1, static_cast<int>(std::round(m_s * 2.0f)));
    }
    void Restore() { Shrink(1.0f); }
    float Shrunk() const { return m_s / m_base; }

    bool Hover(const UiRect& r) const { return r.Contains(mouse.x, mouse.y); }

    void Rect(UiRect r, UiColor c) { m_ui->AddRect(r, c); }

    void Chamfer(UiRect r, float c, UiColor col) {
        c = std::min(c, std::min(r.width, r.height) * 0.5f);
        m_ui->AddRect({r.x + c, r.y, r.width - 2 * c, r.height}, col);
        m_ui->AddRect({r.x, r.y + c, c, r.height - 2 * c}, col);
        m_ui->AddRect({r.Right() - c, r.y + c, c, r.height - 2 * c}, col);
        float h = c * 0.5f;
        m_ui->AddRect({r.x + h, r.y + h, h, h}, col);
        m_ui->AddRect({r.Right() - c, r.y + h, h, h}, col);
        m_ui->AddRect({r.x + h, r.Bottom() - c, h, h}, col);
        m_ui->AddRect({r.Right() - c, r.Bottom() - c, h, h}, col);
    }

    void Panel(UiRect r, UiColor col = kNavy) {
        Chamfer({r.x + 5 * m_s, r.y + 7 * m_s, r.width, r.height}, 14 * m_s, kShadow);
        Chamfer(r, 14 * m_s, col);
    }

    void Octagon(float cx, float cy, float radius, float thick, UiColor col, float rot = 0.0f) {
        for (int i = 0; i < 8; ++i) {
            float a0 = rot + kPi / 8 + i * kPi / 4, a1 = a0 + kPi / 4;
            m_ui->AddLine(cx + std::cos(a0) * radius, cy + std::sin(a0) * radius, cx + std::cos(a1) * radius,
                          cy + std::sin(a1) * radius, col, thick);
        }
    }

    void FilledOctagon(float cx, float cy, float radius, UiColor col) {
        float c = radius * 0.4142f;
        for (float k = 0.0f; k <= 1.001f; k += 0.2f) {
            float e = Lerp(c, radius, k), f = Lerp(radius, c, k);
            m_ui->AddRect({cx - e, cy - f, 2 * e, 2 * f}, col);
        }
    }

    void Text(float x, float y, const std::string& s, UiColor col, int scale = 0) {
        int sc = scale > 0 ? scale : m_text;
        m_ui->AddText(x + sc, y + sc, s, kShadow, sc);
        m_ui->AddText(x, y, s, col, sc);
    }

    void TextCentered(float cx, float y, const std::string& s, UiColor col, int scale = 0) {
        int sc = scale > 0 ? scale : m_text;
        Text(cx - static_cast<float>(strace::MeasureTextWidth(s, sc)) * 0.5f, y, s, col, sc);
    }

    void TextRight(float rx, float y, const std::string& s, UiColor col, int scale = 0) {
        int sc = scale > 0 ? scale : m_text;
        Text(rx - static_cast<float>(strace::MeasureTextWidth(s, sc)), y, s, col, sc);
    }

    bool Button(int id, UiRect r, const std::string& label, bool primary = false) {
        bool hover = Hover(r);
        if (hover) m_hoverNow = id;
        if (hover && mouse.pressed) activeId = id;
        bool held = activeId == id && mouse.down;
        UiColor bg = primary ? (hover ? kOrangeLight : kOrange) : (hover ? kNavyLight : kNavy);
        float off = held ? 3 * m_s : 0.0f;
        Chamfer({r.x + 4 * m_s, r.y + 6 * m_s, r.width, r.height}, 10 * m_s, kShadow);
        Chamfer({r.x, r.y + off, r.width, r.height}, 10 * m_s, bg);
        if (hover && !primary) m_ui->AddRect({r.x + 10 * m_s, r.Bottom() - 6 * m_s + off, r.width - 20 * m_s, 3 * m_s}, kOrange);
        int sc = m_text + 1;
        float ty = r.y + off + (r.height - 7.0f * sc) * 0.5f;
        TextCentered(r.x + r.width * 0.5f, ty, label, primary ? kInk : kPaper, sc);
        bool clicked = hover && mouse.released && activeId == id;
        if (clicked && sound) sound("click");
        return clicked;
    }

    bool Toggle(int id, UiRect r, const std::string& label, bool& value) {
        bool hover = Hover(r);
        if (hover) m_hoverNow = id;
        if (hover && mouse.pressed) activeId = id;
        Chamfer(r, 8 * m_s, hover ? kNavyLight : kNavy);
        Text(r.x + 16 * m_s, r.y + (r.height - 7.0f * m_text) * 0.5f, label, kPaper);
        UiRect sw{r.Right() - 86 * m_s, r.y + r.height * 0.5f - 13 * m_s, 70 * m_s, 26 * m_s};
        Chamfer(sw, 6 * m_s, value ? kOrange : kSteelDark);
        UiRect knob{value ? sw.Right() - 32 * m_s : sw.x + 4 * m_s, sw.y + 4 * m_s, 28 * m_s, 18 * m_s};
        Chamfer(knob, 4 * m_s, value ? kPaper : kSteel);
        bool clicked = hover && mouse.released && activeId == id;
        if (clicked) {
            value = !value;
            if (sound) sound("click");
        }
        return clicked;
    }

    bool Cycle(int id, UiRect r, const std::string& label, int& value, const char* const* names, int count) {
        bool hover = Hover(r);
        if (hover) m_hoverNow = id;
        if (hover && mouse.pressed) activeId = id;
        Chamfer(r, 8 * m_s, hover ? kNavyLight : kNavy);
        Text(r.x + 16 * m_s, r.y + (r.height - 7.0f * m_text) * 0.5f, label, kPaper);
        UiRect box{r.Right() - 176 * m_s, r.y + r.height * 0.5f - 15 * m_s, 160 * m_s, 30 * m_s};
        Chamfer(box, 6 * m_s, kOrange);
        m_ui->AddTextCentered(box, names[value], kInk, m_text);
        bool clicked = hover && mouse.released && activeId == id;
        if (clicked) {
            value = (value + 1) % count;
            if (sound) sound("click");
        }
        return clicked;
    }

    bool Slider(int id, UiRect r, const std::string& label, float& value) {
        bool hover = Hover(r);
        if (hover) m_hoverNow = id;
        if (hover && mouse.pressed) activeId = id;
        Chamfer(r, 8 * m_s, hover || activeId == id ? kNavyLight : kNavy);
        Text(r.x + 16 * m_s, r.y + (r.height - 7.0f * m_text) * 0.5f, label, kPaper);
        UiRect track{r.Right() - 230 * m_s, r.y + r.height * 0.5f - 4 * m_s, 160 * m_s, 8 * m_s};
        bool changed = false;
        if (activeId == id && mouse.down) {
            float v = Clamp((mouse.x - track.x) / track.width, 0.0f, 1.0f);
            changed = std::fabs(v - value) > 1e-4f;
            value = v;
        }
        m_ui->AddRect(track, kInk);
        m_ui->AddRect({track.x, track.y, track.width * value, track.height}, kOrange);
        FilledOctagon(track.x + track.width * value, track.y + track.height * 0.5f, 11 * m_s, kPaper);
        char buf[16];
        std::snprintf(buf, sizeof(buf), "%d%%", static_cast<int>(std::round(value * 100)));
        TextRight(r.Right() - 14 * m_s, r.y + (r.height - 7.0f * m_text) * 0.5f, buf, kOrange);
        if (mouse.released && activeId == id && sound) sound("click");
        return changed;
    }

private:
    strace::UiDrawList* m_ui = nullptr;
    float m_w = 1280, m_h = 720, m_s = 1, m_base = 1;
    int m_text = 2;
    int m_hoverNow = 0, m_hoverLast = 0;
};

// Devuelve la escala base al salir de una pantalla que se ajusto con Fit: si
// no, lo que se dibuja despues (el aviso, el HUD del frame siguiente) heredaria
// la escala achicada de otra pantalla.
struct UiFitScope {
    Ui& ui;
    ~UiFitScope() { ui.Restore(); }
};

}
