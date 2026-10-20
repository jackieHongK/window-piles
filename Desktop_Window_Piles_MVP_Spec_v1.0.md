# Desktop Window Piles — MVP 제품 및 기술 명세서

- 문서 버전: 1.0
- 작성일: 2026-10-20
- 상태: 개발 착수용 초안 (DWM/WGC 호환성 PoC 필요)
- 대상: Windows 11 x64 우선, Windows 10 22H2 x64는 호환성 검증 후 지원
- 구현: C++20, Win32, DWM, Windows Graphics Capture(WGC), Direct3D 11/Direct2D(필요한 경우)

## 0. 개발 원칙과 핵심 의사결정

**한 줄 정의:** 현재 실행 중인 여러 Windows 앱 창을 책상 위 서류처럼 묶어 작은 창 더미(Pile)로 접고, 바탕화면 위에서 더미째 옮기고, 원래의 실제 창들로 다시 펼쳐 쓰는 초경량 유틸리티.

1. 별도의 작업공간 관리자 메인 GUI/브라우저/웹뷰를 만들지 않는다. 시스템 트레이와 바탕화면 위 작은 Pile 표면만 가진다.
2. 기존 창의 부모(parent)를 바꾸거나(`SetParent` 금지), 앱 내부에 DLL을 주입하거나, 앱 실행·문서 상태를 제어하지 않는다.
3. 일반 앱은 기존 HWND·프로세스·세션을 유지한다. 특히 **원본 창의 크기를 줄이지 않는다.**
4. Pile에 표시되는 작은 창들은 실제 앱 UI가 아니라 DWM 썸네일 또는 캡처 스냅샷을 그린 *시각적 대리 표현*이다.
5. **MVP의 기능 보장 경로는 `WGC 사전 스냅샷 → 원본 숨김 → 미리보기 표시`**로 정의한다. DWM 실시간 썸네일은 PoC를 통과하는 환경에만 최적화 경로로 사용한다.
6. 사용자 데이터(브라우저 탭·열린 파일·메시지 등)를 읽거나 저장하지 않는다. 이미지는 메모리에만 두며 디스크 기록을 기본 금지한다.
7. 장애나 사용자 종료 시 *가능한 모든 원본 창 복구*가 최우선이다. 창을 숨기기 전 복구 저널을 반드시 기록한다.
8. 지원하지 않는 보호·승격·특수 창은 실패를 숨기지 말고 안전하게 그룹 대상에서 제외한다.

> **사실/검증 구분:** DWM API로 HWND 축소판을 자체 최상위 창에 렌더링할 수 있는 것은 공식 지원 사항이다. 하지만 숨긴 원본 HWND의 실시간 갱신이나 가시성은 보장사항으로 간주하지 않는다. 이 부분은 반드시 테스트로 판정한다.

## 1. 제품 목표 및 범위

### 1.1 해결하려는 문제

여러 프로젝트의 브라우저 창, 파일 탐색기, Excel, 메모장, Outlook, Windows Terminal 등 서로 다른 앱 창이 한 데 섞인다. 사용자는 작업을 종료하거나 최소화하지 않고 현재 프로젝트의 실제 창들을 접어서 시각적으로 한 묶음으로 남겨두고, 화면에서 이동·정렬한 뒤 다시 펼치고 싶다. 여러 업무를 동시에 소화해야하는데 업무마다 필요한 윈도우창들이 혼재되어있고, 그걸 작업필요할때마다 매번 열거나 다시 재정렬하는건 전환비용이 너무 높아서 일에 몰입을 방해한다. 여러 창을 다 최소화해놓으면, 컨텍스트가 흐려져서 어떤 작업을 해야하는지를 까먹게되고 화면에 한 묶음으로 계속 표시가 되어야 해야할일을 놓치지않고 처리할 수 있다. 그리고 작업이 끝나면 불필요한 창을 제거해서 업무 효율을 높이려고한다. 폴더 같은 경우에도 프로젝트별로 열어놔야할 파일들이 다양하기 때문에, 하나의 앱이라도 여러 윈도우로 열어두면 각각을 독립적인 창으로 보고 묶을 수 있어야한다. 

### 1.2 핵심 사용자 시나리오

1. 사용자 A가 Chrome 창 4개, 탐색기 2개, Excel 1개를 열어둔다.
2. `Win+G`로 정리 모드에 들어가 개별 창을 다른 창/더미 위에 드롭하여 그룹을 만든다.
3. `Win+Shift+G`로 선택된 그룹을 접는다. 그 결과 원본의 실제 크기에 상관없이 약 240×150px의 창 더미가 화면에 남는다.
4. 더미를 마우스로 잡아 화면 오른쪽 위로 이동한다. 원본 창은 숨겨진 상태지만 실행 중이다.
5. 다른 프로젝트를 작업하다 더미를 더블클릭한다. 모든 원본 창이 저장된 위치·크기·표시 상태로 펼쳐진다.
6. 필요하면 더미에서 개별 창을 꺼내거나 다른 더미에 추가한다.
7. 단축키는 사용자가 임의로 지정할 수 있다.(트레이에 최소화된 앱을 실행하여 설정)

### 1.3 MVP 범위

- 실행 중인 표준 데스크톱 최상위 창 수집 및 필터링
- 드래그 기반 그룹 생성/병합/추가/분리 (정리 모드에서만)
- `Pile` 접기/펼치기와 일반 창 복원
- 겹쳐진 축소 미리보기 표시 및 Pile 드래그·배치
- *펼친* 그룹도 지정된 보조 키를 누른 상태로 함께 이동 (창 크기는 그대로)
- 동일 앱의 복수 HWND 독립 그룹핑 (예: Chrome 10개)
- 단축키 + 트레이 메뉴의 긴급 전체 복원
- 장애 복구 저널과 별도 감시/복구 프로세스
- 기본 단일·다중 모니터 및 DPI 지원
- 스냅샷 폴백과 DWM 실시간 썸네일 선택적 검증

### 1.4 제외 범위

- 앱 내부의 브라우저 탭을 꺼내거나 탭 단위 그룹핑
- 다른 가상 데스크톱에 있는 창의 자동 수집·이동
- Windows 재부팅 뒤 파일/탭/프로세스 자체를 재실행하는 세션 복원
- Pile 미리보기 안에서 원본 앱을 직접 클릭·키보드 조작
- 게임 독점 전체화면, 보안/UAC 화면, 보호 콘텐츠 우회
- AI 기반 자동 분류, 동기화, 클라우드 계정, 플러그인 시스템

## 2. UX 및 상호작용 명세

### 2.1 일반 모드

- 기본 상태에서는 기존 Windows 창 이동과 클릭 동작에 관여하지 않는다.
- Pile은 일반 앱 위에 놓이는 별도 *초소형 Win32 최상위 표시 창*이며, 전체화면 GUI나 컨테이너가 아니다.
- Pile에는 원본 창의 축소 미리보기 여러 장이 조금씩 어긋나서 보인다. 이름표·창 개수 정도만 표시한다.
- Pile은 더블클릭 시 펼친다. 드래그하면 더미 자체만 옮긴다.
- 기본 Pile은 항상 위(topmost)가 아니다. 다른 창에 가려진 Pile을 다시 표시하는 전역 단축키를 제공한다. 일시적으로 앞으로 가져오되 계속 화면을 가리지 않는다.
- 마우스로 Pile 하나를 선택했을 때만 선택 윤곽이 나타난다.

### 2.2 정리 모드

- `Win+G`: 정리 모드 on/off (단축키 충돌 시 사용자 설정 가능).
- 정리 모드에서는 드래그 중 대상 창/대상 Pile 주위에 얇은 드롭 경계를 보여준다.
- 창을 다른 창 위에 드롭하면 새 그룹을, 기존 Pile 위에 드롭하면 그룹에 추가한다.
- 그룹 A를 그룹 B에 드롭하면 그룹을 병합한다.
- Pile에서 하나의 썸네일을 끌어 바깥에 드롭하면 해당 창을 복구하면서 그룹에서 분리한다. 구현 위험이 발견되면 1.0에서는 선택 후 `분리` 단축키로 대체 가능하도록 설계한다.
- 일반적인 창 이동 중 우연히 그룹핑되지 않도록 **정리 모드 외의 드롭은 전부 무시**한다.
- 정리 모드의 대상 탐색은 드래그 중 전면 창에 가려진 *아래쪽 Z-order 창*을 고려해야 하며 단순 `WindowFromPoint()`만 사용하지 않는다.
- 드래그 시작·종료는 `SetWinEventHook(EVENT_SYSTEM_MOVESIZESTART/END)`와 이벤트 시점의 커서/키 상태를 통해 관찰한다. 이벤트만으로 정확하지 않은 환경에서는 정리 모드에서만 `WH_MOUSE_LL`을 선택적으로 사용한다. 앱 내부 주입은 금지한다.
- 드래그를 취소(`Esc`)하면 창 원래 위치와 그룹 상태를 유지한다.

### 2.3 단축키 (초안)

| 동작 | 기본값 | 설명 |
|---|---|---|
| 정리 모드 | `Win+G` | 그룹 드롭 동작 활성/비활성 |
| 선택 그룹 접기/펼치기 | `Win+Shift+P` | 현재 전면 창이 속한 그룹 또는 선택된 Pile |
| 모든 Pile 앞으로 가져오기 | `Win+Space` | 순간적으로 모든 Pile 표시, 정리 진입 지원 |
| 펼친 그룹 전체 이동 | `Ctrl+Alt`+대표 창 드래그 | 그룹 상대 위치 보존, 원본 크기 불변 |
| 안전한 전체 복원 | 트레이 메뉴 / 사용자 정의 단축키 | 숨긴 모든 원본 창 표시 |

> 충돌·접근성 문제를 고려해 단축키는 설정 가능하게 한다. 글로벌 단축키 등록 실패 시 명시적으로 알린다.

### 2.4 Pile 시각 규칙

- 초기 미리보기 크기: 240×135px(내용 16:9인 경우), 비율에 맞춰 letterbox; 더미 전체 상한 약 280×190px.
- 미리보기 장수: 겹친 3장만 노출하되 창 수 배지(예: `+7`)로 나머지를 표시. 모든 창의 메타정보는 유지한다.
- 겹침 간격: 12~16px, 2D. 이미지 왜곡을 위한 과한 회전/3D 애니메이션 사용 금지.
- Pile은 사용자 모니터의 작업 영역(work area) 내부에 제한; DPI/해상도 변경 후 자동 재배치.
- 각 창의 제목/아이콘은 가능하면 이용하되 민감한 창 내용이 드러나지 않게 미리보기 숨기기 옵션을 후속 단계에서 고려한다.
- 기본 표시창은 `WS_EX_TOOLWINDOW`(불필요한 Alt+Tab 항목 방지) 및 적절한 포커스 정책을 검토한다. `WS_EX_NOACTIVATE`와 드래그·키보드 조작의 상호작용은 PoC에서 테스트한다.

## 3. 상태 모델 및 데이터 구조

### 3.1 그룹 상태

`EXPANDED -> FOLDING -> FOLDED_SNAPSHOT` (기본) 또는 `FOLDED_LIVE` (검증된 시스템) `-> EXPANDING -> EXPANDED`.

- 오류 시 `RECOVERING`으로 진입해 숨겨진 창 복구를 우선한다.
- 중간 단계 재진입, 중복 hotkey, 연속 클릭을 막기 위한 idempotent 전이 처리.
- 하나의 HWND는 동일 시점에 오직 한 그룹에만 속한다.

### 3.2 메모리 데이터 모델 (예시)

```cpp
struct WindowIdentity {
    HWND hwnd;                 // 현재 세션용, 영속 ID로 사용 금지
    DWORD processId;
    DWORD threadId;
    uint64_t processStartTime; // PID/HWND 재사용 보호
    std::wstring className;
};

struct WindowState {
    WindowIdentity id;
    WINDOWPLACEMENT placement; // showCmd, 복원 좌표 등
    RECT frameBoundsPx;        // 실제 화면 좌표
    bool wasVisible;
    bool wasTopmost;
    bool wasMinimized;
    HMONITOR monitor;
    int originalZOrderIndex;   // 동일 데스크톱 내 best-effort
};

struct Pile {
    std::string uuid;
    std::wstring name;
    std::vector<WindowState> members;
    POINT pilePositionPx;
    POINT expandedOriginPx;
    enum class State { Expanded, Folding, FoldedSnapshot,
                       FoldedLive, Expanding, Recovering } state;
};
```

- `WINDOWPLACEMENT.rcNormalPosition`은 상황에 따라 작업영역 좌표를 사용하므로 `GetWindowRect`/DWM 확장 프레임 좌표와 단순 혼용 금지.
- HWND/PID는 재사용될 수 있다. 복구 전에 `IsWindow`, PID, 프로세스 생성 시간 및 가능한 클래스명을 재검증한다.
- 프로세스 재시작 후 원래 HWND 식별이 불가능하다면 잘못된 앱에 복구 명령을 내리지 않는다.

## 4. Windows API 및 구현 구조

| 모듈 | 권장 API/기술 | 역할/주의사항 |
|---|---|---|
| WindowDiscovery | `EnumWindows`, `IsWindowVisible`, `GetAncestor`, `GetWindowLongPtr`, `DwmGetWindowAttribute` | 실제 사용자가 보는 최상위 창 추적, cloaked/tool/owned 제외 정책 |
| EventTracker | `SetWinEventHook(WINEVENT_OUTOFCONTEXT)` | 생성/삭제/위치/활성화/이동 시작·종료 수신. 메시지 루프 필수 |
| Layout | `GetWindowPlacement`, `SetWindowPlacement`, `GetWindowRect`, `SetWindowPos`, `DeferWindowPos` | 위치·크기·일부 Z-order 복원, 그룹 일괄 이동 |
| Preview (live 실험) | `DwmRegisterThumbnail`, `DwmUpdateThumbnailProperties`, `DwmUnregisterThumbnail` | 자체 프로세스가 소유하는 보이는 최상위 HWND에 연결 |
| Preview (MVP 보장) | WGC `IGraphicsCaptureItemInterop::CreateForWindow` + D3D11 | 원본을 숨기기 *전에* 최신 프레임 수신, 축소 후 메모리에 보관 |
| Pile Surface | Win32 window + Direct2D / D3D11 필요 시 | 작은 표시·입력 표면. 앱 자체의 큰 컨테이너 아님 |
| Runtime | `RegisterHotKey`, 트레이 아이콘(`Shell_NotifyIcon`) | 메인 UI 없이 조작 |
| Safety | 원자적 journal 파일, 별도 watchdog/helper | 비정상 종료 감지 및 숨긴 창 복원 |

### 4.1 원본 창 식별 및 필터링

- `GetAncestor(hwnd, GA_ROOT)`로 최상위 기준 정규화.
- 숨김 창, Shell 자체 창, 작업표시줄/시작 메뉴, desktop host, Pile 자체 HWND는 제외.
- owner가 있는 modal/dialog, 이미 cloaked된 창은 기본 제외하고 해당 소유 앱 창을 안전하게 처리.
- 관리자 권한 창과 UIPI로 제어가 불가능한 창은 그룹 추가 자체를 거부한다. 유틸리티 전체를 관리자 권한으로 실행하도록 요구하지 않는다.
- UWP, Chromium 계열, Terminal, Explorer 등의 대표 앱에서 동작을 실제 검증한다. 앱 종류 무관을 목표로 하지만 **모든 창 100% 호환을 약속하지 않는다.**

### 4.2 DWM 썸네일 기술 검증 (P0)

공식 API 조건: 대상(destination)은 호출 프로세스가 소유하는 최상위 창 또는 데스크톱 창이어야 한다. 원본 창의 크기 변경 없이 `rcDestination` 좌표로 축소판을 표시할 수 있다.

실험 매트릭스:

1. 원본 표시 / 다른 창에 가림 / 최소화 / `SW_HIDE` / 화면 밖 이동.
2. 원본 프레임 변경 시 실시간 갱신 여부.
3. 다른 앱별(Chrome, Excel, Windows Terminal, Explorer, Outlook, 메모장) 차이.
4. 대상 Pile HWND 일반/레이어드/비활성 창 상태별 가시성.
5. 복수 `HTHUMBNAIL`의 순서·겹침·클리핑 / 대상 창 이동·크기 변경.
6. 모니터별 DPI 100/150/200%, 스케일/해상도 변경, 원본 종료.
7. 리소스 핸들 누수, GPU 사용량, 타사 앱 화면 깜빡임.

**판정:** 숨긴 원본에 대한 DWM 동작이 불안정하면 `FOLDED_LIVE`를 일반 기능으로 제공하지 않는다. 그때도 DWM 실험 코드는 기능 플래그 뒤에 두고 기본 경로는 정적 스냅샷이다. DWM 썸네일 API는 비트맵 프레임을 직접 반환하는 API가 아니므로 성공 HRESULT만으로 화면이 실제로 보이는지 검증하지 않는다(시각적 QA 필요).

### 4.3 WGC 스냅샷 기본 경로

1. 원본 창이 보이는 동안 `GraphicsCaptureItem`을 HWND로 생성.
2. 제한된 시간 내 첫 유효 프레임을 수신. 미수신·보호 콘텐츠면 앱 아이콘/타이틀 플레이트 사용.
3. GPU에서 프레임을 축소(예: 240px 너비), 필요 시 최소 데이터만 CPU 메모리로 전송.
4. 캡처 세션과 불필요한 GPU 텍스처 즉시 해제(상시 녹화 금지).
5. 복구 저널을 flush한 뒤에만 원본 창을 숨김.
6. 스냅샷은 프로세스 메모리 전용 캐시; 기록·원격 전송 금지.

- WGC의 CreateForWindow 지원 최소 버전은 Windows 10 1903이다. Windows 11 우선 지원으로 구현 범위를 좁힌다.
- WGC 촬영 테두리·권한·보호 화면 정책은 운영체제 동작에 맡기며 우회하지 않는다.
- 정적 스냅샷이므로 접힌 뒤 변경되는 메신저 메시지나 브라우저 화면은 자동 갱신되지 않는다. 이는 MVP의 **의도적인 제약**이다.
- 콘텐츠가 보이지 않으면 빈 타일과 앱 아이콘을 사용해 접기 자체는 정상 진행할 수 있도록 한다.

### 4.4 원본 숨김·복원

**접기 트랜잭션**

1. 그룹 내 HWND 재검증 및 필터링.
2. 모든 창의 표시/배치/상대 좌표/모니터/최소화·최대화 상태를 저장.
3. 스냅샷을 확보하거나 타이틀/아이콘 폴백 준비.
4. **숨길 예정인 HWND + 원래 상태를 복구 저널에 원자적 저장 및 디스크 flush.**
5. Pile HWND 생성 및 미리보기 표시 준비(실패하면 숨기지 않음).
6. 각 원본에 `ShowWindow(..., SW_HIDE)` 수행. 앱이 다시 표시하거나 실패하면 전체 롤백.
7. 그룹 상태를 `FOLDED_SNAPSHOT`으로 커밋.

**펼치기 트랜잭션**

1. 살아있는 HWND인지, 같은 프로세스인지 재검증.
2. 원본 표시 상태를 복구하고 `SetWindowPlacement`/`SetWindowPos`를 적용.
3. 더미가 이동했다면 기준 좌표 delta를 원본 창의 *일반 표시 좌표*에 적용. 최대화 창은 모니터 기준 최대화 의미를 최대한 보존한다.
4. 다중 모니터의 보이는 작업 영역으로 좌표 보정 (완전히 화면 밖으로 밀어내지 않음).
5. 표시 상태·Z-order를 best-effort 복구하고 Pile HWND/캡처 자원 해제.
6. 성공한 창을 저널에서 제거하고 원자적 커밋.

`SetWindowPlacement`에서 show-state가 변경될 수 있으므로 일괄 복원 시 활성화/포커스 변경을 최소화한다. 윈도우 보안 정책으로 전면 활성화가 거부될 수 있다. "정확한 Z-order 및 포커스 100% 복원"은 보장하지 않는다.

### 4.5 펼친 그룹 이동

- modifier+대표 창 드래그 시 시작 좌표, 그룹 모든 창 위치를 캐시한다.
- 이동 delta를 `DeferWindowPos` 일괄 좌표 변경으로 전파한다. 재귀 WinEvent를 억제하기 위한 manager-origin 플래그와 generation id 사용.
- 최대화된 창은 펼친 그룹 전체 이동에 부적합할 수 있으므로 첫 버전에서는 최대화 창 포함 그룹 이동을 제한하거나 정상 상태로 전환 여부를 사용자에게 명시한다. **창 크기 강제 변경 금지** 원칙을 지킨다.
- 화면 밖으로 완전히 벗어나는 이동을 제한한다.

## 5. 안전성·개인정보·장애 복구

### 5.1 복구 저널

- 경로 예시: `%LOCALAPPDATA%/DesktopWindowPiles/recovery.json` (민감한 스냅샷 저장 안 함).
- 구조: 그룹 UUID, 각 HWND/PID/프로세스 생성 시간, 원래 상태 및 저장 시각, 적용중 여부.
- 원자적 교체: 임시 파일 기록→flush→rename/replace. 전원 장애까지 모든 상황에서 무결성 100% 보장은 불가능하므로 체크섬/스키마 버전 검사.
- 창을 숨기는 행위보다 저널 확정이 항상 먼저 발생해야 한다.

### 5.2 Crash recovery

- 유틸리티는 작은 감시 프로세스를 별도로 실행한다. 매니저 PID와 heartbeat/프로세스 핸들을 추적.
- 매니저의 비정상 종료 감지 시 저널을 읽고 **동일한 창으로 검증되는 항목**만 `SW_SHOWNA`/필요한 배치 API로 복구한다.
- 관리자 권한 창·보안 데스크톱처럼 복구할 수 없는 항목은 로그에 기록하고 사용자에게 알린다.
- 사용자가 정상 종료를 선택하면 먼저 모든 숨김 창을 복구한 뒤 Pile과 후크를 제거한다.
- 감시 프로세스까지 함께 종료되면 복구가 즉시 보장되지 않으므로 다음 프로그램 시작 시 저널 기반 복구를 제공한다.

### 5.3 개인정보

- 창 콘텐츠는 축소된 미리보기로 사용자 본인 화면에만 표시한다. 네트워크 사용 없음.
- 스냅샷을 로그/크래시 덤프/디스크에 자동 보관하지 않는다. 메모리 포함 덤프 수집 시 스냅샷 유출 위험을 줄이기 위해 기본 비활성화한다.
- `WDA_EXCLUDEFROMCAPTURE` 등 운영체제·앱 콘텐츠 보호 정책을 우회하지 않는다.
- 비활성/잠금 화면에서 노출되지 않게 Windows 세션 잠금/전환 이벤트를 처리한다.

## 6. 성능 및 품질 목표 (초기 가설, 실측 후 조정)

- 일반 모드 CPU idle: **평균 0.5% 이하** 목표 (30개 창, 비교적 안정된 데스크톱, 참조 시스템에서 측정).
- 매니저 + watchdog 합계 프로세스 working set: **60MB 이하** 목표, 스냅샷/GPU 메모리 별도 계측.
- 프리뷰 캐시: 너비 240px·BGRA8 기준 16:9 이미지 1개 약 0.124MiB; 30개 약 3.71MiB의 *픽셀 데이터* (추가 D3D/윈도우 리소스 제외).
- 캐시 상한: 총 64MB(실제 리소스 포함 계측 후 조정), 가장 오래된 비활성 썸네일부터 삭제하되 Pile 아이콘은 유지.
- 접기/펼치기 지연: 캐시 준비된 상황에서 입력 후 300ms 이내(P95) 목표. WGC 첫 프레임 대기와 앱 반응 시간은 별도 계측.
- 더미 드래그: 상용 사무용 PC에서 체감 60fps에 근접하도록 목표; 이벤트 타이밍 지터 및 DPI를 별도 프로파일링.
- 6개 그룹·총 30개 창의 반복 접기/펼치기 500회에서 핸들·GDI 객체·프로세스 메모리가 지속 증가하지 않을 것.

> 목표치는 공식 성능 보증이 아니라 개발 수락 기준 초안이다. GPU 드라이버·앱 자체 렌더링·Windows 버전에 따라 실제 수치는 달라진다.

## 7. 기능별 인수 기준 (Acceptance Criteria)

| ID | 필수 시나리오 | 합격 기준 |
|---|---|---|
| AC-01 | 서로 다른 앱 창 5개 묶기 | 해당 5개만 포함, 다른 창 미변경 |
| AC-02 | Chrome 창 10개를 2개 그룹으로 분할 | HWND별 개별 그룹 소속이 정확함 |
| AC-03 | 최소 크기 고정된 앱 창 접기 | 원본 크기·앱 내부 상태 불변, 미리보기는 축소 |
| AC-04 | 접힌 그룹 드래그 | 화면 배치만 바뀌고 원본은 실행 상태 유지 |
| AC-05 | 더미 더블클릭 펼치기 | 모든 지원 창이 화면 안에 보이게 복원, 원래 배치 보존 |
| AC-06 | 원본 창 하나 강제 종료 | 나머지 그룹은 정상 동작, 유령 HWND 없음 |
| AC-07 | 그룹 숨김 직후 manager 강제 종료 | watchdog이 생존·검증 가능한 HWND 복구 시도, 성공 여부 기록 |
| AC-08 | WGC/DWM 실패 | Pile 아이콘 폴백, 앱 전체 정지·블랙 화면 없음 |
| AC-09 | 100%↔150% DPI 및 2모니터 이동 | 더미/창이 모두 접근 가능한 화면에 남음 |
| AC-10 | 일반 모드에서 평소 창 이동 | OS 기본 드래그와 클릭에 부작용 없음 |
| AC-11 | 표준 권한으로 관리자 앱 조작 시도 | 안전하게 제외, 권한 상승/앱 강제 변경 없음 |
| AC-12 | 로그인 세션 잠금·해제 | 화면 노출 및 입력 혼선 없이 정상 복귀 |
| AC-13 | 정상 종료 | 숨긴 원본 창이 남지 않고 후크·DWM 핸들 정리 |
| AC-14 | 정리 모드에서 그룹 드롭 취소 | 원래 창 위치·그룹 소속이 보존됨 |

## 8. 테스트 매트릭스

**앱:** Chrome/Edge(동시 다중 창), Explorer, Excel, 메모장, Outlook(New/Classic 구분), Windows Terminal, VS Code, 크기 고정 유틸리티.

**창 상태:** 일반/최대화/최소화/숨김 전 상태, 모달 대화상자 포함, topmost, 다른 창에 가림, Snap 상태.

**환경:** Windows 11 23H2 이후 지원 버전, Windows 10 22H2(호환성 목표), 단일·듀얼 모니터, 100/150/200% DPI, 듀얼 모니터 연결 해제, RDP 연결/해제, 잠금/해제, 고대비·다크 테마.

**장애:** 프로세스 종료/재시작, 관리자 권한 차이, WGC 프레임 미수신, DWM 장치 재생성, 캡처 방지 창, 시스템 트레이 재시작, 강제 종료, 자원 누수.

## 9. 개발 순서 및 게이트

### P0 — 기술 타당성 PoC (선행 필수)

- PoC-01: 크롬/엑셀/터미널 HWND를 대상 네이티브 Win32 창에 DWM 썸네일로 등록, 여러 개 겹쳐 표시.
- PoC-02: 원본을 `SW_HIDE` 했을 때 썸네일 상태를 정성/정량 측정. 이 결과를 MVP 기본값의 근거로 기록.
- PoC-03: WGC 첫 유효 프레임 취득→240px 축소→원본 숨김→자체 창에 정지 이미지 표시→복원.
- PoC-04: manager 강제 종료 뒤 watchdog 원본 복구.
- **통과 조건:** PoC-03과 04가 안정적으로 작동하면 MVP 개발 진행. PoC-02가 실패해도 프로젝트는 진행한다.

### P1 — MVP 핵심

- HWND discovery/filtering, 세션 단위 GroupRegistry
- 정리 모드와 드래그 타겟 탐색
- Pile snapshot surface, 드래그 이동, 더블클릭 펼치기
- 접기/펼치기 트랜잭션, 그룹 해제 및 병합

### P2 — 안정화

- 멀티모니터/DPI 및 앱별 호환성
- 실행 중 창 종료·창 새로 생성·Crash 복구
- 트레이 메뉴/단축키 충돌 처리, 간단한 설정
- CPU/메모리/핸들 프로파일링 및 테스트 자동화

### P3 — 선택 기능 (MVP 이후)

- 숨겨진 창에서 실시간 표시가 신뢰성 있게 가능한 장치에 한해 DWM live 최적화 활성화
- 그룹 자동 이름 제안 / 고정 위치 / 시각적 스냅 영역
- 새로운 실행 창을 기존 그룹에 자동 포함(선택적)
- 그룹 레이아웃의 재시작 후 재구성(앱별 특수 플러그인 필요 가능)

## 10. 프로젝트 구조 제안

```text
DesktopWindowPiles/
  CMakeLists.txt
  src/
    app/                 # 실행 엔트리, 메시지 루프, 트레이
    windows/             # EnumWindows, eligibility, WinEventHook
    groups/              # 그룹 모델, 상태 머신, 명령
    interaction/         # 정리 모드, 드래그 판정, 단축키
    layout/              # 원래 배치, 일괄 이동, 복원
    preview/             # DWM 시험 구현, WGC snapshot, 캐시
    surface/             # Win32 Pile 창, GDI/Direct2D UI
    recovery/            # 저널 쓰기/읽기, 복구 로직
    common/              # RAII, 오류 및 DPI/좌표 유틸
  watchdog/              # 별도 경량 복구 프로세스
  tests/
    unit/                # 상태 머신/좌표 변환/ID 검증
    integration/         # 실제 앱 조작/복구 테스트
  docs/
    compatibility.md
    poc-results.md
    known-limitations.md
```

**금지/권장:** Electron, Chromium Embedded, 앱 임베딩, 프로세스 DLL injection, 광범위한 전역 mouse hook 상시는 금지. Win32 RAII, 이벤트 기반, 좁은 시점에만 필요한 캡처, 개별 단위 롤백을 권장.

## 11. 외부 공식 기술 문서

1. [DwmRegisterThumbnail — Microsoft Learn](https://learn.microsoft.com/en-us/windows/win32/api/dwmapi/nf-dwmapi-dwmregisterthumbnail)
2. [DwmUpdateThumbnailProperties — Microsoft Learn](https://learn.microsoft.com/en-us/windows/win32/api/dwmapi/nf-dwmapi-dwmupdatethumbnailproperties)
3. [IGraphicsCaptureItemInterop::CreateForWindow — Microsoft Learn](https://learn.microsoft.com/en-us/windows/win32/api/windows.graphics.capture.interop/nf-windows-graphics-capture-interop-igraphicscaptureiteminterop-createforwindow)
4. [SetWinEventHook — Microsoft Learn](https://learn.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-setwineventhook)
5. [GetWindowPlacement — Microsoft Learn](https://learn.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-getwindowplacement)
6. [SetWindowPos — Microsoft Learn](https://learn.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-setwindowpos)
7. [ShowWindow — Microsoft Learn](https://learn.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-showwindow)
8. [SetWindowDisplayAffinity — Microsoft Learn](https://learn.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-setwindowdisplayaffinity)

## 12. 개발 구현 시 우선순위 최종 요약

1. **최우선:** 안전한 원본 숨김 및 원상 복구. 절대로 사용자 창을 잃어버리게 만들지 않는다.
2. **두 번째:** WGC로 얻은 정적 축소 이미지 기반의 Pile 조작. 모든 앱의 resize 가능 여부와 무관.
3. **세 번째:** 정리 모드에서 직관적인 드래그 그룹화. 기존 OS 동작 침범 최소화.
4. **네 번째:** 낮은 자원 사용과 여러 앱·DPI 환경에서의 안정성.
5. **실시간 DWM 미리보기:** 선행 PoC로 검증하지만 성공을 MVP의 필수 조건으로 만들지 않는다.

**출시 차단 조건:** 원본 창의 복구 경로가 신뢰할 수 없거나, 일반 모드에서 타사 앱 조작에 부작용이 있거나, 캡처 보호 정책을 침해하는 경우.
