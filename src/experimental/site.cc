// SPDX-FileCopyrightText: 2026 The ng-log contributors
//
// SPDX-License-Identifier: BSD-3-Clause
//
// Author: Sergiu Deitsch

// Keeps the state of call sites, such as the counters of rate limits.

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <source_location>
#include <string>
#include <string_view>
#include <typeindex>
#include <typeinfo>
#include <unordered_map>
#include <utility>

#include "context.h"
#include "ng-log/experimental/attributes.h"

namespace nglog::experimental::internal {
namespace {

// Identifies a call site by its location.
struct SiteKey {
  std::string file;
  std::string function;
  std::uint_least32_t line;
  std::uint_least32_t column;

  bool operator==(const SiteKey& other) const = default;
};

// Identifies a call site without copying its location.
struct SiteKeyView {
  std::string_view file;
  std::string_view function;
  std::uint_least32_t line;
  std::uint_least32_t column;
};

struct SiteKeyHash {
  using is_transparent = void;

  std::size_t operator()(const SiteKeyView& key) const noexcept {
    std::size_t seed = std::hash<std::string_view>{}(key.file);
    Combine(seed, std::hash<std::string_view>{}(key.function));
    Combine(seed, std::hash<std::uint_least32_t>{}(key.line));
    Combine(seed, std::hash<std::uint_least32_t>{}(key.column));
    return seed;
  }

  std::size_t operator()(const SiteKey& key) const noexcept {
    return (*this)(SiteKeyView{key.file, key.function, key.line, key.column});
  }

  static void Combine(std::size_t& seed, std::size_t value) noexcept {
    // Mixes the value into the seed like boost::hash_combine.
    constexpr std::size_t kGoldenRatio = 0x9e3779b9;
    constexpr int kLeftShift = 6;
    constexpr int kRightShift = 2;
    seed ^= value + kGoldenRatio + (seed << kLeftShift) + (seed >> kRightShift);
  }
};

struct SiteKeyEqual {
  using is_transparent = void;

  static SiteKeyView View(const SiteKey& key) noexcept {
    return {key.file, key.function, key.line, key.column};
  }

  static const SiteKeyView& View(const SiteKeyView& key) noexcept {
    return key;
  }

  template <class L, class R>
  bool operator()(const L& lhs, const R& rhs) const noexcept {
    const SiteKeyView l = View(lhs);
    const SiteKeyView r = View(rhs);
    return l.file == r.file && l.function == r.function && l.line == r.line &&
           l.column == r.column;
  }
};

// States per call site looked up without locking. Covers the states of all
// built-in attributes combined.
constexpr std::size_t kStateSlots = 4;

// Locations of distinct call sites looked up without locking.
constexpr std::size_t kAliasSlots = 4096;

// Slots probed for a location. Bounding the probes keeps a lookup of a
// location that misses the table, such as once it is full, from scanning every
// slot before falling back to locking.
constexpr std::size_t kMaxAliasProbes = 64;

// State of a call site. The registry never destroys it.
class SiteImpl final : public Site {
 public:
  explicit SiteImpl(std::mutex& mutex) noexcept : mutex_{&mutex} {}

 protected:
  void* State(const std::type_info& type, Constructor construct,
              Destructor destroy) override {
    // The states of a site are only ever added, so looking up an existing one
    // needs no lock. The release store of the type publishes the state.
    for (const Slot& slot : slots_) {
      const std::type_info* const slot_type =
          slot.type.load(std::memory_order_acquire);
      if (slot_type == nullptr) {
        break;
      }
      if (*slot_type == type) {
        return slot.state;
      }
    }

    // Constructs outside the lock, since a constructor may log at a call site
    // not seen before, which takes the lock to register it. A thread that
    // loses the race to publish its state destroys it after releasing the
    // lock, since a destructor may log as well.
    void* const constructed = construct();
    void* const state = Publish(type, constructed);
    if (state != constructed) {
      destroy(constructed);
    }
    return state;
  }

 private:
  struct Slot {
    std::atomic<const std::type_info*> type{nullptr};
    void* state = nullptr;
  };

  // Publishes the constructed state unless another thread published a state of
  // the type first. Returns the published state.
  void* Publish(const std::type_info& type, void* constructed) {
    const std::scoped_lock lock{*mutex_};
    for (Slot& slot : slots_) {
      const std::type_info* const slot_type =
          slot.type.load(std::memory_order_relaxed);
      if (slot_type == nullptr) {
        slot.state = constructed;
        slot.type.store(&type, std::memory_order_release);
        return slot.state;
      }
      if (*slot_type == type) {
        return slot.state;
      }
    }
    // Sites with more states than slots fall back to locking.
    void*& state = overflow_[std::type_index{type}];
    if (state == nullptr) {
      state = constructed;
    }
    return state;
  }

  std::mutex* mutex_;
  std::array<Slot, kStateSlots> slots_;
  std::unordered_map<std::type_index, void*> overflow_;
};

// Location of a call site as given by std::source_location, whose strings are
// identified by address. Several such locations can denote the same site, for
// instance an inline function in a header included by several translation
// units.
struct SiteAlias {
  const char* file;
  const char* function;
  std::uint_least32_t line;
  std::uint_least32_t column;
  SiteImpl* site;

  bool Matches(const std::source_location& location) const noexcept {
    return file == location.file_name() &&
           function == location.function_name() && line == location.line() &&
           column == location.column();
  }
};

// Signal handlers look up the states of known sites, which only lock-free
// atomic operations allow.
static_assert(std::atomic<const SiteAlias*>::is_always_lock_free,
              "Signal-safe logging looks up call sites through atomic "
              "pointers, which are not lock-free on this platform. Disable "
              "the experimental logging functions with WITH_EXPERIMENTAL=OFF.");
static_assert(std::atomic<const std::type_info*>::is_always_lock_free,
              "Signal-safe logging looks up the states of call sites through "
              "atomic pointers, which are not lock-free on this platform. "
              "Disable the experimental logging functions with "
              "WITH_EXPERIMENTAL=OFF.");

// Owns the state of all call sites.
class SiteRegistry {
 public:
  SiteImpl& Get(const std::source_location& location) {
    // Locations already seen are looked up without locking in an insert only
    // hash table with linear probing, which is safe in signal handlers.
    const std::size_t hash = Hash(location);
    for (std::size_t i = 0; i != kMaxAliasProbes; ++i) {
      const SiteAlias* const alias =
          aliases_[(hash + i) % kAliasSlots].load(std::memory_order_acquire);
      if (alias == nullptr) {
        break;
      }
      if (alias->Matches(location)) {
        return *alias->site;
      }
    }

    const std::scoped_lock lock{mutex_};
    SiteImpl& site = FindOrInsert(location);
    for (std::size_t i = 0; i != kMaxAliasProbes; ++i) {
      std::atomic<const SiteAlias*>& slot = aliases_[(hash + i) % kAliasSlots];
      const SiteAlias* const alias = slot.load(std::memory_order_relaxed);
      if (alias == nullptr) {
        // Never destroyed, since lookups may still read the alias.
        slot.store(new SiteAlias{location.file_name(), location.function_name(),
                                 location.line(), location.column(), &site},
                   std::memory_order_release);
        break;
      }
      if (alias->Matches(location)) {
        break;
      }
    }
    // Locations whose probed slots are taken are looked up by locking.
    return site;
  }

  static SiteRegistry& Instance() {
    // Sites are never destroyed: VLOG site flags are linked into a list the
    // library keeps for the lifetime of the program.
    static SiteRegistry* const registry = new SiteRegistry;
    return *registry;
  }

 private:
  static std::size_t Hash(const std::source_location& location) noexcept {
    std::size_t seed = std::hash<const char*>{}(location.file_name());
    SiteKeyHash::Combine(seed,
                         std::hash<const char*>{}(location.function_name()));
    SiteKeyHash::Combine(seed,
                         std::hash<std::uint_least32_t>{}(location.line()));
    SiteKeyHash::Combine(seed,
                         std::hash<std::uint_least32_t>{}(location.column()));
    return seed;
  }

  SiteImpl& FindOrInsert(const std::source_location& location) {
    const SiteKeyView view{location.file_name(), location.function_name(),
                           location.line(), location.column()};
    auto it = sites_.find(view);
    if (it == sites_.end()) {
      it = sites_
               .try_emplace(
                   SiteKey{std::string{view.file}, std::string{view.function},
                           view.line, view.column},
                   mutex_)
               .first;
    }
    return it->second;
  }

  std::mutex mutex_;
  std::unordered_map<SiteKey, SiteImpl, SiteKeyHash, SiteKeyEqual> sites_;
  std::array<std::atomic<const SiteAlias*>, kAliasSlots> aliases_{};
};

}  // namespace

Site& FindSite(const std::source_location& location) {
  return SiteRegistry::Instance().Get(location);
}

}  // namespace nglog::experimental::internal
