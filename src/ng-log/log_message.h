// SPDX-FileCopyrightText: 2008-2024 Google Inc.
// SPDX-FileCopyrightText: 2026 The ng-log contributors
//
// SPDX-License-Identifier: BSD-3-Clause
//
// Declares LogMessage, which composes and dispatches a single message, and the
// helpers of failed checks. Unlike logging.h, it defines no unprefixed macros.

#ifndef NGLOG_LOG_MESSAGE_H
#define NGLOG_LOG_MESSAGE_H

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <memory>
#include <ostream>
#include <sstream>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#include "ng-log/export.h"
#include "ng-log/flags.h"
#include "ng-log/internal/styled_value.h"  // IWYU pragma: export
#include "ng-log/log_severity.h"
#include "ng-log/log_sink.h"
#include "ng-log/platform.h"

// The global value of NGLOG_STRIP_LOG. All the messages logged to
// LOG(XXX) with severity less than NGLOG_STRIP_LOG will not be displayed.
// If it can be determined at compile time that the message will not be
// printed, the statement will be compiled out.
//
// Example: to strip out all INFO and WARNING messages, use the value
// of 2 below. To make an exception for WARNING messages from a single
// file, add "#define NGLOG_STRIP_LOG 1" to that file _before_ including
// base/logging.h
#ifndef NGLOG_STRIP_LOG
#  define NGLOG_STRIP_LOG 0
#endif

// Whether debug-mode checks such as DCHECK() are compiled in.
#if defined(NDEBUG) && !defined(DCHECK_ALWAYS_ON)
#  define NGLOG_DCHECK_IS_ON() 0
#else
#  define NGLOG_DCHECK_IS_ON() 1
#endif

// GCC can be told that a certain branch is not likely to be taken (for
// instance, a CHECK failure), and use that information in static analysis.
// Giving it this information can help it optimize for the common case in
// the absence of better information (ie. -fprofile-arcs).
//
#if defined(__has_builtin)
#  if __has_builtin(__builtin_expect)
#    define NGLOG_BUILTIN_EXPECT_PRESENT
#  endif
#endif

#if !defined(NGLOG_BUILTIN_EXPECT_PRESENT) && defined(__GNUG__)
// __has_builtin is not available prior to GCC 10
#  define NGLOG_BUILTIN_EXPECT_PRESENT
#endif

#if defined(NGLOG_BUILTIN_EXPECT_PRESENT)

#  ifndef NGLOG_PREDICT_BRANCH_NOT_TAKEN
#    define NGLOG_PREDICT_BRANCH_NOT_TAKEN(x) (__builtin_expect(x, 0))
#  endif

#  ifndef NGLOG_PREDICT_FALSE
#    define NGLOG_PREDICT_FALSE(x) (__builtin_expect(x, 0))
#  endif

#  ifndef NGLOG_PREDICT_TRUE
#    define NGLOG_PREDICT_TRUE(x) (__builtin_expect(!!(x), 1))
#  endif

#else

#  ifndef NGLOG_PREDICT_BRANCH_NOT_TAKEN
#    define NGLOG_PREDICT_BRANCH_NOT_TAKEN(x) x
#  endif

#  ifndef NGLOG_PREDICT_TRUE
#    define NGLOG_PREDICT_FALSE(x) x
#  endif

#  ifndef NGLOG_PREDICT_TRUE
#    define NGLOG_PREDICT_TRUE(x) x
#  endif

#endif

#undef NGLOG_BUILTIN_EXPECT_PRESENT

namespace nglog {

namespace internal {

//! Determines whether messages of the given severity are logged at all.
inline bool IsLogSeverityEnabled(LogSeverity severity) {
  return severity == NGLOG_FATAL ||
         nglog::GetFlag(FLAGS_minloglevel) <= severity;
}

// A container for a string pointer which can be evaluated to a bool -
// true iff the pointer is nullptr.
struct CheckOpString {
  CheckOpString(std::unique_ptr<std::string> str) : str_(std::move(str)) {}
  explicit operator bool() const noexcept {
    return NGLOG_PREDICT_BRANCH_NOT_TAKEN(str_ != nullptr);
  }
  std::unique_ptr<std::string> str_;
};

// Function is overloaded for integral types to allow static const
// integrals declared in classes and not defined to be used as arguments to
// CHECK* macros. It's not encouraged though.
template <class T>
inline const T& GetReferenceableValue(const T& t) {
  return t;
}
inline char GetReferenceableValue(char t) { return t; }
inline unsigned char GetReferenceableValue(unsigned char t) { return t; }
inline signed char GetReferenceableValue(signed char t) { return t; }
inline short GetReferenceableValue(short t) { return t; }
inline unsigned short GetReferenceableValue(unsigned short t) { return t; }
inline int GetReferenceableValue(int t) { return t; }
inline unsigned int GetReferenceableValue(unsigned int t) { return t; }
inline long GetReferenceableValue(long t) { return t; }
inline unsigned long GetReferenceableValue(unsigned long t) { return t; }
inline long long GetReferenceableValue(long long t) { return t; }
inline unsigned long long GetReferenceableValue(unsigned long long t) {
  return t;
}

// This is a dummy class to define the following operator.
struct DummyClassToDefineOperator {};

// Define global operator<< to declare using ::operator<<.
// This declaration will allow use to use CHECK macros for user
// defined classes which have operator<< (e.g., stl_logging.h).
inline std::ostream& operator<<(std::ostream& out,
                                const DummyClassToDefineOperator&) {
  return out;
}

// This formats a value for a failing CHECK_XX statement.  Ordinarily,
// it uses the definition for operator<<, with a few special cases below.
template <typename T>
inline void MakeCheckOpValueString(std::ostream* os, const T& v) {
  (*os) << v;
}

// Overrides for char types provide readable values for unprintable
// characters.
template <>
NGLOG_EXPORT void MakeCheckOpValueString(std::ostream* os, const char& v);
template <>
NGLOG_EXPORT void MakeCheckOpValueString(std::ostream* os,
                                         const signed char& v);
template <>
NGLOG_EXPORT void MakeCheckOpValueString(std::ostream* os,
                                         const unsigned char& v);

// Provide printable value for nullptr_t
template <>
NGLOG_EXPORT void MakeCheckOpValueString(std::ostream* os,
                                         const std::nullptr_t& v);

// Build the error message string. Specify no inlining for code size.
template <typename T1, typename T2>
NGLOG_ATTRIBUTE_NOINLINE std::unique_ptr<std::string> MakeCheckOpString(
    const T1& v1, const T2& v2, const char* exprtext);

// A helper class for formatting "expr (V1 vs. V2)" in a CHECK_XX
// statement.  See MakeCheckOpString for sample usage.  Other
// approaches were considered: use of a template method (e.g.,
// base::BuildCheckOpString(exprtext, base::Print<T1>, &v1,
// base::Print<T2>, &v2), however this approach has complications
// related to volatile arguments and function-pointer arguments).
class NGLOG_EXPORT CheckOpMessageBuilder {
 public:
  // Inserts "exprtext" and " (" to the stream.
  explicit CheckOpMessageBuilder(const char* exprtext);
  // Releases the stream.
  ~CheckOpMessageBuilder();
  // For inserting the first variable.
  std::ostream* ForVar1() { return stream_.get(); }
  // For inserting the second variable (adds an intermediate " vs. ").
  std::ostream* ForVar2();
  // Get the result (inserts the closing ")").
  std::unique_ptr<std::string> NewString();

 private:
  std::unique_ptr<std::ostringstream> stream_;
};

template <typename T1, typename T2>
std::unique_ptr<std::string> MakeCheckOpString(const T1& v1, const T2& v2,
                                               const char* exprtext) {
  CheckOpMessageBuilder comb(exprtext);
  MakeCheckOpValueString(comb.ForVar1(), v1);
  MakeCheckOpValueString(comb.ForVar2(), v2);
  return comb.NewString();
}

// Helper functions for CHECK_OP macro.
// The (int, int) specialization works around the issue that the compiler
// will not instantiate the template version of the function on values of
// unnamed enum type - see comment below.
#define NGLOG_DEFINE_CHECK_OP_IMPL(name, op)                                 \
  template <typename T1, typename T2>                                        \
  inline std::unique_ptr<std::string> name##Impl(const T1& v1, const T2& v2, \
                                                 const char* exprtext) {     \
    if (NGLOG_PREDICT_TRUE(v1 op v2)) {                                      \
      return nullptr;                                                        \
    }                                                                        \
    return MakeCheckOpString(v1, v2, exprtext);                              \
  }                                                                          \
  inline std::unique_ptr<std::string> name##Impl(int v1, int v2,             \
                                                 const char* exprtext) {     \
    return name##Impl<int, int>(v1, v2, exprtext);                           \
  }

// We use the full name Check_EQ, Check_NE, etc. in case the file including
// base/logging.h provides its own #defines for the simpler names EQ, NE, etc.
// This happens if, for example, those are used as token names in a
// yacc grammar.
NGLOG_DEFINE_CHECK_OP_IMPL(Check_EQ, ==)
NGLOG_DEFINE_CHECK_OP_IMPL(Check_NE, !=)
NGLOG_DEFINE_CHECK_OP_IMPL(Check_LE, <=)
NGLOG_DEFINE_CHECK_OP_IMPL(Check_LT, <)
NGLOG_DEFINE_CHECK_OP_IMPL(Check_GE, >=)
NGLOG_DEFINE_CHECK_OP_IMPL(Check_GT, >)

#undef NGLOG_DEFINE_CHECK_OP_IMPL

// Helper functions for string comparisons.
// To avoid bloat, the definitions are in logging.cc.
#define NGLOG_DECLARE_CHECK_STROP_IMPL(func, expected)                   \
  NGLOG_EXPORT std::unique_ptr<std::string> Check##func##expected##Impl( \
      const char* s1, const char* s2, const char* names);

NGLOG_DECLARE_CHECK_STROP_IMPL(strcmp, true)
NGLOG_DECLARE_CHECK_STROP_IMPL(strcmp, false)
NGLOG_DECLARE_CHECK_STROP_IMPL(strcasecmp, true)
NGLOG_DECLARE_CHECK_STROP_IMPL(strcasecmp, false)

}  // namespace internal

#undef NGLOG_DECLARE_CHECK_STROP_IMPL

namespace internal {
template <bool>
struct CompileAssert {};
struct CrashReason;
}  // namespace internal

// We want the special COUNTER value available for LOG_EVERY_X()'ed messages
struct Counter_t {};
NGLOG_INLINE_VARIABLE constexpr Counter_t COUNTER{};

namespace base_logging {

// LogMessage::LogStream is a std::ostream backed by this streambuf.
// This class ignores overflow and leaves two bytes at the end of the
// buffer to allow for a '\n' and '\0'.
class NGLOG_EXPORT LogStreamBuf : public std::streambuf {
 public:
  // REQUIREMENTS: "len" must be >= 2 to account for the '\n' and '\0'.
  LogStreamBuf(char* buf, int len) { setp(buf, buf + len - 2); }

  void append(const char* data, std::size_t length) {
    std::size_t count = length;
    const std::size_t available = static_cast<std::size_t>(epptr() - pptr());
    if (count > available) count = available;
    std::memcpy(pptr(), data, count);
    pbump(static_cast<int>(count));
  }

  // This effectively ignores overflow.
  int_type overflow(int_type ch) { return ch; }

  // Legacy public ostrstream method.
  std::size_t pcount() const {
    return static_cast<std::size_t>(pptr() - pbase());
  }
  char* pbase() const { return std::streambuf::pbase(); }
};

}  // namespace base_logging

namespace internal {
struct NGLOG_NO_EXPORT LogMessageData;
struct LogMessageAccess;
class StyleRecorder;
}  // namespace internal

//
// This class more or less represents a particular log message.  You
// create an instance of LogMessage and then stream stuff to it.
// When you finish streaming to it, ~LogMessage is called and the
// full message gets streamed to the appropriate destination.
//
// You shouldn't actually use LogMessage's constructor to log things,
// though.  You should use the LOG() macro (and variants thereof)
// of ng-log/logging.h.
class NGLOG_EXPORT LogMessage {
 public:
  enum {
    // Passing kNoLogPrefix for the line number disables the
    // log-message prefix. Useful for using the LogMessage
    // infrastructure as a printing utility. See also the --log_prefix
    // flag for controlling the log-message prefix on an
    // application-wide basis.
    kNoLogPrefix = -1
  };

  // LogStream inherit from non-DLL-exported class (std::ostrstream)
  // and VC++ produces a warning for this situation.
  // However, MSDN says "C4275 can be ignored in Microsoft Visual C++
  // 2005 if you are deriving from a type in the Standard C++ Library"
  // http://msdn.microsoft.com/en-us/library/3tdb471s(VS.80).aspx
  // Let's just ignore the warning.
  NGLOG_MSVC_PUSH_DISABLE_WARNING(4275)
  class NGLOG_EXPORT LogStream : public std::ostream {
    NGLOG_MSVC_POP_WARNING()
   public:
    // In some cases, like when compiling glog as a static library with GCC and
    // linking against a Clang-built executable, this constructor will be
    // removed by the linker. We use this attribute to prevent the linker from
    // discarding it.
    NGLOG_ATTRIBUTE_USED
    LogStream(char* buf, int len, std::int64_t ctr)
        : std::ostream(nullptr),
          streambuf_(buf, len),
          ctr_(ctr),
          self_(this),
          style_recorder_(nullptr) {
      rdbuf(&streambuf_);
    }

    LogStream(LogStream&& other) noexcept
        : std::ostream(nullptr),
          streambuf_(std::move(other.streambuf_)),
          ctr_(std::exchange(other.ctr_, 0)),
          self_(this),
          style_recorder_(std::exchange(other.style_recorder_, nullptr)) {
      rdbuf(&streambuf_);
    }

    LogStream& operator=(LogStream&& other) noexcept {
      streambuf_ = std::move(other.streambuf_);
      ctr_ = std::exchange(other.ctr_, 0);
      style_recorder_ = std::exchange(other.style_recorder_, nullptr);
      rdbuf(&streambuf_);
      return *this;
    }

    std::int64_t ctr() const { return ctr_; }
    void set_ctr(std::int64_t ctr) { ctr_ = ctr; }
    LogStream* self() const { return self_; }

    void SetStyleRecorder(internal::StyleRecorder* recorder);
    void PushStyle(const internal::TextAttributes& attributes);
    void PopStyle();

    // Legacy std::streambuf methods.
    std::size_t pcount() const { return streambuf_.pcount(); }
    char* pbase() const { return streambuf_.pbase(); }
    char* str() const { return pbase(); }
    void append(const char* data, std::size_t length) {
      streambuf_.append(data, length);
    }

    LogStream(const LogStream&) = delete;
    LogStream& operator=(const LogStream&) = delete;

   private:
    base_logging::LogStreamBuf streambuf_;
    std::int64_t ctr_;  // Counter hack (for the LOG_EVERY_X() macro)
    LogStream* self_;  // Consistency check hack
    internal::StyleRecorder* style_recorder_;
  };

 public:
  // icc 8 requires this typedef to avoid an internal compiler error.
  typedef void (LogMessage::*SendMethod)();

  LogMessage(const char* file, int line, LogSeverity severity, std::int64_t ctr,
             SendMethod send_method);

  // Two special constructors that generate reduced amounts of code at
  // LOG call sites for common cases.

  // Used for LOG(INFO): Implied are:
  // severity = INFO, ctr = 0, send_method = &LogMessage::SendToLog.
  //
  // Using this constructor instead of the more complex constructor above
  // saves 19 bytes per call site.
  LogMessage(const char* file, int line);

  // Used for LOG(severity) where severity != INFO.  Implied
  // are: ctr = 0, send_method = &LogMessage::SendToLog
  //
  // Using this constructor instead of the more complex constructor above
  // saves 17 bytes per call site.
  LogMessage(const char* file, int line, LogSeverity severity);

  // Constructor to log this message to a specified sink (if not nullptr).
  // Implied are: ctr = 0, send_method = &LogMessage::SendToSinkAndLog if
  // also_send_to_log is true, send_method = &LogMessage::SendToSink otherwise.
  LogMessage(const char* file, int line, LogSeverity severity, LogSink* sink,
             bool also_send_to_log);

  // Constructor where we also give a vector<string> pointer
  // for storing the messages (if the pointer is not nullptr).
  // Implied are: ctr = 0, send_method = &LogMessage::SaveOrSendToLog.
  LogMessage(const char* file, int line, LogSeverity severity,
             std::vector<std::string>* outvec);

  // Constructor where we also give a string pointer for storing the
  // message (if the pointer is not nullptr).  Implied are: ctr = 0,
  // send_method = &LogMessage::WriteToStringAndLog.
  LogMessage(const char* file, int line, LogSeverity severity,
             std::string* message);

  // A special constructor used for check failures
  LogMessage(const char* file, int line, const internal::CheckOpString& result);

  ~LogMessage() noexcept(false);

  // Flush a buffered message to the sink set in the constructor.  Always
  // called by the destructor, it may also be called from elsewhere if
  // needed.  Only the first call is actioned; any later ones are ignored.
  void Flush();

  // An arbitrary limit on the length of a single log message.  This
  // is so that streaming can be done more efficiently.
  static const std::size_t kMaxLogMessageLen;

  // These should not be called directly outside of logging.*,
  // only passed as SendMethod arguments to other LogMessage methods:
  void SendToLog();           // Actually dispatch to the logs
  void SendToSyslogAndLog();  // Actually dispatch to syslog and the logs

  // Call abort() or similar to perform LOG(FATAL) crash.
  [[noreturn]] static void Fail();

  std::ostream& stream();

  // Appends text and its attributes independently of stream insertion.
  void AppendText(const char* text, std::size_t length,
                  internal::TextAttributes attributes = {});

  int preserved_errno() const;

  // Must be called without the log_mutex held.  (L < log_mutex)
  static std::int64_t num_messages(int severity);

  LogSeverity severity() const noexcept;
  int line() const noexcept;
  const std::thread::id& thread_id() const noexcept;
  const char* fullname() const noexcept;
  const char* basename() const noexcept;
  const LogMessageTime& time() const noexcept;

  LogMessage(const LogMessage&) = delete;
  LogMessage& operator=(const LogMessage&) = delete;

 private:
  // Fully internal SendMethod cases:
  void SendToSinkAndLog();  // Send to sink if provided and dispatch to the logs
  void SendToSink();        // Send to sink if provided, do nothing otherwise.
  bool SendToRegisteredSinks();

  // Write to string if provided and dispatch to the logs.
  void WriteToStringAndLog();

  void SaveOrSendToLog();  // Save to stringvec if provided, else to logs

  void Init(const char* file, int line, LogSeverity severity,
            void (LogMessage::*send_method)());

  // Used to fill in crash information during LOG(FATAL) failures.
  void RecordCrashReason(internal::CrashReason* reason);

  // Counts of messages sent at each priority:
  static std::atomic<std::int64_t> num_messages_[NUM_SEVERITIES];

  // We keep the data in a separate struct so that each instance of
  // LogMessage uses less stack space.
  std::unique_ptr<internal::LogMessageData> allocated_;
  internal::LogMessageData* data_;
  LogMessageTime time_;

  friend class LogDestination;
  friend struct internal::LogMessageAccess;
};

inline std::ostream& operator<<(std::ostream& os,
                                const internal::StylePush& push) {
  auto* const log_stream = dynamic_cast<LogMessage::LogStream*>(&os);
  if (log_stream != nullptr) {
    log_stream->PushStyle(push.attributes);
  }
  return os;
}

inline std::ostream& operator<<(std::ostream& os, const internal::StylePop&) {
  auto* const log_stream = dynamic_cast<LogMessage::LogStream*>(&os);
  if (log_stream != nullptr) {
    log_stream->PopStyle();
  }
  return os;
}

template <typename T>
inline std::ostream& operator<<(std::ostream& os,
                                const internal::StyledValue<T>& styled_value) {
  auto* const log_stream = dynamic_cast<LogMessage::LogStream*>(&os);
  if (log_stream == nullptr) {
    return os << styled_value.value;
  }

  log_stream->PushStyle(styled_value.attributes);
  struct StyleGuard {
    LogMessage::LogStream* stream;
    ~StyleGuard() { stream->PopStyle(); }
  } guard{log_stream};
  os << styled_value.value;
  return os;
}

// This class happens to be thread-hostile because all instances share
// a single data buffer, but since it can only be created just before
// the process dies, we don't worry so much.
class NGLOG_EXPORT LogMessageFatal : public LogMessage {
 public:
  LogMessageFatal(const char* file, int line);
  LogMessageFatal(const char* file, int line,
                  const internal::CheckOpString& result);
  [[noreturn]] ~LogMessageFatal() noexcept(false);
};

// A non-macro interface to the log facility; (useful
// when the logging level is not a compile-time constant).
inline void LogAtLevel(LogSeverity severity, std::string const& msg) {
  LogMessage(__FILE__, __LINE__, severity).stream() << msg;
}

// Allow folks to put a counter in the LOG_EVERY_X()'ed messages. This
// only works if ostream is a LogStream. If the ostream is not a
// LogStream you'll get an assert saying as much at runtime.
NGLOG_EXPORT std::ostream& operator<<(std::ostream& os, const Counter_t&);

// Derived class for PLOG*() of ng-log/logging.h.
class NGLOG_EXPORT ErrnoLogMessage : public LogMessage {
 public:
  ErrnoLogMessage(const char* file, int line, LogSeverity severity,
                  std::int64_t ctr, void (LogMessage::*send_method)());

  // Postpends ": strerror(errno) [errno]".
  ~ErrnoLogMessage();

 private:
  ErrnoLogMessage(const ErrnoLogMessage&);
  void operator=(const ErrnoLogMessage&);
};

// This class is used to explicitly ignore values in the conditional
// logging macros.  This avoids compiler warnings like "value computed
// is not used" and "statement has no effect".

namespace internal {

// Helper for CHECK_NOTNULL().
//
// In C++11, all cases can be handled by a single function. Since the value
// category of the argument is preserved (also for rvalue references),
// member initializer lists like the one below will compile correctly:
//
//   Foo()
//     : x_(CHECK_NOTNULL(MethodReturningUniquePtr())) {}
template <typename T>
T CheckNotNull(const char* file, int line, const char* names, T&& t) {
  if (t == nullptr) {
    LogMessageFatal(file, line, std::make_unique<std::string>(names));
  }
  return std::forward<T>(t);
}

struct LogMessageVoidify {
  // This has to be an operator with a precedence lower than << but
  // higher than ?:
  constexpr void operator&(std::ostream&) const noexcept {}
};

}  // namespace internal

// A class for which we define operator<<, which does nothing.
class NGLOG_EXPORT NullStream : public LogMessage::LogStream {
 public:
  // Initialize the LogStream so the messages can be written somewhere
  // (they'll never be actually displayed). This will be needed if a
  // NullStream& is implicitly converted to LogStream&, in which case
  // the overloaded NullStream::operator<< will not be invoked.
  NullStream();
  NullStream(const char* /*file*/, int /*line*/,
             const internal::CheckOpString& /*result*/);
  NullStream& stream();

 private:
  // A very short buffer for messages (which we discard anyway). This
  // will be needed if NullStream& converted to LogStream& (e.g. as a
  // result of a conditional expression).
  char message_buffer_[3];
};

// Do nothing. This operator is inline, allowing the message to be
// compiled away. The message will not be compiled away if we do
// something like (flag ? LOG(INFO) : LOG(ERROR)) << message; when
// SKIP_LOG=WARNING. In those cases, NullStream will be implicitly
// converted to LogStream and the message will be computed and then
// quietly discarded. The same holds for a severity disabled purely at
// runtime via FLAGS_minloglevel: LOG(severity) still resolves to a
// single, statically typed conditional expression, so its result type
// is fixed at compile time and the message is still computed (only its
// output is discarded) whenever LOG(severity) is used as anything other
// than the immediate left-hand side of its own "<<" chain.
template <class T>
inline NullStream& operator<<(NullStream& str, const T&) {
  return str;
}

// Similar to NullStream, but aborts the program (without stack
// trace), like LogMessageFatal.
class NGLOG_EXPORT NullStreamFatal : public NullStream {
 public:
  using NullStream::NullStream;
  // Prevent the linker from discarding the destructor.
  NGLOG_ATTRIBUTE_USED
  [[noreturn]]
  ~NullStreamFatal();
};

}  // namespace nglog

#endif  // NGLOG_LOG_MESSAGE_H
