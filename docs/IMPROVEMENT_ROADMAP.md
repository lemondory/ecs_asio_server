# Improvement Roadmap

## 완료된 개선 사항

### 버그 수정
- **ComponentManager 메모리 누수** : `void*` raw 포인터 → `unique_ptr<IComponentStore>` 교체
- **listen() 이중 호출** : `game_server::init()`에서 중복 listen 제거
- **has_component 이중 락** : stores_mutex_ 보유 중 get_store() 재진입 제거

### 아키텍처 개선
- **channel_id → Entity O(1) 매핑** : 이동 처리 시 전체 순회(O n) → 해시맵 O(1) 조회
- **broadcast / broadcast_except** : 이동·채팅 결과를 다른 플레이어에게 전달하는 기반 구현
- **asio_server 채널 생명주기** : close 콜백으로 channels_ 자동 제거 (tick() 수동 호출 제거)
- **socket_channel_base 미사용 버퍼 제거** : write/send/read 3개 socket_buffer 제거 (채널당 ~128KB 낭비 제거)
- **frame_acc\_ O(n) erase → 오프셋 방식** : 읽기 위치만 전진, 절반 소비 시 한 번만 compact
- **CRC 검증 활성화** : 페이로드 CRC 불일치 프레임 드롭 처리
- **DB 레이어 (MySQL)** : DbManager(DbRole별 pool) + PlayerDao + FailureSink, 3회 재시도, JSON Lines 스냅샷
- **비동기 DB 처리 (per-player strand)** : `asio::thread_pool db_pool_{4}` + `db_strands_` 맵으로 io_context 블로킹 제거, 플레이어 내 login→logout 순서 보장
- **graceful shutdown** : `stop()`에서 `db_pool_.join()` — pending DB 작업 완료 후 종료
- **orphaned-entity 방지** : login DB 콜백 복귀 시 `channels_.count(ch_id)` 확인, 채널 없으면 entity 미생성

### 코드 품질
- **asio_channel.cpp 주석 정리** : 970줄 → 230줄 (이전 버전 코드 주석 전량 제거)
- **asio_server.cpp 주석 정리** : 140줄 주석 제거
- **CMake 빌드 구조 개선** : 테스트 7개 타겟이 game 소스 중복 컴파일 → `lemondory_game` 정적 라이브러리로 통합, CMakeLists.txt 하단 구버전 주석 제거
- **미사용 코드 제거** : `asio_channel.hpp` 죽은 멤버 6개, `socket_buffer` 클래스 전체, `frame_codec.hpp` 구버전 오버로드 + 주석 블록 제거 (448줄 → 136줄)

---

## 다음 단계: 아키텍처 개선

### 우선순위 높음

| 항목 | 현황 | 목표 |
|------|------|------|
| spdlog 활성화 | std::cout/cerr fallback | CMake 연결 정상화 |
| 설정 파일 로딩 | config/ 미사용 | 서버 시작 시 json/ini 로드 |

### 우선순위 중간

| 항목 | 설명 |
|------|------|
| 공간 인덱스 | get_entities_in_range() 현재 선언만 존재, Grid 또는 Quadtree 필요 |
| ~~ComponentStore 이터레이션 안전성~~ | begin/end 제거, components\_/mutex\_ private화, snapshot() 메서드로 안전한 이터레이션 보장 ✓ |

---

## 다음 단계: 기능 구현

### DB 레이어 (Phase 2 완료)
- ~~플레이어 데이터 저장/로드~~ — find_or_create, save_position, save_stats 구현 완료
- 로그인 인증 (세션 토큰) — Phase 3에서 Auth 서버와 연동 예정
- 인벤토리/아이템 영속화 — 프로토콜 미정의, 향후 구현

### 게임 로직 (스텁 상태)
- 공격 로직: 타겟 검증, 데미지 계산, 결과 브로드캐스트
- 몬스터/NPC 스폰: MapThread 내 핸들러 구현
- 맵 이동(존 이동): 채널을 다른 MapThread로 재배정

### 인프라
- SSL/TLS 지원
- ~~graceful shutdown~~ — DB 저장 완료 후 종료 (`db_pool_.join()`) 구현 완료. 네트워크 write flush는 미구현
- Rate limiting (패킷 빈도 제한)

---

## 남은 이슈 (우선순위별)

### 🔴 높음
- **thread-resilience** : DB 스레드 uncaught exception → try-catch + 로그·재시작 처리

### 🟡 중간
- **periodic-save** : 30초 타이머 기반 위치 자동 저장 (서버 크래시 대비)
- **pool-size-sync** : db_thread_pool 스레드 수 ≤ `DbConfig.pool_size` 규칙 assert

### 🟢 낮음
- **sink-fallback** : FailureSink 파일 I/O 실패 시 stderr/spdlog::critical fallback
- **snapshot-replay** : `logs/failed_saves.jsonl` 재반영 CLI 도구

---

## 알려진 제약 / 의도적 미구현

- **길드 시스템** : `protocol_guild.hpp` 정의 완료, 핸들러 인메모리 구현 (DB 연동 미완)
- **인벤토리** : 프로토콜 미정의
- **전투 결과 검증** : 서버-권위 구조 미적용 (클라이언트 신뢰 방식)
- ~~**MapThread-ECS 동시성**~~ : 각 MapThread가 `game_service_local_`(독립 인스턴스) 소유 → 글로벌 싱글톤 data race 해소 ✓
- ~~**ECS 컴포넌트 메모리 관리**~~ : `FlatPool<T>` 도입 — 블록 단위 사전 할당, O(1) 재사용, `add_component` → `emplace_component` API 전환 ✓

---

## 설계 필요 항목 (구현 전 설계 확정 필요)

### Redis 연동 (구현 완료 — 후속 작업 남음)

**완료**: hiredis 1.2.0 third_party 포함, RedisClient 래퍼, 토큰 검증, 플레이어 데이터 캐싱(로그인/로그아웃), TTL config 연동

**후속 작업 (TODO)**:

| 항목 | 설명 | 연결 조건 |
|------|------|-----------|
| 캐시 히트 시 DB 조회 생략 | 로그인 시 `GET player:{id}:data` 히트이면 MySQL `find_or_create` 생략 | Auth 서버 연동 후 token → player_id 확정 시 구현 가능 |
| 토큰 발급 API | 로그인 성공 시 `SET token:{uuid} player_id EX token_ttl_sec` 호출 위치 결정 | Auth 서버가 발급하거나, 임시로 게임 서버가 발급 |
| 토큰 갱신 (sliding TTL) | 활성 세션의 토큰 TTL을 접속 중에 자동 연장 | 연결 유지 시 주기적 `EXPIRE token:{token} 7200` |
| Redis pub/sub | 길드 알림, 크로스서버 채팅 등 서버 간 이벤트 전파 | 멀티 서버 인스턴스 구성 시 |

**TTL 설정 근거** (`config/server_config.json`):
- `token_ttl_sec: 7200` (2h) — 세션은 로그아웃 시 명시 삭제. TTL은 크래시/네트워크 단절로 고아가 된 토큰 자동 만료용
- `player_ttl_sec: 1800` (30min) — 동일하게 로그아웃 시 명시 삭제. 300s(5분)는 캐시 히트율이 낮아 의미 없음

---

### Redis 설계 원안 (참고)

**설계 방향**:
- `hiredis` 또는 `redis-plus-plus` 클라이언트 라이브러리 선택
- `RedisClient` 싱글톤 or DI 방식으로 `game_server`에서 접근
- 연결 풀: DB pool과 유사하게 `asio::thread_pool` + strand 패턴 적용

**주요 use case**:
```
로그인:  GET session:{token} → player_id  (토큰 검증, TTL 자동 만료)
캐싱:   GET player:{id}:data → JSON  (MySQL read 절감)
        SET player:{id}:data EX 300   (5분 TTL write-through)
```

**볼륨 판단 기준**: 동접 1000명 이하이면 단일 Redis 인스턴스로 충분.
클러스터가 필요한 규모가 되면 `redis-plus-plus`의 cluster 모드로 전환.

---

### 맵 AOI (Area of Interest) / Cell 분할 (설계 필요)

**문제**: 현재 `MapThread::local_players_`는 flat set — 이동 브로드캐스트가 맵 전체 플레이어에게 전송됨.
플레이어 수가 늘수록 패킷 양이 O(n²)으로 증가.

**목표**: 플레이어는 자신의 인접 셀(8방향 + 자신 = 최대 9셀)의 정보만 수신.
셀 경계 이동 시 구독 범위를 갱신하고 이전 셀 정보는 수신 중단.

**설계 방향**:
```
MapThread 내부:
  CELL_SIZE = 100 (유닛)
  cells_: unordered_map<CellKey, Cell>
  Cell { players: set<channel_id>, monsters: set<Entity> }

  이동 처리 흐름:
    1. 패킷 수신 → 새 좌표로 cell_key 계산
    2. cell_key 변경 시: leave(old_cell), enter(new_cell)
    3. 이동 브로드캐스트 대상 = get_neighbor_cells(new_cell) 의 플레이어들만
```

**결정 필요**:
- CELL_SIZE 크기 (맵 크기, 시야 거리에 따라 결정)
- 셀 전환 시 클라이언트에 enter/leave 알림 프로토콜 정의
- 몬스터/NPC도 셀 소속으로 관리할지 여부

---

### 인벤토리 시스템 (설계 필요)

**DB 스키마 방향**:
```sql
inventory (
  id BIGINT PK,
  player_id BIGINT NOT NULL,
  slot_type TINYINT,   -- 0=가방, 1=장비
  slot_index SMALLINT,
  item_id INT NOT NULL,
  quantity INT DEFAULT 1,
  attributes JSON,     -- 내구도, 인챈트 등
  UNIQUE KEY (player_id, slot_type, slot_index)
)
```

**ECS 컴포넌트**:
```cpp
struct ItemSlot { int item_id = 0; int quantity = 0; };
struct InventoryComponent {
    std::array<ItemSlot, 30> bag;    // 일반 슬롯
    std::array<ItemSlot, 8>  equip;  // 장비 슬롯
};
```

**프로토콜 (미정의 상태, 설계 필요)**:
- `2010` 인벤토리 전체 동기화 (로그인 시 1회)
- `2011` 아이템 이동 (슬롯 간 swap)
- `2012` 아이템 사용
- `2013` 아이템 드롭
- `2014` 장착/해제

**동시성**: per-player strand로 DB 직렬화는 기존 구조 재사용 가능.
트레이드(양 플레이어 동시 변경)는 deadlock 방지를 위해 `min(id_A, id_B)` 순서로 strand 획득 필요.

**결정 필요**:
- 아이템 정의 테이블(item_master) 구조 및 서버 캐싱 방식
- 장비 스탯 적용 방식 (Health/Combat 컴포넌트에 반영 시점)
- 트레이드 구현 여부 및 범위

---

### 코드 내 임시 처리 / 하드코딩 (TODO 태그)

코드에 `// TODO(태그): 설명` 형식으로 표시. `grep -rn "TODO(" server common client`로 전체 조회.

| 태그 | 내용 | 위치 |
|------|------|------|
| `combat-damage` | 데미지 공식 하드코딩 (`10 + skill_id*5`, 고정 10) | `player_handlers.cpp` handle_attack, `combat_system.cpp`, `ai_system.cpp` attack_target |
| `respawn` | 부활 시스템 부재 — 재접속 시 hp=0이면 max_hp로 복구하는 임시 가드 | `player_handlers.cpp` 로그인, `combat_system.cpp` |
| `id-unify` | login_res는 db_id, 인게임 패킷은 channel_id — 클라이언트가 자기 id 식별 불가, 몬스터 타겟 불가 | `player_handlers.cpp` 로그인·handle_attack |
| `level-stats` | 스폰 시 레벨 1 스탯 고정, 레벨업 미구현 | `player_handlers.cpp` 로그인 2곳, `health.hpp` |
| `ai-data` | 순찰 속도 0.5, 탐지 거리 20, 스폰 수 3, 몬스터 레벨 1 하드코딩 → CSV 이전 | `ai_system.cpp`, `map_thread.cpp` spawn |
| `cmd-enum` | cmd 번호 매직 넘버, MapThread 내부 cmd(0x1001~)와 체계 불일치 | `player_handlers.cpp` bind_player_handlers |
| `tick-dt` | ECS update dt 0.016 고정 | `game_server.cpp` on_tick |
| `redis-cache` | Redis 플레이어 캐시 조회가 placeholder | `player_handlers.cpp` 로그인 |
| `guild-db` | 길드 인메모리 | `game_server.hpp` |
| `spatial-index` | `get_entities_in_range()` 미구현 | `ecs/entity.cpp` |
| `ecs-cleanup` | 엔티티 제거·크기 합계 미구현 | `ecs/component.cpp` |
| `crc` | 테스트 클라이언트 CRC 0 고정 | `client/game_client.cpp` |

---

### 전투 시스템 (설계 확정 후 구현)

| 항목 | 현재 상태 | 설계 필요 내용 |
|------|-----------|----------------|
| 데미지 계산 (`combat-damage`) | `10 + skill_id * 5` 하드코딩 | 공격자 ATK, 방어자 DEF, 스킬 배율(SkillData) 확정 후 `CombatSystem::calculate_damage()`로 위임. 위치: `player_handlers.cpp` handle_attack |
| 타겟 식별 | `target_id`를 channel_id로 해석 (잘못됨) | entity_id 기반으로 변경, 플레이어/몬스터 구분 |
| 몬스터 전투 | `handle_combat_packet` 빈 함수 | 몬스터 타겟 지원, MapThread 내 처리로 라우팅 |
| 사망 처리 | entity 삭제만, 클라이언트 알림 없음 | `death_notify` 패킷 정의 및 브로드캐스트 |
| AI 반격 | 빈 함수 | 몬스터 공격 주기, 타겟 탐색 범위, 상태머신 연동 |

---

### 스텁 상태 게임 로직 (설계 필요)

아래 항목은 `MapThread` 내에 함수 껍데기만 있고 구현이 없음.
구현 전 설계(데이터 구조, 프로토콜, ECS 연동 방식)를 확정해야 함.

| 항목 | 현재 상태 | 설계 필요 내용 |
|------|-----------|----------------|
| `update_players()` | 빈 루프 | tick 단위 HP 재생, 버프 만료, 이동 검증 로직 |
| AI (`update_object_ai`) | 빈 함수 | 몬스터 상태머신(idle/chase/attack/flee), 타겟 탐색 범위, 경로탐색 여부 |
| 충돌 (`check_object_collisions`) | 빈 함수 | 충돌 판정 단위(AABB/원형), 서버 권위 여부, 맵 장애물 데이터 형식 |
| 맵 이동 후처리 (`process_map_transfers`) | 빈 함수 | 구 맵 thread에서 플레이어 제거 → 새 맵 thread로 add_player 원자적 이전 프로토콜 |
| 길드 DB 연동 | 인메모리 `GuildData` | guild 테이블 스키마, guild_member 관계, 알림 브로드캐스트 범위 |

---

### snapshot-replay CLI (낮은 우선순위)

**목적**: DB 장애 중 `logs/failed_saves.jsonl`에 기록된 저장 실패 항목을
DB 복구 후 재반영하는 독립 CLI 도구.

**설계 방향**:
```
./replay_tool --file logs/failed_saves.jsonl [--dry-run] [--db-url ...]
  - JSONL을 한 줄씩 파싱
  - 각 레코드를 PlayerDao의 적절한 save_* 함수로 재실행
  - 성공 시 처리된 레코드를 별도 파일로 이동 (idempotent 처리 필요)
```

**결정 필요**: 중복 적용 방지 (idempotent) 전략 — 타임스탬프 비교 or sequence 기반.
