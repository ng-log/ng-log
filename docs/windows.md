<!--
SPDX-FileCopyrightText: 2024 Google Inc.
SPDX-FileCopyrightText: 2026 The ng-log contributors

SPDX-License-Identifier: BSD-3-Clause
-->

# Notes for Windows Users

## File and directory paths

All file and directory paths passed to ng-log must use UTF-8 encoding. This
includes logfile destinations, log directories, and paths passed to
`TruncateLogFile`.

ng-log uses narrow C runtime filesystem operations for ASCII paths that remain
shorter than `MAX_PATH` after resolving them against the current directory,
unless they start with the `\\?\` or `\\.\` namespace prefix. Other paths are
converted to UTF-16. Directory enumeration uses Unicode Windows APIs so every
entry name can be represented. Invalid UTF-8 paths are rejected. This behavior
is consistent with the other supported platforms and does not require a change
to the public API.

## Compiler character sets

ng-log expects narrow string arguments to contain UTF-8 bytes. With MSVC, use
[`/utf-8`](https://learn.microsoft.com/en-us/cpp/build/reference/utf-8-set-source-and-executable-character-sets-to-utf-8)
when source files are stored as UTF-8 and ordinary narrow string literals must
also be encoded as UTF-8. This option is equivalent to
`/source-charset:utf-8 /execution-charset:utf-8`. These options enable
`/validate-charset` by default.

Use
[`/source-charset:utf-8`](https://learn.microsoft.com/en-us/cpp/build/reference/source-charset-set-source-character-set)
by itself only when MSVC must read the source as UTF-8 but the program
intentionally uses another execution character set. Non-ASCII ordinary narrow
string literals produced by such a build are not necessarily valid ng-log
input.

Use
[`/execution-charset:utf-8`](https://learn.microsoft.com/en-us/cpp/build/reference/execution-charset-set-execution-character-set)
by itself when the source encoding is already identified correctly, such as by
a byte-order mark or another compiler option, and ordinary narrow string
literals must be UTF-8. For UTF-8 source files without a byte-order mark, also
specify `/source-charset:utf-8` or use `/utf-8`.

These options affect source decoding and compile-time string literals. They do
not convert runtime text obtained from files, environment variables, or other
APIs. Applications must convert such text to UTF-8 before passing it to ng-log.
ASCII input is valid UTF-8.

## Long paths

ng-log supports file and directory paths longer than `MAX_PATH` without
requiring callers to add an extended-length path prefix. Relative paths,
drive paths, and UNC paths are converted to absolute extended-length paths
when necessary. Paths that already use an extended-length or device prefix
are preserved.

The short ASCII fast path is unchanged. It avoids UTF-16 conversion and path
normalization while a path remains below `MAX_PATH`. The length of a relative
path includes the current directory it is resolved against, so a short
relative path inside a deep directory is also treated as a long path. Longer
paths use the Unicode path handling described above. Individual path components and the
complete path remain subject to the limits of the underlying Windows file
system.

Long-path normalization uses the Windows PathCch API. As a result, ng-log now
requires Windows 8 or Windows Server 2012 or later.

## Log output

Log messages are expected to use UTF-8. When standard output or standard error
is connected to a Windows console, an ASCII fast path writes messages directly
to the console handle without converting them to UTF-16. Messages containing
non-ASCII characters are converted to UTF-16 and written with the Unicode
console API. Redirected output is written as UTF-8 through the stream's
existing translation mode, so a text-mode stream translates line feeds into
CRLF sequences exactly like the application's own output. Ill-formed UTF-8
sequences are replaced with U+FFFD when writing to the console or the debugger.
Debugger output uses the corresponding Unicode debugger API.

## Log file links

On other platforms, ng-log creates a symbolic link to the latest log file of
each severity. On Windows, it creates a shell shortcut named
`<basename>.<severity>.lnk` instead, because creating symbolic links requires
elevated privileges or developer mode. The `log_link` flag places an
additional shortcut in the given directory.

## Severity levels

ng-log defines the severity level `ERROR`, which is also defined by `windows.h`.
You can make nglog not define `INFO`, `WARNING`, `ERROR`, and `FATAL` by
defining `NGLOG_NO_ABBREVIATED_SEVERITIES` before including `nglog/logging.h`.
The prefixed constants `NGLOG_INFO`, `NGLOG_WARNING`, `NGLOG_ERROR`, and
`NGLOG_FATAL` are always available in the global namespace.

Even with this macro, you can still use the iostream like logging facilities:

``` cpp
#define NGLOG_NO_ABBREVIATED_SEVERITIES
#include <windows.h>
#include <ng-log/logging.h>

// ...

LOG(ERROR) << "This should work";
LOG_IF(ERROR, x > y) << "This should be also OK";
```

However, you cannot use `INFO`, `WARNING`, `ERROR`, and `FATAL` anymore for
functions defined in `nglog/logging.h`.

``` cpp
#define NGLOG_NO_ABBREVIATED_SEVERITIES
#include <windows.h>
#include <ng-log/logging.h>

// ...

// This won’t work.
// nglog::FlushLogFiles(nglog::ERROR);

// Use this instead.
nglog::FlushLogFiles(NGLOG_ERROR);
```

If you don't need `ERROR` defined by `windows.h`, there are a couple of more
workarounds which sometimes don't work[^1]:

-  `#!cpp #define WIN32_LEAN_AND_MEAN` or `NOGDI` **before**
   `#!cpp #include <windows.h>`.
-  `#!cpp #undef ERROR` **after** `#!cpp #include <windows.h>`.

[^1]: For more information refer to [this
      issue](http://code.google.com/p/google-glog/issues/detail?id=33).
