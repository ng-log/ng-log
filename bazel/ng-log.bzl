# Implement a macro nglog_library() that the BUILD.bazel file can load.

# By default, ng-log is built with gflags support.  You can change this behavior
# by using nglog_library(with_gflags=0)
#
# This file is inspired by the following sample BUILD files:
#       https://github.com/google/glog/issues/61
#       https://github.com/google/glog/files/393474/BUILD.txt
#
# Known issue: the namespace parameter is not supported on Win32.

def expand_template_impl(ctx):
    ctx.actions.expand_template(
        template = ctx.file.template,
        output = ctx.outputs.out,
        substitutions = ctx.attr.substitutions,
    )

expand_template = rule(
    implementation = expand_template_impl,
    attrs = {
        "template": attr.label(mandatory = True, allow_single_file = True),
        "substitutions": attr.string_dict(mandatory = True),
        "out": attr.output(mandatory = True),
    },
)

def nglog_library(with_gflags = 1, **kwargs):
    if native.repository_name() != "@":
        repo_name = native.repository_name()[1:]  # Strip the first leading @
        gendir = "$(GENDIR)/external/" + repo_name
        src_windows = "external/%s/src/windows" % repo_name
    else:
        gendir = "$(GENDIR)"
        src_windows = "src/windows"

    # Config setting for WebAssembly target.
    native.config_setting(
        name = "wasm",
        values = {"cpu": "wasm"},
    )

    # Detect when building with clang-cl on Windows.
    native.config_setting(
        name = "clang-cl",
        values = {"compiler": "clang-cl"},
    )

    common_copts = [
        "-std=c++14",
        "-I%s/ng-log_internal" % gendir,
    ] + (["-DNGLOG_USE_GFLAGS"] if with_gflags else [])

    wasm_copts = [
        # Disable warnings that exists in ng-log.
        "-Wno-sign-compare",
        "-Wno-unused-function",
        "-Wno-unused-local-typedefs",
        "-Wno-unused-variable",
        # Allows src/logging.cc to determine the host name.
        "-DHAVE_SYS_UTSNAME_H",
        # For src/utilities.cc.
        "-DHAVE_SYS_TIME_H",
        # NOTE: users could optionally patch -DHAVE_UNWIND off if
        # stacktrace dumping is not needed
        "-DHAVE_UNWIND",
        # Enable dumping stacktrace upon sigaction.
        "-DHAVE_SIGACTION",
        # For logging.cc.
        "-DHAVE_PREAD",
        # -DHAVE_MODE_T prevent repeated typedef mode_t leading
        # to emcc compilation failure
        "-DHAVE_MODE_T",
        "-DHAVE_UNISTD_H",
    ]

    linux_or_darwin_copts = wasm_copts + [
        "-DNGLOG_EXPORT=__attribute__((visibility(\\\"default\\\")))",
        "-DNGLOG_NO_EXPORT=__attribute__((visibility(\\\"default\\\")))",
        "-DHAVE_POSIX_FADVISE",
        "-DHAVE_SSIZE_T",
        "-DHAVE_SYS_TYPES_H",
        # For src/utilities.cc.
        "-DHAVE_SYS_SYSCALL_H",
        # For src/logging.cc to create symlinks.
        "-fvisibility-inlines-hidden",
        "-fvisibility=hidden",
    ]

    freebsd_only_copts = [
        # Enable declaration of _Unwind_Backtrace
        "-D_GNU_SOURCE",
        "-DHAVE_LINK_H",
        "-DHAVE_SYMBOLIZE",  # Supported by <link.h>
    ]

    linux_only_copts = [
        # For utilities.h.
        "-DHAVE_EXECINFO_H",
        "-DHAVE_LINK_H",
        "-DHAVE_SYMBOLIZE",  # Supported by <link.h>
    ]

    darwin_only_copts = [
        # For stacktrace.
        "-DHAVE_DLADDR",
    ]

    windows_only_copts = [
        # Override -DNGLOG_EXPORT= from the cc_library's defines.
        "-DNGLOG_EXPORT=__declspec(dllexport)",
        "-DNGLOG_NO_ABBREVIATED_SEVERITIES",
        "-DNGLOG_NO_EXPORT=",
        "-DNGLOG_USE_WINDOWS_PORT",
        "-DHAVE__CHSIZE_S",
        "-DHAVE_DBGHELP",
        "-I" + src_windows,
    ]

    clang_cl_only_copts = [
        # Allow the override of -DNGLOG_EXPORT.
        "-Wno-macro-redefined",
    ]

    windows_only_srcs = [
        "src/windows/dirent.h",
        "src/windows/port.cc",
        "src/windows/port.h",
    ]

    gflags_deps = ["@gflags//:gflags"] if with_gflags else []

    final_lib_defines = select({
        # NGLOG_EXPORT is normally set by export.h, but that's not
        # generated for Bazel.
        "@bazel_tools//src/conditions:windows": [
            "NGLOG_DEPRECATED=__declspec(deprecated)",
            "NGLOG_EXPORT=",
            "NGLOG_NO_ABBREVIATED_SEVERITIES",
            "NGLOG_NO_EXPORT=",
        ],
        "//conditions:default": [
            "NGLOG_DEPRECATED=__attribute__((deprecated))",
            "NGLOG_EXPORT=__attribute__((visibility(\\\"default\\\")))",
            "NGLOG_NO_EXPORT=__attribute__((visibility(\\\"default\\\")))",
        ],
    })

    final_lib_copts = select({
        "@bazel_tools//src/conditions:windows": common_copts + windows_only_copts,
        "@bazel_tools//src/conditions:darwin": common_copts + linux_or_darwin_copts + darwin_only_copts,
        "@bazel_tools//src/conditions:freebsd": common_copts + linux_or_darwin_copts + freebsd_only_copts,
        ":wasm": common_copts + wasm_copts,
        "//conditions:default": common_copts + linux_or_darwin_copts + linux_only_copts,
    }) + select({
        ":clang-cl": clang_cl_only_copts,
        "//conditions:default": [],
    })

    # Needed to use these headers in `ng-log` and the test targets without
    # exposing them as public targets in `ng-log`
    native.filegroup(
        name = "shared_headers",
        srcs = [
            "src/base/commandlineflags.h",
            "src/stacktrace.h",
            "src/utilities.h",
        ]
    )

    native.cc_library(
        name = "ng-log",
        visibility = ["//visibility:public"],
        srcs = [
            ":config_h",
            ":shared_headers",
            "src/demangle.cc",
            "src/demangle.h",
            "src/flags.cc",
            "src/initializer.h",
            "src/logging.cc",
            "src/raw_logging.cc",
            "src/signalhandler.cc",
            "src/stacktrace.cc",
            "src/stacktrace.h",
            "src/stacktrace_generic-inl.h",
            "src/stacktrace_libunwind-inl.h",
            "src/stacktrace_powerpc-inl.h",
            "src/stacktrace_unwind-inl.h",
            "src/stacktrace_windows-inl.h",
            "src/stacktrace_x86-inl.h",
            "src/symbolize.cc",
            "src/symbolize.h",
            "src/utilities.cc",
            "src/utilities.h",
            "src/vlog_is_on.cc",
        ] + select({
            "@bazel_tools//src/conditions:windows": windows_only_srcs,
            "//conditions:default": [],
        }),
        hdrs = [
            "src/ng-log/flags.h",
            "src/ng-log/log_severity.h",
            "src/ng-log/logging.h",
            "src/ng-log/platform.h",
            "src/ng-log/raw_logging.h",
            "src/ng-log/stl_logging.h",
            "src/ng-log/types.h",
            "src/ng-log/vlog_is_on.h",
        ],
        # https://github.com/google/glog/issues/837: Replacing
        # `strip_include_prefix` with `includes` would avoid spamming
        # downstream projects with compiler warnings, but would also leak
        # private headers like stacktrace.h, because strip_include_prefix's
        # implementation only creates symlinks for the public hdrs. I suspect
        # the only way to avoid this is to refactor the project including the
        # CMake build, so that the private headers are in a ng-log_internal
        # subdirectory.
        strip_include_prefix = "src",
        defines = final_lib_defines,
        copts = final_lib_copts,
        deps = gflags_deps + select({
            "@bazel_tools//src/conditions:windows": [":strip_include_prefix_hack"],
            "//conditions:default": [],
        }),
        linkopts = select({
            "@bazel_tools//src/conditions:windows": ["dbghelp.lib"],
            "//conditions:default": [],
        }),
        **kwargs
    )

    test_list = [
        "cleanup_immediately",
        "cleanup_with_absolute_prefix",
        "cleanup_with_relative_prefix",
        # "demangle", # Broken
        # "logging", # Broken
        # "mock-log", # Broken
        # "signalhandler", # Pointless
        "stacktrace",
        "stl_logging",
        # "symbolize", # Broken
        "utilities",
    ]

    test_only_copts = [
        "-DTEST_SRC_DIR=\\\"%s/tests\\\"" % gendir,
    ]

    for test_name in test_list:
        native.cc_test(
            name = test_name + "_test",
            visibility = ["//visibility:public"],
            srcs = [
                ":config_h",
                ":shared_headers",
                "src/googletest.h",
                "src/" + test_name + "_unittest.cc",
            ],
            defines = final_lib_defines,
            copts = final_lib_copts + test_only_copts,
            deps = gflags_deps + [
                ":ng-log",
                "@googletest//:gtest",
            ],
            **kwargs
        )

    # Workaround https://github.com/bazelbuild/bazel/issues/6337 by declaring
    # the dependencies without strip_include_prefix.
    native.cc_library(
        name = "strip_include_prefix_hack",
        hdrs = [
            "src/ng-log/flags.h",
            "src/ng-log/log_severity.h",
            "src/ng-log/logging.h",
            "src/ng-log/platform.h",
            "src/ng-log/raw_logging.h",
            "src/ng-log/stl_logging.h",
            "src/ng-log/types.h",
            "src/ng-log/vlog_is_on.h",
        ],
    )

    expand_template(
        name = "config_h",
        template = "src/config.h.cmake.in",
        out = "ng-log_internal/config.h",
        substitutions = {"#cmakedefine": "//cmakedefine"},
    )
