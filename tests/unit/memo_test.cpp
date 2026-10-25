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
