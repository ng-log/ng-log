# ng-log

ng-log (formerly Google Logging Library or glog) is a C++14 library
that implements application-level logging. The library provides logging APIs
based on C++-style streams and various helper macros.

!!! note "Compatibility to glog"
    To avoid trademark issues, ng-log introduces a new set of functions derived
    from their glog counterparts. During a grace period, ng-log will maintain
    API compatibility to glog up to version 0.9.0 of the library. We therefore
    recommend updating the API as soon possible as the compatibility layer will
    be eventually removed.

# How to Use

You can log a message by simply streaming things to `LOG`(<a particular
[severity level](logging.md#severity-levels)\>), e.g.,

``` cpp title="main.cpp"
#include <ng-log/logging.h>

int main(int argc, char* argv[]) {
    nglog::InitializeLogging(argv[0]); // (1)!
    LOG(INFO) << "Found " << num_cookies << " cookies"; // (2)!
}
```

1. Initialize library
2. Log a message with informational severity

The library can be installed using various [package managers](packages.md) or
compiled [from source](build.md). For a detailed overview of ng-log features and
their usage, please refer to the [user guide](logging.md).

!!! warning
    The above example requires further [Bazel](build.md#bazel) or
    [CMake](usage.md) setup for use in own projects.
