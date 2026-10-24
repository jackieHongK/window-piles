#include "pile_store.h"

#include <cstdio>

#include "common/json.h"
#include "common/win_util.h"
#include "ui/material.h"

namespace wp {

namespace {

constexpr int kSchema = 1;

std::string Num(uint64_t v) { return std::to_string(v); }
std::string Num(long long v) { return std::to_string(v); }

std::string WriteMissing(const MissingWindow& m) {
    return "{\"exe\":" + json::Quote(m.exePath) + ",\"app\":" + json::Quote(m.appName) +
           ",\"title\":" + json::Quote(m.title) + "}";
}

MissingWindow ReadMissing(const json::Value& v) {
    MissingWindow m;
    if (auto* x = v.Get("exe")) m.exePath = x->WStr();
    if (auto* x = v.Get("app")) m.appName = x->WStr();
    if (auto* x = v.Get("title")) m.title = x->WStr();
    return m;
}

}  // namespace

std::wstring PileStatePath() { return AppDataDir() + L"\\piles.json"; }

namespace {

// Background writes: the flushed write takes tens of ms on a hard disk, so it runs on a pool
// thread. Only the newest state matters; one writer at a time keeps the replace atomic.
SRWLOCK g_writeLock = SRWLOCK_INIT;    // held while a file write is in progress
SRWLOCK g_pendingLock = SRWLOCK_INIT;  // guards the two below
std::string g_pending;
bool g_hasPending = false, g_writerQueued = false;

bool WriteState(const std::string& s) { return WriteFileReplacing(PileStatePath(), s, true); }

void CALLBACK WriteLatest(PTP_CALLBACK_INSTANCE, void*) {
    for (;;) {
        AcquireSRWLockExclusive(&g_writeLock);
        AcquireSRWLockExclusive(&g_pendingLock);
        if (!g_hasPending) {
            g_writerQueued = false;
            ReleaseSRWLockExclusive(&g_pendingLock);
            ReleaseSRWLockExclusive(&g_writeLock);
            return;
        }
        std::string s;
        s.swap(g_pending);
        g_hasPending = false;
        ReleaseSRWLockExclusive(&g_pendingLock);
        WriteState(s);
        ReleaseSRWLockExclusive(&g_writeLock);
    }
}

}  // namespace

bool SavePileState(const std::vector<SavedPile>& piles, bool background) {
    std::string s = "{\"schema\":" + std::to_string(kSchema) + ",\"piles\":[";
    for (size_t i = 0; i < piles.size(); ++i) {
        const SavedPile& p = piles[i];
        if (i) s += ",";
        s += "{\"id\":" + Num((long long)p.id) + ",\"name\":" + json::Quote(p.name) +
             ",\"customName\":" + (p.customName ? "true" : "false") + ",\"color\":" + Num((uint64_t)p.color) +
             ",\"material\":\"" + std::string(kMaterials[ClampMaterial(p.material)].id) + "\"" +
             ",\"docked\":" + (p.docked ? "true" : "false") +
             ",\"x\":" + Num((long long)p.pos.x) + ",\"y\":" + Num((long long)p.pos.y) +
             ",\"folded\":" + (p.folded ? "true" : "false") + ",\"members\":[";
        for (size_t k = 0; k < p.members.size(); ++k) {
            const SavedMember& m = p.members[k];
            const RECT& r = m.wp.rcNormalPosition;
            if (k) s += ",";
            // hwnd/createTime as strings: exact 64-bit round trip
            s += "{\"hwnd\":\"" + Num(m.hwnd) + "\",\"pid\":" + Num((uint64_t)m.pid) + ",\"createTime\":\"" +
                 Num(m.createTime) + "\",\"class\":" + json::Quote(m.className) + ",\"exe\":" + json::Quote(m.exePath) +
                 ",\"title\":" + json::Quote(m.title) + ",\"hidden\":" + (m.hidden ? "true" : "false") +
                 ",\"z\":" + Num(m.stamp) +
                 ",\"showCmd\":" + Num((uint64_t)m.wp.showCmd) + ",\"flags\":" + Num((uint64_t)m.wp.flags) +
                 ",\"normal\":[" + Num((long long)r.left) + "," + Num((long long)r.top) + "," + Num((long long)r.right) +
                 "," + Num((long long)r.bottom) + "],\"maxPos\":[" + Num((long long)m.wp.ptMaxPosition.x) + "," +
                 Num((long long)m.wp.ptMaxPosition.y) + "]}";
        }
        s += "],\"missing\":[";
        for (size_t k = 0; k < p.missing.size(); ++k) s += (k ? "," : "") + WriteMissing(p.missing[k]);
        s += "],\"files\":[";
        for (size_t k = 0; k < p.files.size(); ++k) s += (k ? "," : "") + json::Quote(p.files[k]);
        s += "]}";
    }
    s += "]}";

    if (background) {
        AcquireSRWLockExclusive(&g_pendingLock);
        g_pending.swap(s);
        g_hasPending = true;
        bool queue = !g_writerQueued;
        g_writerQueued = true;
        ReleaseSRWLockExclusive(&g_pendingLock);
        if (queue && !TrySubmitThreadpoolCallback(WriteLatest, nullptr, nullptr)) WriteLatest(nullptr, nullptr);
        return true;
    }
    // Synchronous (exit, logoff): this state supersedes anything still queued.
    AcquireSRWLockExclusive(&g_writeLock);
    AcquireSRWLockExclusive(&g_pendingLock);
    g_pending.clear();
    g_hasPending = false;
    ReleaseSRWLockExclusive(&g_pendingLock);
    bool ok = WriteState(s);
    ReleaseSRWLockExclusive(&g_writeLock);
    return ok;
}

bool LoadPileState(std::vector<SavedPile>& piles) {
    HANDLE f = CreateFileW(PileStatePath().c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING,
                           FILE_ATTRIBUTE_NORMAL, nullptr);
    if (f == INVALID_HANDLE_VALUE) return false;
    LARGE_INTEGER size{};
    GetFileSizeEx(f, &size);
    if (size.QuadPart <= 0 || size.QuadPart > (8 << 20)) {
        CloseHandle(f);
        return false;
    }
    std::string text((size_t)size.QuadPart, '\0');
    DWORD read = 0;
    ReadFile(f, text.data(), (DWORD)text.size(), &read, nullptr);
    CloseHandle(f);
    text.resize(read);

    json::Value root;
    if (!json::Parse(text, root) || root.type != json::Value::Type::Object) return false;
    const json::Value* schema = root.Get("schema");
    if (!schema || schema->Int() != kSchema) return false;
    const json::Value* arr = root.Get("piles");
    if (!arr || arr->type != json::Value::Type::Array) return false;
    for (const auto& pv : arr->arr) {
        SavedPile p;
        if (auto* x = pv.Get("id")) p.id = (int)x->Int();
        if (auto* x = pv.Get("name")) p.name = x->WStr();
        if (auto* x = pv.Get("customName")) p.customName = x->Bool();
        if (auto* x = pv.Get("color")) p.color = (COLORREF)x->U64();
        if (auto* x = pv.Get("material")) p.material = MaterialFromId(Utf8(x->WStr()).c_str());
        if (auto* x = pv.Get("docked")) p.docked = x->Bool();
        if (auto* x = pv.Get("x")) p.pos.x = (LONG)x->Int();
        if (auto* x = pv.Get("y")) p.pos.y = (LONG)x->Int();
        if (auto* x = pv.Get("folded")) p.folded = x->Bool(true);
        if (auto* ms = pv.Get("members"))
            for (const auto& mv : ms->arr) {
                SavedMember m;
                if (auto* x = mv.Get("hwnd")) m.hwnd = x->U64();
                if (auto* x = mv.Get("pid")) m.pid = (DWORD)x->U64();
                if (auto* x = mv.Get("createTime")) m.createTime = x->U64();
                if (auto* x = mv.Get("class")) m.className = x->WStr();
                if (auto* x = mv.Get("exe")) m.exePath = x->WStr();
                if (auto* x = mv.Get("title")) m.title = x->WStr();
                if (auto* x = mv.Get("hidden")) m.hidden = x->Bool();
                if (auto* x = mv.Get("z")) m.stamp = x->U64();  // absent in files from v0.2.0 and earlier
                if (auto* x = mv.Get("showCmd")) m.wp.showCmd = (UINT)x->U64();
                if (auto* x = mv.Get("flags")) m.wp.flags = (UINT)x->U64();
                if (auto* x = mv.Get("normal"); x && x->arr.size() == 4)
                    m.wp.rcNormalPosition = {(LONG)x->arr[0].Int(), (LONG)x->arr[1].Int(), (LONG)x->arr[2].Int(),
                                             (LONG)x->arr[3].Int()};
                if (auto* x = mv.Get("maxPos"); x && x->arr.size() == 2)
                    m.wp.ptMaxPosition = {(LONG)x->arr[0].Int(), (LONG)x->arr[1].Int()};
                m.wp.ptMinPosition = {-1, -1};
                p.members.push_back(m);
            }
        if (auto* ms = pv.Get("missing"))
            for (const auto& mv : ms->arr) p.missing.push_back(ReadMissing(mv));
        if (auto* fs = pv.Get("files"))
            for (const auto& fv : fs->arr) p.files.push_back(fv.WStr());
        piles.push_back(std::move(p));
    }
    return true;
}

}  // namespace wp
