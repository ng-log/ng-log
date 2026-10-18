<!--
SPDX-FileCopyrightText: 2024 Google Inc.
SPDX-FileCopyrightText: 2026 The ng-log contributors

SPDX-License-Identifier: BSD-3-Clause
-->

# ng-log

ng-log (formerly Google Logging Library or glog) is a C++14 library
that implements application-level logging. The library provides logging APIs
based on C++-style streams and various helper macros.

!!! tip "Coming in 0.9"
    A C++20 logging API without `LOG` and `CHECK` macros, with format strings
    checked at compile time. [Take a look](#experimental-logging-functions)
    and help shape it before it becomes the default.

# How to Use

You can log a message by simply streaming things to `LOG`(<a particular
[severity level](logging/index.md#severity-levels)\>), e.g.,

``` cpp title="main.cpp"
#include <ng-log/logging.h>

int main(int argc, char* argv[]) {
    nglog::InitializeLogging(argv[0]); // (1)!
    const int num_cookies = 3;
    LOG(INFO) << "Found " << num_cookies << " cookies"; // (2)!
}
```

1. Initialize library
2. Log a message with informational severity

The [user guide](logging/index.md) covers the macros in detail. C++20 code can
use the [experimental logging functions](#experimental-logging-functions)
instead.

# Experimental Logging Functions

With C++20, the [experimental logging functions](logging/experimental.md) log
the same message using `std::format` syntax:

``` cpp title="main.cpp"
#include <ng-log/experimental/logging.h>

namespace ngx = nglog::experimental;

int main(int argc, char* argv[]) {
    nglog::InitializeLogging(argv[0]);
    const int num_cookies = 3;
    ngx::Log("Found {} cookies", num_cookies); // (1)!
}
```

1. Log a message with informational severity

The macros and their counterparts side by side, assuming
`using namespace std::chrono_literals;`:

<div class="grid" markdown>

``` cpp title="Macros"
LOG(WARNING) << "x=" << x;
DLOG(INFO) << x;
LOG_EVERY_N(ERROR, 10) << x;

LOG_EVERY_T(INFO, 0.5) << x;
VLOG(2) << x;
CHECK_GT(n, 0);
RAW_LOG(ERROR, "%d", x);

RAW_CHECK(fd >= 0, "bad fd");
```

``` cpp title="Functions"
ngx::Log(ngx::Warning, "x={}", x);
ngx::Log(ngx::Debug, "{}", x);
ngx::Log(ngx::Error | ngx::Every{10},
         "{}", x);
ngx::Log(ngx::Every{500ms}, "{}", x);
ngx::Log(ngx::Verbose{2}, "{}", x);
ngx::Ensure(ngx::That(n) > 0);
ngx::Log(ngx::SignalSafe | ngx::Error,
         "{}", x);
ngx::Ensure(fd >= 0, ngx::SignalSafe,
            "bad fd");
```

</div>

- Format strings are checked at compile time, and a message is formatted only
  if it is logged.
- Attributes such as `#!cpp ngx::Warning | ngx::Every{10}` compose instead of
  requiring a macro per variant, and can be
  [user-defined](logging/experimental.md#custom-attributes).
- No `#!cpp LOG` or `#!cpp CHECK` macros are defined, so the functions
  coexist with Abseil's logging and other libraries.
- With [`#!cpp ngx::SignalSafe`](logging/experimental.md#signal-safe-logging),
  logging and checks neither allocate nor acquire locks, like `RAW_LOG` and
  with the same caveats, for use in signal handlers.

The functions share the implementation of the macros, so their output is
identical and both can be mixed in one program. They are
[planned](logging/experimental.md#migrating-from-the-macros) to supersede the
macros in ng-log 0.10.0. Until then, the API may still change, and
[feedback](https://github.com/ng-log/ng-log/issues) is welcome.

# Installation

The library can be installed using various [package managers](packages.md) or
compiled [from source](build.md). For a detailed overview of ng-log features and
their usage, please refer to the [user guide](logging/index.md).

!!! warning
    The above example requires further [CMake](usage.md) setup for use in your
    own projects.
