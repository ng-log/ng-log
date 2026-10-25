# Feature Comparison

This page compares the logging features exposed by ng-log and selected logging
libraries. The comparison is based on their public interfaces.

<!-- The library set and source revisions follow the cross-library benchmark suite. -->

The libraries are:

* [ng-log](https://github.com/ng-log/ng-log)
* [spdlog](https://github.com/gabime/spdlog)
* [Abseil logging](https://github.com/abseil/abseil-cpp/tree/master/absl/log)
* [Boost.Log](https://www.boost.org/doc/libs/release/libs/log/)
* [plog](https://github.com/SergiusTheBest/plog)
* [Easylogging++](https://github.com/abumq/easyloggingpp)

## Feature matrix

Select a column heading in either table to sort that table. Library columns
sort by support status, while the feature column sorts alphabetically.

The matrix focuses on less common capabilities that distinguish the libraries.
Common baseline features such as severity levels and console output are
omitted.

### Logging features

| Feature | ng-log | spdlog | Abseil | Boost.Log | plog | Easylogging++ |
| --- | :---: | :---: | :---: | :---: | :---: | :---: |
| Format-string log statements | ✅[^1] | ✅ | ❌ | ❌ | ❌ | ❌ |
| Wide-character text output | ❌ | ⚠️[^2] | ✅ | ✅ | ✅ | ⚠️[^3] |
| Per-file or module verbosity | ✅ | ❌ | ✅ | ⚠️[^4] | ❌ | ✅ |
| Periodic or limited-count logging | ✅ | ❌ | ✅ | ❌ | ❌ | ✅ |
| Assertion and check macros | ✅ | ❌ | ✅ | ❌ | ❌ | ✅ |
| System-error logging | ✅ | ❌ | ✅ | ❌ | ❌ | ✅ |
| Structured or key-value fields | ⚠️[^5] | ❌ | ⚠️[^6] | ✅ | ❌ | ❌ |
| Named loggers or channels | ❌ | ✅ | ❌ | ✅ | ⚠️[^7] | ✅ |
| Asynchronous dispatch | ❌ | ✅ | ❌ | ✅ | ❌ | ⚠️[^8] |

### Output and sink features

| Feature | ng-log | spdlog | Abseil | Boost.Log | plog | Easylogging++ |
| --- | :---: | :---: | :---: | :---: | :---: | :---: |
| Unicode-aware file paths | ✅[^9] | ⚠️[^10] | ❌ | ✅ | ✅ | ❌ |
| Explicit POSIX long-path handling | ✅[^11] | ❌ | ❌ | ❌ | ❌ | ❌ |
| Explicit Windows extended-path handling | ✅[^12] | ❌ | ❌ | ❌ | ❌ | ❌ |
| Syslog output | ✅ | ✅ | ❌ | ✅ | ❌ | ✅ |
| Platform-specific output sinks | ⚠️[^13] | ✅ | ⚠️[^14] | ✅ | ✅ | ❌ |
| Network output sinks | ❌ | ✅ | ❌ | ⚠️[^15] | ❌ | ❌ |
| Colorized terminal output | ✅ | ✅ | ❌ | ❌ | ✅ | ✅ |
| Email output | ✅ | ❌ | ❌ | ❌ | ❌ | ❌ |

### Failure and diagnostic features

| Feature | ng-log | spdlog | Abseil | Boost.Log | plog | Easylogging++ |
| --- | :---: | :---: | :---: | :---: | :---: | :---: |
| Targeted or buffered backtrace capture | ✅ | ✅ | ✅ | ❌ | ❌ | ❌ |
| Stack trace on fatal failures | ✅ | ❌ | ✅ | ❌ | ❌ | ⚠️[^16] |
| Raw low-level logging | ✅ | ❌ | ❌ | ❌ | ❌ | ❌ |

[^1]: ng-log provides format strings through its C++20
    [experimental logging functions](logging/experimental.md), which use
    `std::format` and check format strings at compile time. The log macros
    use C++ streams, and `RAW_LOG` uses printf-style varargs.
[^2]: spdlog converts wide-character log messages to UTF-8 only when
    `SPDLOG_WCHAR_TO_UTF8_SUPPORT` is enabled.
[^3]: Easylogging++ uses wide-character log messages and streams only when
    `ELPP_UNICODE` is enabled.
[^4]: Boost.Log supports channel and attribute filters, but does not expose a
    dedicated per-file verbosity control.
[^5]: Custom attributes of the experimental logging functions attach named
    fields that structured sinks receive. The text output omits these fields.
[^6]: Abseil records streamed values in a structured encoding for log sinks,
    but log statements cannot attach named fields.
[^7]: plog's template instance IDs provide separate logger instances, but not
    a named logger or channel API.
[^8]: Easylogging++ provides asynchronous logging only as an experimental
    feature when `ELPP_EXPERIMENTAL_ASYNC` is defined.
[^9]: ng-log expects UTF-8 paths and converts them to wide-character paths on
    Windows.
[^10]: spdlog uses wide-character file paths on Windows only when
     `SPDLOG_WCHAR_FILENAMES` is enabled.
[^11]: ng-log opens paths beyond `PATH_MAX` component by component relative to
     their parent directories.
[^12]: ng-log adds the `\\?\` and `\\?\UNC\` prefixes to paths beyond
     `MAX_PATH`.
[^13]: ng-log writes to syslog, Android logcat, the Windows debugger and the
     Emscripten console, but does not expose separate platform sink classes.
[^14]: Abseil provides Android native log integration, but not a general family
     of platform-specific sink classes.
[^15]: Boost.Log provides UDP through its syslog backend, but not a general
     network sink family.
[^16]: Easylogging++ provides crash logging only when
     `ELPP_FEATURE_CRASH_LOG` is enabled.

| Mark | Meaning |
| --- | --- |
| ✅ | The inspected public API provides the feature |
| ⚠️ | The footnote explains the limitation, condition, or indirect support |
| ❌ | The feature is not provided by the compared library interface |

The ❌ mark describes the compared library interface. Applications can still
provide the capability through their own code or an external integration.

The feature set is intentionally limited to capabilities that can be mapped to
a concrete public API.
