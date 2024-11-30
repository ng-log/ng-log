# Building from Source

## Bazel

To use ng-log within a project which uses the [Bazel](https://bazel.build/) build
tool, add the following lines to your `MODULE.bazel` file:

``` bazel title="MODULE.bazel"
bazel_dep(name = "glog")

archive_override(
    module_name = "ng-log",
    urls = "https://github.com/ng-log/ng-log/archive/cc0de6c200375b33d907ee7632eee2f173b33a09.tar.gz",
    strip_prefix = "ng-log-cc0de6c200375b33d907ee7632eee2f173b33a09",  # Latest commit as of 2024-06-08.
    integrity = "sha256-rUrv4EBkdc+4Wbhfxp+KoRstlj2Iw842/OpLfDq0ivg=",
)
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
