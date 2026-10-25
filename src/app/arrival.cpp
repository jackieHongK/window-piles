#include "app/arrival.h"

#include <algorithm>
#include <cmath>
#include <cstdint>

#include "common/canvas.h"
#include "common/win_util.h"

namespace wp {

namespace {

bool SetKey(HKEY root, const std::wstring& path, const wchar_t* name, const std::wstring& value) {
    HKEY k;
    if (RegCreateKeyExW(root, path.c_str(), 0, nullptr, 0, KEY_SET_VALUE, nullptr, &k, nullptr) != ERROR_SUCCESS) return false;
    LONG r = RegSetValueExW(k, name, 0, REG_SZ, reinterpret_cast<const BYTE*>(value.c_str()), DWORD((value.size() + 1) * sizeof(wchar_t)));
    RegCloseKey(k);
    return r == ERROR_SUCCESS;
}

constexpr wchar_t kPulseClass[] = L"WindowPilesArrivalPulse";
constexpr double kPulseMs = 900;

struct Pulse {
    RECT mon{};
    POINT touch{};  // in monitor-local px
    double start = 0;
};

float Band(float d, float r, float band) {  // 0 outside [r - band, r], peak at 75 % of the way to r
    float t = (d - (r - band)) / band;
    if (t <= 0 || t >= 1) return 0;
    return t < 0.75f ? t / 0.75f : (1 - t) / 0.25f;
}

// One frame, drawn at half resolution and doubled (a soft gradient loses nothing). Additive light, premultiplied.
void DrawPulse(Canvas& c, const Pulse& p, double k) {
    const int W = c.width(), H = c.height(), w2 = (W + 1) / 2, h2 = (H + 1) / 2;
    const float cx = p.touch.x / 2.0f, cy = p.touch.y / 2.0f, fade = float(1 - k);
    const float R = std::hypot(float(w2), float(h2)) * 1.1f * float(1 - std::pow(1 - k, 3));
    const float glowR = h2 * float(0.35 + 0.4 * k), band = h2 * 0.28f;
    struct Ring { float r, g, b, lag, a; };
    const Ring rings[] = {{130, 200, 255, 0.00f, 0.60f}, {200, 150, 255, 0.07f, 0.50f}, {255, 150, 200, 0.14f, 0.45f}, {255, 205, 140, 0.21f, 0.40f}};
    uint32_t* px = c.pixels();
    for (int y = 0; y < h2; ++y)
        for (int x = 0; x < w2; ++x) {
            float d = std::hypot(x - cx, y - cy);
            float g = std::max(0.0f, 1 - d / glowR) * 0.75f * fade;
            float r = 255 * g, gg = 255 * g, b = 255 * g;
            for (const Ring& q : rings) {
                float v = Band(d, std::max(2.0f, R * (1 - q.lag)), band) * q.a * fade;
                r += q.r * v; gg += q.g * v; b += q.b * v;
            }
            uint32_t R8 = uint32_t(std::min(255.0f, r)), G8 = uint32_t(std::min(255.0f, gg)), B8 = uint32_t(std::min(255.0f, b));
            uint32_t A8 = std::max({R8, G8, B8});
            uint32_t v = (A8 << 24) | (R8 << 16) | (G8 << 8) | B8;
            int X = x * 2, Y = y * 2;
            px[Y * W + X] = v;
            if (X + 1 < W) px[Y * W + X + 1] = v;
            if (Y + 1 < H) { px[(Y + 1) * W + X] = v; if (X + 1 < W) px[(Y + 1) * W + X + 1] = v; }
        }
}

LRESULT CALLBACK PulseProc(HWND h, UINT msg, WPARAM wp, LPARAM lp) {
    auto* p = reinterpret_cast<Pulse*>(GetWindowLongPtrW(h, GWLP_USERDATA));
    switch (msg) {
        case WM_TIMER: {
            double k = std::min(1.0, (NowMs() - p->start) / kPulseMs);
            Canvas c(p->mon.right - p->mon.left, p->mon.bottom - p->mon.top);
            DrawPulse(c, *p, k);
            c.Present(h, {p->mon.left, p->mon.top});
            if (k >= 1) DestroyWindow(h);
            return 0;
        }
        case WM_NCDESTROY:
            delete p;
            return 0;
    }
    return DefWindowProcW(h, msg, wp, lp);
}

}  // namespace

void RegisterUrlProtocol() {
    wchar_t exe[MAX_PATH];
    GetModuleFileNameW(nullptr, exe, MAX_PATH);
    const std::wstring base = L"Software\\Classes\\windowpiles";
    std::wstring cmd = L"\"" + std::wstring(exe) + L"\" \"%1\"";
    // Only write when it isn't already pointing at us (a copy elsewhere, a rebuilt path).
    wchar_t cur[MAX_PATH * 2] = {};
    DWORD size = sizeof(cur);
    if (RegGetValueW(HKEY_CURRENT_USER, (base + L"\\shell\\open\\command").c_str(), nullptr, RRF_RT_REG_SZ, nullptr, cur, &size) == ERROR_SUCCESS &&
        cmd == cur)
        return;
    bool ok = SetKey(HKEY_CURRENT_USER, base, nullptr, L"URL:Stack Neverflow") && SetKey(HKEY_CURRENT_USER, base, L"URL Protocol", L"") &&
              SetKey(HKEY_CURRENT_USER, base + L"\\DefaultIcon", nullptr, L"\"" + std::wstring(exe) + L"\",0") &&
              SetKey(HKEY_CURRENT_USER, base + L"\\shell\\open\\command", nullptr, cmd);
    Log("url protocol: windowpiles:// %s", ok ? "registered" : "could not be registered");
}

bool ParseArriveUrl(const std::wstring& arg, int* yPermille) {
    std::wstring a = arg;
    std::transform(a.begin(), a.end(), a.begin(), ::towlower);
    if (a.rfind(L"windowpiles://organizer/arrive", 0) != 0) return false;
    *yPermille = 500;
    size_t q = a.find(L"y=");
    if (q != std::wstring::npos) *yPermille = std::clamp(int(_wtof(a.c_str() + q + 2) * 1000 + 0.5), 0, 1000);
    return true;
}

static int pendingArrive = -1;
void SetPendingArrive(int yPermille) { pendingArrive = yPermille; }
int TakePendingArrive() { int y = pendingArrive; pendingArrive = -1; return y; }

void PlayArrivalPulse(POINT touch) {
    static bool registered = false;
    HINSTANCE inst = GetModuleHandleW(nullptr);
    if (!registered) {
        WNDCLASSW wc{};
        wc.lpfnWndProc = PulseProc;
        wc.hInstance = inst;
        wc.lpszClassName = kPulseClass;
        RegisterClassW(&wc);
        registered = true;
    }
    MONITORINFO mi{sizeof(mi)};
    GetMonitorInfoW(MonitorFromPoint(touch, MONITOR_DEFAULTTONEAREST), &mi);
    auto* p = new Pulse{mi.rcMonitor, {touch.x - mi.rcMonitor.left, touch.y - mi.rcMonitor.top}, NowMs()};
    HWND h = CreateWindowExW(WS_EX_LAYERED | WS_EX_TRANSPARENT | WS_EX_TOPMOST | WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE, kPulseClass, L"",
                             WS_POPUP, mi.rcMonitor.left, mi.rcMonitor.top, mi.rcMonitor.right - mi.rcMonitor.left,
                             mi.rcMonitor.bottom - mi.rcMonitor.top, nullptr, nullptr, inst, nullptr);
    if (!h) { delete p; return; }
    SetWindowLongPtrW(h, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(p));
    ShowWindow(h, SW_SHOWNOACTIVATE);
    SetTimer(h, 1, 15, nullptr);
    PostMessageW(h, WM_TIMER, 1, 0);
}

}  // namespace wp
