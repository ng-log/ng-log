<!--
SPDX-FileCopyrightText: 2024 Google Inc.
SPDX-FileCopyrightText: 2026 The ng-log contributors

SPDX-License-Identifier: BSD-3-Clause
-->

# Building from Source

ng-log can be compiled using [CMake](http://www.cmake.org) on a wide range of
platforms. The typical workflow for building ng-log on a Unix-like system with GNU
Make as build tool is as follows:

1.  Clone the repository and change into source directory.
  ``` bash
  git clone https://github.com/ng-log/ng-log.git
  cd ng-log
  ```
2.  Run CMake to configure the build tree.
  ``` bash
  cmake -S . -B build -G "Unix Makefiles"
  ```
  CMake provides different generators, and by default will pick the most
  relevant one to your environment. If you need a specific version of Visual
  Studio, use `#!bash cmake -S . -B build -G <generator-name>`, and see
  `#!bash cmake --help` for the available generators. Also see
  `-T <toolset-name>`, which can be used to request the native x64 toolchain
  with `-T host=x64`.
3.  Afterwards, generated files can be used to compile the project.
  ``` bash
  cmake --build build
  ```
4.  Test the build software (optional).
  ``` bash
  cmake --build build --target test
  ```
5.  Install the built files (optional).
  ``` bash
  cmake --build build --target install
  ```

Building the tests requires GoogleTest and GoogleMock 1.11 or later.

## Build Options

The following CMake options configure the build, for example with
`#!bash cmake -S . -B build -DWITH_LINE_INFO=addr2line`:

`BUILD_SHARED_LIBS` (default `ON`)

:   Build ng-log as a shared library instead of a static one.

`BUILD_EXAMPLES` (default `ON`)

:   Build the examples in the `examples` directory.

`BUILD_TESTING` (default `ON`)

:   Build the tests.

`WITH_SYMBOLIZE` (default `ON`)

:   Resolve function names in stack traces.

`WITH_LINE_INFO` (default `auto`)

:   Select how source files and line numbers in stack traces are resolved:
    `auto`, `addr2line`, `libbacktrace` or `none`. See [resolving file names and
    line numbers](failures.md#resolving-file-names-and-line-numbers).

`WITH_UNWIND` (default `libunwind`)

:   Select the stack unwinder that CMake may use when it is available:
    `libunwind`, `unwind` for `_Unwind_Backtrace`, or `none`. See the
    [unwinder notes](unwinder.md).

`PRINT_UNSYMBOLIZED_STACK_TRACES` (default `OFF`)

:   Print file offsets in stack traces instead of symbolizing them.

`WITH_EXPERIMENTAL` (default `ON`)

:   Build the [experimental logging functions](logging/experimental.md) if the
    compiler supports them.

`WITH_PKGCONFIG` (default `OFF`)

:   Install a pkg-config file. Its library name follows the build
    configuration, such as `ng-logd` for debug builds. Installing several
    configurations into one prefix keeps the file of the last one.

`WITH_LOCK_METRICS` (default `OFF`)

:   Record internal lock contention metrics, which the benchmarks report.

`WITH_FUZZING` (default `none`)

:   Build the fuzz targets for `libfuzzer` or `ossfuzz`.

Once successfully built, ng-log can be [integrated into own projects](usage.md).
