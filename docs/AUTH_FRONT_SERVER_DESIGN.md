# Auth / Front Server 설계 가이드

## 전체 서버 구조

```
Client
  │  TCP (프레임 프로토콜)
  ▼
[Front Server]  ──────────────────────────────────────┐
  │  내부 REST/gRPC                                   │ 브로드캐스트
  ├──▶ [Auth Server]  (토큰 발급·검증·갱신)           │
  └──▶ [Game Server]  (ECS·맵·전투·길드)  ◀───────────┘
          │
      [MySQL]  [Redis]
```

- **Front Server**: 클라이언트가 직접 붙는 유일한 진입점. SSL 종단, 패킷 검증, 라우팅.
- **Auth Server**: 로그인 처리, 세션 토큰 발급, 토큰 검증 API 제공.
- **Game Server**: 현재 코드베이스. 인증된 요청만 수신.

---

## Auth Server

### 책임 범위
- 계정 인증 (아이디/비밀번호 → bcrypt 검증)
- 세션 토큰 발급 (`SET token:{uuid} player_id EX token_ttl_sec`)
- 토큰 검증 API (`GET token:{uuid}`)
- 토큰 갱신 (sliding TTL: 활성 세션에 주기적 `EXPIRE`)
- 로그아웃 (명시적 `DEL token:{uuid}`)

### 추천 구현 스택
| 항목 | 선택 | 이유 |
|------|------|------|
| 프레임워크 | C++ Asio HTTP (또는 별도 Go/Node 서비스) | 현재 스택 통일 vs 빠른 개발 |
| 저장소 | Redis (세션) + MySQL (계정 테이블) | 현재 인프라 재사용 |
| 토큰 형식 | UUID v4 (128-bit random) | 단순·충돌없음. JWT는 stateless 필요 시 고려 |
| 비밀번호 해싱 | bcrypt (cost=12) | argon2id 더 강하지만 C++ 바인딩 복잡 |

### 계정 테이블 스키마
```sql
CREATE TABLE accounts (
    id          BIGINT       NOT NULL AUTO_INCREMENT PRIMARY KEY,
    username    VARCHAR(32)  NOT NULL UNIQUE,
    password_hash VARCHAR(72) NOT NULL,  -- bcrypt 출력
    created_at  TIMESTAMP    NOT NULL DEFAULT CURRENT_TIMESTAMP,
    last_login  TIMESTAMP    NULL,
    banned      TINYINT(1)   NOT NULL DEFAULT 0,
    ban_reason  VARCHAR(255) NULL
);
```

### 토큰 발급 흐름
```
Client ──[login_req(user, pw)]──▶ Auth Server
Auth Server:
  1. accounts 테이블에서 password_hash 조회
  2. bcrypt_verify(pw, hash) — 불일치 시 401
  3. banned=1 확인 — 금지 계정 차단
  4. UUID 생성: token = uuid_v4()
  5. Redis: SET token:{token} {player_id} EX token_ttl_sec
  6. last_login 업데이트 (비동기 OK)
  7. 응답: { token, player_id, expires_in }
```

### 토큰 검증 API (Game Server에서 호출)
```
GET /validate?token={uuid}
→ 200 { player_id: 12345 }
→ 401 { error: "invalid_or_expired" }
```

내부 호출이므로 mTLS 또는 공유 시크릿 헤더로 보호.

---

## Front Server

### 책임 범위
- SSL/TLS 종단 (클라이언트는 평문 불가)
- 클라이언트 연결 수락 및 프레임 디코딩 (현재 `frame_codec` 재사용)
- 로그인 전 패킷을 Auth Server로 포워딩
- 로그인 후 패킷을 Game Server로 포워딩
- Rate limiting (IP 단위, 세션 단위)
- DDoS 1차 차단 (연결 수 제한, 패킷 빈도 제한)

### 연결 생명주기
```
1. TCP accept → SSL handshake
2. 첫 패킷 = login_req (cmd=1001) → Auth Server로 전달
3. Auth 성공 → 토큰 수신 → 내부 세션 등록
4. 이후 패킷 → Game Server로 투명 포워딩
5. 연결 종료 → Game Server에 logout 이벤트 전달
```

### 포워딩 프로토콜

Front→Game 내부 채널은 현재 `frame_codec`을 그대로 사용.
세션 컨텍스트(player_id, channel_id)를 패킷 헤더에 추가하거나
별도 연결당 컨텍스트 맵으로 관리.

```cpp
// 내부 프레임 확장안 (선택적)
struct InternalHeader {
    std::uint32_t player_id;   // Auth에서 확인된 ID
    std::uint32_t session_seq; // 재전송 방지용 시퀀스
};
```

### Rate Limiting 설계
```
per-IP:
  - 초당 연결 시도: 최대 5회
  - 동시 연결: 최대 3개

per-session:
  - 초당 패킷: 최대 60개 (60fps 기준)
  - 패킷 크기: 최대 4096 bytes

구현: token bucket (std::atomic counter + reset timer)
```

---

## Game Server 연동 변경 사항

현재 Game Server는 클라이언트에서 직접 `session_token`을 받아 처리한다.
Front Server 도입 후에는:

1. **Front Server가 토큰 검증을 대신** — Game Server에는 이미 인증된 `player_id`가 헤더로 들어온다.
2. `handle_login` 핸들러의 Redis 토큰 검증 블록 → Front Server의 책임으로 이전.
3. Game Server는 `player_id`를 신뢰하고 바로 `find_or_create` DB 조회만 수행.

```cpp
// 현재 (Game Server가 직접 검증)
#ifdef LEMONDORY_HAVE_REDIS
if (redis_client_ && !req.session_token.empty()) {
    auto pid = redis_client_->get_player_id_by_token(req.session_token);
    if (!pid) { /* 401 */ return; }
}
#endif

// Front Server 도입 후 (검증 생략, 신뢰된 내부 채널)
std::int64_t player_id = req.player_id; // 헤더에서 추출, 이미 검증됨
```

---

## 구현 우선순위

| 단계 | 내용 | 전제 조건 |
|------|------|-----------|
| Phase 3-A | Auth Server: 로그인 + 토큰 발급 | MySQL accounts 테이블 생성 |
| Phase 3-B | Game Server: 내부 채널 token→player_id 헤더 신뢰 | Auth Server 완성 |
| Phase 3-C | Front Server: SSL 종단 + 포워딩 | Phase 3-B 완성 |
| Phase 3-D | Rate limiting + DDoS 기초 방어 | Front Server 완성 |

---

## 결정 필요 항목

- **Auth Server 언어**: C++ (현재 스택 통일) vs Go/Node (개발 속도). 내부 서버이므로 REST API 표면이 작음 — Go 추천 (표준 라이브러리로 충분).
- **Front→Game 통신**: 현재 `frame_codec` TCP 재사용 vs gRPC. 단일 게임 서버이면 TCP 직접 연결로 충분.
- **토큰 갱신 주체**: 클라이언트가 주기적으로 Front에 갱신 요청 vs Front Server가 활성 세션 자동 연장. 자동 연장 권장 (클라이언트 구현 단순화).
- **세션 다중 로그인**: 동일 계정의 중복 접속 허용 여부. 허용 시 token 키를 `token:{uuid}`, 차단 시 `session:{player_id}` 키를 추가로 관리.
