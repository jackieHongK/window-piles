// Unit test: the memo's formatting rules (headings, lists, Enter continuing a list, titles).
// Exit code = failures.
#include <cstdio>
#include <string>

#include "notes/memo_format.h"

using namespace wp;

namespace {

int g_fail = 0, g_cases = 0;

void Check(bool ok, const char* what) {
    ++g_cases;
    if (!ok) ++g_fail, printf("[FAIL] %s\n", what);
}

}  // namespace

int main() {
    // classification
    Check(ClassifyMemoLine(L"# 제목").kind == MemoLine::H1 && ClassifyMemoLine(L"# 제목").markerLen == 2, "# is H1");
    Check(ClassifyMemoLine(L"## 소제목").kind == MemoLine::H2, "## is H2");
    Check(ClassifyMemoLine(L"### 작은 제목").kind == MemoLine::H3 && ClassifyMemoLine(L"### a").markerLen == 4, "### is H3");
    Check(ClassifyMemoLine(L"#### 넷").kind == MemoLine::Body, "#### is body");
    Check(ClassifyMemoLine(L"#붙여씀").kind == MemoLine::Body, "# without a space is body");
    Check(ClassifyMemoLine(L"- 항목").kind == MemoLine::Bullet && ClassifyMemoLine(L"* 항목").kind == MemoLine::Bullet,
          "- and * are bullets");
    Check(ClassifyMemoLine(L"-붙여씀").kind == MemoLine::Body, "- without a space is body");
    MemoLineInfo n = ClassifyMemoLine(L"12. 열두째");
    Check(n.kind == MemoLine::Number && n.number == 12 && n.markerLen == 4, "12. is numbered");
    Check(ClassifyMemoLine(L"3.14 원주율").kind == MemoLine::Body, "3.14 is body");
    Check(ClassifyMemoLine(L"").kind == MemoLine::Body, "empty is body");

    // Enter
    Check(MemoOnEnter(L"- 사과").nextPrefix == L"- ", "bullet continues");
    Check(MemoOnEnter(L"* 사과").nextPrefix == L"* ", "star bullet continues with a star");
    Check(MemoOnEnter(L"3. 셋째").nextPrefix == L"4. ", "number counts up");
    Check(MemoOnEnter(L"- ").endList && MemoOnEnter(L"- ").nextPrefix.empty(), "empty bullet ends the list");
    Check(MemoOnEnter(L"7.  ").endList, "empty number ends the list");
    Check(!MemoOnEnter(L"# 제목").endList && MemoOnEnter(L"# 제목").nextPrefix.empty(), "heading: plain new line");
    Check(MemoOnEnter(L"그냥 글").nextPrefix.empty(), "body: plain new line");

    // nested lists (a tab per level), round bullets, typing that turns into symbols
    MemoLineInfo nb = ClassifyMemoLine(L"\t\t- 손자");
    Check(nb.kind == MemoLine::Bullet && nb.indent == 2 && nb.markerLen == 4, "tabs before a bullet nest it");
    Check(ClassifyMemoLine(L"\t3. 셋").number == 3 && ClassifyMemoLine(L"\t3. 셋").indent == 1, "nested number");
    Check(ClassifyMemoLine(L"\t☐ 할 일").kind == MemoLine::Task, "nested to-do");
    Check(ClassifyMemoLine(L"\t그냥").kind == MemoLine::Body && ClassifyMemoLine(L"\t그냥").markerLen == 0, "tab body");
    Check(ClassifyMemoLine(L"• 둥근").kind == MemoLine::Bullet, "round bullet is a bullet");
    Check(MemoOnEnter(L"\t- 사과").nextPrefix == L"\t- ", "nested bullet continues at its level");
    Check(MemoOnEnter(L"\t2. 둘").nextPrefix == L"\t3. ", "nested number counts up");
    Check(MemoOnEnter(L"\t- ").outdent && !MemoOnEnter(L"\t- ").endList, "empty nested item goes a level out");
    Check(MemoBulletsShown(L"- a\r\n\t* b\r\n1. c\r\n-x", true) == L"• a\r\n\t• b\r\n1. c\r\n-x", "bullets shown round");
    Check(MemoBulletsShown(L"• a\r\t• b\r* c", false) == L"- a\r\t- b\r* c", "round bullets saved as dashes");
    std::wstring w;
    Check(MemoPrettify(L"a ->", &w) == 2 && w == L"→", "-> is an arrow");
    Check(MemoPrettify(L"x ←>", &w) == 2 && w == L"↔", "<-> is a two-way arrow");
    Check(MemoPrettify(L"..", &w) == 0 && MemoPrettify(L"...", &w) == 3 && w == L"…", "... is an ellipsis");
    Check(MemoPrettify(L"a >=", &w) == 2 && w == L"≥", ">= is greater-or-equal");
    Check(MemoPrettify(L"- ", &w) == 0 && MemoPrettify(L">", &w) == 0, "nothing else changes");

    // titles
    Check(MemoTitle(L"\r\n# 장보기\r- 우유\r- 계란") == L"장보기", "title skips blanks and the marker");
    Check(MemoExcerpt(L"# 장보기\r- 우유\r- 계란") == L"우유", "excerpt is the next line");
    Check(MemoTitle(L"") == L"" && MemoExcerpt(L"한 줄") == L"", "empty title / no excerpt");
    Check(MemoTitle(std::wstring(60, L'가')).size() == 40, "long titles are clipped to 40");

    // file names
    Check(MemoFileName(L"2026-10-25", L"장보기") == L"2026-10-25 장보기.txt", "date + title");
    Check(MemoFileName(L"2026-10-25", L"a/b:c*d?\"e<f>g|h\\i") == L"2026-10-25 a b c d e f g h i.txt", "characters Windows refuses");
    Check(MemoFileName(L"2026-10-25", L"회의 🎉 준비!!") == L"2026-10-25 회의 준비.txt", "symbols and emoji go, spaces collapse");
    Check(MemoFileName(L"2026-10-25", L"끝에 점...  ") == L"2026-10-25 끝에 점.txt", "no trailing dots or spaces");
    Check(MemoFileName(L"2026-10-25", L"") == L"2026-10-25 메모.txt" && MemoFileName(L"2026-10-25", L"★☆") == L"2026-10-25 메모.txt",
          "nothing left: 메모");
    Check(MemoFileName(L"2026-10-25", std::wstring(80, L'가')).size() == 10 + 1 + 50 + 4, "titles are cut to 50");
    Check(MemoFileName(L"2026-10-25", L"Café (v2) [초안]") == L"2026-10-25 Café (v2) [초안].txt", "accents and brackets stay");

    printf("%d cases\n%s (%d failed)\n", g_cases, g_fail ? "MEMO FAILED" : "MEMO PASSED", g_fail);
    return g_fail;
}
