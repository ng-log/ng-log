# Release Notes

## ng-log

### 0.9.0 <small>TBD</small> { id="0.9.0" }

- Resolve source file names and line numbers in stack traces via
  [addr2line](failures.md#resolving-file-names-and-line-numbers)
- Remove the glog compatibility layer
- Remove Bazel build support
- Prevent concurrent log writers from blocking during memory-drop maintenance
- Make log cleanup work with non-ASCII Windows directories
- Omit the default header format when a custom prefix formatter is installed

!!! warning "Compatibility"
    If your application depends on glog compatibility, use the 0.8.x series.
    It will receive updates until 0.10.0 is released.

### 0.8.4 <small>TBD</small> { id="0.8.4" }

- Prevent `drop_log_memory` from blocking concurrent log writers
- Prevent failure-signal handling from deadlocking when reentered
- Make crash-trace demangling safe when symbol names exceed the output buffer
- Use the ng-log version in CMake package compatibility errors
- Restore Linux LWP identifiers in crash signal reports
- Report sub-hour UTC offsets through `LogMessageTime::gmtoffset`
- Cap oversized `max_log_size` values instead of using the 1 MB minimum

### 0.8.3 <small>July 10, 2026</small> { id="0.8.3" }

- Mapped the CMake option value `none` to a proper CMake boolean string
- Fixed reading uninitialized output when a demangled name is truncated

### 0.8.2 <small>August 16, 2025</small> { id="0.8.2" }

- Propagated the configured language standard to the unit tests

### 0.8.1 <small>June 15, 2025</small> { id="0.8.1" }

- Fixed `LogCleaner` deleting only `INFO` level logs when enabled for all
  severities
- Fixed a regression in thread-local storage support
- Deprecated the `glog` CMake import target in favor of `ng-log`
- Added support for compiling without RTTI

### 0.8.0 <small>May 1, 2025</small> { id="0.8.0" }

First public release under the new name.

!!! info "Compatibility"
    A full compatibility layer is provided so that existing code can keep
    including the legacy headers and using the legacy API up to version
    0.9.0.

## Pre-migration history

The releases below predate the rename and are kept here for historical
reference.

### 0.7.1 <small>June 8, 2024</small> { id="0.7.1" }

- Fixed Android detection
- Fixed formatting of unknown stack frames

### 0.7.0 <small>February 17, 2024</small> { id="0.7.0" }

Major overhaul to take advantage of C++14 language and library features,
including using `chrono` types in the public API.

!!! warning "ABI incompatible changes"
    Source compatibility with the previous release was maintained where
    possible, but downstream clients need to recompile due to ABI
    incompatible changes.

__Changes__:

- Added C++14 as the primary language standard
- Added [Emscripten](https://emscripten.org/) support
- Changed `LogSeverity` to an unscoped enum, which can surface previously
  silent implicit conversions between `int` and `LogSeverity`
- Changed CMake to no longer generate pkg-config files by default (opt in
  via `WITH_PKGCONFIG`)
- Deprecated `LogMessage::getMessageTime` in favor of `LogMessage::time`
- Deprecated `LogMessageTime::timestamp` in favor of `LogMessageTime::when`
- Deprecated `LogMessageTime::gmtoff` in favor of
  `LogMessageTime::gmtoffset`
- Deprecated `EnableLogCleaner(unsigned)` in favor of
  `EnableLogCleaner(std::chrono::minutes)`
- Deprecated the `LogSink::send` overload accepting `std::tm` in favor of
  one accepting `LogMessageTime`
- Deprecated `LogMessageInfo` and its prefix callback in favor of
  `LogMessage` and `InstallPrefixFormatter`

    All deprecated methods were removed in 0.8.0.

### 0.6.0 <small>April 5, 2022</small> { id="0.6.0" }

- Added time-based periodic logging (`LOG_EVERY_T`)
- Added `LogMessageTime::gmtoff()` to obtain the GMT offset
- Added support for stack unwinding on Android
- Added `FLAGS_log_year_in_prefix` to control whether the year is included
  in the log prefix
- Added `logtostdout` and `colorlogtostdout` flags to support logging to
  stdout
- Exposed `IsGoogleLoggingInitialized()` in the public API
- Changed `LogCleaner` to support relative paths
- Changed `--max_log_size` to use `uint32` and prevented it from
  overflowing
- Renamed the `GOOGLE_GLOG_DLL_DECL` export macro to `GLOG_EXPORT`
- Enabled the custom prefix option by default
- Deprecated the `LogSink::send` overload accepting `std::tm` in favor of
  one accepting `LogMessageTime`, while keeping a backward-compatible
  overload
- Raised the minimum required CMake version to 3.16 and gflags to 2.2.2
- Fixed CMake configuration on Cygwin and various compiler warnings across
  toolchains

### 0.5.0 <small>May 8, 2021</small> { id="0.5.0" }

- Added support for customizing the log line prefix
  (`InstallPrefixFormatter`)
- Added automatic removal of old log files (`EnableLogCleaner`)
- Added a `FLAGS_log_utc_time` option to write timestamps in UTC
- Added the year to the default log line prefix
- Added support for outputting log messages to Android's logcat
- Added pkg-config support and vcpkg installation instructions
- Added support for using libunwind as an imported CMake target
- Changed the build to require a C++11 compiler; a C compiler and
  Windows-specific or legacy C headers are no longer needed
- Changed CMake to build shared libraries by default
- Removed the autoconf-based build in favor of CMake only
- Fixed race conditions in `LOG_EVERY_N` and related macros
- Fixed handling of `--max_log_size` overflow
- Fixed various Windows linker warnings

### 0.4.0 <small>January 22, 2019</small> { id="0.4.0" }

- Added support for building with Bazel
- Added Cygwin support
- Added stack tracing support on Windows
- Renamed the CMake project from `google-glog` to `glog`
- Reduced heap allocations on the logging fast path
- Fixed username lookup when the `USER` environment variable is missing
- Fixed mingw cross compilation and redefinition warnings from `config.h`
- Fixed a number of missing symbol exports on Windows
- Added support for colored output on the konsole terminal family

### 0.3.5 <small>May 9, 2017</small> { id="0.3.5" }

- Added CMake build support
- Added `DCHECK_ALWAYS_ON` to keep `D*` macros enabled under `NDEBUG`
- Added `logfile_mode` to control the permissions of created log files
- Added support for PowerPC and color output for tmux terminals
- Changed `CHECK_NOTNULL` to work with smart pointers when compiled as
  C++11
- Changed `Symbolize` to calculate a module's base address from its
  program headers
- Fixed an `AddLogSink` memory leak
- Fixed a double-free in a Windows unit test
- Fixed a dangling sink pointer after removal
- Fixed memory allocation from the signal handler
- Various MinGW and Windows build fixes

### 0.3.4 <small>March 9, 2015</small> { id="0.3.4" }

- Moved the repository to GitHub
- Added libc++ support
- Added a callback for `OpenObjectFileContainingPcAndGetStartAddress`
- Added `StrError` and replaced the `posix_strerror_r` call
- Added support for `unordered_map`/`unordered_set` in STL logging
- Reduced dynamic allocation from three to one allocation per log message
- Fixed a build issue in `demangle.cc`
- Fixed missing `GOOGLE_GLOG_DLL_DECL` for the latest Visual Studio

### 0.3.3 <small>February 1, 2013</small> { id="0.3.3" }

- Added a `--disable-rtti` configure option
- Added color output support via `GLOG_colorlogtostderr`
- Allowed re-initializing the library after `ShutdownGoogleLogging`
- Changed the ABI around flags to be compatible with gflags
- Fixed `LOG_SYSRESULT`
- Fixed Visual Studio, QNX, and FreeBSD builds

### 0.3.2 <small>January 12, 2012</small> { id="0.3.2" }

- Added Clang support
- Improved the demangler and stack trace support for newer GCC versions
- Fixed `fork(2)` corrupting log files
- Fixed `ERROR` being defined by `windows.h`

### 0.3.1 <small>June 15, 2010</small> { id="0.3.1" }

- Added `DCHECK_NOTNULL`
- Added `ShutdownGoogleLogging` to close syslog
- Added support for building and testing out of tree
- Fixed `GLOG_*` environment variables being ignored when gflags is
  installed
- Fixed the `--enable-frame-pointers` option and libunwind detection

### 0.3.0 <small>July 30, 2009</small> { id="0.3.0" }

- Added NetBSD and OpenBSD support
- Fixed a deadlock triggered when using a recent version of gflags
- Fixed hostname and username detection on Windows

### 0.2.1 <small>April 10, 2009</small> { id="0.2.1" }

- Added pkg-config support
- Added a `--with-gflags` configure option
- Fixed timestamps produced by the VC++ build
- Fixed building with gtest

### 0.2 <small>January 23, 2009</small> { id="0.2" }

- Added initial Windows VC++ support
- Added the `LOG_TO_STRING`, `LOG_AT_LEVEL`, `DVLOG`, and
  `LOG_TO_SINK_ONLY` macros
- Added the `--log_backtrace_at` option
- Added microsecond precision to logged timestamps
- Added automatic linking of the pthread library

### 0.1.2 <small>November 18, 2008</small> { id="0.1.2" }

- Added `InstallFailureSignalHandler()`
- Changed how stack traces are produced
- Removed the unnecessary `DISALLOW_EVIL_CONSTRUCTORS` macro

### 0.1.1 <small>October 15, 2008</small> { id="0.1.1" }

- Added symbolization support on macOS 10.5
- Fixed `--vmodule` not working together with gflags
- Fixed `symbolize_unittest` with GCC 4.3

### 0.1 <small>October 7, 2008</small> { id="0.1" }

Initial release: a library implementing application-level logging, with
logging APIs based on C++-style streams and various helper macros.
