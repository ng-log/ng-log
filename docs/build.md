# Building from Source

## Bazel

To use ng-log within a project which uses the [Bazel](https://bazel.build/) build
tool, add the following lines to your `MODULE.bazel` file:

``` bazel title="MODULE.bazel"
bazel_dep(name = "gflags", version = "2.2.2")
bazel_dep(name = "ng-log", version = "0.8.1")
```

You can then add `@ng-log//:ng-log` to
the deps section of a `cc_binary` or
`cc_library` rule, and `#!cpp #include <ng-log/logging.h>` to
include it in your source code.

!!! example "Using ng-log in a Bazel project"
    ``` bazel
    cc_binary(
        name = "main",
        srcs = ["main.cc"],
        deps = ["@ng-log//:ng-log"],
    )
    ```

### Migrating from google/glog

The Bazel target doesn't include the glog compatibility layer, so you'll need to
adjust your code as follows:

-   replace `<glog/logging.h>` with `<ng-log/logging.h>` and similarly for other heads (flags, log_severity, stl_logging, etc)
-   replace the `google::` namespace with `nglog::`
-   replace `InitGoogleLogging` with `InitializeLogging`
-   replace `IsGoogleLoggingInitialized` with `IsLoggingInitialized`
-   replace `ShutdownGoogleLogging` with `ShutdownLogging`

## CMake

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
  Studio, use `#!bash cmake . -G <generator-name>`, and see `#!bash cmake
  --help` for the available generators. Also see `-T <toolset-name>`, which can
  be used to request the native x64 toolchain with `-T host=x64`.
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

Once successfully built, ng-log can be [integrated into own projects](usage.md).
