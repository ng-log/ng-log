// SPDX-FileCopyrightText: 2024 Google Inc.
// SPDX-FileCopyrightText: 2026 The ng-log contributors
//
// SPDX-License-Identifier: BSD-3-Clause
//

#ifndef NGLOG_FLAGS_H
#define NGLOG_FLAGS_H

#include <algorithm>
#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <new>
#include <string>
#include <thread>
#include <tuple>
#include <type_traits>
#include <utility>
#include <vector>

#include "ng-log/export.h"
#include "ng-log/platform.h"

namespace nglog {

class FlagBase;
class FlagGroup;

// A file permission mode whose string form uses octal notation, e.g., 0644.
class FileMode {
 public:
  // Constant, so that flags of this type can be constant initialized.
  constexpr FileMode() noexcept = default;
  NGLOG_EXPORT explicit FileMode(std::uint32_t mode) noexcept;

  NGLOG_EXPORT std::uint32_t value() const noexcept;

 private:
  std::uint32_t mode_ = 0;
};

namespace internal {
struct HelpWriterAccess;
struct FlagRegistryAccess;
struct FlagValueAccess;

struct FlagParseError {
  std::string invalid_value;
  std::string minimum;
  std::string maximum;
  std::vector<std::string> expected_values;
};

// Selects the constant initialized constructor of a flag defined by
// NGLOG_DEFINE.
struct StaticFlagTag {
  explicit constexpr StaticFlagTag(int) noexcept {}
};

constexpr StaticFlagTag kStaticFlag{0};

// A lock without a destructor. Flags with static storage duration therefore
// remain usable while other objects are destroyed during program termination.
class FlagSpinLock {
 public:
  constexpr FlagSpinLock() noexcept = default;
  FlagSpinLock(const FlagSpinLock&) = delete;
  FlagSpinLock& operator=(const FlagSpinLock&) = delete;

  NGLOG_EXPORT void lock() noexcept;
  NGLOG_EXPORT void unlock() noexcept;

 private:
  std::atomic<bool> locked_{false};
};

// Stores the current and the default value of a flag behind a lock. Both are
// computed from the default value function on first access, so that a flag
// can be used before the dynamic initialization of the translation unit that
// defines it. The function runs without holding the lock, so that it may use
// other flags and the registry.
template <typename T>
class LockedFlagValueStorage {
 public:
  using DefaultValueFunction = T (*)();

  constexpr explicit LockedFlagValueStorage(
      DefaultValueFunction default_value) noexcept;
  explicit LockedFlagValueStorage(const T& value);
  LockedFlagValueStorage(const LockedFlagValueStorage&) = delete;
  LockedFlagValueStorage& operator=(const LockedFlagValueStorage&) = delete;

  T Get() const;
  T GetDefault() const;

  // Assigns the current value and marks it as set explicitly. The observer
  // receives the stored current value while the lock is held.
  template <typename U, typename Observer>
  void Set(U&& value, Observer&& observer);

  // Assigns the default value. The current value follows unless it was set
  // explicitly, in which case the observer is not called.
  template <typename Observer>
  void SetDefault(const T& value, Observer&& observer);

  // Passes the current value to the observer while the lock is held.
  template <typename Observer>
  void Observe(Observer&& observer) const;

  // Reports whether the current value was set explicitly.
  bool ExplicitlySet() const;

  // Assigns a previously saved current value together with whether it was set
  // explicitly. The observer receives the stored current value.
  template <typename Observer>
  void Restore(const T& value, bool explicitly_set, Observer&& observer);

  // Computes the values from the default value function unless that already
  // happened.
  void Initialize() const;
  void Destroy() noexcept;
  void LockForFork() const noexcept;
  void UnlockAfterFork() const noexcept;

 private:
  void ConstructLocked(const T& value) const;

  mutable FlagSpinLock lock_;
  DefaultValueFunction default_value_;
  mutable std::atomic<bool> initialized_{false};
  mutable T* current_ = nullptr;
  mutable T* default_ = nullptr;
  mutable bool modified_ = false;
  alignas(T) mutable unsigned char current_buffer_[sizeof(T)] = {};
  alignas(T) mutable unsigned char default_buffer_[sizeof(T)] = {};
};

template <typename T>
class FlagValueStorage {
 public:
  using DefaultValueFunction = T (*)();

  constexpr explicit FlagValueStorage(
      DefaultValueFunction default_value) noexcept;
  explicit FlagValueStorage(const T& value);

  T Get() const;
  T GetDefault() const;

  template <typename U>
  void Set(U&& value);
  void SetDefault(const T& value);
  bool ExplicitlySet() const;
  void Restore(const T& value, bool explicitly_set);

  void Initialize() const;
  void Destroy() noexcept;
  void LockForFork() const noexcept;
  void UnlockAfterFork() const noexcept;

 private:
  LockedFlagValueStorage<T> storage_;
};

// Stores values that fit into lock-free atomics. Reading does not lock. The
// default value is computed on first access without holding a lock.
template <typename T>
class AtomicFlagValueStorage {
 public:
  using DefaultValueFunction = T (*)();

  constexpr explicit AtomicFlagValueStorage(
      DefaultValueFunction default_value) noexcept;
  explicit AtomicFlagValueStorage(T value);
  AtomicFlagValueStorage(const AtomicFlagValueStorage&) = delete;
  AtomicFlagValueStorage& operator=(const AtomicFlagValueStorage&) = delete;

  T Get() const;
  T GetDefault() const;
  // Assigns the current value and marks it as set explicitly.
  void Set(T value);
  // Assigns the default value. The current value follows unless it was set
  // explicitly.
  void SetDefault(T value);
  // Reports whether the current value was set explicitly.
  bool ExplicitlySet() const;
  // Assigns a previously saved current value together with whether it was set
  // explicitly.
  void Restore(T value, bool explicitly_set);

  void Initialize() const;
  void Destroy() noexcept;
  // Completes a pending initialization and holds the lock of the writers, so
  // that a forked child never observes either half done.
  void LockForFork() const;
  void UnlockAfterFork() const noexcept;

 private:
  enum : unsigned char {
    kUninitialized,
    kInitializing,
    kReady,
  };

  DefaultValueFunction default_value_;
  // Serializes the writers, so that a default never replaces an explicitly
  // set value.
  mutable FlagSpinLock lock_;
  mutable std::atomic<unsigned char> state_;
  mutable std::atomic<T> current_;
  mutable std::atomic<T> default_;
  mutable bool modified_ = false;
};

// Tracks whether the current string is empty so that hot paths can skip the
// lock while the flag is unset.
template <>
class FlagValueStorage<std::string> {
 public:
  using DefaultValueFunction = std::string (*)();

  constexpr explicit FlagValueStorage(
      DefaultValueFunction default_value) noexcept;
  NGLOG_EXPORT explicit FlagValueStorage(const std::string& value);

  NGLOG_EXPORT std::string Get() const;
  NGLOG_EXPORT std::string GetDefault() const;

  template <typename U>
  void Set(U&& value);
  NGLOG_EXPORT void SetDefault(const std::string& value);
  NGLOG_EXPORT bool ExplicitlySet() const;
  NGLOG_EXPORT void Restore(const std::string& value, bool explicitly_set);

  NGLOG_EXPORT bool Empty() const;
  NGLOG_EXPORT void Initialize() const;
  NGLOG_EXPORT void Destroy() noexcept;
  NGLOG_EXPORT void LockForFork() const noexcept;
  NGLOG_EXPORT void UnlockAfterFork() const noexcept;

 private:
  // A zero initialized value means unknown, which is the state before the
  // values are computed.
  enum : unsigned char {
    kUnknown,
    kEmpty,
    kNotEmpty,
  };

  NGLOG_EXPORT void Record(const std::string& stored) const noexcept;

  LockedFlagValueStorage<std::string> storage_;
  mutable std::atomic<unsigned char> emptiness_{kUnknown};
};

template <>
class FlagValueStorage<bool> : public AtomicFlagValueStorage<bool> {
 public:
  using AtomicFlagValueStorage<bool>::AtomicFlagValueStorage;
};

template <>
class FlagValueStorage<std::int32_t>
    : public AtomicFlagValueStorage<std::int32_t> {
 public:
  using AtomicFlagValueStorage<std::int32_t>::AtomicFlagValueStorage;
};

template <>
class FlagValueStorage<std::uint32_t>
    : public AtomicFlagValueStorage<std::uint32_t> {
 public:
  using AtomicFlagValueStorage<std::uint32_t>::AtomicFlagValueStorage;
};

template <>
class FlagValueStorage<FileMode> : public AtomicFlagValueStorage<FileMode> {
 public:
  using AtomicFlagValueStorage<FileMode>::AtomicFlagValueStorage;
};
}  // namespace internal

// Selects the semantic style used for text written by a help message.
enum class HelpTextStyle {
  kPlain,                // Ordinary explanatory text.
  kHeading,              // A section or usage heading.
  kUsageOptions,         // The [options] placeholder in a usage header.
  kOptionPrefix,         // The -- prefix before a command-line option.
  kOption,               // A command-line option name.
  kCode,                 // A source identifier or code reference.
  kConstant,             // A named constant such as true or false.
  kEnvironmentVariable,  // An environment variable name.
  kType,                 // A flag type or type-shaped argument.
  kValue,                // A flag value or value-shaped argument.
  kExecutable,           // An executable or program name.
  kMeta,                 // A metadata reference such as file:line.
  kStream,               // An output stream such as stdout or stderr.
};

// Writes generated usage text or an explicitly styled flag description.
class HelpWriter;
// Writes one usage or flag description message.
using HelpMessageFunction = std::function<void(HelpWriter&)>;
// Writes the styled description of one flag. A function pointer keeps flags
// constant initialized.
using HelpCallback = void (*)(HelpWriter&);
struct FlagInfo;
template <typename T>
class ScopedFlag;
// Writes one flag's help entry. The callback replaces the generated entry and
// is responsible for writing all required line breaks.
using FlagHelpFunction = std::function<void(HelpWriter&, const FlagInfo&)>;

// Writes a generated usage message with the terminal's configured styling.
class NGLOG_EXPORT HelpWriter {
 public:
  HelpWriter(const HelpWriter&) = delete;
  HelpWriter& operator=(const HelpWriter&) = delete;

  // Writes unstyled text. Newlines are preserved as written.
  void Write(const std::string& text);

  // Writes text using a semantic help style. The style is omitted from output
  // when the destination does not support terminal styling.
  void Write(HelpTextStyle style, const std::string& text);

 private:
  struct Impl;
  explicit HelpWriter(Impl* impl) noexcept;

  friend struct internal::HelpWriterAccess;

  Impl* const impl_;
};

/**
 * Describes a registered command-line option without exposing its value type.
 *
 * The destructor is trivial so that flags with static storage duration stay
 * valid until the process ends. A class derived from FlagBase that registers
 * itself with RegisterFlag must call UnregisterFlag at the beginning of its
 * destructor.
 */
class NGLOG_EXPORT FlagBase {
 public:
  FlagBase(const FlagBase&) = delete;
  FlagBase& operator=(const FlagBase&) = delete;

  const char* name() const noexcept;
  const char* filename() const noexcept;
  // The plain description. It is empty when a help callback describes the
  // flag. FlagInfo::help always contains the plain text.
  const char* help() const noexcept;
  HelpCallback help_callback() const noexcept;

  virtual const char* type_name() const noexcept = 0;
  virtual bool SetFromString(const std::string& value,
                             std::string* error) const = 0;
  virtual bool SetFromString(const std::string& value, std::string* error,
                             internal::FlagParseError* details) const;
  virtual bool SetDefaultFromString(const std::string& value,
                                    std::string* error) const;
  virtual std::string CurrentValue() const = 0;
  virtual std::string DefaultValue() const = 0;

 protected:
  // The string form is rendered as ordinary explanatory text.
  constexpr FlagBase(const char* name, const char* filename,
                     const char* help) noexcept;
  // The callback form explicitly marks semantic text such as options and
  // environment variables with HelpWriter styles.
  constexpr FlagBase(const char* name, const char* filename,
                     HelpCallback help) noexcept;
  // Defaulted here because only a destructor defaulted on its first
  // declaration is trivial.
  ~FlagBase() = default;

  // Computes the values that are derived from the default value, so that the
  // default is determined when the flag is registered.
  virtual void InitializeValues() const;
  // Reports whether the current value was set explicitly, which decides
  // whether a later default replaces it.
  virtual bool ValueIsExplicitlySet() const;
  // Restores a saved current value together with whether it was set
  // explicitly.
  virtual bool RestoreFromString(const std::string& value, bool explicitly_set,
                                 std::string* error) const;
  // Holds the locks that protect the flag values while the process forks.
  virtual void LockValuesForFork() const;
  virtual void UnlockValuesAfterFork() const;

 private:
  friend struct internal::FlagRegistryAccess;

  const char* const name_;
  const char* const filename_;
  const char* const help_;
  const HelpCallback help_callback_;
};

// Owns a mandatory help group within one flag module.
class NGLOG_EXPORT FlagGroup {
 public:
  // Associates an unassigned flag with this group. Returns false for an
  // unknown flag or a flag already associated with a module or group.
  bool AddFlag(FlagBase& flag) const;

 private:
  friend class FlagModule;

  FlagGroup();
  FlagGroup(const std::string& module, const std::string& name,
            const std::shared_ptr<const void>& module_identity,
            const std::shared_ptr<const void>& identity);

  const std::string module_;
  const std::string name_;
  const std::shared_ptr<const void> module_identity_;
  const std::shared_ptr<const void> identity_;
};

// Owns the help metadata for one named module and its command-line options.
// A flag can be assigned to only one module and one of its groups. Keep the
// module object private to the library or application that defines its flags.
class NGLOG_EXPORT FlagModule {
 public:
  // Module names must be unique within a process. Duplicate names terminate
  // the process with a styled diagnostic.
  FlagModule(const std::string& name, const std::string& header,
             const std::string& environment_prefix);
  FlagModule(const FlagModule&) = delete;
  FlagModule& operator=(const FlagModule&) = delete;
  // Releases the module name and detaches the module's flags from their
  // groups. The flags themselves remain registered.
  ~FlagModule();

  // Creates or returns a named group owned by this module. An empty name
  // returns an invalid group.
  FlagGroup AddGroup(const std::string& name) const;

 private:
  const std::string name_;
  const std::shared_ptr<const void> identity_;
};

// Registers a flag with the process-wide ng-log flag registry. Registering a
// flag that is already registered terminates the process with a diagnostic.
NGLOG_EXPORT void RegisterFlag(FlagBase& flag);
// Removes a flag from the registry. A class derived from FlagBase must call it
// at the beginning of its destructor, before any state used by CurrentValue,
// DefaultValue, or SetFromString is destroyed. Calling it again has no effect.
NGLOG_EXPORT void UnregisterFlag(FlagBase& flag);

namespace internal {

// Returns the identifier of the flag's current registration, or zero if the
// flag is not registered.
NGLOG_EXPORT std::uint64_t FlagRegistration(const FlagBase* flag) noexcept;

// Orders scoped flag saves so that they can be undone in reverse order.
NGLOG_EXPORT std::uint64_t NextScopedFlagSequence() noexcept;

// Locks the registry for the lifetime of the object and reports whether the
// flag still has the given registration. A zero registration denotes a flag
// that was not registered yet. Such a flag is defined by NGLOG_DEFINE and is
// never destroyed.
class NGLOG_EXPORT RegisteredFlagLock {
 public:
  RegisteredFlagLock(const FlagBase* flag, std::uint64_t registration);
  RegisteredFlagLock(const RegisteredFlagLock&) = delete;
  RegisteredFlagLock& operator=(const RegisteredFlagLock&) = delete;
  ~RegisteredFlagLock();

  explicit operator bool() const noexcept;

 private:
  bool registered_;
};

// Records the type used by an NGLOG_DECLARE declaration. A declaration whose
// type differs from the definition would access the flag through the wrong
// type, so the registry terminates the process with a diagnostic instead.
class NGLOG_EXPORT FlagDeclaration {
 public:
  FlagDeclaration(const char* name, const char* type, const char* filename);
  FlagDeclaration(const FlagDeclaration&) = delete;
  FlagDeclaration& operator=(const FlagDeclaration&) = delete;
  ~FlagDeclaration();
};

// Reports whether a type is complete. NGLOG_DECLARE uses it to recognize the
// names of built-in flags, for which ng-log/flags.h defines a marker type.
template <typename T, typename = void>
struct IsCompleteType : std::false_type {};

template <typename T>
struct IsCompleteType<T, decltype(static_cast<void>(sizeof(T)))>
    : std::true_type {};

// Registers a flag defined by NGLOG_DEFINE during dynamic initialization and
// unregisters it during program termination or when its module is unloaded.
class NGLOG_EXPORT FlagRegistrar {
 public:
  explicit FlagRegistrar(FlagBase& flag);
  FlagRegistrar(const FlagRegistrar&) = delete;
  FlagRegistrar& operator=(const FlagRegistrar&) = delete;
  ~FlagRegistrar();

 private:
  FlagBase& flag_;
};

}  // namespace internal

// Specialize this trait to add a copyable flag type. A specialization must
// provide TypeName, Parse, and ToString with the signatures used below.
template <typename T>
struct FlagTraits;

template <>
struct NGLOG_EXPORT FlagTraits<bool> {
  static const char* TypeName() noexcept;
  static bool Parse(const std::string& value, bool* output, std::string* error);
  static bool ParseWithDetails(const std::string& value, bool* output,
                               std::string* error,
                               internal::FlagParseError* details);
  static std::string ToString(bool value);
};

template <>
struct NGLOG_EXPORT FlagTraits<std::int32_t> {
  static const char* TypeName() noexcept;
  static bool Parse(const std::string& value, std::int32_t* output,
                    std::string* error);
  static bool ParseWithDetails(const std::string& value, std::int32_t* output,
                               std::string* error,
                               internal::FlagParseError* details);
  static std::string ToString(std::int32_t value);
};

template <>
struct NGLOG_EXPORT FlagTraits<std::uint32_t> {
  static const char* TypeName() noexcept;
  static bool Parse(const std::string& value, std::uint32_t* output,
                    std::string* error);
  static bool ParseWithDetails(const std::string& value, std::uint32_t* output,
                               std::string* error,
                               internal::FlagParseError* details);
  static std::string ToString(std::uint32_t value);
};

template <>
struct NGLOG_EXPORT FlagTraits<FileMode> {
  static const char* TypeName() noexcept;
  static bool Parse(const std::string& value, FileMode* output,
                    std::string* error);
  static bool ParseWithDetails(const std::string& value, FileMode* output,
                               std::string* error,
                               internal::FlagParseError* details);
  static std::string ToString(FileMode value);
};

template <>
struct NGLOG_EXPORT FlagTraits<std::string> {
  static const char* TypeName() noexcept;
  static bool Parse(const std::string& value, std::string* output,
                    std::string* error);
  static std::string ToString(const std::string& value);
};

namespace internal {

template <typename T>
auto ParseFlagValue(const std::string& value, T* output, std::string* error,
                    FlagParseError* details, int)
    -> decltype(FlagTraits<T>::ParseWithDetails(value, output, error, details));

template <typename T>
bool ParseFlagValue(const std::string& value, T* output, std::string* error,
                    FlagParseError* details, long);

template <typename T>
bool ParseFlagValue(const std::string& value, T* output, std::string* error,
                    FlagParseError* details);

}  // namespace internal

/**
 * A typed, synchronized object for one command-line option. The object itself
 * is not the flag value. Use GetFlag to read it and SetFlag to update it.
 *
 * Define flags with static storage duration with NGLOG_DEFINE. Such a flag is
 * constant initialized and can be read and updated before its registration,
 * for example from the static initializer of another translation unit. It
 * returns its default value until it is changed. The flag is never destroyed,
 * so it remains usable during program termination. Use DynamicFlag for a flag
 * whose lifetime is bounded by a scope.
 */
template <typename T>
class Flag : public FlagBase {
 public:
  using DefaultValueFunction = T (*)();

  // Used by NGLOG_DEFINE. The flag is registered by a separate registrar.
  constexpr Flag(internal::StaticFlagTag, const char* name,
                 const char* filename, const char* help,
                 DefaultValueFunction default_value) noexcept;
  constexpr Flag(internal::StaticFlagTag, const char* name,
                 const char* filename, HelpCallback help,
                 DefaultValueFunction default_value) noexcept;
  Flag(const Flag&) = delete;
  Flag& operator=(const Flag&) = delete;
  // Trivial, so that a flag with static storage duration is never destroyed.
  // Only a destructor defaulted on its first declaration is trivial.
  ~Flag() = default;

  const char* type_name() const noexcept override;
  bool SetFromString(const std::string& value,
                     std::string* error) const override;
  bool SetFromString(const std::string& value, std::string* error,
                     internal::FlagParseError* details) const override;
  bool SetDefaultFromString(const std::string& value,
                            std::string* error) const override;
  std::string CurrentValue() const override;
  std::string DefaultValue() const override;

 protected:
  Flag(const char* name, const char* filename, const char* help,
       const T& value);
  Flag(const char* name, const char* filename, HelpCallback help,
       const T& value);

  void DestroyValues() noexcept;
  void InitializeValues() const override;
  bool ValueIsExplicitlySet() const override;
  bool RestoreFromString(const std::string& value, bool explicitly_set,
                         std::string* error) const override;
  void LockValuesForFork() const override;
  void UnlockValuesAfterFork() const override;

 private:
  friend struct internal::FlagValueAccess;
  template <typename U>
  friend class ScopedFlag;
  template <typename U>
  friend U GetFlag(const Flag<U>& flag);
  template <typename U, typename Value>
  friend auto SetFlag(Flag<U>& flag, Value&& value)
      -> std::enable_if_t<std::is_constructible<U, Value&&>::value ||
                          std::is_assignable<U&, Value&&>::value>;

  T Get() const;

  template <typename U>
  void SetValue(U&& value, std::true_type) const;
  template <typename U>
  void SetValue(U&& value, std::false_type) const;
  template <typename U>
  void Set(U&& value) const;

  mutable internal::FlagValueStorage<T> values_;
};

/**
 * A flag whose registration is bound to the lifetime of the object, for
 * example a flag created by a test or by a component that is created and
 * destroyed at run time. It is registered on construction and unregistered on
 * destruction.
 */
template <typename T>
class DynamicFlag final : public Flag<T> {
 public:
  DynamicFlag(const char* name, const char* filename, const char* help,
              const T& value);
  DynamicFlag(const char* name, const char* filename, HelpCallback help,
              const T& value);
  ~DynamicFlag();
};

// Returns a thread-safe copy of a typed flag's current value.
template <typename T>
T GetFlag(const Flag<T>& flag);

// Assigns a new value to a typed flag using its synchronization mechanism.
// The supplied value may be constructible or assignable as the flag type.
template <typename T, typename U>
auto SetFlag(Flag<T>& flag, U&& value)
    -> std::enable_if_t<std::is_constructible<T, U&&>::value ||
                        std::is_assignable<T&, U&&>::value>;

namespace internal {

struct FlagValueAccess {
  // Returns whether a string flag is empty without locking its value.
  NGLOG_EXPORT static bool Empty(const Flag<std::string>& flag);
};

}  // namespace internal

// A copy of the metadata and values reported for one registered flag.
struct FlagInfo {
  std::string name;
  std::string type;
  std::string help;
  std::string current_value;
  std::string default_value;
  std::string filename;
  // Empty means that the flag is listed without a named group.
  std::string group;
  // Callback that produces the explicitly styled description text.
  HelpMessageFunction help_function;
  // Empty means that the flag is listed without a named module.
  std::string module;
};

template <typename... Pairs>
class ScopedFlags;

// Describes one typed flag value to save and restore. The value is not
// restored if the flag was destroyed or replaced in the meantime.
template <typename T>
class ScopedFlag {
 public:
  template <typename Value>
  ScopedFlag(Flag<T>& flag, Value&& value);
  ScopedFlag(const ScopedFlag&) = delete;
  ScopedFlag& operator=(const ScopedFlag&) = delete;
  ScopedFlag(ScopedFlag&& other) noexcept(
      std::is_nothrow_move_constructible<T>::value);
  ScopedFlag& operator=(ScopedFlag&&) = delete;
  ~ScopedFlag();

 private:
  template <typename... Pairs>
  friend class ScopedFlags;

  template <typename Value>
  static T Apply(Flag<T>& flag, Value&& value);
  void Reapply();
  void Restore();

  Flag<T>* flag_;
  std::uint64_t registration_;
  T old_value_;
  bool old_explicitly_set_;
  std::uint64_t sequence_;
  T new_value_;
};

template <typename T, typename Value>
ScopedFlag<T> MakeScopedFlag(Flag<T>& flag, Value&& value);

// Saves and restores selected flags for the lifetime of the object. Construct
// it with MakeScopedFlags and one or more MakeScopedFlag values. When a flag
// appears more than once, the last value in argument order takes effect and
// the value from before the first save is restored.
template <typename... Pairs>
class ScopedFlags {
 public:
  explicit ScopedFlags(Pairs&&... pairs);
  ScopedFlags(const ScopedFlags&) = delete;
  ScopedFlags& operator=(const ScopedFlags&) = delete;
  ScopedFlags(ScopedFlags&&) = default;
  ScopedFlags& operator=(ScopedFlags&&) = delete;
  ~ScopedFlags();

 private:
  struct PendingRestore {
    std::uint64_t sequence;
    void (*restore)(void*);
    void* pair;
  };

  template <typename Pair>
  static void RestorePair(void* pair);
  template <std::size_t... Index>
  void ReapplyInOrder(std::index_sequence<Index...>);
  template <std::size_t... Index>
  void RestoreInReverse(std::index_sequence<Index...>);

  std::tuple<Pairs...> pairs_;
};

// Saves and restores all registered flags for the lifetime of the object.
template <>
class NGLOG_EXPORT ScopedFlags<> {
 public:
  ScopedFlags();
  ScopedFlags(const ScopedFlags&) = delete;
  ScopedFlags& operator=(const ScopedFlags&) = delete;
  ScopedFlags(ScopedFlags&& other) noexcept;
  ScopedFlags& operator=(ScopedFlags&&) = delete;
  ~ScopedFlags();

 private:
  // The flag, its registration, its value, and whether the value was set
  // explicitly.
  std::vector<std::tuple<const FlagBase*, std::uint64_t, std::string, bool>>
      values_;
};

template <typename... Pairs>
auto MakeScopedFlags(Pairs&&... pairs);

// Selects whether recognized arguments remain in the argument vector.
enum class FlagParsingMode {
  kKeepFlags,
  kRemoveFlags,
};

// Selects how an unrecognized long option is handled.
enum class UnknownFlagMode {
  kError,
  kIgnore,
};

// Copies all registered flag metadata into the supplied vector.
NGLOG_EXPORT void GetAllFlags(std::vector<FlagInfo>* flags);
// Gets a flag value in its canonical string representation.
NGLOG_EXPORT bool GetCommandLineOption(const char* name, std::string* value);
// Sets a flag from its canonical string representation.
NGLOG_EXPORT bool SetCommandLineOption(const char* name, const char* value);
// Sets a flag from its canonical string representation and reports a parse
// error when the operation fails.
NGLOG_EXPORT bool SetCommandLineOption(const char* name, const char* value,
                                       std::string* error);
// Sets the usage text printed before the flag list.
NGLOG_EXPORT void SetUsageMessage(const std::string& usage);
// Sets a function that writes the usage text printed before the flag list.
NGLOG_EXPORT void SetUsageMessage(const HelpMessageFunction& usage);
// Sets or clears the function that writes one flag's help entry. An empty
// function restores the generated representation. Returns false for an
// unknown flag name.
NGLOG_EXPORT bool SetFlagHelpFormatter(const char* name,
                                       const FlagHelpFunction& formatter);
// Returns the configured usage text.
NGLOG_EXPORT std::string ProgramUsage();
// Prints the usage text and all registered flags with terminal styling.
NGLOG_EXPORT void PrintCommandLineFlags();
// Parses long options and returns the number of recognized flags parsed.
// Unknown options produce a styled error with a suggestion by default. Use
// UnknownFlagMode::kIgnore when another parser owns the remaining options.
NGLOG_EXPORT std::uint32_t ParseCommandLineFlags(
    int* argc, char*** argv, FlagParsingMode mode,
    UnknownFlagMode unknown_mode = UnknownFlagMode::kError);

// Templates and constant expressions must be defined in the header. They are
// kept out of the class definitions above.

namespace internal {

template <typename T>
constexpr LockedFlagValueStorage<T>::LockedFlagValueStorage(
    DefaultValueFunction default_value) noexcept
    : default_value_(default_value) {}

template <typename T>
LockedFlagValueStorage<T>::LockedFlagValueStorage(const T& value)
    : default_value_(nullptr) {
  ConstructLocked(value);
}

template <typename T>
T LockedFlagValueStorage<T>::Get() const {
  Initialize();
  const std::lock_guard<FlagSpinLock> lock(lock_);
  return *current_;
}

template <typename T>
T LockedFlagValueStorage<T>::GetDefault() const {
  Initialize();
  const std::lock_guard<FlagSpinLock> lock(lock_);
  return *default_;
}

template <typename T>
template <typename U, typename Observer>
void LockedFlagValueStorage<T>::Set(U&& value, Observer&& observer) {
  Initialize();
  const std::lock_guard<FlagSpinLock> lock(lock_);
  *current_ = std::forward<U>(value);
  modified_ = true;
  observer(*current_);
}

template <typename T>
template <typename Observer>
void LockedFlagValueStorage<T>::SetDefault(const T& value,
                                           Observer&& observer) {
  Initialize();
  const std::lock_guard<FlagSpinLock> lock(lock_);
  *default_ = value;
  if (!modified_) {
    *current_ = value;
    observer(*current_);
  }
}

template <typename T>
template <typename Observer>
void LockedFlagValueStorage<T>::Observe(Observer&& observer) const {
  Initialize();
  const std::lock_guard<FlagSpinLock> lock(lock_);
  observer(*current_);
}

template <typename T>
bool LockedFlagValueStorage<T>::ExplicitlySet() const {
  Initialize();
  const std::lock_guard<FlagSpinLock> lock(lock_);
  return modified_;
}

template <typename T>
template <typename Observer>
void LockedFlagValueStorage<T>::Restore(const T& value, bool explicitly_set,
                                        Observer&& observer) {
  Initialize();
  const std::lock_guard<FlagSpinLock> lock(lock_);
  *current_ = value;
  modified_ = explicitly_set;
  observer(*current_);
}

template <typename T>
void LockedFlagValueStorage<T>::Initialize() const {
  if (initialized_.load(std::memory_order_acquire)) {
    return;
  }
  // Computed without the lock. Concurrent first accesses may compute the
  // default more than once, and only the first result is kept.
  const T value = default_value_();
  const std::lock_guard<FlagSpinLock> lock(lock_);
  if (current_ == nullptr) {
    ConstructLocked(value);
  }
}

template <typename T>
void LockedFlagValueStorage<T>::Destroy() noexcept {
  if (current_ != nullptr) {
    current_->~T();
    default_->~T();
    current_ = nullptr;
    default_ = nullptr;
  }
}

template <typename T>
void LockedFlagValueStorage<T>::LockForFork() const noexcept {
  lock_.lock();
}

template <typename T>
void LockedFlagValueStorage<T>::UnlockAfterFork() const noexcept {
  lock_.unlock();
}

// Publishes either both values or neither. If the second copy fails, the first
// one is destroyed again, so that a later access computes both anew.
template <typename T>
void LockedFlagValueStorage<T>::ConstructLocked(const T& value) const {
  T* const current = ::new (static_cast<void*>(current_buffer_)) T(value);
  struct Rollback {
    T* value;
    ~Rollback() {
      if (value != nullptr) {
        value->~T();
      }
    }
  } rollback{current};
  default_ = ::new (static_cast<void*>(default_buffer_)) T(value);
  rollback.value = nullptr;
  current_ = current;
  initialized_.store(true, std::memory_order_release);
}

template <typename T>
constexpr FlagValueStorage<T>::FlagValueStorage(
    DefaultValueFunction default_value) noexcept
    : storage_(default_value) {}

template <typename T>
FlagValueStorage<T>::FlagValueStorage(const T& value) : storage_(value) {}

template <typename T>
T FlagValueStorage<T>::Get() const {
  return storage_.Get();
}

template <typename T>
T FlagValueStorage<T>::GetDefault() const {
  return storage_.GetDefault();
}

template <typename T>
template <typename U>
void FlagValueStorage<T>::Set(U&& value) {
  storage_.Set(std::forward<U>(value), [](const T&) {});
}

template <typename T>
void FlagValueStorage<T>::SetDefault(const T& value) {
  storage_.SetDefault(value, [](const T&) {});
}

template <typename T>
bool FlagValueStorage<T>::ExplicitlySet() const {
  return storage_.ExplicitlySet();
}

template <typename T>
void FlagValueStorage<T>::Restore(const T& value, bool explicitly_set) {
  storage_.Restore(value, explicitly_set, [](const T&) {});
}

template <typename T>
void FlagValueStorage<T>::Initialize() const {
  storage_.Initialize();
}

template <typename T>
void FlagValueStorage<T>::Destroy() noexcept {
  storage_.Destroy();
}

template <typename T>
void FlagValueStorage<T>::LockForFork() const noexcept {
  storage_.LockForFork();
}

template <typename T>
void FlagValueStorage<T>::UnlockAfterFork() const noexcept {
  storage_.UnlockAfterFork();
}

template <typename T>
constexpr AtomicFlagValueStorage<T>::AtomicFlagValueStorage(
    DefaultValueFunction default_value) noexcept
    : default_value_(default_value),
      state_(kUninitialized),
      current_(T()),
      default_(T()) {}

template <typename T>
AtomicFlagValueStorage<T>::AtomicFlagValueStorage(T value)
    : default_value_(nullptr),
      state_(kReady),
      current_(value),
      default_(value) {}

template <typename T>
T AtomicFlagValueStorage<T>::Get() const {
  Initialize();
  return current_.load(std::memory_order_relaxed);
}

template <typename T>
T AtomicFlagValueStorage<T>::GetDefault() const {
  Initialize();
  return default_.load(std::memory_order_relaxed);
}

template <typename T>
void AtomicFlagValueStorage<T>::Set(T value) {
  Initialize();
  const std::lock_guard<FlagSpinLock> lock(lock_);
  current_.store(value, std::memory_order_relaxed);
  modified_ = true;
}

template <typename T>
void AtomicFlagValueStorage<T>::SetDefault(T value) {
  Initialize();
  const std::lock_guard<FlagSpinLock> lock(lock_);
  default_.store(value, std::memory_order_relaxed);
  if (!modified_) {
    current_.store(value, std::memory_order_relaxed);
  }
}

template <typename T>
bool AtomicFlagValueStorage<T>::ExplicitlySet() const {
  Initialize();
  const std::lock_guard<FlagSpinLock> lock(lock_);
  return modified_;
}

template <typename T>
void AtomicFlagValueStorage<T>::Restore(T value, bool explicitly_set) {
  Initialize();
  const std::lock_guard<FlagSpinLock> lock(lock_);
  current_.store(value, std::memory_order_relaxed);
  modified_ = explicitly_set;
}

template <typename T>
void AtomicFlagValueStorage<T>::Initialize() const {
  if (state_.load(std::memory_order_acquire) == kReady) {
    return;
  }
  // Computed before claiming the initialization, so that no other thread
  // waits while the default value function runs.
  const T value = default_value_();
  unsigned char expected = kUninitialized;
  if (state_.compare_exchange_strong(expected, kInitializing,
                                     std::memory_order_acq_rel)) {
    current_.store(value, std::memory_order_relaxed);
    default_.store(value, std::memory_order_relaxed);
    state_.store(kReady, std::memory_order_release);
    return;
  }
  while (state_.load(std::memory_order_acquire) != kReady) {
    std::this_thread::yield();
  }
}

template <typename T>
void AtomicFlagValueStorage<T>::Destroy() noexcept {}

template <typename T>
void AtomicFlagValueStorage<T>::LockForFork() const {
  Initialize();
  lock_.lock();
}

template <typename T>
void AtomicFlagValueStorage<T>::UnlockAfterFork() const noexcept {
  lock_.unlock();
}

constexpr FlagValueStorage<std::string>::FlagValueStorage(
    DefaultValueFunction default_value) noexcept
    : storage_(default_value) {}

template <typename U>
void FlagValueStorage<std::string>::Set(U&& value) {
  storage_.Set(std::forward<U>(value),
               [this](const std::string& stored) { Record(stored); });
}

template <typename T>
auto ParseFlagValue(const std::string& value, T* output, std::string* error,
                    FlagParseError* details, int)
    -> decltype(FlagTraits<T>::ParseWithDetails(value, output, error,
                                                details)) {
  return FlagTraits<T>::ParseWithDetails(value, output, error, details);
}

template <typename T>
bool ParseFlagValue(const std::string& value, T* output, std::string* error,
                    FlagParseError* /*details*/, long) {
  return FlagTraits<T>::Parse(value, output, error);
}

template <typename T>
bool ParseFlagValue(const std::string& value, T* output, std::string* error,
                    FlagParseError* details) {
  return ParseFlagValue(value, output, error, details, 0);
}

}  // namespace internal

constexpr FlagBase::FlagBase(const char* name, const char* filename,
                             const char* help) noexcept
    : name_(name), filename_(filename), help_(help), help_callback_() {}

constexpr FlagBase::FlagBase(const char* name, const char* filename,
                             HelpCallback help) noexcept
    : name_(name), filename_(filename), help_(""), help_callback_(help) {}

template <typename T>
constexpr Flag<T>::Flag(internal::StaticFlagTag, const char* name,
                        const char* filename, const char* help,
                        DefaultValueFunction default_value) noexcept
    : FlagBase(name, filename, help), values_(default_value) {}

template <typename T>
constexpr Flag<T>::Flag(internal::StaticFlagTag, const char* name,
                        const char* filename, HelpCallback help,
                        DefaultValueFunction default_value) noexcept
    : FlagBase(name, filename, help), values_(default_value) {}

template <typename T>
Flag<T>::Flag(const char* name, const char* filename, const char* help,
              const T& value)
    : FlagBase(name, filename, help), values_(value) {}

template <typename T>
Flag<T>::Flag(const char* name, const char* filename, HelpCallback help,
              const T& value)
    : FlagBase(name, filename, help), values_(value) {}

template <typename T>
const char* Flag<T>::type_name() const noexcept {
  return FlagTraits<T>::TypeName();
}

template <typename T>
bool Flag<T>::SetFromString(const std::string& value,
                            std::string* error) const {
  return SetFromString(value, error, nullptr);
}

template <typename T>
bool Flag<T>::SetFromString(const std::string& value, std::string* error,
                            internal::FlagParseError* details) const {
  T parsed = values_.Get();
  if (details != nullptr) {
    details->invalid_value = value;
  }
  if (!internal::ParseFlagValue(value, &parsed, error, details)) {
    return false;
  }
  values_.Set(parsed);
  return true;
}

// The current value follows the new default unless it was set explicitly
// before, for example during static initialization.
template <typename T>
bool Flag<T>::SetDefaultFromString(const std::string& value,
                                   std::string* error) const {
  T parsed = values_.GetDefault();
  if (!internal::ParseFlagValue(value, &parsed, error, nullptr)) {
    return false;
  }
  values_.SetDefault(parsed);
  return true;
}

template <typename T>
std::string Flag<T>::CurrentValue() const {
  return FlagTraits<T>::ToString(values_.Get());
}

template <typename T>
std::string Flag<T>::DefaultValue() const {
  return FlagTraits<T>::ToString(values_.GetDefault());
}

template <typename T>
void Flag<T>::DestroyValues() noexcept {
  values_.Destroy();
}

template <typename T>
void Flag<T>::InitializeValues() const {
  values_.Initialize();
}

template <typename T>
bool Flag<T>::ValueIsExplicitlySet() const {
  return values_.ExplicitlySet();
}

template <typename T>
bool Flag<T>::RestoreFromString(const std::string& value, bool explicitly_set,
                                std::string* error) const {
  T parsed = values_.Get();
  if (!internal::ParseFlagValue(value, &parsed, error, nullptr)) {
    return false;
  }
  values_.Restore(parsed, explicitly_set);
  return true;
}

template <typename T>
void Flag<T>::LockValuesForFork() const {
  values_.LockForFork();
}

template <typename T>
void Flag<T>::UnlockValuesAfterFork() const {
  values_.UnlockAfterFork();
}

template <typename T>
T Flag<T>::Get() const {
  return values_.Get();
}

template <typename T>
template <typename U>
void Flag<T>::SetValue(U&& value, std::true_type) const {
  values_.Set(T(std::forward<U>(value)));
}

template <typename T>
template <typename U>
void Flag<T>::SetValue(U&& value, std::false_type) const {
  values_.Set(std::forward<U>(value));
}

template <typename T>
template <typename U>
void Flag<T>::Set(U&& value) const {
  SetValue(std::forward<U>(value), std::is_constructible<T, U&&>{});
}

template <typename T>
DynamicFlag<T>::DynamicFlag(const char* name, const char* filename,
                            const char* help, const T& value)
    : Flag<T>(name, filename, help, value) {
  RegisterFlag(*this);
}

template <typename T>
DynamicFlag<T>::DynamicFlag(const char* name, const char* filename,
                            HelpCallback help, const T& value)
    : Flag<T>(name, filename, help, value) {
  RegisterFlag(*this);
}

template <typename T>
DynamicFlag<T>::~DynamicFlag() {
  UnregisterFlag(*this);
  this->DestroyValues();
}

template <typename T>
T GetFlag(const Flag<T>& flag) {
  return flag.Get();
}

template <typename T, typename U>
auto SetFlag(Flag<T>& flag, U&& value)
    -> std::enable_if_t<std::is_constructible<T, U&&>::value ||
                        std::is_assignable<T&, U&&>::value> {
  flag.Set(std::forward<U>(value));
}

template <typename T>
template <typename Value>
ScopedFlag<T>::ScopedFlag(Flag<T>& flag, Value&& value)
    : flag_(&flag),
      registration_(internal::FlagRegistration(&flag)),
      old_value_(GetFlag(flag)),
      old_explicitly_set_(flag.values_.ExplicitlySet()),
      sequence_(internal::NextScopedFlagSequence()),
      new_value_(Apply(flag, std::forward<Value>(value))) {}

template <typename T>
ScopedFlag<T>::ScopedFlag(ScopedFlag&& other) noexcept(
    std::is_nothrow_move_constructible<T>::value)
    : flag_(std::exchange(other.flag_, nullptr)),
      registration_(other.registration_),
      old_value_(std::move(other.old_value_)),
      old_explicitly_set_(other.old_explicitly_set_),
      sequence_(other.sequence_),
      new_value_(std::move(other.new_value_)) {}

template <typename T>
ScopedFlag<T>::~ScopedFlag() {
  Restore();
}

template <typename T>
template <typename Value>
T ScopedFlag<T>::Apply(Flag<T>& flag, Value&& value) {
  SetFlag(flag, std::forward<Value>(value));
  return GetFlag(flag);
}

template <typename T>
void ScopedFlag<T>::Reapply() {
  if (flag_ != nullptr) {
    const internal::RegisteredFlagLock lock(flag_, registration_);
    if (lock) {
      SetFlag(*flag_, new_value_);
    }
  }
}

template <typename T>
void ScopedFlag<T>::Restore() {
  if (flag_ != nullptr) {
    {
      const internal::RegisteredFlagLock lock(flag_, registration_);
      if (lock) {
        flag_->values_.Restore(old_value_, old_explicitly_set_);
      }
    }
    flag_ = nullptr;
  }
}

template <typename T, typename Value>
ScopedFlag<T> MakeScopedFlag(Flag<T>& flag, Value&& value) {
  return ScopedFlag<T>(flag, std::forward<Value>(value));
}

template <typename... Pairs>
ScopedFlags<Pairs...>::ScopedFlags(Pairs&&... pairs)
    : pairs_(std::move(pairs)...) {
  // Function arguments are evaluated in an unspecified order. Applying the
  // values again in argument order makes the effective values deterministic.
  ReapplyInOrder(std::index_sequence_for<Pairs...>{});
}

template <typename... Pairs>
ScopedFlags<Pairs...>::~ScopedFlags() {
  RestoreInReverse(std::index_sequence_for<Pairs...>{});
}

template <typename... Pairs>
template <typename Pair>
void ScopedFlags<Pairs...>::RestorePair(void* pair) {
  static_cast<Pair*>(pair)->Restore();
}

template <typename... Pairs>
template <std::size_t... Index>
void ScopedFlags<Pairs...>::ReapplyInOrder(std::index_sequence<Index...>) {
  using Expand = int[];
  static_cast<void>(Expand{0, (std::get<Index>(pairs_).Reapply(), 0)...});
}

// Undoes the saves in reverse order of construction so that a flag saved more
// than once ends up with the value it had before the first save.
template <typename... Pairs>
template <std::size_t... Index>
void ScopedFlags<Pairs...>::RestoreInReverse(std::index_sequence<Index...>) {
  std::array<PendingRestore, sizeof...(Pairs)> pending{
      {{std::get<Index>(pairs_).sequence_, &RestorePair<Pairs>,
        &std::get<Index>(pairs_)}...}};
  std::sort(pending.begin(), pending.end(),
            [](const PendingRestore& first, const PendingRestore& second) {
              return first.sequence > second.sequence;
            });
  for (const PendingRestore& entry : pending) {
    entry.restore(entry.pair);
  }
}

template <typename... Pairs>
auto MakeScopedFlags(Pairs&&... pairs) {
  return ScopedFlags<std::decay_t<Pairs>...>{std::forward<Pairs>(pairs)...};
}

}  // namespace nglog

// Defines one typed flag and registers it with the ng-log flag registry.
#ifndef NGLOG_EXPORT_FLAG
#  ifdef ng_log_EXPORTS
#    define NGLOG_EXPORT_FLAG NGLOG_EXPORT
#  else
#    define NGLOG_EXPORT_FLAG
#  endif
#endif

// Enforces constant initialization of flags where the language supports it.
#if defined(__cpp_constinit) && __cpp_constinit >= 201907L
#  define NGLOG_INTERNAL_CONSTINIT constinit
#else
#  define NGLOG_INTERNAL_CONSTINIT
#endif

#define NGLOG_INTERNAL_CONCAT_IMPL(first, second) first##second
#define NGLOG_INTERNAL_CONCAT(first, second) \
  NGLOG_INTERNAL_CONCAT_IMPL(first, second)

// Declares a flag defined elsewhere. Each declaration records its type, so that
// a declaration whose type differs from the definition is reported. The
// recording object is named after the flag and the line, which keeps repeated
// declarations of a flag within a translation unit apart. Built-in
// flags are already declared by this header with the linkage of the ng-log
// library, so declaring one again is rejected.
#define NGLOG_DECLARE(Type, name)                                         \
  static_assert(!nglog::internal::IsCompleteType<                         \
                    struct NGLOG_INTERNAL_BUILTIN_FLAG_##name>::value,    \
                "FLAGS_" #name                                            \
                " is a built-in ng-log flag that ng-log/flags.h already " \
                "declares. Remove the declaration.");                     \
  extern NGLOG_EXPORT_FLAG nglog::Flag<Type> FLAGS_##name;                \
  static const nglog::internal::FlagDeclaration NGLOG_INTERNAL_CONCAT(    \
      NGLOG_INTERNAL_FLAG_DECLARATION_##name##_, __LINE__)(               \
      #name, nglog::FlagTraits<Type>::TypeName(), __FILE__)

// The flag object is constant initialized, so that it can be used before the
// dynamic initialization of this translation unit. Its default value is
// computed on first use and the registrar adds it to the registry.
#define NGLOG_DEFINE(Type, name, value, help)                                \
  static Type NGLOG_INTERNAL_FLAG_DEFAULT_##name() { return value; }         \
  NGLOG_INTERNAL_CONSTINIT NGLOG_EXPORT_FLAG nglog::Flag<Type> FLAGS_##name( \
      nglog::internal::kStaticFlag, #name, __FILE__, help,                   \
      &NGLOG_INTERNAL_FLAG_DEFAULT_##name);                                  \
  static const nglog::internal::FlagRegistrar                                \
  NGLOG_INTERNAL_FLAG_REGISTRAR_##name(FLAGS_##name)

// The marker type lets NGLOG_DECLARE recognize the name of a built-in flag.
#define NGLOG_INTERNAL_DECLARE_BUILTIN(Type, name) \
  struct NGLOG_INTERNAL_BUILTIN_FLAG_##name {};    \
  extern NGLOG_EXPORT nglog::Flag<Type> FLAGS_##name

NGLOG_INTERNAL_DECLARE_BUILTIN(std::int32_t, logemaillevel);
NGLOG_INTERNAL_DECLARE_BUILTIN(std::int32_t, logcleansecs);

#ifdef NGLOG_OS_LINUX
NGLOG_INTERNAL_DECLARE_BUILTIN(bool, drop_log_memory);
#endif
NGLOG_INTERNAL_DECLARE_BUILTIN(std::string, alsologtoemail);
NGLOG_INTERNAL_DECLARE_BUILTIN(std::string, log_backtrace_at);
NGLOG_INTERNAL_DECLARE_BUILTIN(bool, timestamp_in_logfile_name);
NGLOG_INTERNAL_DECLARE_BUILTIN(bool, logtostdout);
NGLOG_INTERNAL_DECLARE_BUILTIN(bool, colorlogtostdout);
NGLOG_INTERNAL_DECLARE_BUILTIN(bool, logtostderr);
NGLOG_INTERNAL_DECLARE_BUILTIN(bool, alsologtostderr);
NGLOG_INTERNAL_DECLARE_BUILTIN(bool, colorlogtostderr);
NGLOG_INTERNAL_DECLARE_BUILTIN(std::int32_t, stderrthreshold);
NGLOG_INTERNAL_DECLARE_BUILTIN(bool, log_file_header);
NGLOG_INTERNAL_DECLARE_BUILTIN(bool, log_prefix);
NGLOG_INTERNAL_DECLARE_BUILTIN(bool, log_year_in_prefix);
NGLOG_INTERNAL_DECLARE_BUILTIN(std::int32_t, logbuflevel);
NGLOG_INTERNAL_DECLARE_BUILTIN(std::int32_t, logbufsecs);
NGLOG_INTERNAL_DECLARE_BUILTIN(std::int32_t, minloglevel);
NGLOG_INTERNAL_DECLARE_BUILTIN(std::string, log_dir);
NGLOG_INTERNAL_DECLARE_BUILTIN(nglog::FileMode, logfile_mode);
NGLOG_INTERNAL_DECLARE_BUILTIN(std::string, log_link);
NGLOG_INTERNAL_DECLARE_BUILTIN(std::int32_t, v);
NGLOG_INTERNAL_DECLARE_BUILTIN(std::string, vmodule);
NGLOG_INTERNAL_DECLARE_BUILTIN(std::uint32_t, max_log_size);
NGLOG_INTERNAL_DECLARE_BUILTIN(bool, stop_logging_if_full_disk);
NGLOG_INTERNAL_DECLARE_BUILTIN(bool, log_utc_time);
NGLOG_INTERNAL_DECLARE_BUILTIN(std::string, logmailer);
NGLOG_INTERNAL_DECLARE_BUILTIN(bool, symbolize_stacktrace);
NGLOG_INTERNAL_DECLARE_BUILTIN(bool, symbolize_line_info);
NGLOG_INTERNAL_DECLARE_BUILTIN(bool, symbolize_hyperlinks);
NGLOG_INTERNAL_DECLARE_BUILTIN(std::string, symbolize_file_base_path);
NGLOG_INTERNAL_DECLARE_BUILTIN(std::int32_t, addr2line_timeout_ms);

#undef NGLOG_INTERNAL_DECLARE_BUILTIN

#endif  // NGLOG_FLAGS_H
