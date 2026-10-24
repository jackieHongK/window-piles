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

    printf("%d cases\n%s (%d failed)\n", g_cases, g_fail ? "MEMO FAILED" : "MEMO PASSED", g_fail);
    return g_fail;
}
