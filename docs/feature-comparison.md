# Feature Comparison

This page compares the logging features exposed by the libraries used in the
cross-library benchmark suite. The comparison covers the source trees
inspected for this documentation update.

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

| Feature | ng-log | spdlog | Abseil | Boost.Log | plog | Easylogging++ |
| --- | :---: | :---: | :---: | :---: | :---: | :---: |
| C++ stream-style log statements | ✅ | ❌ | ✅ | ✅ | ✅ | ✅ |
| Format-string log statements | ⚠️[^1] | ✅ | ❌ | ❌ | ❌ | ❌ |
| Multiple severity levels | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ |
| Runtime severity filtering | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ |
| Per-file or module verbosity | ✅ | ❌ | ✅ | ⚠️[^2] | ❌ | ✅ |
| Conditional logging | ✅ | ✅ | ✅ | ⚠️[^3] | ✅ | ✅ |
| Periodic or limited-count logging | ✅ | ❌ | ✅ | ❌ | ❌ | ✅ |
| Fatal logging | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ |
| Assertion and check macros | ✅ | ❌ | ✅ | ❌ | ❌ | ✅ |
| System-error logging | ✅ | ❌ | ✅ | ❌ | ❌ | ✅ |
| Source file and line metadata | ✅ | ✅ | ✅ | ✅ | ⚠️[^4] | ✅ |
| Thread and process metadata | ✅ | ✅ | ✅ | ✅ | ⚠️[^5] | ✅ |
| Custom output formatting | ✅ | ✅ | ⚠️[^6] | ✅ | ✅ | ✅ |
| Structured or key-value fields | ❌ | ❌ | ✅ | ✅ | ❌ | ❌ |
| Named loggers or channels | ❌ | ✅ | ❌ | ✅ | ⚠️[^7] | ✅ |
| Console output | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ |
| Built-in file output | ✅ | ✅ | ❌ | ✅ | ✅ | ✅ |
| File rotation | ✅ | ✅ | ❌ | ✅ | ✅ | ✅ |
| Multiple sinks or appenders | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ |
| Custom sink or appender interface | ✅ | ✅ | ✅ | ✅ | ✅ | ⚠️[^8] |
| Asynchronous dispatch | ❌ | ✅ | ❌ | ✅ | ❌ | ✅ |
| Syslog output | ✅ | ✅ | ❌ | ✅ | ❌ | ✅ |
| Platform-specific output sinks | ⚠️[^9] | ✅ | ⚠️[^10] | ✅ | ✅ | ❌ |
| Network output sinks | ❌ | ✅ | ❌ | ⚠️[^11] | ❌ | ❌ |
| Colorized output | ✅ | ✅ | ❌ | ❌ | ✅ | ✅ |
| Targeted or buffered backtrace capture | ✅ | ✅ | ✅ | ❌ | ❌ | ❌ |
| Stack trace on fatal failures | ✅ | ❌ | ✅ | ❌ | ❌ | ⚠️[^12] |
| Compile-time log stripping | ✅ | ✅ | ✅ | ❌ | ✅ | ⚠️[^13] |
| Raw low-level logging | ✅ | ❌ | ❌ | ❌ | ❌ | ❌ |
| Log cleanup or retention management | ✅ | ✅ | ❌ | ✅ | ⚠️[^14] | ✅ |
| Email output | ✅ | ❌ | ❌ | ❌ | ❌ | ❌ |

[^1]: ng-log provides printf-style varargs through `RAW_LOG`, while its
    regular log statements use C++ streams.
[^2]: Boost.Log supports channel and attribute filters, but does not expose a
    dedicated per-file verbosity control.
[^3]: Boost.Log supports expression filters, but does not expose a dedicated
    conditional logging macro.
[^4]: plog includes the source file only when `PLOG_CAPTURE_FILE` is defined.
[^5]: plog records thread context, but does not provide the same combined
    process and thread metadata interface.
[^6]: Abseil permits custom sink processing, but does not expose a public
    pattern formatter API comparable to the other libraries.
[^7]: plog's template instance IDs provide separate logger instances, but not
    a named logger or channel API.
[^8]: Easylogging++ exposes dispatch callbacks, but not a public sink or
    appender base interface comparable to the other libraries.
[^9]: ng-log provides syslog output and platform-dependent logging paths, but
    does not expose separate platform sink classes.
[^10]: Abseil provides Android native log integration, but not a general family
     of platform-specific sink classes.
[^11]: Boost.Log provides UDP through its syslog backend, but not a general
     network sink family.
[^12]: Easylogging++ provides crash logging only when
     `ELPP_FEATURE_CRASH_LOG` is enabled.
[^13]: Easylogging++ can compile out selected debug and check macros through its
     feature flags, but the inspected API does not provide general stripping
     for normal log calls.
[^14]: plog couples retention to the rolling file appender's size and file-count
     settings rather than providing a separate retention manager.

The ❌ mark describes the compared library interface. Applications can still
provide the capability through their own code or an external integration.

The feature set is intentionally limited to capabilities that can be mapped to
a concrete public API. Performance results belong in the benchmark results,
not in this feature comparison.
