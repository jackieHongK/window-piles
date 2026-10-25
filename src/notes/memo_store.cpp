#include "memo_store.h"

#include <shellapi.h>
#include <shlobj.h>

#include <algorithm>
#include <cwctype>
#include <functional>

#include "common/win_util.h"
#include "notes/memo_format.h"

namespace wp {

namespace {

std::wstring IniPath() { return AppDataDir() + L"\\organizer.ini"; }

std::wstring FileOf(const std::wstring& path) { return path.substr(path.find_last_of(L'\\') + 1); }

constexpr int kColors = 7;  // the organizer's Post-it colors

std::wstring Today() {
    SYSTEMTIME t;
    GetLocalTime(&t);
    wchar_t d[16];
    swprintf_s(d, L"%04u-%02u-%02u", t.wYear, t.wMonth, t.wDay);
    return d;
}

// "dir\base.txt", or "dir\base (2).txt" ... if that name is taken by another file.
std::wstring FreeName(const std::wstring& dir, const std::wstring& name, const std::wstring& self = L"") {
    std::wstring base = name.substr(0, name.size() - 4), path = dir + L"\\" + name;
    for (int i = 2; _wcsicmp(path.c_str(), self.c_str()) != 0 && GetFileAttributesW(path.c_str()) != INVALID_FILE_ATTRIBUTES; ++i)
        path = dir + L"\\" + base + L" (" + std::to_wstring(i) + L").txt";
    return path;
}

std::wstring DefaultDir() {
    if (UsingTestDataDir()) return AppDataDir() + L"\\memos";  // tests never write to the user's Documents
    PWSTR docs = nullptr;
    std::wstring dir;
    if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_Documents, 0, nullptr, &docs))) dir = docs;
    CoTaskMemFree(docs);
    if (dir.empty()) dir = AppDataDir();
    return dir + L"\\Stack Neverflow 메모";
}

}  // namespace

std::wstring MemoDir() {
    wchar_t buf[MAX_PATH] = {};
    GetPrivateProfileStringW(L"memo", L"memoDir", L"", buf, MAX_PATH, IniPath().c_str());
    std::wstring dir = buf;
    if (dir.empty()) {
        dir = DefaultDir();
        // Remember where they are, so the same folder is used (and found) from now on.
        WritePrivateProfileStringW(L"memo", L"memoDir", dir.c_str(), IniPath().c_str());
    }
    SHCreateDirectoryExW(nullptr, dir.c_str(), nullptr);
    return dir;
}

std::vector<MemoInfo> ListMemos() {
    std::vector<MemoInfo> out;
    std::wstring dir = MemoDir();
    WIN32_FIND_DATAW fd;
    HANDLE f = FindFirstFileW((dir + L"\\*.txt").c_str(), &fd);
    if (f == INVALID_HANDLE_VALUE) return out;
    do {
        if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) continue;
        MemoInfo m;
        m.path = dir + L"\\" + fd.cFileName;
        std::wstring text = ReadMemo(m.path);
        m.title = MemoTitle(text);
        m.excerpt = MemoExcerpt(text);
        m.created = fd.ftCreationTime;
        // Its flag's color: chosen, or one that comes from the name, so it never changes by itself.
        m.color = (int)GetPrivateProfileIntW(L"memoColor", fd.cFileName, -1, IniPath().c_str());
        if (m.color < 0 || m.color >= kColors) m.color = (int)(std::hash<std::wstring>()(fd.cFileName) % kColors);
        out.push_back(std::move(m));
    } while (FindNextFileW(f, &fd));
    FindClose(f);
    std::sort(out.begin(), out.end(), [](const MemoInfo& a, const MemoInfo& b) { return CompareFileTime(&a.created, &b.created) > 0; });
    // Then the order they were dragged into; memos not in it (new, or added outside the app) stay on top.
    std::vector<wchar_t> buf(32768);
    GetPrivateProfileStringW(L"memo", L"order", L"", buf.data(), (DWORD)buf.size(), IniPath().c_str());
    std::wstring order = L"|" + std::wstring(buf.data()) + L"|";
    auto rank = [&](const MemoInfo& m) {
        size_t at = order.find(L"|" + m.path.substr(m.path.find_last_of(L'\\') + 1) + L"|");
        return at == std::wstring::npos ? -1 : (int)at;
    };
    std::stable_sort(out.begin(), out.end(), [&](const MemoInfo& a, const MemoInfo& b) { return rank(a) < rank(b); });
    return out;
}

void SaveMemoOrder(const std::vector<MemoInfo>& memos) {
    std::wstring order;
    for (const MemoInfo& m : memos) order += (order.empty() ? L"" : L"|") + m.path.substr(m.path.find_last_of(L'\\') + 1);
    WritePrivateProfileStringW(L"memo", L"order", order.c_str(), IniPath().c_str());
}

std::wstring NewMemo() {
    std::wstring path = FreeName(MemoDir(), MemoFileName(Today(), L""));
    return WriteMemo(path, L"") ? path : L"";
}

void SetMemoColor(const std::wstring& path, int color) {
    WritePrivateProfileStringW(L"memoColor", FileOf(path).c_str(), std::to_wstring(color).c_str(), IniPath().c_str());
}

std::wstring RenameMemoForTitle(const std::wstring& path, const std::wstring& title) {
    std::wstring dir = path.substr(0, path.find_last_of(L'\\')), old = FileOf(path);
    // The date it was made: from its name when it has one, else today.
    std::wstring date = old.size() > 10 && iswdigit(old[0]) && old[4] == L'-' && old[7] == L'-' ? old.substr(0, 10) : Today();
    std::wstring want = MemoFileName(date, title);
    if (_wcsicmp(want.c_str(), old.c_str()) == 0) return path;
    std::wstring to = FreeName(dir, want, path);
    if (_wcsicmp(to.c_str(), path.c_str()) == 0 || !MoveFileExW(path.c_str(), to.c_str(), 0)) return path;
    std::wstring now = FileOf(to);
    // Its place in the arranged order and its color go with it.
    std::vector<wchar_t> buf(32768);
    GetPrivateProfileStringW(L"memo", L"order", L"", buf.data(), (DWORD)buf.size(), IniPath().c_str());
    std::wstring order = L"|" + std::wstring(buf.data()) + L"|";
    if (size_t at = order.find(L"|" + old + L"|"); at != std::wstring::npos) {
        order.replace(at + 1, old.size(), now);
        WritePrivateProfileStringW(L"memo", L"order", order.substr(1, order.size() - 2).c_str(), IniPath().c_str());
    }
    int color = (int)GetPrivateProfileIntW(L"memoColor", old.c_str(), -1, IniPath().c_str());
    if (color >= 0) {
        WritePrivateProfileStringW(L"memoColor", old.c_str(), nullptr, IniPath().c_str());
        SetMemoColor(to, color);
    }
    return to;
}

std::wstring ReadMemo(const std::wstring& path) {
    HANDLE f = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr,
                           OPEN_EXISTING, 0, nullptr);
    if (f == INVALID_HANDLE_VALUE) return L"";
    std::string bytes;
    LARGE_INTEGER size{};
    if (GetFileSizeEx(f, &size) && size.QuadPart > 0 && size.QuadPart < (16 << 20)) {
        bytes.resize((size_t)size.QuadPart);
        DWORD n = 0;
        ReadFile(f, bytes.data(), (DWORD)bytes.size(), &n, nullptr);
        bytes.resize(n);
    }
    CloseHandle(f);
    if (bytes.size() >= 3 && (unsigned char)bytes[0] == 0xEF && (unsigned char)bytes[1] == 0xBB && (unsigned char)bytes[2] == 0xBF)
        bytes.erase(0, 3);
    return Widen(bytes);
}

bool WriteMemo(const std::wstring& path, const std::wstring& text) {
    // Windows line breaks, so Notepad and every other editor show the lines.
    std::wstring crlf;
    crlf.reserve(text.size() + 16);
    for (size_t i = 0; i < text.size(); ++i) {
        wchar_t c = text[i];
        if (c == L'\r') {
            crlf += L"\r\n";
            if (i + 1 < text.size() && text[i + 1] == L'\n') ++i;
        } else if (c == L'\n') {
            crlf += L"\r\n";
        } else {
            crlf += c;
        }
    }
    return WriteFileReplacing(path, Utf8(crlf), true);
}

bool RecycleMemo(const std::wstring& path) {
    if (UsingTestDataDir()) return DeleteFileW(path.c_str()) != FALSE;  // tests don't fill the user's Recycle Bin
    std::wstring from = path + L'\0';  // double-null terminated list
    SHFILEOPSTRUCTW op{};
    op.wFunc = FO_DELETE;
    op.pFrom = from.c_str();
    op.fFlags = FOF_ALLOWUNDO | FOF_NOCONFIRMATION | FOF_SILENT | FOF_NOERRORUI;
    return SHFileOperationW(&op) == 0 && !op.fAnyOperationsAborted;
}

}  // namespace wp
