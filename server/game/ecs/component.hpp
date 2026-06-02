#pragma once

#include "entity.hpp"
#include <unordered_map>
#include <vector>
#include <memory>
#include <typeindex>
#include <mutex>
#include <stdexcept>

namespace lemondory::game {

// Component는 순수 데이터만 저장
class Component {
public:
    virtual ~Component() = default;
    virtual std::unique_ptr<Component> clone() const = 0;
};

// 타입 소거(type-erase)를 위한 추상 베이스
struct IComponentStore {
    virtual ~IComponentStore() = default;
};

// ──────────────────────────────────────────────────────────────────────────────
// ComponentStore<T>  —  Dense Packed Array (Sparse Set 패턴)
//
// 구조:
//   dense_[i]          : T 객체 연속 배열  ← 순회 시 이걸 순서대로 접근
//   idx_to_entity_[i]  : dense_[i] 를 소유한 entity
//   entity_to_idx_[e]  : entity e 의 dense_ 인덱스  (O(1) 랜덤 접근)
//
// 삭제 — swap-and-pop:
//   삭제 대상 슬롯 ← 마지막 슬롯으로 채움 → pop_back (배열 밀집 유지)
//
// 주의: emplace() 가 reallocation 을 유발하면 기존 T* 무효화.
//       T* 는 호출 프레임 내에서만 사용할 것. 장기 보관 금지.
//       initial_capacity 를 충분히 크게 잡으면 reallocation 발생 안 함.
// ──────────────────────────────────────────────────────────────────────────────
template<typename T>
class ComponentStore : public IComponentStore {
    std::vector<T>      dense_;           // 연속 T 배열 — 순회의 핵심
    std::vector<Entity> idx_to_entity_;   // dense_[i] 소유 entity
    std::unordered_map<Entity, std::size_t> entity_to_idx_;
    mutable std::mutex mutex_;

public:
    explicit ComponentStore(std::size_t initial_capacity = 256) {
        dense_.reserve(initial_capacity);
        idx_to_entity_.reserve(initial_capacity);
        entity_to_idx_.reserve(initial_capacity);
    }

    // 풀에서 생성/교체 — 새 entity면 push_back, 기존이면 값만 덮어씀
    template<typename... Args>
    void emplace(Entity entity, Args&&... args) {
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = entity_to_idx_.find(entity);
        if (it != entity_to_idx_.end()) {
            dense_[it->second] = T(std::forward<Args>(args)...);
            return;
        }
        entity_to_idx_[entity] = dense_.size();
        idx_to_entity_.push_back(entity);
        dense_.emplace_back(std::forward<Args>(args)...);
    }

    // swap-and-pop 삭제 — O(1), 배열 밀집 유지
    void remove(Entity entity) {
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = entity_to_idx_.find(entity);
        if (it == entity_to_idx_.end()) return;

        const std::size_t idx  = it->second;
        const std::size_t last = dense_.size() - 1;

        if (idx != last) {
            // 삭제 슬롯에 마지막 원소 이동
            dense_[idx]          = std::move(dense_[last]);
            idx_to_entity_[idx]  = idx_to_entity_[last];
            entity_to_idx_[idx_to_entity_[idx]] = idx;   // 이동된 entity 인덱스 갱신
        }

        dense_.pop_back();
        idx_to_entity_.pop_back();
        entity_to_idx_.erase(it);
    }

    T* get(Entity entity) {
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = entity_to_idx_.find(entity);
        return it != entity_to_idx_.end() ? &dense_[it->second] : nullptr;
    }

    const T* get(Entity entity) const {
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = entity_to_idx_.find(entity);
        return it != entity_to_idx_.end() ? &dense_[it->second] : nullptr;
    }

    bool has(Entity entity) const {
        std::lock_guard<std::mutex> lock(mutex_);
        return entity_to_idx_.count(entity) > 0;
    }

    // 시스템 순회용 스냅샷 — dense_ 를 선형으로 복사, 캐시 친화적
    std::vector<std::pair<Entity, T*>> snapshot() {
        std::lock_guard<std::mutex> lock(mutex_);
        std::vector<std::pair<Entity, T*>> out;
        out.reserve(dense_.size());
        for (std::size_t i = 0; i < dense_.size(); ++i)
            out.emplace_back(idx_to_entity_[i], &dense_[i]);
        return out;
    }

    std::vector<Entity> snapshot_entities() {
        std::lock_guard<std::mutex> lock(mutex_);
        return idx_to_entity_;
    }

    std::size_t size() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return dense_.size();
    }

    // 런타임 reallocation 방지 — 서버 시작 시 한 번만 호출
    void reserve_capacity(std::size_t n) {
        std::lock_guard<std::mutex> lock(mutex_);
        dense_.reserve(n);
        idx_to_entity_.reserve(n);
        entity_to_idx_.reserve(n);
    }

    std::size_t capacity() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return dense_.capacity();
    }
};

// ──────────────────────────────────────────────────────────────────────────────
// ComponentManager
// ──────────────────────────────────────────────────────────────────────────────
class ComponentManager {
public:
    static ComponentManager& instance();

    template<typename T>
    ComponentStore<T>& get_store() {
        std::lock_guard<std::mutex> lock(stores_mutex_);
        const std::type_index key = typeid(T);
        auto it = stores_.find(key);
        if (it == stores_.end()) {
            auto store = std::make_unique<ComponentStore<T>>();
            auto* raw  = store.get();
            stores_.emplace(key, std::move(store));
            return *raw;
        }
        return *static_cast<ComponentStore<T>*>(it->second.get());
    }

    template<typename T>
    const ComponentStore<T>& get_store() const {
        std::lock_guard<std::mutex> lock(stores_mutex_);
        const std::type_index key = typeid(T);
        auto it = stores_.find(key);
        if (it == stores_.end())
            throw std::runtime_error("Component store not found");
        return *static_cast<const ComponentStore<T>*>(it->second.get());
    }

    template<typename T, typename... Args>
    void emplace_component(Entity entity, Args&&... args) {
        get_store<T>().emplace(entity, std::forward<Args>(args)...);
    }

    template<typename T>
    void remove_component(Entity entity) {
        get_store<T>().remove(entity);
    }

    template<typename T>
    T* get_component(Entity entity) {
        return get_store<T>().get(entity);
    }

    template<typename T>
    bool has_component(Entity entity) const {
        std::lock_guard<std::mutex> lock(stores_mutex_);
        auto it = stores_.find(std::type_index(typeid(T)));
        if (it == stores_.end()) return false;
        return static_cast<const ComponentStore<T>*>(it->second.get())->has(entity);
    }

    template<typename T>
    std::vector<Entity> get_entities_with_component() {
        return get_store<T>().snapshot_entities();
    }

    template<typename T>
    std::vector<std::pair<Entity, T*>> get_components() {
        return get_store<T>().snapshot();
    }

    void remove_all_components(Entity entity);

    std::size_t get_component_count() const;
    std::size_t get_component_type_count() const;

public:
    ComponentManager() = default;
    std::unordered_map<std::type_index, std::unique_ptr<IComponentStore>> stores_;
    mutable std::mutex stores_mutex_;
};

} // namespace lemondory::game
