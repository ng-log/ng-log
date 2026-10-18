// SPDX-FileCopyrightText: 2026 The ng-log contributors
//
// SPDX-License-Identifier: BSD-3-Clause
//
// Author: Sergiu Deitsch

// Attributes of the experimental logging functions.
//
// Every class type that provides at least one of the following const member
// functions is an attribute. The hooks of all attributes run phase by phase in
// the order the attributes were composed in:
//
// - `A Capture()` returns a copy of the attribute with state captured at the
//   call site, such as errno.
// - `void Configure(Context&)` selects the severity, the tag or the
//   destination of a message.
// - `bool Admit(Context&)` decides whether the message is logged, such as
//   depending on the verbosity.
// - `bool Throttle(Context&)` runs only once all attributes admitted the
//   message and rate limits it using the per-site state of the context.
// - `void Fields(FieldWriter&)` adds fields for structured sinks.
// - `void Prefix(Message&)` appends text before the formatted message.
// - `void Suffix(Message&)` appends text after the formatted message.
//
// An attribute may define a `Category` member type. Distinct attributes of the
// same category exclude each other. An attribute appears at most once unless
// its `Multiplicity` member type is Multiple. Compositions that violate these
// rules, such as two severities, are rejected at compile time.
//
// Besides the protocol, the header provides the built-in attributes, such as
// the severities, Tag, Errno, Debug, Verbose, the rate limits and the
// destinations.

#ifndef NGLOG_EXPERIMENTAL_ATTRIBUTES_H
#define NGLOG_EXPERIMENTAL_ATTRIBUTES_H

#include "ng-log/experimental/requirements.h"  // IWYU pragma: keep

#if defined(NGLOG_INTERNAL_EXPERIMENTAL_LOGGING_SUPPORTED)

#  include <array>
#  include <cerrno>
#  include <chrono>
#  include <concepts>
#  include <cstddef>
#  include <cstdint>
#  include <format>
#  include <iterator>
#  include <limits>
#  include <ratio>
#  include <source_location>
#  include <string>
#  include <string_view>
#  include <tuple>
#  include <type_traits>
#  include <typeinfo>
#  include <utility>
#  include <vector>

#  include "ng-log/export.h"
#  include "ng-log/log_severity.h"
#  include "ng-log/log_sink.h"

namespace nglog::experimental {

// State a message keeps per call site, such as the counters of a rate limit.
//
// A call site is identified by its file, function, line and column. Distinct
// instantiations of a function template therefore have distinct sites.
class Site {
 public:
  // Returns the state of type T at this site and constructs it on first
  // use. The state lives until the program ends and must be safe for
  // concurrent use.
  template <class T>
  T& state() {
    return *static_cast<T*>(State(typeid(T), &Construct<T>, &Destroy<T>));
  }

 protected:
  ~Site() = default;

  // Constructs a state object, which the site keeps until the program ends.
  using Constructor = void* (*)();
  // Destroys a state object that lost the race to be kept.
  using Destructor = void (*)(void* state);

  virtual void* State(const std::type_info& type, Constructor construct,
                      Destructor destroy) = 0;

 private:
  template <class T>
  static void* Construct() {
    return new T{};
  }

  template <class T>
  static void Destroy(void* state) {
    delete static_cast<T*>(state);
  }
};

// Multiplicity of an attribute, which its member type `Multiplicity` names. A
// single attribute appears at most once in a set, which attributes are unless
// they declare multiple multiplicity.
struct Single {};
struct Multiple {};

// Message settings the attributes inspect and modify.
class Context {
 public:
  virtual LogSeverity severity() const noexcept = 0;
  virtual void set_severity(LogSeverity severity) noexcept = 0;

  // Tag platform loggers attribute the message to, or null for the default.
  virtual const char* tag() const noexcept = 0;
  // Sets a tag that must outlive the logging call.
  virtual void set_tag(const char* tag) noexcept = 0;

  // Whether debug checks are enabled where the message is logged.
  virtual bool debug() const noexcept = 0;

  // Sets the errno the message reports and leaves to the caller. It defaults to
  // errno after the arguments were evaluated, unless an attribute such as
  // Errno captured it before them.
  virtual void set_preserved_errno(int value) noexcept = 0;

  virtual const std::source_location& location() const noexcept = 0;

  // Returns the state of the call site.
  virtual Site& site() = 0;

 protected:
  ~Context() = default;
};

// Collects the fields of a message for structured sinks.
class FieldWriter {
 public:
  // Adds a field. The name and the value are copied.
  virtual void Add(std::string_view name, std::string_view value) = 0;

 protected:
  ~FieldWriter() = default;
};

class Sink;

// Text of a message under construction.
class Message {
 public:
  virtual void Append(std::string_view text) = 0;

  // Output iterator that appends characters, such as for std::format_to(),
  // without formatting into a temporary first.
  virtual std::ostreambuf_iterator<char> out() = 0;

  // The errno the message reports, by default errno after the arguments were
  // evaluated, unless an attribute captured it before them.
  virtual int preserved_errno() const noexcept = 0;

 protected:
  ~Message() = default;
};

namespace internal {

template <class A>
concept HasConfigure = requires(const A& attribute, Context& context) {
  attribute.Configure(context);
};

template <class A>
concept HasAdmit = requires(const A& attribute, Context& context) {
  { attribute.Admit(context) } -> std::convertible_to<bool>;
};

template <class A>
concept HasThrottle = requires(const A& attribute, Context& context) {
  { attribute.Throttle(context) } -> std::convertible_to<bool>;
};

template <class A>
concept HasPrefix = requires(const A& attribute, Message& message) {
  attribute.Prefix(message);
};

template <class A>
concept HasSuffix = requires(const A& attribute, Message& message) {
  attribute.Suffix(message);
};

template <class A>
concept HasFields = requires(const A& attribute, FieldWriter& fields) {
  attribute.Fields(fields);
};

// Returns a copy of the attribute with state captured at the call site.
template <class A>
concept HasCapture = requires(const A& attribute) {
  { attribute.Capture() } -> std::same_as<A>;
};

template <class A>
constexpr A CaptureAttribute(const A& attribute) {
  if constexpr (HasCapture<A>) {
    return attribute.Capture();
  } else {
    return attribute;
  }
}

}  // namespace internal

// A type whose objects influence how a message is logged.
template <class A>
concept Attribute =
    std::is_class_v<A> && (internal::HasConfigure<A> || internal::HasAdmit<A> ||
                           internal::HasThrottle<A> || internal::HasPrefix<A> ||
                           internal::HasSuffix<A> || internal::HasFields<A>);

namespace internal {

// Type-erased hooks of an attribute.
struct Hooks {
  void (*configure)(const void* attribute, Context& context);
  bool (*admit)(const void* attribute, Context& context);
  bool (*throttle)(const void* attribute, Context& context);
  void (*prefix)(const void* attribute, Message& message);
  void (*suffix)(const void* attribute, Message& message);
  void (*fields)(const void* attribute, FieldWriter& fields);
};

// Whether a type declares a single member of the name, which taking its
// address detects. A member that does not match the hook of the same name
// would otherwise be ignored silently.
template <class A>
concept DeclaresConfigure = requires { &A::Configure; };

template <class A>
concept DeclaresAdmit = requires { &A::Admit; };

template <class A>
concept DeclaresThrottle = requires { &A::Throttle; };

template <class A>
concept DeclaresPrefix = requires { &A::Prefix; };

template <class A>
concept DeclaresSuffix = requires { &A::Suffix; };

template <class A>
concept DeclaresFields = requires { &A::Fields; };

template <class A>
constexpr Hooks MakeHooks() noexcept {
  static_assert(!DeclaresConfigure<A> || HasConfigure<A>,
                "Configure() of an attribute must be const and take a "
                "Context&, or it would never run.");
  static_assert(!DeclaresAdmit<A> || HasAdmit<A>,
                "Admit() of an attribute must be const, take a Context& and "
                "return a bool, or it would never run.");
  static_assert(!DeclaresThrottle<A> || HasThrottle<A>,
                "Throttle() of an attribute must be const, take a Context& and "
                "return a bool, or it would never run.");
  static_assert(!DeclaresPrefix<A> || HasPrefix<A>,
                "Prefix() of an attribute must be const and take a Message&, "
                "or it would never run.");
  static_assert(!DeclaresSuffix<A> || HasSuffix<A>,
                "Suffix() of an attribute must be const and take a Message&, "
                "or it would never run.");
  static_assert(!DeclaresFields<A> || HasFields<A>,
                "Fields() of an attribute must be const and take a "
                "FieldWriter&, or it would never run.");
  Hooks hooks{};
  if constexpr (HasConfigure<A>) {
    hooks.configure = [](const void* attribute, Context& context) {
      static_cast<const A*>(attribute)->Configure(context);
    };
  }
  if constexpr (HasAdmit<A>) {
    hooks.admit = [](const void* attribute, Context& context) -> bool {
      return static_cast<const A*>(attribute)->Admit(context);
    };
  }
  if constexpr (HasThrottle<A>) {
    hooks.throttle = [](const void* attribute, Context& context) -> bool {
      return static_cast<const A*>(attribute)->Throttle(context);
    };
  }
  if constexpr (HasPrefix<A>) {
    hooks.prefix = [](const void* attribute, Message& message) {
      static_cast<const A*>(attribute)->Prefix(message);
    };
  }
  if constexpr (HasSuffix<A>) {
    hooks.suffix = [](const void* attribute, Message& message) {
      static_cast<const A*>(attribute)->Suffix(message);
    };
  }
  if constexpr (HasFields<A>) {
    hooks.fields = [](const void* attribute, FieldWriter& fields) {
      static_cast<const A*>(attribute)->Fields(fields);
    };
  }
  return hooks;
}

template <class A>
inline constexpr Hooks kHooks = MakeHooks<A>();

// An attribute whose type has been erased.
struct ErasedAttribute {
  const void* attribute;
  const Hooks* hooks;
};

// Settings of the translation unit that issues a logging call.
struct CallerConfiguration {
  int strip_level;
  bool debug;
};

template <class A>
struct CategoryOf {
  using type = void;
};

template <class A>
  requires requires { typename A::Category; }
struct CategoryOf<A> {
  using type = typename A::Category;
};

template <class A>
struct MultiplicityOf {
  using type = Single;
};

template <class A>
  requires requires { typename A::Multiplicity; }
struct MultiplicityOf<A> {
  static_assert(
      std::is_same_v<typename A::Multiplicity, Single> ||
          std::is_same_v<typename A::Multiplicity, Multiple>,
      "The multiplicity of an attribute is either Single or Multiple");
  using type = typename A::Multiplicity;
};

// Whether the predicate holds for any pair of the attributes.
template <template <class, class> class Predicate, class... As>
constexpr bool AnyPair() noexcept {
  if constexpr (sizeof...(As) == 0) {
    return false;
  } else {
    return []<class Head, class... Rest>(std::type_identity<Head>,
                                         std::type_identity<Rest>...) {
      return (Predicate<Head, Rest>::value || ...) ||
             AnyPair<Predicate, Rest...>();
    }(std::type_identity<As>{}...);
  }
}

// Whether both attributes are the same attribute that is not Multiple.
template <class L, class R>
struct RepeatedSingle
    : std::bool_constant<
          std::is_same_v<L, R> &&
          std::is_same_v<typename MultiplicityOf<L>::type, Single>> {};

// Whether distinct attributes belong to the same category.
template <class L, class R>
struct SharedCategory : std::false_type {};

template <class L, class R>
  requires(!std::is_same_v<L, R> &&
           !std::is_void_v<typename CategoryOf<L>::type>)
struct SharedCategory<L, R>
    : std::is_same<typename CategoryOf<L>::type, typename CategoryOf<R>::type> {
};

// The rules of composing attributes. Their names appear in the diagnostics of
// compositions that violate them. An attribute that is not Multiple, such as a
// severity, appears at most once. Declare `using Multiplicity = Multiple;` in
// attributes that may appear more than once.
template <class... As>
concept SingleAttributesAreUnique = !AnyPair<RepeatedSingle, As...>();

// Distinct attributes of the same category, such as two destinations, are
// ambiguous.
template <class... As>
concept CategoriesAreDistinct = !AnyPair<SharedCategory, As...>();

// Attributes that can be composed into a single set.
template <class... As>
concept Compatible =
    SingleAttributesAreUnique<As...> && CategoriesAreDistinct<As...>;

struct SeverityCategory;
struct TagCategory;
struct ErrnoCategory;
struct DebugCategory;
struct DestinationCategory;
struct VerbosityCategory;
struct RateLimitCategory;
struct LogFileCategory;
struct EscalationCategory;

}  // namespace internal

// An ordered set of attributes.
template <Attribute... As>
  requires internal::Compatible<As...>
class Attributes {
 public:
  constexpr explicit Attributes(const As&... attributes)
      : attributes_{attributes...} {}

  // Erases the types of the attributes, which must outlive the result.
  constexpr std::array<internal::ErasedAttribute, sizeof...(As)> Erase()
      const noexcept {
    return std::apply(
        [](const As&... attributes) {
          return std::array<internal::ErasedAttribute, sizeof...(As)>{
              internal::ErasedAttribute{&attributes, &internal::kHooks<As>}...};
        },
        attributes_);
  }

  // Returns a copy in which the attributes with a Capture() hook captured their
  // state.
  constexpr Attributes Capture() const {
    return std::apply(
        [](const As&... attributes) {
          return Attributes{internal::CaptureAttribute(attributes)...};
        },
        attributes_);
  }

  // Returns a set that additionally contains attribute at the end.
  template <Attribute A>
    requires internal::Compatible<As..., A>
  constexpr Attributes<As..., A> Append(const A& attribute) const {
    return std::apply(
        [&attribute](const As&... attributes) {
          return Attributes<As..., A>{attributes..., attribute};
        },
        attributes_);
  }

  // Returns a set that additionally contains attribute at the beginning.
  template <Attribute A>
    requires internal::Compatible<A, As...>
  constexpr Attributes<A, As...> Prepend(const A& attribute) const {
    return std::apply(
        [&attribute](const As&... attributes) {
          return Attributes<A, As...>{attribute, attributes...};
        },
        attributes_);
  }

  // Returns a set that additionally contains the attributes of other at the
  // end.
  template <class... Bs>
    requires internal::Compatible<As..., Bs...>
  constexpr Attributes<As..., Bs...> Concat(
      const Attributes<Bs...>& other) const {
    return std::apply(
        [this](const Bs&... appended) {
          return std::apply(
              [&appended...](const As&... attributes) {
                return Attributes<As..., Bs...>{attributes..., appended...};
              },
              attributes_);
        },
        other.attributes_);
  }

 private:
  template <Attribute... Bs>
    requires internal::Compatible<Bs...>
  friend class Attributes;

  std::tuple<As...> attributes_;
};

// The deduced return types let incompatible operands fail the constraints of
// the operators, whose diagnostics name the violated rule. Compilers that form
// the return type before checking the constraints, such as Clang before 20,
// would otherwise only report the constraints of Attributes.
template <Attribute L, Attribute R>
  requires internal::Compatible<L, R>
constexpr auto operator|(const L& lhs, const R& rhs) {
  return Attributes<L, R>{lhs, rhs};
}

template <class... As, Attribute R>
  requires internal::Compatible<As..., R>
constexpr auto operator|(const Attributes<As...>& lhs, const R& rhs) {
  return lhs.Append(rhs);
}

template <Attribute L, class... As>
  requires internal::Compatible<L, As...>
constexpr auto operator|(const L& lhs, const Attributes<As...>& rhs) {
  return rhs.Prepend(lhs);
}

template <class... As, class... Bs>
  requires internal::Compatible<As..., Bs...>
constexpr auto operator|(const Attributes<As...>& lhs,
                         const Attributes<Bs...>& rhs) {
  return lhs.Concat(rhs);
}

namespace internal {

template <class T>
inline constexpr bool kIsAttributes = false;

template <class... As>
inline constexpr bool kIsAttributes<Attributes<As...>> = true;

template <Attribute A>
constexpr Attributes<A> AsAttributes(const A& attribute) {
  return Attributes<A>{attribute};
}

template <class... As>
constexpr const Attributes<As...>& AsAttributes(
    const Attributes<As...>& attributes) {
  return attributes;
}

}  // namespace internal

// An attribute or a set of attributes.
template <class T>
concept AttributeSet = Attribute<T> || internal::kIsAttributes<T>;

namespace internal {

// Terminates like a failed check. Not constexpr, so that constructing an
// invalid severity during constant evaluation fails to compile naming the
// reason.
[[noreturn]] NGLOG_EXPORT void SeverityIsOutOfRange(LogSeverity severity);

}  // namespace internal

// Severity of a message, which defaults to Info.
struct Severity {
  using Category = internal::SeverityCategory;

  // Ensures that the severity is one of the defined ones, which the prefix
  // names.
  constexpr explicit Severity(LogSeverity severity) : value{severity} {
    if (severity < NGLOG_INFO || severity > NGLOG_FATAL) {
      internal::SeverityIsOutOfRange(severity);
    }
  }

  LogSeverity value;

  NGLOG_EXPORT void Configure(Context& context) const;
};

inline constexpr Severity Info{NGLOG_INFO};
inline constexpr Severity Warning{NGLOG_WARNING};
inline constexpr Severity Error{NGLOG_ERROR};
inline constexpr Severity Fatal{NGLOG_FATAL};

// Raises the severity of a message to Fatal in debug mode and leaves it
// unchanged otherwise. `Error | FatalInDebug` behaves like LOG(DFATAL),
// regardless of the order of the attributes.
struct FatalEscalation {
  using Category = internal::EscalationCategory;

  NGLOG_EXPORT void Configure(Context& context) const;
};

inline constexpr FatalEscalation FatalInDebug{};

// Attributes the message to a tag on platforms whose system logger supports
// tags, such as Android. Defaults to the short name of the program.
struct Tag {
  using Category = internal::TagCategory;

  // Must outlive the logging call.
  const char* name;

  NGLOG_EXPORT void Configure(Context& context) const;
};

// Appends ": strerror(errno) [errno]" like PLOG(). The two-stage form of Log()
// captures errno before evaluating the arguments, and so does the braced form
// except with MSVC 19.51, which evaluates braced arguments out of order.
struct ErrnoDescription {
  using Category = internal::ErrnoCategory;

  bool captured = false;
  int value = 0;

  constexpr ErrnoDescription Capture() const noexcept {
    // errno cannot be read during constant evaluation.
    if (std::is_constant_evaluated()) {
      return *this;
    }
    return ErrnoDescription{true, errno};
  }

  NGLOG_EXPORT void Configure(Context& context) const;
  NGLOG_EXPORT void Suffix(Message& message) const;
};

inline constexpr ErrnoDescription Errno{};

// Logs only if debug checks are enabled like DLOG().
struct DebugOnly {
  using Category = internal::DebugCategory;

  NGLOG_EXPORT bool Admit(Context& context) const;
};

inline constexpr DebugOnly Debug{};

namespace internal {

// Standard integer types of counters and levels. The library instantiates the
// attributes using them for each of these types.
template <class T>
inline constexpr bool kStandardInteger =
    std::is_same_v<T, signed char> || std::is_same_v<T, unsigned char> ||
    std::is_same_v<T, short> || std::is_same_v<T, unsigned short> ||
    std::is_same_v<T, int> || std::is_same_v<T, unsigned> ||
    std::is_same_v<T, long> || std::is_same_v<T, unsigned long> ||
    std::is_same_v<T, long long> || std::is_same_v<T, unsigned long long>;

template <class T>
inline constexpr bool kDuration = false;

template <class Rep, class Period>
inline constexpr bool kDuration<std::chrono::duration<Rep, Period>> = true;

template <class T>
concept StandardInteger = kStandardInteger<T>;

template <class T>
concept ChronoDuration = kDuration<T>;

// Fails a static assertion only once the template that uses it is
// instantiated.
template <class T>
inline constexpr bool kAlwaysFalse = false;

NGLOG_EXPORT bool ThrottlePeriod(Context& context,
                                 std::chrono::nanoseconds period);

// Converts a period to nanoseconds like std::chrono::duration_cast but
// saturates periods outside the range of nanoseconds instead of overflowing.
// NaN saturates to the maximum.
template <class Rep, class Period>
constexpr std::chrono::nanoseconds SaturatingNanoseconds(
    std::chrono::duration<Rep, Period> period) noexcept {
  using std::chrono::nanoseconds;
  using Ratio = std::ratio_divide<Period, std::nano>;
  constexpr nanoseconds::rep kMax = nanoseconds::max().count();
  constexpr nanoseconds::rep kMin = nanoseconds::min().count();
  if constexpr (std::is_floating_point_v<Rep>) {
    constexpr auto kMaxScaled = static_cast<long double>(kMax);
    constexpr auto kMinScaled = static_cast<long double>(kMin);
    const long double scaled =
        static_cast<long double>(period.count()) * Ratio::num / Ratio::den;
    if (!(scaled < kMaxScaled)) {
      return nanoseconds::max();
    }
    if (!(scaled > kMinScaled)) {
      return nanoseconds::min();
    }
    // duration_cast would compute in the floating-point type, in which a count
    // just below the limit can round up to it and overflow.
    return nanoseconds{static_cast<nanoseconds::rep>(scaled)};
  } else {
    static_assert(
        Ratio::den <= std::numeric_limits<std::intmax_t>::max() / Ratio::num,
        "The remainder of dividing a count by the denominator of its "
        "ratio to nanoseconds must be convertible without overflow, "
        "which requires the product of numerator and denominator to "
        "fit std::intmax_t.");
    // Unlike duration_cast, which multiplies by the numerator first, divides
    // by the denominator first, so that only a result outside the range of
    // nanoseconds saturates. The remainder contributes the truncated rest,
    // which has the sign of the count like the quotient.
    using Wide = std::conditional_t<std::is_signed_v<Rep>, std::intmax_t,
                                    std::uintmax_t>;
    const auto count = static_cast<Wide>(period.count());
    const Wide quotient = count / static_cast<Wide>(Ratio::den);
    const auto rest =
        static_cast<std::intmax_t>(count % static_cast<Wide>(Ratio::den));
    if (std::cmp_greater(quotient, kMax / Ratio::num)) {
      return nanoseconds::max();
    }
    if (std::cmp_less(quotient, kMin / Ratio::num)) {
      return nanoseconds::min();
    }
    const auto whole = static_cast<nanoseconds::rep>(quotient) * Ratio::num;
    const auto fraction =
        static_cast<nanoseconds::rep>(rest * Ratio::num / Ratio::den);
    if (fraction > 0 && whole > kMax - fraction) {
      return nanoseconds::max();
    }
    if (fraction < 0 && whole < kMin - fraction) {
      return nanoseconds::min();
    }
    return nanoseconds{whole + fraction};
  }
}

}  // namespace internal

// Logs only if the verbosity of the site reaches the level like VLOG(). Levels
// outside the range of the verbosity flag saturate.
template <internal::StandardInteger T = int>
struct Verbose {
  using Category = internal::VerbosityCategory;

  T level;

  NGLOG_EXPORT bool Admit(Context& context) const;
};

// Limits the rate of a message by counting occurrences like LOG_EVERY_N() or
// by a period like LOG_EVERY_T(), which the argument selects, such as
// Every{10} or Every{500ms}. A number without a unit is rejected.
template <class T>
struct Every {
  static_assert(internal::kAlwaysFalse<T>,
                "Every expects a count of occurrences of a standard integer "
                "type, such as Every{10}, or a period of a std::chrono "
                "duration type, such as Every{500ms}.");
};

namespace internal {

// Rate limits that count occurrences using T. A class template of its own,
// since MSVC does not match members of a constrained partial specialization
// defined outside of it.
template <StandardInteger T>
struct EveryCount {
  using Category = RateLimitCategory;

  T n;

  NGLOG_EXPORT bool Throttle(Context& context) const;
};

template <StandardInteger T>
struct FirstCount {
  using Category = RateLimitCategory;

  T n;

  NGLOG_EXPORT bool Throttle(Context& context) const;
};

}  // namespace internal

// Logs the first and then every n-th occurrence like LOG_EVERY_N(). Counts
// occurrences using T, which follows the type of n.
template <internal::StandardInteger T>
struct Every<T> : internal::EveryCount<T> {};

// Logs at most once per period like LOG_EVERY_T().
template <internal::ChronoDuration Duration>
struct Every<Duration> {
  using Category = internal::RateLimitCategory;

  Duration period;

  bool Throttle(Context& context) const {
    // Converts like LOG_EVERY_T() converts its period in seconds.
    return internal::ThrottlePeriod(context,
                                    internal::SaturatingNanoseconds(period));
  }
};

template <class T>
Every(T) -> Every<T>;

// Logs the first n occurrences like LOG_FIRST_N(). Counts occurrences using
// T, which follows the type of n.
template <class T = int>
struct First {
  static_assert(internal::kAlwaysFalse<T>,
                "First expects a count of occurrences of a standard integer "
                "type, such as First{10}.");
};

template <internal::StandardInteger T>
struct First<T> : internal::FirstCount<T> {};

template <class T>
First(T) -> First<T>;

// Placeholder argument that formats how often the call site occurred like
// LOG_OCCURRENCES, in the counter type of the rate limit. Requires Every or
// First counting occurrences and an empty specification.
struct OccurrenceArgument {};

inline constexpr OccurrenceArgument Occurrence{};

namespace internal {

template <class T>
inline constexpr bool kCountsOccurrences = false;

template <class T>
inline constexpr bool kCountsOccurrences<Every<T>> = StandardInteger<T>;

template <class T>
inline constexpr bool kCountsOccurrences<First<T>> = StandardInteger<T>;

template <class... As>
inline constexpr bool kCountsOccurrences<Attributes<As...>> =
    (kCountsOccurrences<As> || ...);

template <class... Args>
inline constexpr bool kUsesOccurrence =
    (std::is_same_v<std::remove_cvref_t<Args>, OccurrenceArgument> || ...);

// Formats the occurrences the rate limit of the message being formatted
// counted.
NGLOG_EXPORT std::format_context::iterator FormatOccurrence(
    std::format_context& context);

// Not constexpr, so that calling it while checking a format string at compile
// time names the reason the format string is rejected.
inline void OccurrenceSpecificationIsNotSupported() noexcept {}

}  // namespace internal

// Additionally sends the message to syslog like SYSLOG().
struct SyslogDestination {
  using Category = internal::DestinationCategory;

  NGLOG_EXPORT void Configure(Context& context) const;
};

inline constexpr SyslogDestination Syslog{};

// Additionally sends the message to a sink like LOG_TO_SINK(). Repeating it
// sends the message to each sink. A structured Sink also receives the fields
// of the attributes.
struct ToSink {
  using Category = internal::DestinationCategory;
  using Multiplicity = Multiple;

  constexpr ToSink() noexcept = default;
  constexpr explicit ToSink(std::nullptr_t) noexcept {}
  constexpr explicit ToSink(LogSink* log_sink) noexcept : sink{log_sink} {}
  constexpr explicit ToSink(Sink* structured_sink) noexcept
      : structured{structured_sink} {}

  LogSink* sink = nullptr;
  Sink* structured = nullptr;

  NGLOG_EXPORT void Configure(Context& context) const;
};

// Sends the message only to the sinks of ToSink() like
// LOG_TO_SINK_BUT_NOT_TO_LOGFILE(). Composing it without ToSink() is rejected
// at compile time. A null sink, like with the macro, logs the message nowhere.
struct LogFileExclusion {
  using Category = internal::LogFileCategory;

  NGLOG_EXPORT void Configure(Context& context) const;
};

inline constexpr LogFileExclusion SkipLogFile{};

// Additionally stores the message text in a string like LOG_TO_STRING().
// Repeating it stores the text in each string.
struct ToString {
  using Category = internal::DestinationCategory;
  using Multiplicity = Multiple;

  std::string* text;

  NGLOG_EXPORT void Configure(Context& context) const;
};

// Appends the message text to a vector instead of logging it like
// LOG_STRING(). Repeating it appends the text to each vector. Null vectors are
// ignored, and the message is logged if all of them are null.
struct ToVector {
  using Category = internal::DestinationCategory;
  using Multiplicity = Multiple;

  std::vector<std::string>* texts;

  NGLOG_EXPORT void Configure(Context& context) const;
};

}  // namespace nglog::experimental

// Formats the occurrences of a rate-limited message like the logging macros
// stream them.
template <>
struct std::formatter<nglog::experimental::OccurrenceArgument, char> {
  constexpr std::format_parse_context::iterator parse(
      std::format_parse_context& context) {
    const auto it = context.begin();
    if (it != context.end() && *it != '}') {
      nglog::experimental::internal::OccurrenceSpecificationIsNotSupported();
    }
    return it;
  }

  std::format_context::iterator format(
      const nglog::experimental::OccurrenceArgument&,
      std::format_context& context) const {
    return nglog::experimental::internal::FormatOccurrence(context);
  }
};

#endif  // defined(NGLOG_INTERNAL_EXPERIMENTAL_LOGGING_SUPPORTED)

#endif  // NGLOG_EXPERIMENTAL_ATTRIBUTES_H
