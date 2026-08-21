# Adjusting Output

Several flags influence ng-log's output behavior.

## Using Command-line Parameters and Environment Variables

If the [Google gflags
library](https://github.com/gflags/gflags) is installed on your machine,
the build system will automatically detect and use it, allowing you to
pass flags on the command line.

!!! example "Activate `--logtostderr` in an application from the command line"
    A binary `your_application` that uses ng-log can be started using
    ``` bash
    ./your_application --logtostderr=1
    ```
    to log to `stderr` instead of writing the output to a log file.

!!! tip
    You can set boolean flags to `true` by specifying `1`, `true`, or `yes`. To
    set boolean flags to `false`, specify `0`, `false`, or `no`. In either case
    the spelling is case-insensitive.


If the Google gflags library isn't installed, you set flags via
environment variables, prefixing the flag name with `NGLOG_`, e.g.,

!!! example "Activate `logtostderr` without gflags"
    ``` bash
    NGLOG_logtostderr=1 ./your_application
    ```

The following flags are most commonly used:

`logtostderr` (`bool`, default=`false`)

:   Use `stderr` as the primary destination instead of logfiles. Setting this
    flag to `false` does not suppress `stderr` output. The
    `stderrthreshold` flag still copies messages at or
    above its configured severity to `stderr` in addition to their normal
    destination. During crash handling, the fatal message is written directly
    to `stderr` so it remains visible as the last message in crash output.
    When `logtostderr` is `false`, the fatal message is also replayed through
    the configured logfile or `stdout` destination.

`stderrthreshold` (`int`, default=2, which is `ERROR`)

:   Copy log messages at or above this level to `stderr` in addition to
    logfiles. The numbers of severity levels `INFO`, `WARNING`, `ERROR`,
    and `FATAL` are 0, 1, 2, and 3, respectively.

`minloglevel` (`int`, default=0, which is `INFO`)

:   Log messages at or above this level. Again, the numbers of severity
    levels `INFO`, `WARNING`, `ERROR`, and `FATAL` are 0, 1, 2, and 3,
    respectively.

`log_dir` (`string`, default="")

:   If specified, logfiles are written into this directory instead of
    the default logging directory.

`v` (`int`, default=0)

:   Show all `#!cpp VLOG(m)` messages for `m` less or equal the value of this
    flag. The applicable `#!bash --vmodule` entry overrides this value. Refer
    to [verbose
    logging](logging.md#verbose-logging) for more detail.

`vmodule` (`string`, default="")

:   Per-module verbose level. The argument has to contain a
    comma-separated list of `<module name>=<log level>`. `<module name>` is a
    glob pattern (e.g., `gfs*` for all modules whose name starts with "gfs"),
    matched against the source filename base. The base is the part before the
    first period, with a trailing `-inl` suffix removed. `<log level>`
    overrides any value given by `--v`. See also [verbose
    logging](logging.md#verbose-logging) for more details.

Additional flags are defined in
[flags.cc](https://github.com/ng-log/ng-log/blob/master/src/flags.cc). Please see
the source for their complete list.

## Colorizing Output

ng-log colorizes messages logged to a terminal, as well as crash stack
traces produced by the [failure signal
handler](failures.md#stacktrace-as-default-failure-handler) or an
unsatisfied `CHECK`/`LOG(FATAL)`. Colorizing is opt-out, not opt-in: it
defaults to on, and is automatically disabled unless it is actually safe
to colorize, namely the destination must be a real terminal (not a pipe
or a redirected file), and the terminal must not have opted out via a
[`NO_COLOR`](https://no-color.org) environment variable or `TERM=dumb`.

Each field of a log line's prefix, severity character, timestamp,
thread id, and `file:line` reference, is colorized separately, and the
message body is colorized by severity. A crash's stack trace applies
the same per-field treatment to each frame's address, `file:line`, and
function name, so both regular log output and a crash report share a
consistent, readable color scheme. See
[`examples/color_stacktrace.cc`](https://github.com/ng-log/ng-log/blob/master/examples/color_stacktrace.cc)
for a runnable demonstration covering every severity and logging macro,
as well as a colorized, hyperlinked crash trace.

`colorlogtostderr` (`bool`, default=`true`)

:   Color messages logged to `stderr`, including crash stack traces.
    Set to `false` to disable color unconditionally.

`colorlogtostdout` (`bool`, default=`true`)

:   Same as `colorlogtostderr`, but for `stdout`.

`symbolize_hyperlinks` (`bool`, default=`true`)

:   Wrap `file:line` references in log output and crash stack traces with
    [OSC 8](https://gist.github.com/egmontkob/eb114294efbcd5adb1944c9f3cb5feda)
    terminal hyperlinks pointing at the source file. Automatically has no
    effect unless the terminal is recognized as one of a known set of
    OSC 8-capable terminal emulators (detected via environment variables
    such as `WT_SESSION`, `VTE_VERSION`, `KONSOLE_VERSION`, or
    `TERM_PROGRAM`). Set to `false` to disable unconditionally.

!!! warning "Windows Terminal limitation"
    Windows Terminal currently supports OSC 8 `file://` links only when the
    hostname is empty or `localhost`. Links with another hostname do not open
    there. ng-log includes the originating hostname as required by the OSC 8
    specification. This is a Windows Terminal
    [limitation](https://github.com/microsoft/terminal/issues/14116), not an
    ng-log hyperlink-generation problem. ng-log does not rewrite or remove the
    hostname because doing so could weaken the safety of file links.

`symbolize_file_base_path` (`string`, default=`""`)

:   Base path used to resolve relative source file paths recorded in debug
    info (e.g. the build directory) when generating a hyperlink for log
    output or a crash stack trace. Absolute paths recorded in debug info are
    used as-is. Relative ones are only hyperlinked once this is set to an
    absolute path they can be joined onto.

!!! tip
    Hyperlinks won't open if you're viewing the log on a different machine
    than where it was generated, e.g. over SSH or from a copied log file:
    the link embeds the originating hostname, and terminals (e.g. iTerm2,
    VS Code) refuse to open it unless that matches their own, as a
    safeguard against a log silently pointing at a path on another
    machine.

!!! tip
    On some Windows consoles (pre-Windows 10, or VT support otherwise
    disabled) you'll see colored text but no hyperlinks or finer styling,
    even though the docs describe those features. Text still gets colored
    through an older, more limited fallback, just not the full experience
    ANSI/VT escape sequences enable elsewhere.

## Modifying Flags Programmatically

You can also modify flag values in your program by modifying global variables
`FLAGS_*`. Most settings start working immediately after you update `FLAGS_*`.
The exceptions are the flags related to destination files. For instance, you
might want to set `FLAGS_log_dir` before calling `nglog::InitializeLogging`.

!!! example "Setting `log_dir` at runtime"
    ``` cpp
    LOG(INFO) << "file";
    // Most flags work immediately after updating values.
    FLAGS_logtostderr = 1;
    LOG(INFO) << "stderr";
    FLAGS_logtostderr = 0;
    // This won’t change the log destination. If you want to set this
    // value, you should do this before nglog::InitializeLogging .
    FLAGS_log_dir = "/some/log/directory";
    LOG(INFO) << "the same file";
    ```
