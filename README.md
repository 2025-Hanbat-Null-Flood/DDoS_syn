# TCP Command Server & Client (명령 구조체 기반)

이 프로젝트는 TCP 기반의 서버-클라이언트 제어 시스템입니다.
서버는 여러 클라이언트에 `start` / `stop` 명령을 전송하고,
클라이언트는 명령을 수행 후 1바이트 ACK로 응답합니다.
실험 목적의 네트워크 트래픽 제어(예: 패킷 송신 스레드)용으로 설계되었습니다.

---

## 프로젝트 구조

| 파일 | 역할 |
|------|------|
| `server_main.c` | 서버 메인 루프, select 기반 이벤트 처리 |
| `server_control.c` | 명령 파싱 및 브로드캐스트 제어 |
| `server_print.c` | 터미널 UI 렌더링, 입력 표시 |
| `server.h` | 공용 구조체 및 상수 정의 |
| `client.c` | 서버 명령 수신 및 동작 수행 클라이언트 |
| `makefile` | 빌드 스크립트 |

---

## 빌드 방법

### 서버
```bash
gcc server_main.c server_control.c server_print.c -o server
```

### 클라이언트
```bash
gcc client.c -o client -lpthread
```

---

## 실행 방법

### 1. 서버 실행
```bash
./server
```
- 포트: 8080
- 콘솔 명령을 통해 클라이언트를 제어
- UI에 클라이언트 상태 표시

### 2. 클라이언트 실행
```bash
sudo ./client
```
- 서버에 TCP로 접속
- 서버의 명령(`start`, `stop`) 수신 후 ACK 전송
- `q` 입력 시 종료

---

## 서버 콘솔 명령어

| 명령어 | 설명 |
|--------|------|
| `set <ip>:<port>` | 제어 대상 IP 및 포트 지정 |
| `start [fd]` | 특정 또는 모든 클라이언트에 `state=1` 명령 전송 |
| `stop [fd]` | 특정 또는 모든 클라이언트에 `state=0` 명령 전송 |
| `quit` | 서버 종료 |

예시:
```bash
> set 192.168.0.10:8080
> start all
> stop 2
> quit
```

---

## 통신 프로토콜

### 서버 → 클라이언트 (8바이트 구조체)
```c
typedef struct {
    uint32_t target_ip;   // network byte order
    uint16_t target_port; // network byte order
    uint8_t  state;       // 0=stop, 1=start
    uint8_t  padding;     // 항상 0
} command_t;
```

### 클라이언트 → 서버 (1바이트 ACK)
| 값 | 의미 |
|----|------|
| `0x00` | STOP 상태 보고 |
| `0x01` | START 상태 보고 |

---

## 내부 동작 흐름

### 서버
1. `socket → bind → listen` 후 `select()` 루프 진입  
2. 새 연결 감지 시 `accept()` → 클라이언트 등록  
3. STDIN 입력을 명령으로 처리 (`handle_command()`)  
4. 클라이언트로 명령 구조체 전송  
5. `recv(1 byte)` ACK 수신 시 상태 업데이트  
6. 2초 내 응답이 없으면 타임아웃 및 연결 제거  

### 클라이언트
1. 서버에 TCP 연결 후 `recv_thread` 시작  
2. 서버 명령(8B)을 수신하면:
   - `cmd.state` 값에 따라 `start_attack()` 또는 `stop_attack()` 실행  
   - 즉시 ACK(1B) 전송  
3. 사용자가 `q` 입력 시 종료  

---

## 클라이언트 내부 구성

| 구성 요소 | 역할 |
|------------|------|
| `recv_thread()` | 서버 명령 수신 및 ACK 전송 |
| `attack_thread_fn()` | RAW 소켓으로 TCP SYN 패킷 생성 및 송신 |
| `start_attack()` / `stop_attack()` | 스레드 제어 및 실행 상태 관리 |
| `checksum()` / `get_random_uint()` | IP/TCP 헤더용 유틸리티 함수 |

주의: `attack_thread_fn()`은 실제 네트워크 공격을 수행할 수 있으므로 테스트 환경 외 사용을 금지합니다.

---

## UI 예시 (서버 화면)

```
[*] select/TUI server on 8080
----------------------------------------------------------------
FD    Peer                  Err  Wait  State  LastSeen  Deadline
----------------------------------------------------------------
3     192.168.0.12          0    yes   ON     0~        0~
4     192.168.0.13          2    no    OFF    0~        -
----------------------------------------------------------------
> set 192.168.0.10:80
```

---

## 주요 상수

| 상수 | 의미 |
|------|------|
| `PORT 8080` | 서버 기본 포트 |
| `ECHO_DEADLINE_MS 2000` | ACK 타임아웃 (ms) |
| `MAX_ERR 10` | 최대 허용 오류 수 |
| `BACKLOG 128` | listen 대기열 크기 |

---

## 특징 요약

- 바이너리 명령 프로토콜 (8B → 1B)  
- select 기반 비동기 서버  
- 멀티클라이언트 지원  
- 터미널 기반 UI 표시 및 실시간 갱신  
- 간단한 제어 CLI (set/start/stop/quit)  
- 클라이언트 측 스레드 기반 동작 수행

---

## 주의사항

- 클라이언트의 `attack_thread_fn()`은 네트워크 패킷을 생성합니다.
  실제 인터넷 환경에서 실행하면 법적 문제가 발생할 수 있습니다.
  반드시 격리된 로컬 테스트 환경에서만 사용하십시오.

---

## 라이선스

본 코드는 연구 및 학습 목적으로 자유롭게 수정 및 배포 가능합니다.
