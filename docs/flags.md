<!--
SPDX-FileCopyrightText: 2024 Google Inc.
SPDX-FileCopyrightText: 2026 The ng-log contributors

SPDX-License-Identifier: BSD-3-Clause
-->

# Adjusting Output

Several flags influence ng-log's output behavior.

## Using Command-line Parameters and Environment Variables

ng-log provides its own typed flag registry and command-line parser. No
external flag library is required. Command-line options take effect once the
application passes its arguments to `nglog::ParseCommandLineFlags`, as shown in
the [quick start](#quick-start).

!!! example "Activate `--logtostderr` in an application from the command line"
    A binary `your_application` that uses ng-log can be started using
    ``` bash
    ./your_application --logtostderr=1
    ```
    to log to `stderr` instead of writing the output to a log file.

!!! tip
    You can set boolean flags to `true` by specifying `1`, `true`, `t`, `yes`,
    or `y`. To set boolean flags to `false`, specify `0`, `false`, `f`, `no`,
    or `n`. In either case the spelling is case-insensitive.


Environment variables can provide defaults by prefixing the flag name with
`NGLOG_`, e.g.,

!!! example "Activate `logtostderr` from the environment"
    ``` bash
    NGLOG_logtostderr=1 ./your_application
    ```

An environment value uses the same syntax as the corresponding command-line
option. A value that cannot be parsed is ignored and the flag keeps its
declared default.

## Quick Start

Define a flag in one source file, parse the command line at the beginning of
`main`, and read the value with `nglog::GetFlag`:

``` cpp
#include <cstdint>

#include <ng-log/flags.h>
#include <ng-log/logging.h>

NGLOG_DEFINE(std::int32_t, retries, 3, "Retry failed requests this many times.");

int main(int argc, char* argv[]) {
  nglog::ParseCommandLineFlags(&argc, &argv,
                               nglog::FlagParsingMode::kRemoveFlags);
  nglog::InitializeLogging(argv[0]);

  LOG(INFO) << "Retrying up to " << nglog::GetFlag(FLAGS_retries) << " times";
}
```

The application now accepts its own options and those of ng-log:

``` bash
./your_application --retries=5 --logtostderr
```

Parse the command line before `nglog::InitializeLogging` so that options such
as `--log_dir` apply to the first log file. `--help` lists all options with
their types, defaults, and descriptions, and a misspelled option is reported
with a suggestion. Use `NGLOG_DECLARE(std::int32_t, retries)` in a header to
access the flag from other source files. Application flags receive environment
defaults once they are added to a [module](#public-flags-api) with an
environment-variable prefix.

## Public Flags API

Flags are declared and defined with one typed macro form. The type is part of
the declaration and definition, which allows the compiler and the registry to
detect misuse.

``` cpp
NGLOG_DECLARE(bool, dry_run);
NGLOG_DEFINE(bool, dry_run, false, "Print the actions instead of running them.");
```

A flag name starts with a letter or an underscore and contains only letters,
digits, and underscores. Because `--noname` disables the boolean flag `name`,
a flag named `noname` cannot coexist with a boolean flag named `name`. Both
mistakes terminate the program at startup with a diagnostic.

The built-in trait specializations support `bool`, `std::int32_t`,
`std::uint32_t`, `std::string`, and `nglog::FileMode`. Integers are decimal
unless they start with `0x`, and leading zeros do not select octal. A
`nglog::FileMode` is always written in octal, e.g., `0640`. Applications can add
copyable flag types by specializing `nglog::FlagTraits` with `TypeName`,
`Parse`, and `ToString`. Definitions normally belong in one source file and
declarations belong in a header. `FLAGS_name` is a synchronized typed flag
object rather than a raw value. Read it with `nglog::GetFlag` and update it with
`nglog::SetFlag`:

``` cpp
NGLOG_DECLARE(std::int32_t, retry_count);

const std::int32_t count = nglog::GetFlag(FLAGS_retry_count);
nglog::SetFlag(FLAGS_retry_count, 3);
```

Declarations record their type. A declaration whose type differs from the
definition, such as `NGLOG_DECLARE(bool, retry_count)` for the definition above,
terminates the program at startup with a diagnostic that names both locations,
because code using that declaration would access the flag through the wrong
type. The built-in flags are already declared by `ng-log/flags.h` with the
linkage of the ng-log library. Declaring one of them again, for example with
`NGLOG_DECLARE(bool, logtostderr)`, is a compile error.

Flags defined with `NGLOG_DEFINE` are initialized by the compiler and never
destroyed. They can be read and updated from the static initializer or
destructor of any translation unit, that is, before `main` and during program
termination. This applies to the flags only. Logging itself is supported only
while `main` runs. Before the flag is registered, it holds its declared default.
The default is determined when the flag is first used or registered, whichever
comes first. Afterward, only the environment variable of a flag module replaces
it, when the flag is added to a group of that module. The default value
expression may be evaluated more than once when threads use the flag
concurrently for the first time, so it should not have side effects. The environment and the command line
apply once the flag is registered and the arguments are parsed. An environment
default does not replace a value that was set explicitly before. Restoring a
saved value with `ScopedFlag` or `ScopedFlags` also restores whether the value
counts as set explicitly.

A flag whose lifetime is bounded by a scope, for example one created by a test,
is an `nglog::DynamicFlag`. It is registered on construction and unregistered on
destruction:

``` cpp
nglog::DynamicFlag<std::int32_t> attempts("attempts", __FILE__,
                                          "Attempts in this test.", 3);
```

`GetFlag` and `SetFlag` are safe to call concurrently for the same flag.
`GetFlag` returns a value copy, so a string result remains valid independently
of later flag updates. `SetFlag` accepts a value that is constructible or
assignable as the flag type, so a string literal can be assigned directly to a
`std::string` flag. Use an application mutex when several flags must be read or
updated as one consistent configuration.

Use `nglog::ScopedFlags<> save` to save and restore every registered flag for
the lifetime of `save`. To limit the saved set, use `MakeScopedFlags` with
typed flag and value pairs:

``` cpp
auto save = nglog::MakeScopedFlags(
    nglog::MakeScopedFlag(FLAGS_logtostderr, true),
    nglog::MakeScopedFlag(FLAGS_v, 1));
```

If the same flag appears more than once, the last value in argument order takes
effect and the value from before the first save is restored.

A class derived directly from `nglog::FlagBase` registers itself with
`nglog::RegisterFlag` and must call `nglog::UnregisterFlag` at the beginning of
its destructor, before it destroys the state that its value accessors use.
`nglog::DynamicFlag` and `NGLOG_DEFINE` do this automatically.

The public flag macros do not apply ng-log's shared-library visibility to
application-owned flags. This allows an application linked to a shared ng-log
library to define its own flags. If an application flag must cross one of the
application's DLL boundaries, its owning project must provide the corresponding
import and export declarations.

Applications parse arguments with `nglog::ParseCommandLineFlags`. The parsing
mode is explicit:

``` cpp
nglog::ParseCommandLineFlags(
    &argc, &argv, nglog::FlagParsingMode::kRemoveFlags);
```

`kRemoveFlags` removes recognized flags from `argv`. Use `kKeepFlags` to leave
the argument vector unchanged. Boolean flags accept the values listed above. A
boolean can also be disabled with `--noflag`. Options must start with two
dashes. A single-dash argument such as `-v=1` is reported as an unknown option
along with the suggested `--v` spelling. A lone `-` remains a positional
argument. An argument `--` ends the options, and all arguments after it remain
positional, including those starting with a dash. `kRemoveFlags` removes the
`--` along with the parsed flags.

Unknown options are errors by default. The diagnostic uses fuzzy matching to
suggest a likely spelling when one is available, such as `Did you mean
--logtostderr?`. Applications that intentionally share argument parsing with
another library can explicitly preserve unknown options:

``` cpp
nglog::ParseCommandLineFlags(
    &argc, &argv, nglog::FlagParsingMode::kRemoveFlags,
    nglog::UnknownFlagMode::kIgnore);
```

In this mode, single-dash arguments and `--` remain in `argv`, so that the other
parser can handle them and also recognizes the end of the options.

Applications can generate styled usage text with a `HelpWriter` callback:

``` cpp
void WriteUsage(nglog::HelpWriter& help) {
  help.Write(nglog::HelpTextStyle::kHeading, "Usage:");
  help.Write(" ");
  help.Write(nglog::HelpTextStyle::kExecutable, "your_application");
  help.Write(" ");
  help.Write(nglog::HelpTextStyle::kUsageOptions, "[options]");
  help.Write("\n\nUse ");
  help.Write(nglog::HelpTextStyle::kOptionPrefix, "--");
  help.Write(nglog::HelpTextStyle::kOption, "logtostderr");
  help.Write("=");
  help.Write(nglog::HelpTextStyle::kConstant, "true");
  help.Write(" to write log messages to ");
  help.Write(nglog::HelpTextStyle::kStream, "stderr");
  help.Write(".\n");
}

nglog::SetUsageMessage(WriteUsage);
```

The semantic styles highlight headings, options, executable names, code
stream references such as `stdout` and `stderr`, metadata such as `file:line`,
flag types and values, boolean or other constants such as `true` and `false`,
and environment variable names. Styling is disabled automatically when the help
output is not a suitable terminal. Styled flag descriptions are wrapped to the
current terminal width while preserving each explicitly styled span.
`ProgramUsage()` returns generated usage text without styling.

Flag descriptions can use the same callback form when a description contains
semantic references. This keeps styling explicit instead of inferring meaning
from ordinary prose:

``` cpp
void WriteReportHelp(nglog::HelpWriter& help) {
  help.Write("Write the report to ");
  help.Write(nglog::HelpTextStyle::kStream, "stdout");
  help.Write(" instead of a file.");
}

NGLOG_DEFINE(bool, report_to_stdout, false, WriteReportHelp);
```

The callback is a plain function, so that the flag can be constant initialized.

An ordinary string description is always written literally. A flag help
callback writes only the description line and does not include the flag name,
type, default value, or indentation. The callback's plain text is available in
`FlagInfo::help`, while `FlagInfo::help_function` retains its styling.

Options are assigned to a mandatory named group through their owning module
without changing the typed definition. The association is hierarchical: a
module owns groups and each group owns its flags. A module has a name, a
human-readable header prefix, and an optional environment-variable prefix:

``` cpp
const nglog::FlagModule module("myapp", "Options provided by", "MYAPP_");
const auto retries = module.AddGroup("Retries");
retries.AddFlag(FLAGS_retry_count);
```

The built-in flags already belong to the `ng-log` module, so they cannot be
added to an application module.

The module object can be kept private to the library or application that owns
its flags. The module name must be unique among the modules that exist at the
same time. Constructing another module with the same name while the first one
exists is a fatal configuration error. Destroying a module releases its name
and detaches its flags from their groups, which keeps the flags registered and
allows a reloaded library to construct its module again.

The generated help prints each named module with a header such as `Options
provided by myapp:` followed by an empty line. The header prefix and colon are
white, while the module name is highlighted in bold white. The header is
followed by that module's flag groups and options. Groups are printed in
alphabetical order and each group is separated by an empty line. A flag can be
added to only one group. `FlagGroup::AddFlag` returns `false` for an unknown or
already associated flag, or for a group owned by another module. A flag is
unknown until it has been registered. `NGLOG_DEFINE` registers a flag during
the static initialization of the translation unit that defines it, and
`DynamicFlag` registers on construction. Calling `AddFlag` from the static
initializer of another translation unit therefore depends on the
initialization order, so add flags of other translation units from `main` or
later. The matching environment variable is applied as the
flag default when `AddFlag` runs. The
implicit `--help` option is always listed first and applies to all modules. The
name `help` is reserved for this built-in option.

The environment-variable section combines the prefixes of all modules, for
example `NGLOG_<flag>=<value>` followed by `MYAPP_<flag>=<value>`. Each entry
identifies the module whose flag default it sets. The prefix should include its
separator, such as the trailing underscore.

The representation of an individual flag can be replaced when its type or
default value needs a more useful presentation:

``` cpp
void WriteRetryCountHelp(nglog::HelpWriter& help,
                         const nglog::FlagInfo& flag) {
  help.Write("  --");
  help.Write(nglog::HelpTextStyle::kOption, flag.name);
  help.Write("=");
  help.Write(nglog::HelpTextStyle::kType, "count");
  help.Write(" (default=");
  help.Write(nglog::HelpTextStyle::kValue, flag.default_value + " attempts");
  help.Write(")\n      Retry failed requests this many times.\n");
}

nglog::SetFlagHelpFormatter("retry_count", WriteRetryCountHelp);
```

The callback receives a copy of the flag metadata and replaces the complete
generated entry, including its line breaks. Pass an empty callback to restore
the generated entry. For the built-in flags, the generated help uses a list-shaped
type such as `string[,string,...]` for `vmodule`, displays binary sizes with
binary units such as `MiB`, and displays time units such as `s` and `ms`.

Duplicate flag names are fatal. The diagnostic reports both source file names
and registered types, the likely cause, and how to resolve it. On Linux and
Windows, separate ng-log library instances in one process share their flag names
through memory owned by the process, so duplicates are also detected when
components privately link static or shared copies of the library. The instances
find that memory through an operating-system object: a private mapping of an
anonymous memory file on Linux and a named file mapping in the session namespace
on Windows. Neither the environment nor open file descriptors are involved, and
an instance that is unloaded releases its flags. Because another process in the
same session could create the Windows object first, an object that does not
refer to a registry in the memory of this process is ignored, which keeps the
flags working without the detection across library instances. A child created
with `fork` starts with a copy of the registry, while a program started by the
process starts with an empty one. On other platforms, duplicates are detected
within each library instance. Separate processes have independent registries.
The registry and the flag values remain usable in a forked child even if other
threads of the parent were using them at the time of the fork.

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

`stderrthreshold` (`int32`, default=2, which is `ERROR`)

:   Copy log messages at or above this severity level to `stderr` in addition
    to log files. The numbers of severity levels `INFO`, `WARNING`, `ERROR`,
    and `FATAL` are 0, 1, 2, and 3, respectively.

`minloglevel` (`int32`, default=0, which is `INFO`)

:   Discard messages below this severity level. The numbers of severity levels
    `INFO`, `WARNING`, `ERROR`, and `FATAL` are 0, 1, 2, and 3,
    respectively.

`log_dir` (`string`, default="")

:   Write log files to this directory instead of the default logging directory.

`logfile_mode` (`mode`, default=`0664`)

:   Set the permissions for newly created log files. The value is written in
    octal, with or without a leading zero, and ranges from `0000` through
    `07777`.

`max_log_size` (`uint32`, default=`1800 MiB`)

:   Limit each log file to approximately this size in MiB. A value of 0 is
    treated as 1 and values above 4095 are capped at 4095.

`v` (`int32`, default=0)

:   Show `#!cpp VLOG(m)` messages when `m` is at most this value. The applicable
    `#!bash --vmodule` entry overrides this value. Refer
    to [verbose
    logging](logging/index.md#verbose-logging) for more detail.

`vmodule` (`string[,string,...]`, default="")

:   Set per-module verbose levels. The argument has to contain a
    comma-separated list of `<module name>=<log level>`. `<module name>` is a
    glob pattern (e.g., `gfs*` for all modules whose name starts with "gfs"),
    matched against the source filename base. The base is the part before the
    first period, with a trailing `-inl` suffix removed. `<log level>`
    overrides any value given by `--v`. See also [verbose
    logging](logging/index.md#verbose-logging) for more details.

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
nonempty [`NO_COLOR`](https://no-color.org) environment variable or
`TERM=dumb`. A `CLICOLOR_FORCE` environment variable with a value other
than empty or `0` enables colors for pipes and files as well, unless
`NO_COLOR` disables them.

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

:   Color log messages written to `stderr`, including crash stack traces.
    Set to `false` to disable coloring.

`colorlogtostdout` (`bool`, default=`true`)

:   Color log messages written to `stdout`, including crash stack traces.
    Set to `false` to disable coloring.

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

You can also modify flag values in your program with `nglog::SetFlag`. Most
settings start working immediately after you update them. The exceptions are
the flags related to destination files. For instance, you might want to set
`FLAGS_log_dir` before calling `nglog::InitializeLogging`.

!!! example "Setting `log_dir` at runtime"
    ``` cpp
    LOG(INFO) << "file";
    // Most flags work immediately after updating values.
    nglog::SetFlag(FLAGS_logtostderr, true);
    LOG(INFO) << "stderr";
    nglog::SetFlag(FLAGS_logtostderr, false);
    // This won’t change the log destination. If you want to set this
    // value, you should do this before nglog::InitializeLogging .
    nglog::SetFlag(FLAGS_log_dir, "/some/log/directory");
    LOG(INFO) << "the same file";
    ```
