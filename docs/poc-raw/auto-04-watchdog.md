## PoC-04 — 복구 저널 + 감시자 프로세스

- OS: Windows 11 Pro 25H2 (build 26200.9711)

### 저널 쓰기 비용 (창 30개, 5045 bytes, 50회)

| 방식 | 평균 ms | P95 ms | 최대 ms |
|---|---|---|---|
| flush 생략 | 3.72 | 7.11 | 11.81 |
| FlushFileBuffers + WRITE_THROUGH | 4.86 | 6.29 | 7.11 |

- 직렬화 왕복: 일치
- 1바이트 손상 감지: 감지됨 (checksum mismatch)

### 강제 종료 복구 (AC-07)

- 대상 3개 숨김: 성공
- 매니저 TerminateProcess 후 모든 창 다시 보임: 성공 (46.7 ms)
- 감시자 로그:
```
managerExitCode=57005
RESTORE 0x5D128E TW-Normal-1
RESTORE 0x3212C8 TW-Normal-2
RESTORE 0x451272 TW-Normal-3
SKIP 0x5D128E : 프로세스 생성 시각 불일치(PID 재사용)
SKIP 0x7FFF1234 : 창이 더 이상 없음
restored=3 skipped=2
recoveryMs=36.0
```

### 정상 종료 (AC-13)

- 매니저가 직접 복원 후 저널 clean 표시 → 모든 창 보임: 성공
- 감시자 로그:
```
managerExitCode=0
clean shutdown: nothing to do
recoveryMs=7.9
```

### 매니저+감시자 모두 강제 종료 → 다음 실행 시 저널 복구

- 둘 다 죽은 직후 창이 숨김 상태로 남음: 예(예상대로)
- `recover` 실행 후 모든 창 다시 보임: 성공 (146.2 ms)

## PoC-04 — 복구 저널 + 감시자 프로세스

- OS: Windows 11 Pro 25H2 (build 26200.9711)

### 저널 쓰기 비용 (창 30개, 5044 bytes, 50회)

| 방식 | 평균 ms | P95 ms | 최대 ms |
|---|---|---|---|
| flush 생략 | 1.83 | 2.60 | 5.48 |
| FlushFileBuffers + WRITE_THROUGH | 2.54 | 3.21 | 3.64 |

- 직렬화 왕복: 일치
- 1바이트 손상 감지: 감지됨 (checksum mismatch)

### 강제 종료 복구 (AC-07)

- 대상 3개 숨김: 성공
- 매니저 TerminateProcess 후 모든 창 다시 보임: 성공 (31.5 ms)
- 감시자 로그:
```
managerExitCode=57005
RESTORE 0x2E0D1A TW-Normal-1
RESTORE 0xC7069A TW-Normal-2
RESTORE 0x1A31446 TW-Normal-3
SKIP 0x2E0D1A : 프로세스 생성 시각 불일치(PID 재사용)
SKIP 0x7FFF1234 : 창이 더 이상 없음
restored=3 skipped=2
recoveryMs=22.1
```

### 정상 종료 (AC-13)

- 매니저가 직접 복원 후 저널 clean 표시 → 모든 창 보임: 성공
- 감시자 로그:
```
managerExitCode=0
clean shutdown: nothing to do
recoveryMs=5.2
```

### 매니저+감시자 모두 강제 종료 → 다음 실행 시 저널 복구

- 둘 다 죽은 직후 창이 숨김 상태로 남음: 예(예상대로)
- `recover` 실행 후 모든 창 다시 보임: 성공 (65.7 ms)

