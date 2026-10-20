## PoC-01/02 — DWM 썸네일

- OS: Windows 11 Pro 25H2 (build 26200.9711)
- 소스: TW-Normal-1 (TestWindows.exe); TW-Normal-2 (TestWindows.exe); TW-Normal-3 (TestWindows.exe); 

- 썸네일 3개 등록+속성 설정: 0.52 ms

| 원본 상태 | TW-Normal-1 (TestWindows.exe) | TW-Normal-2 (TestWindows.exe) | TW-Normal-3 (TestWindows.exe) |
|---|---|---|---|
| 1. 원본 표시 | 보임(색 일치) / 실시간 [bg=0.00, Δlum=255, src=344x201] {원본:보임} | 보임(색 일치) / 실시간 [bg=0.00, Δlum=255, src=344x201] {원본:보임} | 보임(색 일치) / 실시간 [bg=0.00, Δlum=255, src=344x201] {원본:보임} |
| 2. 다른 창에 가림 | 보임(색 일치) / 실시간 [bg=0.00, Δlum=255, src=344x201] {원본:보임} | 보임(색 일치) / 실시간 [bg=0.00, Δlum=255, src=344x201] {원본:보임} | 보임(색 일치) / 실시간 [bg=0.00, Δlum=255, src=344x201] {원본:보임} |
| 3. 최소화 | 보임(색 일치) / 정지 [bg=0.00, Δlum=0, src=344x201] {원본:최소화} | 보임(색 일치) / 정지 [bg=0.00, Δlum=0, src=344x201] {원본:최소화} | 보임(색 일치) / 정지 [bg=0.00, Δlum=0, src=344x201] {원본:최소화} |
| 4. SW_HIDE (숨김) | 보임(색 일치) / 정지 [bg=0.00, Δlum=0, src=344x201] {원본:숨김} | 보임(색 일치) / 정지 [bg=0.00, Δlum=0, src=344x201] {원본:숨김} | 보임(색 일치) / 정지 [bg=0.00, Δlum=0, src=344x201] {원본:숨김} |
| 5. 화면 밖으로 이동 | 보임(색 일치) / 정지 [bg=0.10, Δlum=0, src=344x201] {원본:보임} | 보임(색 일치) / 정지 [bg=0.10, Δlum=0, src=344x201] {원본:보임} | 보임(색 일치) / 정지 [bg=0.10, Δlum=0, src=344x201] {원본:보임} |

### 숨긴 상태 장시간 유지 (45초)

| 경과 | TW-Normal-1 (TestWindows.exe) | TW-Normal-2 (TestWindows.exe) | TW-Normal-3 (TestWindows.exe) | 
|---|---|---|---|
| 0s | 보임(색 일치) / 정지 [bg=0.10, Δlum=0, src=344x201] {원본:숨김} | 보임(색 일치) / 정지 [bg=0.10, Δlum=0, src=344x201] {원본:숨김} | 보임(색 일치) / 정지 [bg=0.10, Δlum=0, src=344x201] {원본:숨김} |
| 15s | 보임(색 일치) / 정지 [bg=0.10, Δlum=0, src=344x201] {원본:숨김} | 보임(색 일치) / 정지 [bg=0.10, Δlum=0, src=344x201] {원본:숨김} | 보임(색 일치) / 정지 [bg=0.10, Δlum=0, src=344x201] {원본:숨김} |
| 30s | 보임(색 일치) / 정지 [bg=0.10, Δlum=0, src=344x201] {원본:숨김} | 보임(색 일치) / 정지 [bg=0.10, Δlum=0, src=344x201] {원본:숨김} | 보임(색 일치) / 정지 [bg=0.10, Δlum=0, src=344x201] {원본:숨김} |
| 45s | 보임(색 일치) / 정지 [bg=0.10, Δlum=0, src=344x201] {원본:숨김} | 보임(색 일치) / 정지 [bg=0.10, Δlum=0, src=344x201] {원본:숨김} | 보임(색 일치) / 정지 [bg=0.10, Δlum=0, src=344x201] {원본:숨김} |

### 대상(destination) 창 종류별 표시 여부 (원본 표시 상태)

| 대상 창 | 결과 |
|---|---|
| 일반 WS_POPUP + TOPMOST + NOACTIVATE | 보임(색 일치) / 실시간 [bg=0.10, Δlum=230, src=344x201] |
| WS_EX_LAYERED (SetLayeredWindowAttributes α=255) | 보임(색 일치) / 실시간 [bg=0.10, Δlum=230, src=344x201] |
| WS_EX_LAYERED (UpdateLayeredWindow, per-pixel α) | 보임(색 일치) / 실시간 [bg=0.00, Δlum=255, src=344x201] |

### 겹친 썸네일 (12~16px 어긋나게 3장)

- 겹친 영역 맨 위에 보이는 것: 등록 순서 #3 (TW-Normal-3 (TestWindows.exe), 색 거리 0)

### 등록/해제 500회 반복

- 소요: 50.5 ms (회당 0.101 ms), 실패 0
- 핸들 127→127, GDI 1→1, USER 3→3

- 프로세스 working set: 9.6 MB

## PoC-01/02 — DWM 썸네일

- OS: Windows 11 Pro 25H2 (build 26200.9711)
- 소스: 계산기 (ApplicationFrameHost.exe); 

- 썸네일 1개 등록+속성 설정: 1.90 ms

| 원본 상태 | 계산기 (ApplicationFrameHost.exe) |
|---|---|
| 1. 원본 표시 | 보임 / 정지 [bg=0.00, Δlum=0, src=404x592] {원본:보임} |
| 2. 다른 창에 가림 | 보임 / 정지 [bg=0.00, Δlum=0, src=404x592] {원본:보임} |
| 3. 최소화 | 보임 / 정지 [bg=0.00, Δlum=0, src=404x592] {원본:최소화} |
| 4. SW_HIDE (숨김) | 보임 / 정지 [bg=0.00, Δlum=0, src=404x592] {원본:숨김} |
| 5. 화면 밖으로 이동 | 보임 / 정지 [bg=0.00, Δlum=0, src=404x592] {원본:보임} |

### 숨긴 상태 장시간 유지 (30초)

| 경과 | 계산기 (ApplicationFrameHost.exe) | 
|---|---|
| 0s | 보임 / 정지 [bg=0.00, Δlum=0, src=404x592] {원본:숨김} |
| 10s | 보임 / 정지 [bg=0.00, Δlum=0, src=404x592] {원본:숨김} |
| 20s | 보임 / 정지 [bg=0.00, Δlum=0, src=404x592] {원본:숨김} |
| 30s | 보임 / 정지 [bg=0.00, Δlum=0, src=404x592] {원본:숨김} |

### 대상(destination) 창 종류별 표시 여부 (원본 표시 상태)

| 대상 창 | 결과 |
|---|---|
| 일반 WS_POPUP + TOPMOST + NOACTIVATE | 보임 / 정지 [bg=0.00, Δlum=0, src=404x592] |
| WS_EX_LAYERED (SetLayeredWindowAttributes α=255) | 보임 / 정지 [bg=0.00, Δlum=0, src=404x592] |
| WS_EX_LAYERED (UpdateLayeredWindow, per-pixel α) | 보임 / 정지 [bg=0.00, Δlum=0, src=404x592] |

### 등록/해제 500회 반복

- 소요: 69.6 ms (회당 0.139 ms), 실패 0
- 핸들 127→127, GDI 1→1, USER 3→3

- 프로세스 working set: 8.0 MB

