<!--
SPDX-FileCopyrightText: 2020-2024 Google Inc.
SPDX-FileCopyrightText: 2026 The ng-log contributors

SPDX-License-Identifier: BSD-3-Clause
-->

# ng-log

[![CI](https://github.com/ng-log/ng-log/actions/workflows/ci.yml/badge.svg)](https://github.com/ng-log/ng-log/actions/workflows/ci.yml)
[![Codecov](https://codecov.io/gh/ng-log/ng-log/branch/master/graph/badge.svg?token=7R5D3HA36B)](https://codecov.io/gh/ng-log/ng-log)

ng-log is a C++14 library for application-level logging. It provides
stream-based logging and helper macros.

ng-log is the community-maintained successor to glog, originally developed by
Google. The project, however, is not affiliated with Google, which maintains its
own logging library as part of
[abseil-cpp](https://github.com/abseil/abseil-cpp).

> [!TIP]
> **Coming in 0.9:** a C++20 logging API without `LOG` and `CHECK` macros,
> with format strings checked at compile time.
> [Take a look](#experimental-logging-functions) and help shape it before it
> becomes the default.

## Getting Started

Log a message by streaming it to `LOG`:

```cpp
#include <ng-log/logging.h>

int main(int argc, char* argv[]) {
  nglog::InitializeLogging(argv[0]);
  const int num_cookies = 3;
  LOG(INFO) << "Found " << num_cookies << " cookies";
}
```

The [documentation](https://ng-log.github.io/ng-log/) covers the macros in
detail. C++20 code can use the
[experimental logging functions](#experimental-logging-functions) instead.

## Experimental Logging Functions

With C++20, the [experimental logging functions](https://ng-log.github.io/ng-log/0.9.0/logging/experimental/)
log the same message using `std::format` syntax:

```cpp
#include <ng-log/experimental/logging.h>

namespace ngx = nglog::experimental;

int main(int argc, char* argv[]) {
  nglog::InitializeLogging(argv[0]);
  const int num_cookies = 3;
  ngx::Log("Found {} cookies", num_cookies);
}
```

The macros and their counterparts side by side, assuming
`using namespace std::chrono_literals;`:

<table>
<tr><th>Macros</th><th>Functions</th></tr>
<tr>
<td valign="top">

```cpp
LOG(WARNING) << "x=" << x;
DLOG(INFO) << x;
LOG_EVERY_N(ERROR, 10) << x;

LOG_EVERY_T(INFO, 0.5) << x;
VLOG(2) << x;
CHECK_GT(n, 0);
RAW_LOG(ERROR, "%d", x);

RAW_CHECK(fd >= 0, "bad fd");
```

</td>
<td valign="top">

```cpp
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

</td>
</tr>
</table>

- Format strings are checked at compile time, and a message is formatted only
  if it is logged.
- Attributes such as `ngx::Warning | ngx::Every{10}` compose instead of
  requiring a macro per variant, and can be user-defined.
- No `LOG` or `CHECK` macros are defined, so the functions coexist with
  Abseil's logging and other libraries.
- With `ngx::SignalSafe`, logging and checks neither allocate nor acquire
  locks, like `RAW_LOG` and with the same caveats, for use in signal handlers.

The functions share the implementation of the macros, so their output is
identical and both can be mixed in one program. They are planned to supersede
the macros in ng-log 0.10.0. Until then, the API may still change, and
[feedback](https://github.com/ng-log/ng-log/issues) is welcome.

## License

ng-log is licensed under the 3-Clause BSD License. Code inherited from glog is
copyrighted by Google Inc., later changes by the ng-log contributors. Each file
states its copyright and license in SPDX tags as required by
[REUSE](https://reuse.software/). The [license documentation](docs/license.md)
lists the few files licensed differently.
