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
