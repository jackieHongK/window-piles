# Window Piles (창 더미)

실행 중인 여러 앱 창을 '창 더미'로 접어 화면에 남겨 두고, 다시 원래 창들로 펼쳐 쓰는 초경량 Windows 유틸리티.

- 명세: [Desktop_Window_Piles_MVP_Spec_v1.0.md](Desktop_Window_Piles_MVP_Spec_v1.0.md)
- 개발 계획: [Development_Plan_v1.md](Development_Plan_v1.md)
- P0 PoC 결과: [docs/poc-results.md](docs/poc-results.md)

## 사용법 (최소 사용 가능 버전)

**실행:** `build\bin\Release\WindowPiles.exe`를 더블클릭하면 트레이(화면 오른쪽 아래)에 아이콘이 생기고 안내 알림이 뜹니다.
Windows 11은 새 트레이 아이콘을 `^` 안에 숨기므로, 아이콘이 안 보이면 거기서 찾아 작업 표시줄로 끌어 두세요.

| 하고 싶은 것 | 방법 |
|---|---|
| 창을 더미에 넣기 | 창 제목줄을 잡고 끌면 **화면 위쪽 가운데**에 `＋ 새 더미`가 나타납니다. 거기에 놓으면 창이 숨겨지고 화면 오른쪽 위에 더미가 생깁니다 |
| 같은 더미에 더 넣기 | 다른 창을 끌어서 **더미 위에** 놓습니다 (더미가 파란 테두리로 빛남) |
| 펼치기 / 접기 | 더미를 **한 번 클릭**. 펼치면 창들이 끌기 전 원래 자리·크기로 돌아오고, 더미는 작은 칩으로 바뀝니다. 칩을 누르면 다시 접힙니다 |
| 더미 옮기기 | 더미를 끌어서 원하는 곳에 놓습니다 (화면 가장자리에 붙음) |
| 더미 풀기·모두 꺼내기·종료 | 더미 **우클릭** 메뉴, 또는 트레이 아이콘 우클릭 메뉴 |
| 끄기 | 트레이/더미 메뉴의 `종료` — 숨긴 창을 모두 꺼낸 뒤 끝납니다 |

**안전장치**
- 프로그램이 비정상 종료돼도 함께 뜬 감시 프로세스가 숨긴 창을 즉시(약 0.05초) 되돌립니다.
- 둘 다 죽었다면 다음 실행 때 되돌립니다.
- Windows를 끄거나 로그아웃할 때는 숨긴 창을 먼저 모두 꺼내므로, 각 앱의 "저장할까요?" 창이 정상적으로 보입니다.
- 창을 강제로 닫거나 앱을 종료하지 않습니다.

**넣을 수 없는 창:** 관리자 권한으로 실행된 앱, 대화상자, 작업 표시줄·바탕화면 같은 시스템 창. 이런 창을 끌면 바에 "이 창은 더미에 넣을 수 없어요"가 표시됩니다.

**아직 없는 기능:** 미리보기 부채 펼침, 되돌리기, 이름·색 바꾸기, 창 모두 닫기, 설정·단축키, 전체화면 자동 숨김. ([개발 계획](Development_Plan_v1.md) P1~P2 참고)

## 빌드

필요: Visual Studio 2022 또는 Build Tools 2022 (C++ 데스크톱 워크로드, Windows SDK 10.0.22621 이상)

```bash
powershell -ExecutionPolicy Bypass -File build.ps1
```

결과물: `build\bin\Release\`

## 구성

| 경로 | 내용 |
|---|---|
| `src/app/` | 진입점(`--watchdog` 모드 포함), 앱 코어(더미 관리·접기/펼치기·트레이), 아이콘 |
| `src/interaction/drag_watcher.*` | 다른 앱 창 끌기 감지(WinEvent, 주입 없음) + 상단 놓는 곳 바 |
| `src/surface/pile_window.*` | 더미 표시 창 (레이어드 창, 변경 시에만 다시 그림) |
| `src/preview/snapshot.*` | WGC 1회 캡처 스냅샷(WARP 우선, 유휴 시 해제) + 아이콘 카드 폴백 |
| `src/recovery/journal.*` | 복구 저널 + 감시자 |
| `src/common/` | Win32 유틸, CPU 드로잉(Canvas) |
| `tests/harness/TestWindows.cpp` | 테스트용 창 생성기 |
| `tests/e2e/e2e_piles.cpp` | 실제 마우스 입력으로 넣기→펼치기→접기→강제 종료 복구까지 확인 |
| `poc/`, `scripts/run_poc.ps1` | P0 기술 검증 도구 |

E2E 테스트 실행: `WindowPiles.exe`와 `TestWindows.exe --normal 3`을 띄운 뒤 `e2e_piles.exe`를 실행합니다. **마우스가 약 10초간 자동으로 움직입니다.**
