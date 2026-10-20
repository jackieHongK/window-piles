# Window Piles (창 더미)

실행 중인 여러 앱 창을 '창 더미'로 접어 화면에 남겨 두고, 다시 원래 창들로 펼쳐 쓰는 초경량 Windows 유틸리티.

- 명세: [Desktop_Window_Piles_MVP_Spec_v1.0.md](Desktop_Window_Piles_MVP_Spec_v1.0.md)
- 개발 계획: [Development_Plan_v1.md](Development_Plan_v1.md)
- P0 PoC 결과: [docs/poc-results.md](docs/poc-results.md)

## 빌드

필요: Visual Studio 2022 또는 Build Tools 2022 (C++ 데스크톱 워크로드, Windows SDK 10.0.22621 이상)

```bash
powershell -ExecutionPolicy Bypass -File build.ps1
```

결과물: `build\bin\Release\`

## 구성 (현재: P0 단계)

| 경로 | 내용 |
|---|---|
| `tests/harness/TestWindows.cpp` | 테스트용 창 생성기 (일반/크기 고정/소유 대화상자/다시 나타남/캡처 방지/응답 없음/최대화) |
| `poc/poc01_dwm_thumbnail.cpp` | PoC-01/02 DWM 썸네일 + 원본 상태별 측정 |
| `poc/poc03_wgc_snapshot.cpp` | PoC-03 WGC 스냅샷 → 축소 → 숨김 → 표시 → 복원 |
| `poc/poc04_watchdog.cpp` | PoC-04 복구 저널 + 감시자 프로세스 |
| `poc/poc05_drag_probe.cpp` | PoC-05 창 끌기 감지 + 놓는 곳 바 프로토타입 |
| `scripts/run_poc.ps1` | 모든 PoC 실행 → `docs/poc-raw/<PC이름>-*.md` |

PoC 도구들은 기본적으로 `TestWindows.exe` 창만 조작한다. `--hwnd`로 다른 앱 창을 지정하면 그 창을 잠깐 숨기거나 옮길 수 있다.
