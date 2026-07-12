# Failure Signal Handler

## Stacktrace as Default Failure Handler

The library provides a convenient signal handler that will dump useful
information when the program crashes on certain signals such as `SIGSEGV`. The
signal handler can be installed by `#!cpp
nglog::InstallFailureSignalHandler()`.

<!--
The transcript below was captured from a terminal and converted to HTML with
ansi2html using these commands:

env -u NO_COLOR TERM=xterm-256color script -q -c 'build-debug/color_stacktrace_example' /tmp/ng-log-color-stacktrace.typescript
perl -0pe 's/\r//g; s/\AScript started.*?\n//s; s/\nScript done.*\z//s; s/\e\]8;;[^\e]*\e\\//g; s/\A.*?(?=\*\*\* Aborted)//s' /tmp/ng-log-color-stacktrace.typescript | ansi2html -i -W -s xterm | perl -pe 's{<span style="color: #00cdcd">((src|examples)/([^:<]+):([0-9]+))</span>}{<a href="https://github.com/ng-log/ng-log/blob/master/$2/$3#L$4" style="color: inherit; text-decoration: underline dotted; text-decoration-thickness: 1px; text-underline-offset: 3px"><span style="color: #00cdcd">$1</span></a>}g' > docs/failure-stacktrace.html
-->

The example intentionally aborts, so a nonzero exit status is expected. The
time, addresses, process identifiers, thread identifiers, and source lines
vary between runs.

<pre style="overflow-x: auto">
--8<-- "docs/failure-stacktrace.html"
</pre>

When writing to a terminal, this output is
[colorized](flags.md#colorizing-output): the address, `file:line`, and
function name of each frame are colored separately, and `file:line`
references become clickable hyperlinks back to the source.

## Customizing Handler Output

By default, the signal handler writes the failure dump to the standard error.
However, it is possible to customize the destination by installing a callback
using the `#!cpp nglog::InstallFailureWriter()` function. The function expects
a pointer to a function with the following signature:

``` cpp
void YourFailureWriter(const char* message/* (1)! */, std::size_t length/* (2)! */);
```

1. The pointer references the start of the failure message.

    !!! danger
        The string is **not null-terminated**.

2. The message length in characters.

!!! warning "Possible overflow errors"
    Users should not expect the `message` string to be null-terminated.

## User-defined Failure Function

`FATAL` severity level messages or unsatisfied `CHECK` condition
terminate your program. You can change the behavior of the termination
by `nglog::InstallFailureFunction`.

``` cpp
void YourFailureFunction() {
  // Reports something...
  exit(EXIT_FAILURE);
}

int main(int argc, char* argv[]) {
  nglog::InstallFailureFunction(&YourFailureFunction);
}
```

By default, ng-log tries to dump the stacktrace and calls `#!cpp std::abort`. The
stacktrace is generated only when running the application on a system
supported[^1] by ng-log.

[^1]: To extract the stack trace, ng-log currently supports the following targets:

    * x86, x86_64,
    * PowerPC architectures,
    * `libunwind`,
    * and the Debug Help Library (`dbghelp`) on Windows.

## Resolving File Names and Line Numbers

When built with `WITH_LINE_INFO` set to `auto` (the default), `addr2line`, or
`libbacktrace`, ng-log resolves the source file and line number of each stack
frame, in addition to the symbol name. This applies to both the failure signal
handler and `LOG(FATAL)`/unsatisfied `CHECK` traces. `WITH_LINE_INFO=none`
disables the feature.

Two backends provide this:

* `addr2line` invokes the `addr2line` command-line tool as an external
  process with a bounded timeout, so a missing or unresponsive
  `addr2line` only suppresses the file and line information rather than
  affecting the rest of the crash report.
* `libbacktrace` resolves the symbol name and the file and line number
  in-process, directly from the DWARF debug information, without spawning a
  subprocess per frame. It requires the
  [libbacktrace](https://github.com/ianlancetaylor/libbacktrace) library to be
  available at build time.

`auto` prefers `libbacktrace` when it is available and falls back to `addr2line`
otherwise. Forcing one backend with `WITH_LINE_INFO` means the other is never
even probed.

When built with MinGW, `addr2line` or `libbacktrace` also replace `dbghelp` as
the symbol resolver entirely rather than supplementing it, since `dbghelp`
cannot read the DWARF debug information a MinGW build emits by default. MSVC
builds always use `dbghelp`, since neither backend understands MSVC's mangled
names or debug information.

Resolution can be disabled at runtime without recompiling by setting `#!cpp
FLAGS_symbolize_line_info` to `false`, or `--symbolize_line_info=false` on the
command line. The `#!cpp FLAGS_addr2line_timeout_ms` flag controls how long, in
milliseconds, ng-log waits for `addr2line` to resolve a single address before
giving up on it. It has no effect when `libbacktrace` is the active backend.
