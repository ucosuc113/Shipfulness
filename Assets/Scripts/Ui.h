#pragma once

#include "Shipfulness.h"
#include "Engine/UI/UiService.h"
#include "Engine/Input/InputService.h"

#define WIN32_LEAN_AND_MEAN
#include <Windows.h>
#pragma comment(lib, "user32.lib")

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
        bool now;
        if (m_engine) {
            x = f.cursorX;
            y = f.cursorY;
            wheel = f.wheelDelta;
            now = f.MouseButtonDown(strace::MouseButton::Left);
        } else {
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
        m_s = std::max(0.75f, m_h / 720.0f);
        m_text = std::max(1, static_cast<int>(std::round(m_s * 2.0f)));
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
    float m_w = 1280, m_h = 720, m_s = 1;
    int m_text = 2;
    int m_hoverNow = 0, m_hoverLast = 0;
};

}
