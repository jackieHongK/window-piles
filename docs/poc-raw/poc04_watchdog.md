## PoC-04 — 복구 저널 + 감시자 프로세스

- OS: Windows 11 Pro 25H2 (build 26200.9711)

### 저널 쓰기 비용 (창 30개, 5045 bytes, 50회)

| 방식 | 평균 ms | P95 ms | 최대 ms |
|---|---|---|---|
| flush 생략 | 3.59 | 4.98 | 9.22 |
| FlushFileBuffers + WRITE_THROUGH | 5.21 | 6.53 | 7.53 |

- 직렬화 왕복: 일치
- 1바이트 손상 감지: 감지됨 (checksum mismatch)

### 강제 종료 복구 (AC-07)

- 대상 3개 숨김: 성공
- 매니저 TerminateProcess 후 모든 창 다시 보임: 성공 (31.1 ms)
- 감시자 로그:
```
managerExitCode=57005
RESTORE 0x4D0DCC TW-Normal-1
RESTORE 0x4605CE TW-Normal-2
RESTORE 0xB009AC TW-Normal-3
SKIP 0x4D0DCC : 프로세스 생성 시각 불일치(PID 재사용)
SKIP 0x7FFF1234 : 창이 더 이상 없음
restored=3 skipped=2
recoveryMs=31.1
```

### 정상 종료 (AC-13)

- 매니저가 직접 복원 후 저널 clean 표시 → 모든 창 보임: 성공
- 감시자 로그:
```
managerExitCode=0
clean shutdown: nothing to do
recoveryMs=7.4
```

### 매니저+감시자 모두 강제 종료 → 다음 실행 시 저널 복구

- 둘 다 죽은 직후 창이 숨김 상태로 남음: 예(예상대로)
- `recover` 실행 후 모든 창 다시 보임: 성공 (107.8 ms)

