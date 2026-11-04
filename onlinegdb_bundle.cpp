#ifndef PYDICT_STANDALONE_HPP
#define PYDICT_STANDALONE_HPP

/**
 * ============================================================================
 * PyDict++: Python-Inspired Dictionary Library for C++
 * ============================================================================
 * Single-Header Standalone C++17 Library.
 * Replicates Python dictionary semantics using a custom dual-chain hash table
 * without relying on std::unordered_map or std::map.
 * ============================================================================
 */

#include <iostream>
#include <vector>
#include <string>
#include <utility>
#include <optional>
#include <algorithm>
#include <functional>
#include <chrono>
#include <ctime>
#include <iomanip>
#include <sstream>
#include <fstream>
#include <cstdint>
#include <cctype>
#include <stdexcept>
#include <memory>
#include <variant>
#include <type_traits>
#include <initializer_list>

namespace pydictpp {

// SFINAE streamable trait
template <typename T, typename = void>
struct is_streamable : std::false_type {};

template <typename T>
struct is_streamable<T, std::void_t<decltype(std::declval<std::ostream&>() << std::declval<const T&>())>> : std::true_type {};

// ============================================================================
// EXCEPTIONS
// ============================================================================

class PyDictException : public std::runtime_error {
public:
    explicit PyDictException(const std::string& msg) : std::runtime_error(msg) {}
};

class KeyError : public PyDictException {
    std::string key_repr_;
public:
    explicit KeyError(const std::string& key)
        : PyDictException("KeyError: '" + key + "'"), key_repr_(key) {}

    template <typename K>
    static KeyError from_key(const K& key) {
        if constexpr (is_streamable<K>::value) {
            std::ostringstream oss;
            oss << key;
            return KeyError(oss.str());
        } else {
            return KeyError("<custom_key>");
        }
    }

    const std::string& key() const noexcept { return key_repr_; }
};

class ValueError : public PyDictException {
public:
    explicit ValueError(const std::string& msg) : PyDictException("ValueError: " + msg) {}
};

class HistoryEmptyError : public PyDictException {
public:
    explicit HistoryEmptyError(const std::string& msg = "No operations in history")
        : PyDictException("HistoryEmptyError: " + msg) {}
};

class SerializationError : public PyDictException {
public:
    explicit SerializationError(const std::string& msg) : PyDictException("SerializationError: " + msg) {}
};

// ============================================================================
// CORE DATA STRUCTURE: DUAL-CHAIN NODE
// ============================================================================

namespace core {

template <typename Key, typename Value>
struct Node {
    Key key;
    Value value;
    size_t hash_val;

    Node* next_in_bucket{nullptr}; // Bucket collision chain
    Node* prev_global{nullptr};    // Global insertion-order doubly-linked list
    Node* next_global{nullptr};

    Node(const Key& k, const Value& v, size_t h)
        : key(k), value(v), hash_val(h),
          next_in_bucket(nullptr), prev_global(nullptr), next_global(nullptr) {}

    Node(Key&& k, Value&& v, size_t h)
        : key(std::move(k)), value(std::move(v)), hash_val(h),
          next_in_bucket(nullptr), prev_global(nullptr), next_global(nullptr) {}

    Node(const Key& k, Value&& v, size_t h)
        : key(k), value(std::move(v)), hash_val(h),
          next_in_bucket(nullptr), prev_global(nullptr), next_global(nullptr) {}

    Node(Key&& k, const Value& v, size_t h)
        : key(std::move(k)), value(v), hash_val(h),
          next_in_bucket(nullptr), prev_global(nullptr), next_global(nullptr) {}

    Node(const Node&) = delete;
    Node& operator=(const Node&) = delete;
};

inline const std::vector<size_t>& get_prime_list() {
    static const std::vector<size_t> primes = {
        17, 31, 67, 131, 257, 521, 1031, 2053, 4099, 8209, 16411,
        32771, 65537, 131101, 262147, 524309, 1048583, 2097169,
        4194319, 8388617, 16777259, 33554467, 67108879
    };
    return primes;
}

inline size_t next_prime_capacity(size_t min_cap) {
    for (size_t p : get_prime_list()) {
        if (p >= min_cap) return p;
    }
    return min_cap | 1;
}

// ============================================================================
// HASH TABLE ENGINE (MEMBER 1)
// ============================================================================

template <typename Key, typename Value, typename Hash = std::hash<Key>, typename KeyEqual = std::equal_to<Key>>
class HashTable {
public:
    using NodeType = Node<Key, Value>;
    using size_type = std::size_t;

protected:
    std::vector<NodeType*> buckets_;
    NodeType* head_{nullptr};
    NodeType* tail_{nullptr};
    size_type size_{0};
    float max_load_factor_{0.75f};
    size_type collision_count_{0};

    Hash hash_fn_{};
    KeyEqual equal_fn_{};

    size_type bucket_index(size_t hash_val) const noexcept {
        return hash_val % buckets_.size();
    }

    size_type compute_hash(const Key& key) const {
        return hash_fn_(key);
    }

public:
    explicit HashTable(size_type initial_cap = 17, const Hash& h = Hash(), const KeyEqual& eq = KeyEqual())
        : hash_fn_(h), equal_fn_(eq) {
        buckets_.assign(next_prime_capacity(std::max<size_type>(initial_cap, 7)), nullptr);
    }

    virtual ~HashTable() { clear_internal(); }

    HashTable(const HashTable& other)
        : max_load_factor_(other.max_load_factor_), hash_fn_(other.hash_fn_), equal_fn_(other.equal_fn_) {
        buckets_.assign(other.buckets_.size(), nullptr);
        for (NodeType* curr = other.head_; curr != nullptr; curr = curr->next_global) {
            insert_internal(curr->key, curr->value);
        }
    }

    HashTable(HashTable&& other) noexcept
        : buckets_(std::move(other.buckets_)), head_(other.head_), tail_(other.tail_),
          size_(other.size_), max_load_factor_(other.max_load_factor_),
          collision_count_(other.collision_count_), hash_fn_(std::move(other.hash_fn_)),
          equal_fn_(std::move(other.equal_fn_)) {
        other.head_ = other.tail_ = nullptr;
        other.size_ = other.collision_count_ = 0;
        other.buckets_.assign(17, nullptr);
    }

    HashTable& operator=(const HashTable& other) {
        if (this != &other) {
            clear_internal();
            hash_fn_ = other.hash_fn_;
            equal_fn_ = other.equal_fn_;
            max_load_factor_ = other.max_load_factor_;
            buckets_.assign(other.buckets_.size(), nullptr);
            for (NodeType* curr = other.head_; curr != nullptr; curr = curr->next_global) {
                insert_internal(curr->key, curr->value);
            }
        }
        return *this;
    }

    HashTable& operator=(HashTable&& other) noexcept {
        if (this != &other) {
            clear_internal();
            buckets_ = std::move(other.buckets_);
            head_ = other.head_;
            tail_ = other.tail_;
            size_ = other.size_;
            max_load_factor_ = other.max_load_factor_;
            collision_count_ = other.collision_count_;
            hash_fn_ = std::move(other.hash_fn_);
            equal_fn_ = std::move(other.equal_fn_);
            other.head_ = other.tail_ = nullptr;
            other.size_ = other.collision_count_ = 0;
            other.buckets_.assign(17, nullptr);
        }
        return *this;
    }

    size_type size() const noexcept { return size_; }
    bool empty() const noexcept { return size_ == 0; }
    size_type bucketCount() const noexcept { return buckets_.size(); }
    float loadFactor() const noexcept { return buckets_.empty() ? 0.0f : static_cast<float>(size_) / buckets_.size(); }
    float max_load_factor() const noexcept { return max_load_factor_; }
    void max_load_factor(float mlf) { max_load_factor_ = mlf; check_and_rehash(); }
    size_type collisionCount() const noexcept { return collision_count_; }

    size_type maxBucketDepth() const noexcept {
        size_type max_d = 0;
        for (const auto* b_head : buckets_) {
            size_type d = 0;
            for (const auto* curr = b_head; curr != nullptr; curr = curr->next_in_bucket) ++d;
            if (d > max_d) max_d = d;
        }
        return max_d;
    }

    size_type emptyBucketCount() const noexcept {
        size_type cnt = 0;
        for (const auto* b : buckets_) if (b == nullptr) ++cnt;
        return cnt;
    }

    void printStats(std::ostream& os = std::cout) const {
        os << "=== PyDict++ Hash Table Statistics ===\n"
           << "  Elements (size)     : " << size_ << "\n"
           << "  Buckets             : " << buckets_.size() << "\n"
           << "  Load Factor         : " << std::fixed << std::setprecision(4) << loadFactor() << "\n"
           << "  Collision Count     : " << collision_count_ << "\n"
           << "  Max Bucket Depth    : " << maxBucketDepth() << "\n"
           << "  Empty Buckets       : " << emptyBucketCount() << "\n"
           << "======================================\n";
    }

    NodeType* find_node(const Key& key) const {
        if (buckets_.empty()) return nullptr;
        size_t h = compute_hash(key);
        NodeType* curr = buckets_[bucket_index(h)];
        while (curr != nullptr) {
            if (curr->hash_val == h && equal_fn_(curr->key, key)) return curr;
            curr = curr->next_in_bucket;
        }
        return nullptr;
    }

    bool contains(const Key& key) const { return find_node(key) != nullptr; }

    Value& at(const Key& key) {
        NodeType* node = find_node(key);
        if (!node) throw KeyError::from_key(key);
        return node->value;
    }

    const Value& at(const Key& key) const {
        NodeType* node = find_node(key);
        if (!node) throw KeyError::from_key(key);
        return node->value;
    }

    void rehash(size_type new_cap) {
        size_type target = next_prime_capacity(std::max(new_cap, size_ + 1));
        if (target == buckets_.size()) return;
        std::vector<NodeType*> new_buckets(target, nullptr);
        for (NodeType* curr = head_; curr != nullptr; curr = curr->next_global) {
            size_type idx = curr->hash_val % target;
            curr->next_in_bucket = new_buckets[idx];
            new_buckets[idx] = curr;
        }
        buckets_ = std::move(new_buckets);
    }

protected:
    void check_and_rehash() {
        if (loadFactor() >= max_load_factor_) rehash(buckets_.size() * 2 + 1);
    }

    template <typename K, typename V>
    std::pair<NodeType*, bool> insert_internal(K&& key, V&& value) {
        check_and_rehash();
        size_t h = compute_hash(key);
        size_type idx = bucket_index(h);
        NodeType* curr = buckets_[idx];
        while (curr != nullptr) {
            if (curr->hash_val == h && equal_fn_(curr->key, key)) {
                curr->value = std::forward<V>(value);
                return {curr, false};
            }
            curr = curr->next_in_bucket;
        }
        if (buckets_[idx] != nullptr) ++collision_count_;
        NodeType* n = new NodeType(std::forward<K>(key), std::forward<V>(value), h);
        n->next_in_bucket = buckets_[idx];
        buckets_[idx] = n;
        if (!tail_) { head_ = tail_ = n; }
        else { tail_->next_global = n; n->prev_global = tail_; tail_ = n; }
        ++size_;
        return {n, true};
    }

    bool erase_internal(const Key& key, Value* out_old_val = nullptr) {
        if (buckets_.empty() || size_ == 0) return false;
        size_t h = compute_hash(key);
        size_type idx = bucket_index(h);
        NodeType* prev = nullptr;
        NodeType* curr = buckets_[idx];
        while (curr != nullptr) {
            if (curr->hash_val == h && equal_fn_(curr->key, key)) {
                if (out_old_val) *out_old_val = std::move(curr->value);
                if (!prev) buckets_[idx] = curr->next_in_bucket;
                else prev->next_in_bucket = curr->next_in_bucket;
                if (curr->prev_global) curr->prev_global->next_global = curr->next_global;
                else head_ = curr->next_global;
                if (curr->next_global) curr->next_global->prev_global = curr->prev_global;
                else tail_ = curr->prev_global;
                delete curr;
                --size_;
                return true;
            }
            prev = curr;
            curr = curr->next_in_bucket;
        }
        return false;
    }

    void clear_internal() noexcept {
        NodeType* curr = head_;
        while (curr != nullptr) {
            NodeType* next = curr->next_global;
            delete curr;
            curr = next;
        }
        head_ = tail_ = nullptr;
        size_ = 0;
        std::fill(buckets_.begin(), buckets_.end(), nullptr);
    }
};

} // namespace core

// ============================================================================
// ITERATORS & VIEWS (MEMBER 2)
// ============================================================================

namespace dict {

template <typename Key, typename Value>
class PyDictIterator {
    core::Node<Key, Value>* node_{nullptr};
public:
    using iterator_category = std::bidirectional_iterator_tag;
    using value_type = std::pair<const Key&, Value&>;
    using difference_type = std::ptrdiff_t;

    PyDictIterator() = default;
    explicit PyDictIterator(core::Node<Key, Value>* n) : node_(n) {}

    std::pair<const Key&, Value&> operator*() const noexcept { return {node_->key, node_->value}; }

    struct ArrowProxy {
        std::pair<const Key&, Value&> pair;
        const std::pair<const Key&, Value&>* operator->() const noexcept { return &pair; }
    };
    ArrowProxy operator->() const noexcept { return ArrowProxy{{node_->key, node_->value}}; }

    PyDictIterator& operator++() noexcept { if (node_) node_ = node_->next_global; return *this; }
    PyDictIterator operator++(int) noexcept { auto t = *this; ++(*this); return t; }
    PyDictIterator& operator--() noexcept { if (node_) node_ = node_->prev_global; return *this; }
    PyDictIterator operator--(int) noexcept { auto t = *this; --(*this); return t; }
    bool operator==(const PyDictIterator& o) const noexcept { return node_ == o.node_; }
    bool operator!=(const PyDictIterator& o) const noexcept { return node_ != o.node_; }
    core::Node<Key, Value>* get_node() const noexcept { return node_; }
};

template <typename Key, typename Value>
class ConstPyDictIterator {
    const core::Node<Key, Value>* node_{nullptr};
public:
    using iterator_category = std::bidirectional_iterator_tag;
    using value_type = std::pair<const Key&, const Value&>;
    using difference_type = std::ptrdiff_t;

    ConstPyDictIterator() = default;
    explicit ConstPyDictIterator(const core::Node<Key, Value>* n) : node_(n) {}
    ConstPyDictIterator(const PyDictIterator<Key, Value>& it) : node_(it.get_node()) {}

    std::pair<const Key&, const Value&> operator*() const noexcept { return {node_->key, node_->value}; }

    struct ArrowProxy {
        std::pair<const Key&, const Value&> pair;
        const std::pair<const Key&, const Value&>* operator->() const noexcept { return &pair; }
    };
    ArrowProxy operator->() const noexcept { return ArrowProxy{{node_->key, node_->value}}; }

    ConstPyDictIterator& operator++() noexcept { if (node_) node_ = node_->next_global; return *this; }
    ConstPyDictIterator operator++(int) noexcept { auto t = *this; ++(*this); return t; }
    ConstPyDictIterator& operator--() noexcept { if (node_) node_ = node_->prev_global; return *this; }
    ConstPyDictIterator operator--(int) noexcept { auto t = *this; --(*this); return t; }
    bool operator==(const ConstPyDictIterator& o) const noexcept { return node_ == o.node_; }
    bool operator!=(const ConstPyDictIterator& o) const noexcept { return node_ != o.node_; }
};

template <typename Key, typename Value>
class KeysView {
    const core::Node<Key, Value>* head_;
    size_t sz_;
public:
    KeysView(const core::Node<Key, Value>* h, size_t s) : head_(h), sz_(s) {}

    struct Iter {
        const core::Node<Key, Value>* n;
        const Key& operator*() const noexcept { return n->key; }
        Iter& operator++() noexcept { if (n) n = n->next_global; return *this; }
        bool operator!=(const Iter& o) const noexcept { return n != o.n; }
    };
    Iter begin() const noexcept { return Iter{head_}; }
    Iter end() const noexcept { return Iter{nullptr}; }
    size_t size() const noexcept { return sz_; }

    operator std::vector<Key>() const {
        std::vector<Key> v; v.reserve(sz_);
        for (const auto& k : *this) v.push_back(k);
        return v;
    }

    friend std::ostream& operator<<(std::ostream& os, const KeysView& kw) {
        os << "dict_keys([";
        bool f = true;
        for (const auto& k : kw) { if (!f) os << ", "; f = false; os << k; }
        return os << "])";
    }
};

template <typename Key, typename Value>
class ValuesView {
    core::Node<Key, Value>* head_;
    size_t sz_;
public:
    ValuesView(core::Node<Key, Value>* h, size_t s) : head_(h), sz_(s) {}

    struct Iter {
        core::Node<Key, Value>* n;
        Value& operator*() noexcept { return n->value; }
        Iter& operator++() noexcept { if (n) n = n->next_global; return *this; }
        bool operator!=(const Iter& o) const noexcept { return n != o.n; }
    };
    Iter begin() noexcept { return Iter{head_}; }
    Iter end() noexcept { return Iter{nullptr}; }
    size_t size() const noexcept { return sz_; }

    operator std::vector<Value>() const {
        std::vector<Value> v; v.reserve(sz_);
        for (auto curr = head_; curr; curr = curr->next_global) v.push_back(curr->value);
        return v;
    }

    friend std::ostream& operator<<(std::ostream& os, const ValuesView& vw) {
        os << "dict_values([";
        bool f = true;
        for (auto curr = vw.head_; curr; curr = curr->next_global) {
            if (!f) os << ", "; f = false; os << curr->value;
        }
        return os << "])";
    }
};

template <typename Key, typename Value>
class ItemsView {
    core::Node<Key, Value>* head_;
    size_t sz_;
public:
    ItemsView(core::Node<Key, Value>* h, size_t s) : head_(h), sz_(s) {}
    auto begin() noexcept { return PyDictIterator<Key, Value>(head_); }
    auto end() noexcept { return PyDictIterator<Key, Value>(nullptr); }
    auto begin() const noexcept { return ConstPyDictIterator<Key, Value>(head_); }
    auto end() const noexcept { return ConstPyDictIterator<Key, Value>(nullptr); }
    size_t size() const noexcept { return sz_; }

    operator std::vector<std::pair<Key, Value>>() const {
        std::vector<std::pair<Key, Value>> v; v.reserve(sz_);
        for (auto curr = head_; curr; curr = curr->next_global) v.emplace_back(curr->key, curr->value);
        return v;
    }

    friend std::ostream& operator<<(std::ostream& os, const ItemsView& iw) {
        os << "dict_items([";
        bool f = true;
        for (const auto& [k, v] : iw) {
            if (!f) os << ", "; f = false; os << "(" << k << ", " << v << ")";
        }
        return os << "])";
    }
};

} // namespace dict

// ============================================================================
// HISTORY & UNDO SYSTEM (MEMBER 3)
// ============================================================================

namespace features {

enum class OpType { INSERT, UPDATE, ERASE, CLEAR };

template <typename Key, typename Value>
struct HistoryRecord {
    OpType type;
    Key key;
    Value old_value;
    Value new_value;
    std::vector<std::pair<Key, Value>> clear_snapshot;
    std::chrono::system_clock::time_point timestamp;

    static HistoryRecord make_insert(const Key& k, const Value& v) {
        HistoryRecord r; r.type = OpType::INSERT; r.key = k; r.new_value = v;
        r.timestamp = std::chrono::system_clock::now(); return r;
    }
    static HistoryRecord make_update(const Key& k, const Value& old_v, const Value& new_v) {
        HistoryRecord r; r.type = OpType::UPDATE; r.key = k; r.old_value = old_v; r.new_value = new_v;
        r.timestamp = std::chrono::system_clock::now(); return r;
    }
    static HistoryRecord make_erase(const Key& k, const Value& old_v) {
        HistoryRecord r; r.type = OpType::ERASE; r.key = k; r.old_value = old_v;
        r.timestamp = std::chrono::system_clock::now(); return r;
    }
    static HistoryRecord make_clear(std::vector<std::pair<Key, Value>> snap) {
        HistoryRecord r; r.type = OpType::CLEAR; r.clear_snapshot = std::move(snap);
        r.timestamp = std::chrono::system_clock::now(); return r;
    }
};

template <typename Key, typename Value>
class HistoryEngine {
    using Record = HistoryRecord<Key, Value>;
    std::vector<Record> undo_stack_;
    std::vector<Record> redo_stack_;
    std::vector<Record> log_;
    bool enabled_{true};
public:
    void enable(bool s) noexcept { enabled_ = s; }
    bool is_enabled() const noexcept { return enabled_; }
    void clear_history() noexcept { undo_stack_.clear(); redo_stack_.clear(); log_.clear(); }
    size_t history_size() const noexcept { return log_.size(); }
    bool can_undo() const noexcept { return !undo_stack_.empty(); }
    bool can_redo() const noexcept { return !redo_stack_.empty(); }

    void record_operation(const Record& r) {
        if (!enabled_) return;
        undo_stack_.push_back(r);
        redo_stack_.clear();
        log_.push_back(r);
    }

    template <typename Dict>
    bool perform_undo(Dict& d) {
        if (undo_stack_.empty()) throw HistoryEmptyError();
        Record r = std::move(undo_stack_.back());
        undo_stack_.pop_back();
        bool prev = enabled_; enabled_ = false;
        try {
            switch (r.type) {
                case OpType::INSERT: d.erase(r.key); break;
                case OpType::UPDATE: case OpType::ERASE: d.insert_or_assign(r.key, r.old_value); break;
                case OpType::CLEAR: for (auto& p : r.clear_snapshot) d.insert_or_assign(p.first, p.second); break;
            }
        } catch (...) { enabled_ = prev; throw; }
        enabled_ = prev;
        redo_stack_.push_back(std::move(r));
        return true;
    }

    template <typename Dict>
    bool perform_redo(Dict& d) {
        if (redo_stack_.empty()) throw HistoryEmptyError();
        Record r = std::move(redo_stack_.back());
        redo_stack_.pop_back();
        bool prev = enabled_; enabled_ = false;
        try {
            switch (r.type) {
                case OpType::INSERT: case OpType::UPDATE: d.insert_or_assign(r.key, r.new_value); break;
                case OpType::ERASE: d.erase(r.key); break;
                case OpType::CLEAR: d.clear(); break;
            }
        } catch (...) { enabled_ = prev; throw; }
        enabled_ = prev;
        undo_stack_.push_back(std::move(r));
        return true;
    }

    void print_history(std::ostream& os = std::cout) const {
        os << "=== PyDict++ Operation History Log (" << log_.size() << " entries) ===\n";
        for (size_t i = 0; i < log_.size(); ++i) {
            const auto& r = log_[i];
            os << "  #" << (i + 1) << " ";
            if (r.type == OpType::INSERT) os << "INSERT | Key: " << r.key << " => Value: " << r.new_value;
            else if (r.type == OpType::UPDATE) os << "UPDATE | Key: " << r.key << " => Old: " << r.old_value << ", New: " << r.new_value;
            else if (r.type == OpType::ERASE) os << "ERASE  | Key: " << r.key << " (was: " << r.old_value << ")";
            else if (r.type == OpType::CLEAR) os << "CLEAR  | Cleared " << r.clear_snapshot.size() << " elements";
            os << "\n";
        }
        os << "========================================================\n";
    }
};

// ============================================================================
// JSON & SERIALIZATION ENGINE (MEMBER 3)
// ============================================================================

struct JsonValue {
    enum class Type { Null, Boolean, Integer, Number, String, Array, Object };
    Type type{Type::Null};
    bool bool_val{false};
    int64_t int_val{0};
    double num_val{0.0};
    std::string str_val{};
    std::vector<JsonValue> arr_val{};
    std::vector<std::pair<std::string, JsonValue>> obj_val{};

    JsonValue() = default;
    explicit JsonValue(bool b) : type(Type::Boolean), bool_val(b) {}
    explicit JsonValue(int64_t i) : type(Type::Integer), int_val(i), num_val(static_cast<double>(i)) {}
    explicit JsonValue(double d) : type(Type::Number), int_val(static_cast<int64_t>(d)), num_val(d) {}
    explicit JsonValue(std::string s) : type(Type::String), str_val(std::move(s)) {}

    static JsonValue make_object() { JsonValue j; j.type = Type::Object; return j; }
    static JsonValue make_array() { JsonValue j; j.type = Type::Array; return j; }

    std::string dump(int indent = 2, int lvl = 0) const {
        std::string ind(lvl * indent, ' ');
        std::string n_ind((lvl + 1) * indent, ' ');
        switch (type) {
            case Type::Null: return "null";
            case Type::Boolean: return bool_val ? "true" : "false";
            case Type::Integer: return std::to_string(int_val);
            case Type::Number: {
                std::ostringstream ss; ss << std::setprecision(10) << num_val;
                std::string s = ss.str();
                if (s.find('.') == std::string::npos && s.find('e') == std::string::npos) s += ".0";
                return s;
            }
            case Type::String: return "\"" + str_val + "\"";
            case Type::Array: {
                if (arr_val.empty()) return "[]";
                std::string res = "[\n";
                for (size_t i = 0; i < arr_val.size(); ++i) {
                    res += n_ind + arr_val[i].dump(indent, lvl + 1) + (i + 1 < arr_val.size() ? ",\n" : "\n");
                }
                return res + ind + "]";
            }
            case Type::Object: {
                if (obj_val.empty()) return "{}";
                std::string res = "{\n";
                for (size_t i = 0; i < obj_val.size(); ++i) {
                    res += n_ind + "\"" + obj_val[i].first + "\": " + obj_val[i].second.dump(indent, lvl + 1) + (i + 1 < obj_val.size() ? ",\n" : "\n");
                }
                return res + ind + "}";
            }
        }
        return "null";
    }
};

class JsonParser {
    const std::string& src_;
    size_t pos_{0};

    void skip_ws() { while (pos_ < src_.size() && std::isspace(static_cast<unsigned char>(src_[pos_]))) ++pos_; }
    char peek() { skip_ws(); return pos_ < src_.size() ? src_[pos_] : '\0'; }
    char get() { skip_ws(); return pos_ < src_.size() ? src_[pos_++] : '\0'; }

    std::string parse_string() {
        get(); // consume '"'
        std::string s;
        while (pos_ < src_.size()) {
            char c = src_[pos_++];
            if (c == '"') return s;
            if (c == '\\' && pos_ < src_.size()) c = src_[pos_++];
            s += c;
        }
        throw SerializationError("Unterminated JSON string");
    }

    JsonValue parse_num() {
        size_t start = pos_;
        if (pos_ < src_.size() && (src_[pos_] == '-' || src_[pos_] == '+')) ++pos_;
        bool is_flt = false;
        while (pos_ < src_.size() && (std::isdigit(static_cast<unsigned char>(src_[pos_])) || src_[pos_] == '.' || src_[pos_] == 'e' || src_[pos_] == 'E' || src_[pos_] == '+' || src_[pos_] == '-')) {
            if (src_[pos_] == '.' || src_[pos_] == 'e' || src_[pos_] == 'E') is_flt = true;
            ++pos_;
        }
        std::string num = src_.substr(start, pos_ - start);
        if (is_flt) return JsonValue(std::stod(num));
        return JsonValue(static_cast<int64_t>(std::stoll(num)));
    }

public:
    explicit JsonParser(const std::string& s) : src_(s) {}

    JsonValue parse_value() {
        char c = peek();
        if (c == '"') return JsonValue(parse_string());
        if (c == '{') {
            get(); JsonValue obj = JsonValue::make_object();
            if (peek() == '}') { get(); return obj; }
            while (true) {
                std::string k = parse_string();
                if (get() != ':') throw SerializationError("Expected ':'");
                obj.obj_val.emplace_back(k, parse_value());
                char nc = peek();
                if (nc == '}') { get(); break; }
                if (nc == ',') { get(); continue; }
                throw SerializationError("Expected ',' or '}'");
            }
            return obj;
        }
        if (c == '[') {
            get(); JsonValue arr = JsonValue::make_array();
            if (peek() == ']') { get(); return arr; }
            while (true) {
                arr.arr_val.push_back(parse_value());
                char nc = peek();
                if (nc == ']') { get(); break; }
                if (nc == ',') { get(); continue; }
                throw SerializationError("Expected ',' or ']'");
            }
            return arr;
        }
        if (c == 't' || c == 'f') {
            std::string w;
            while (pos_ < src_.size() && std::isalpha(static_cast<unsigned char>(src_[pos_]))) w += src_[pos_++];
            return JsonValue(w == "true");
        }
        if (c == 'n') {
            while (pos_ < src_.size() && std::isalpha(static_cast<unsigned char>(src_[pos_]))) ++pos_;
            return JsonValue();
        }
        if (c == '-' || std::isdigit(static_cast<unsigned char>(c))) return parse_num();
        throw SerializationError("Unexpected token in JSON");
    }
};

template <typename T>
struct JsonConverter {
    static JsonValue to_json(const T& v) {
        if constexpr (std::is_same_v<T, std::string>) return JsonValue(v);
        else if constexpr (std::is_same_v<T, bool>) return JsonValue(v);
        else if constexpr (std::is_integral_v<T>) return JsonValue(static_cast<int64_t>(v));
        else if constexpr (std::is_floating_point_v<T>) return JsonValue(static_cast<double>(v));
        else { std::ostringstream ss; ss << v; return JsonValue(ss.str()); }
    }
    static T from_json(const JsonValue& j) {
        if constexpr (std::is_same_v<T, std::string>) return j.str_val;
        else if constexpr (std::is_same_v<T, bool>) return j.bool_val;
        else if constexpr (std::is_integral_v<T>) return static_cast<T>(j.int_val);
        else if constexpr (std::is_floating_point_v<T>) return static_cast<T>(j.num_val);
        else { std::istringstream ss(j.str_val); T v{}; ss >> v; return v; }
    }
};

} // namespace features

// Forward declaration
template <typename Key, typename Value, typename Hash, typename KeyEqual>
class PyDict;

// Dynamic heterogeneous nested dictionary
namespace features {

class PyValue {
public:
    using PyAnyDict = PyDict<std::string, PyValue, std::hash<std::string>, std::equal_to<std::string>>;
    using DictPtr = std::shared_ptr<PyAnyDict>;
    using ListType = std::vector<PyValue>;
    using VariantType = std::variant<std::monostate, bool, int64_t, double, std::string, ListType, DictPtr>;

private:
    VariantType data_;

public:
    PyValue() : data_(std::monostate{}) {}
    PyValue(bool b) : data_(b) {}
    PyValue(int i) : data_(static_cast<int64_t>(i)) {}
    PyValue(long long i) : data_(static_cast<int64_t>(i)) {}
    PyValue(double d) : data_(d) {}
    PyValue(const char* s) : data_(std::string(s)) {}
    PyValue(std::string s) : data_(std::move(s)) {}
    PyValue(ListType l) : data_(std::move(l)) {}
    PyValue(const PyAnyDict& d);

    bool is_dict() const noexcept { return std::holds_alternative<DictPtr>(data_); }
    bool is_list() const noexcept { return std::holds_alternative<ListType>(data_); }
    bool as_bool() const { return std::holds_alternative<bool>(data_) ? std::get<bool>(data_) : false; }
    int64_t as_int() const { return std::get<int64_t>(data_); }
    double as_double() const { return std::holds_alternative<double>(data_) ? std::get<double>(data_) : static_cast<double>(std::get<int64_t>(data_)); }
    const std::string& as_string() const { return std::get<std::string>(data_); }
    const ListType& as_list() const { return std::get<ListType>(data_); }
    ListType& as_list() { if (!is_list()) data_ = ListType{}; return std::get<ListType>(data_); }

    PyAnyDict& as_dict();
    const PyAnyDict& as_dict() const;

    PyValue& operator[](const std::string& key);
    const PyValue& operator[](const std::string& key) const;

    PyValue& operator[](size_t idx) { return as_list()[idx]; }
    void append(PyValue v) { as_list().push_back(std::move(v)); }

    friend std::ostream& operator<<(std::ostream& os, const PyValue& val);
};

using PyAnyDict = PyDict<std::string, PyValue, std::hash<std::string>, std::equal_to<std::string>>;

} // namespace features

// ============================================================================
// MAIN PYDICT CLASS TEMPLATE (UNIFIED)
// ============================================================================

template <typename Key, typename Value, typename Hash = std::hash<Key>, typename KeyEqual = std::equal_to<Key>>
class PyDict : public core::HashTable<Key, Value, Hash, KeyEqual> {
public:
    using Base = core::HashTable<Key, Value, Hash, KeyEqual>;
    using NodeType = typename Base::NodeType;
    using key_type = Key;
    using mapped_type = Value;
    using iterator = dict::PyDictIterator<Key, Value>;
    using const_iterator = dict::ConstPyDictIterator<Key, Value>;

private:
    features::HistoryEngine<Key, Value> history_;

public:
    PyDict() : Base() {}
    explicit PyDict(size_t cap, const Hash& h = Hash(), const KeyEqual& eq = KeyEqual()) : Base(cap, h, eq) {}
    PyDict(std::initializer_list<std::pair<Key, Value>> list) : Base() {
        for (const auto& item : list) insert(item.first, item.second);
    }

    iterator begin() noexcept { return iterator(this->head_); }
    iterator end() noexcept { return iterator(nullptr); }
    const_iterator begin() const noexcept { return const_iterator(this->head_); }
    const_iterator end() const noexcept { return const_iterator(nullptr); }

    bool insert(const Key& key, const Value& val) {
        NodeType* exist = this->find_node(key);
        if (exist) {
            Value old_v = exist->value;
            exist->value = val;
            history_.record_operation(features::HistoryRecord<Key, Value>::make_update(key, old_v, val));
            return false;
        }
        this->insert_internal(key, val);
        history_.record_operation(features::HistoryRecord<Key, Value>::make_insert(key, val));
        return true;
    }

    void insert_or_assign(const Key& k, const Value& v) {
        NodeType* exist = this->find_node(k);
        if (exist) exist->value = v;
        else this->insert_internal(k, v);
    }

    bool erase(const Key& key) {
        Value old_v{};
        if (this->erase_internal(key, &old_v)) {
            history_.record_operation(features::HistoryRecord<Key, Value>::make_erase(key, old_v));
            return true;
        }
        return false;
    }

    Value get(const Key& key, const Value& dflt = Value()) const {
        NodeType* n = this->find_node(key);
        return n ? n->value : dflt;
    }

    Value& operator[](const Key& key) {
        NodeType* n = this->find_node(key);
        if (n) return n->value;
        insert(key, Value());
        return this->find_node(key)->value;
    }

    const Value& operator[](const Key& key) const { return Base::at(key); }

    void clear() {
        if (this->empty()) return;
        history_.record_operation(features::HistoryRecord<Key, Value>::make_clear(items()));
        this->clear_internal();
    }

    PyDict copy() const { return PyDict(*this); }

    Value& setdefault(const Key& key, const Value& dflt = Value()) {
        NodeType* n = this->find_node(key);
        if (n) return n->value;
        insert(key, dflt);
        return this->find_node(key)->value;
    }

    Value pop(const Key& key) {
        Value v{};
        if (!this->erase_internal(key, &v)) throw KeyError::from_key(key);
        history_.record_operation(features::HistoryRecord<Key, Value>::make_erase(key, v));
        return v;
    }

    Value pop(const Key& key, const Value& dflt) {
        Value v{};
        if (!this->erase_internal(key, &v)) return dflt;
        history_.record_operation(features::HistoryRecord<Key, Value>::make_erase(key, v));
        return v;
    }

    std::pair<Key, Value> popitem() {
        if (this->empty() || !this->tail_) throw KeyError("popitem(): dictionary is empty");
        Key k = this->tail_->key;
        Value v = this->tail_->value;
        erase(k);
        return {k, v};
    }

    dict::KeysView<Key, Value> keys() const noexcept { return {this->head_, this->size_}; }
    dict::ValuesView<Key, Value> values() noexcept { return {this->head_, this->size_}; }
    dict::ItemsView<Key, Value> items() noexcept { return {this->head_, this->size_}; }
    dict::ItemsView<Key, Value> items() const noexcept { return {this->head_, this->size_}; }

    void update(const PyDict& other) { for (const auto& [k, v] : other) insert(k, v); }
    void update(std::initializer_list<std::pair<Key, Value>> l) { for (const auto& [k, v] : l) insert(k, v); }

    PyDict operator+(const PyDict& o) const { PyDict r = *this; r.update(o); return r; }
    PyDict& operator+=(const PyDict& o) { update(o); return *this; }
    PyDict operator|(const PyDict& o) const { return *this + o; }
    PyDict& operator|=(const PyDict& o) { update(o); return *this; }

    PyDict operator-(const PyDict& o) const {
        PyDict r;
        for (const auto& [k, v] : *this) if (!o.contains(k)) r.insert(k, v);
        return r;
    }
    PyDict& operator-=(const PyDict& o) { for (const auto& [k, v] : o) erase(k); return *this; }

    template <typename Compare = std::less<Key>>
    std::vector<std::pair<Key, Value>> sortByKey(bool asc = true, Compare comp = Compare()) {
        std::vector<NodeType*> nodes;
        for (auto curr = this->head_; curr; curr = curr->next_global) nodes.push_back(curr);
        std::sort(nodes.begin(), nodes.end(), [&comp, asc](NodeType* a, NodeType* b) {
            return asc ? comp(a->key, b->key) : comp(b->key, a->key);
        });
        relink_nodes(nodes);
        return items();
    }

    template <typename Compare = std::less<Value>>
    std::vector<std::pair<Key, Value>> sortByValue(bool asc = true, Compare comp = Compare()) {
        std::vector<NodeType*> nodes;
        for (auto curr = this->head_; curr; curr = curr->next_global) nodes.push_back(curr);
        std::sort(nodes.begin(), nodes.end(), [&comp, asc](NodeType* a, NodeType* b) {
            return asc ? comp(a->value, b->value) : comp(b->value, a->value);
        });
        relink_nodes(nodes);
        return items();
    }

    std::vector<Key> findValue(const Value& val) const {
        std::vector<Key> res;
        for (const auto& [k, v] : *this) if (v == val) res.push_back(k);
        return res;
    }

    bool undo() { return history_.perform_undo(*this); }
    bool redo() { return history_.perform_redo(*this); }
    void printHistory(std::ostream& os = std::cout) const { history_.print_history(os); }
    void enableHistory(bool s) noexcept { history_.enable(s); }

    std::string toJSONString(int indent = 2) const {
        features::JsonValue root = features::JsonValue::make_object();
        for (const auto& [k, v] : *this) {
            std::ostringstream ss; ss << k;
            root.obj_val.emplace_back(ss.str(), features::JsonConverter<Value>::to_json(v));
        }
        return root.dump(indent);
    }

    void fromJSONString(const std::string& str) {
        features::JsonParser p(str);
        features::JsonValue root = p.parse_value();
        clear();
        for (const auto& pair : root.obj_val) {
            Key k{};
            if constexpr (std::is_same_v<Key, std::string>) k = pair.first;
            else { std::istringstream ss(pair.first); ss >> k; }
            insert(k, features::JsonConverter<Value>::from_json(pair.second));
        }
    }

    void saveToJSON(const std::string& path) const {
        std::ofstream ofs(path);
        if (!ofs) throw SerializationError("Could not write: " + path);
        ofs << toJSONString();
    }

    void loadFromJSON(const std::string& path) {
        std::ifstream ifs(path);
        if (!ifs) throw SerializationError("Could not read: " + path);
        std::stringstream ss; ss << ifs.rdbuf();
        fromJSONString(ss.str());
    }

    bool operator==(const PyDict& o) const {
        if (this->size() != o.size()) return false;
        for (const auto& [k, v] : *this) {
            auto* n = o.find_node(k);
            if (!n || !(n->value == v)) return false;
        }
        return true;
    }

    friend std::ostream& operator<<(std::ostream& os, const PyDict& dict) {
        os << "{";
        bool first = true;
        for (const auto& [k, v] : dict) {
            if (!first) os << ", "; first = false;
            if constexpr (std::is_same_v<Key, std::string>) os << "'" << k << "'";
            else if constexpr (is_streamable<Key>::value) os << k;
            else os << "<key>";
            os << ": ";
            if constexpr (std::is_same_v<Value, std::string>) os << "'" << v << "'";
            else if constexpr (is_streamable<Value>::value) os << v;
            else os << "<value>";
        }
        return os << "}";
    }

    friend std::istream& operator>>(std::istream& is, PyDict& dict) {
        char c;
        while (is >> c && c != '{') {}
        if (!is) return is;
        dict.clear();
        char nc;
        if (!(is >> nc) || nc == '}') return is;
        is.putback(nc);
        while (is) {
            Key k{}; Value v{};
            if constexpr (std::is_same_v<Key, std::string>) {
                std::string ks; char qc; if (!(is >> qc)) break;
                if (qc == '\'' || qc == '"') {
                    char ch; while (is.get(ch) && ch != qc) { if (ch == '\\' && is.peek() != EOF) is.get(ch); ks += ch; }
                } else {
                    ks += qc; char ch; while (is.get(ch) && ch != ':' && !std::isspace(static_cast<unsigned char>(ch))) ks += ch;
                    if (ch == ':') is.putback(ch);
                }
                k = ks;
            } else { if (!(is >> k)) break; }

            char colon; while (is >> colon && colon != ':') {}

            if constexpr (std::is_same_v<Value, std::string>) {
                std::string vs; char qc; if (!(is >> qc)) break;
                if (qc == '\'' || qc == '"') {
                    char ch; while (is.get(ch) && ch != qc) { if (ch == '\\' && is.peek() != EOF) is.get(ch); vs += ch; }
                } else {
                    vs += qc; char ch; while (is.get(ch) && ch != ',' && ch != '}' && !std::isspace(static_cast<unsigned char>(ch))) vs += ch;
                    if (ch == ',' || ch == '}') is.putback(ch);
                }
                v = vs;
            } else { if (!(is >> v)) break; }

            dict.insert(k, v);
            char sep; while (is >> sep && sep != ',' && sep != '}') {}
            if (sep == '}' || !is) break;
        }
        return is;
    }

private:
    void relink_nodes(const std::vector<NodeType*>& nodes) {
        if (nodes.empty()) { this->head_ = this->tail_ = nullptr; return; }
        this->head_ = nodes.front(); this->tail_ = nodes.back();
        nodes.front()->prev_global = nullptr;
        for (size_t i = 0; i < nodes.size(); ++i) {
            if (i > 0) nodes[i]->prev_global = nodes[i - 1];
            if (i + 1 < nodes.size()) nodes[i]->next_global = nodes[i + 1];
            else nodes[i]->next_global = nullptr;
        }
    }
};

// Inline implementations for dynamic PyValue
namespace features {

inline PyValue::PyValue(const PyAnyDict& d) : data_(std::make_shared<PyAnyDict>(d)) {}
inline PyAnyDict& PyValue::as_dict() {
    if (!is_dict() || !std::get<DictPtr>(data_)) data_ = std::make_shared<PyAnyDict>();
    return *std::get<DictPtr>(data_);
}
inline const PyAnyDict& PyValue::as_dict() const { return *std::get<DictPtr>(data_); }
inline PyValue& PyValue::operator[](const std::string& k) { return as_dict()[k]; }
inline const PyValue& PyValue::operator[](const std::string& k) const { return as_dict()[k]; }

inline std::ostream& operator<<(std::ostream& os, const PyValue& val) {
    if (std::holds_alternative<std::monostate>(val.data_)) os << "None";
    else if (val.is_dict()) os << val.as_dict();
    else if (val.is_list()) {
        os << "[";
        bool f = true;
        for (const auto& item : val.as_list()) { if (!f) os << ", "; f = false; os << item; }
        os << "]";
    } else if (std::holds_alternative<std::string>(val.data_)) os << "'" << val.as_string() << "'";
    else if (std::holds_alternative<bool>(val.data_)) os << (val.as_bool() ? "True" : "False");
    else if (std::holds_alternative<int64_t>(val.data_)) os << val.as_int();
    else if (std::holds_alternative<double>(val.data_)) os << val.as_double();
    return os;
}

} // namespace features

} // namespace pydictpp

// Global convenience alias
template <typename Key, typename Value, typename Hash = std::hash<Key>, typename KeyEqual = std::equal_to<Key>>
using pydict = pydictpp::PyDict<Key, Value, Hash, KeyEqual>;

using pydictpp::PyDict;
using pydictpp::KeyError;
using pydictpp::ValueError;
using pydictpp::PyDictException;
using pydictpp::features::PyValue;
using pydictpp::features::PyAnyDict;

#endif // PYDICT_STANDALONE_HPP


#include <iostream>
#include <string>
// PyDict++ Library included above

using namespace std;

int main() {
    cout << "=========================================================\n";
    cout << "                  PYDICT++ SHOWCASE                      \n";
    cout << "=========================================================\n\n";

    // 1. Core Python Dictionary CRUD
    cout << "1. Core Operations:\n";
    pydict<string, double> grocery = {
        {"apples", 2.99},
        {"milk", 3.49},
        {"bread", 2.25}
    };
    grocery["eggs"] = 4.19;
    cout << "   Dictionary: " << grocery << "\n";
    cout << "   Contains 'milk': " << (grocery.contains("milk") ? "Yes" : "No") << "\n";
    cout << "   Safe get('cheese', 0.0): $" << grocery.get("cheese", 0.0) << "\n";
    cout << "   Popped last item: " << grocery.popitem().first << "\n\n";

    // 2. Views and Range-based Loop (Insertion Order)
    cout << "2. Views & Iteration in Insertion Order:\n";
    cout << "   Keys:   " << grocery.keys() << "\n";
    cout << "   Values: " << grocery.values() << "\n";
    cout << "   Items:  " << grocery.items() << "\n";
    for (const auto& [item, price] : grocery) {
        cout << "     * " << item << " -> $" << price << "\n";
    }
    cout << "\n";

    // 3. Dictionary Merge & Difference
    cout << "3. Merge (+) and Difference (-):\n";
    pydict<string, int> storeA = {{"laptops", 10}, {"keyboards", 25}};
    pydict<string, int> storeB = {{"keyboards", 50}, {"monitors", 15}};
    auto merged = storeA | storeB;
    auto diff = storeA - storeB;
    cout << "   Merged:     " << merged << "\n";
    cout << "   Difference: " << diff << "\n\n";

    // 4. Undo / Redo & Operation History
    cout << "4. Undo / Redo System:\n";
    pydict<string, int> game = {{"score", 100}, {"level", 1}};
    game.insert("score", 250); // Updated score to 250
    cout << "   Before undo: " << game << "\n";
    game.undo(); // Undoes to 100
    cout << "   After undo:  " << game << "\n";
    game.redo(); // Redoes to 250
    cout << "   After redo:  " << game << "\n\n";

    // 5. In-Place Sorting
    cout << "5. In-Place Sorting:\n";
    pydict<string, int> students = {{"Charlie", 82}, {"Alice", 98}, {"Bob", 74}};
    students.sortByKey(true); // Alphabetical
    cout << "   Sorted by key:   " << students << "\n";
    students.sortByValue(false); // Ranked
    cout << "   Sorted by value: " << students << "\n\n";

    // 6. JSON Export & Import
    cout << "6. JSON Round-Trip:\n";
    string json = students.toJSONString(2);
    cout << "   Exported JSON:\n" << json << "\n";
    pydict<string, int> from_json;
    from_json.fromJSONString(json);
    cout << "   Restored from JSON: " << from_json << "\n\n";

    // 7. Dynamic Heterogeneous Nested Dict
    cout << "7. Dynamic Nested Dictionary:\n";
    PyAnyDict config;
    config["server"]["host"] = "127.0.0.1";
    config["server"]["port"] = 8080;
    config["active"] = true;
    cout << "   Config: " << config << "\n";
    cout << "   Host: " << config["server"]["host"].as_string() << "\n\n";

    // 8. Stream I/O (like iostream)
    cout << "8. Stream I/O:\n";
    stringstream ss("{'book': 15, 'pen': 3}");
    pydict<string, int> stationery;
    ss >> stationery; // Stream extraction
    cout << "   Read from stream (cin >> dict): " << stationery << "\n";
    cout << "   Written to stream (cout << dict): " << stationery << "\n\n";

    cout << "=========================================================\n";
    cout << "                  ALL TESTS COMPLETED!                   \n";
    cout << "=========================================================\n";
    return 0;
}

