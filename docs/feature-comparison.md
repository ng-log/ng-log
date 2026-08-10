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

Select a column heading to sort the matrix. Library columns sort by support
status, while the feature column sorts alphabetically.

The matrix focuses on less common capabilities that distinguish the libraries.
Common baseline features such as severity levels and console output are
omitted.

| Feature | ng-log | spdlog | Abseil | Boost.Log | plog | Easylogging++ |
| --- | :---: | :---: | :---: | :---: | :---: | :---: |
| Format-string log statements | ⚠️[^1] | ✅ | ❌ | ❌ | ❌ | ❌ |
| Wide-character text output | ❌ | ⚠️[^2] | ✅ | ✅ | ✅ | ⚠️[^3] |
| Unicode-aware file paths | ❌ | ⚠️[^4] | ❌ | ✅ | ✅ | ❌ |
| Explicit POSIX long-path handling | ❌ | ❌ | ❌ | ❌ | ❌ | ❌ |
| Explicit Windows extended-path handling | ❌ | ❌ | ❌ | ❌ | ❌ | ❌ |
| Per-file or module verbosity | ✅ | ❌ | ✅ | ⚠️[^5] | ❌ | ✅ |
| Periodic or limited-count logging | ✅ | ❌ | ✅ | ❌ | ❌ | ✅ |
| Assertion and check macros | ✅ | ❌ | ✅ | ❌ | ❌ | ✅ |
| System-error logging | ✅ | ❌ | ✅ | ❌ | ❌ | ✅ |
| Structured or key-value fields | ❌ | ❌ | ✅ | ✅ | ❌ | ❌ |
| Named loggers or channels | ❌ | ✅ | ❌ | ✅ | ⚠️[^6] | ✅ |
| Asynchronous dispatch | ❌ | ✅ | ❌ | ✅ | ❌ | ✅ |
| Syslog output | ✅ | ✅ | ❌ | ✅ | ❌ | ✅ |
| Platform-specific output sinks | ⚠️[^7] | ✅ | ⚠️[^8] | ✅ | ✅ | ❌ |
| Network output sinks | ❌ | ✅ | ❌ | ⚠️[^9] | ❌ | ❌ |
| Colorized terminal output | ✅ | ✅ | ❌ | ❌ | ✅ | ✅ |
| Targeted or buffered backtrace capture | ✅ | ✅ | ✅ | ❌ | ❌ | ❌ |
| Stack trace on fatal failures | ✅ | ❌ | ✅ | ❌ | ❌ | ⚠️[^10] |
| Raw low-level logging | ✅ | ❌ | ❌ | ❌ | ❌ | ❌ |
| Email output | ✅ | ❌ | ❌ | ❌ | ❌ | ❌ |

[^1]: ng-log provides printf-style varargs through `RAW_LOG`, while its
    regular log statements use C++ streams.
[^2]: spdlog converts wide-character log messages to UTF-8 only when
    `SPDLOG_WCHAR_TO_UTF8_SUPPORT` is enabled.
[^3]: Easylogging++ uses wide-character log messages and streams only when
    `ELPP_UNICODE` is enabled.
[^4]: spdlog uses wide-character file paths on Windows only when
    `SPDLOG_WCHAR_FILENAMES` is enabled.
[^5]: Boost.Log supports channel and attribute filters, but does not expose a
    dedicated per-file verbosity control.
[^6]: plog's template instance IDs provide separate logger instances, but not
    a named logger or channel API.
[^7]: ng-log provides syslog output and platform-dependent logging paths, but
    does not expose separate platform sink classes.
[^8]: Abseil provides Android native log integration, but not a general family
     of platform-specific sink classes.
[^9]: Boost.Log provides UDP through its syslog backend, but not a general
     network sink family.
[^10]: Easylogging++ provides crash logging only when
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
