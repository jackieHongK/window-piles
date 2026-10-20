// "How to use" card shown in the middle of the screen (first run, menus, second launch).
// Replaces the tray balloon, which cut the text off and read like a task notification.
#pragma once

#include <windows.h>

#include <string>
#include <vector>

namespace wp {

class GuideWindow {
public:
    GuideWindow() = default;
    ~GuideWindow();
    void Show();
    void Close();

private:
    static LRESULT CALLBACK Proc(HWND, UINT, WPARAM, LPARAM);
    LRESULT Handle(UINT msg, WPARAM wp, LPARAM lp);
    void Layout();
    void Paint(HDC dc);

    struct Step {
        std::wstring title, text;
        RECT badge{}, titleRect{}, textRect{};
    };

    HWND hwnd_ = nullptr;
    UINT dpi_ = 96;
    SIZE size_{};
    std::vector<Step> steps_;
    RECT closeRect_{}, buttonRect_{}, tipRect_{}, tipTitleRect_{}, tipTextRect_{};
    int headerH_ = 0;
    bool hoverClose_ = false, hoverButton_ = false;
};

}  // namespace wp
