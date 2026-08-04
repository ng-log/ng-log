# Notes for Windows Users

## File and directory paths

All file and directory paths passed to ng-log must use UTF-8 encoding. This
includes logfile destinations, log directories, and paths passed to
`TruncateLogFile`.

ng-log converts these paths to UTF-16 and uses Unicode Windows filesystem APIs
so every filename can be represented. Invalid UTF-8 paths are rejected. This
behavior is consistent with the other supported platforms and does not require
a change to the public API.

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

## Log output

Log messages are expected to use UTF-8. When standard output or standard error
is connected to a Windows console, ng-log converts the message to UTF-16 and
writes it with the Unicode console API. Redirected output remains UTF-8 bytes.
Debugger output uses the corresponding Unicode debugger API.

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
ASCII input is valid UTF-8 and uses the fast paths described above.

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
