// "설정 · 사용법": one card in the middle of the screen with two tabs. 설정 holds everything that can be
// set (start with Windows, show or hide the app's surfaces, the edge organizer: on/off, side, slide,
// material; the memo folder); 사용법 is the how-to (first run, menus, second launch).
#pragma once

#include <windows.h>

#include <functional>
#include <string>
#include <vector>

namespace wp {

struct AppSettings {
    bool autostart = false;
    bool shown = true;       // cards, organizer and frame on screen ("항상 위로"), or put away ("숨기기")
    bool organizer = true;   // the edge organizer is on
    bool right = true;       // ... on the right edge
    bool slide = true;       // ... its drawer slides out
    std::string material = "walnut";
    std::wstring memoDir;
};

class GuideWindow {
public:
    enum class Tab { Settings, Usage };
    // `get` reads the settings as they are now; `set` applies them (whatever changed).
    GuideWindow(std::function<AppSettings()> get, std::function<void(const AppSettings&)> set)
        : get_(std::move(get)), set_(std::move(set)) {}
    ~GuideWindow();
    void Show(Tab tab = Tab::Usage);
    void Close();

private:
    static LRESULT CALLBACK Proc(HWND, UINT, WPARAM, LPARAM);
    LRESULT Handle(UINT msg, WPARAM wp, LPARAM lp);
    void Layout();
    void Paint(HDC dc);
    int HitAt(POINT p) const;  // a control's id (kHit*), or 0
    void Click(int hit);

    struct Step {
        std::wstring title, text;
        RECT badge{}, titleRect{}, textRect{};
    };
    struct Row {  // a setting: label and note on the left, its control on the right
        int hit;
        std::wstring label, note;
        RECT rect{}, control{};
        bool organizerOnly = false;
    };

    std::function<AppSettings()> get_;
    std::function<void(const AppSettings&)> set_;
    AppSettings s_;
    HWND hwnd_ = nullptr;
    UINT dpi_ = 96;
    SIZE size_{};
    Tab tab_ = Tab::Usage;
    std::vector<Step> steps_;
    std::vector<Row> rows_;
    std::vector<std::pair<int, RECT>> sections_;  // index into kSections, title rect
    std::vector<RECT> swatches_;
    RECT tabsRect_{}, tabRects_[2]{}, sideRects_[2]{}, materialNameRect_{}, folderRect_{}, folderButton_{};
    RECT closeRect_{}, buttonRect_{}, tipRect_{}, tipTitleRect_{}, tipTextRect_{};
    int headerH_ = 0, contentTop_ = 0;
    int hover_ = 0;
};

}  // namespace wp
