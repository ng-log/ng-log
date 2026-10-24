# SPDX-FileCopyrightText: 2026 The ng-log contributors
#
# SPDX-License-Identifier: BSD-3-Clause
#
# Author: Sergiu Deitsch

# Prints the using-declarations that the nglog.experimental module exports,
# namely the declarations of ng-log/experimental/logging.h in
# nglog::experimental and those of the headers it re-exports in nglog and
# nglog::base. Clang dumps each outermost nglog namespace block that the
# filter matches as a separate object together with its file, which selects
# the headers. Run from the source directory with a build directory <build>
# that provides ng-log/export.h:
#
#   echo '#include "ng-log/experimental/logging.h"' |
#     clang++ -std=c++20 -I<build> -Isrc -fsyntax-only -x c++ - \
#       -Xclang -ast-dump=json -Xclang -ast-dump-filter=nglog |
#     jq -r -f src/experimental/module_exports.jq | sort -u
#
# Adding --target=x86_64-w64-mingw32 with the build directory of a MinGW
# build yields the declarations made only on Windows as well, which the
# module guards with NGLOG_OS_WINDOWS. The consumer of the module in
# src/package_config_unittest/components names the same declarations to check
# that the module exports each of them.

def names($namespace):
  .inner[]?
  | select((.isImplicit | not) and .name != null
      and (.name | startswith("<") | not)
      and (.kind | test("^(Namespace|Using|StaticAssert)") | not))
  | "using \($namespace)::\(.name);";

select(.kind == "NamespaceDecl" and .name == "nglog")
| (.loc.file // .loc.spellingLoc.file // "") as $file
| (if ($file | test("/log_(setup|sink|severity)\\.h$"))
   then names("nglog"),
     (.inner[]? | select(.kind == "NamespaceDecl" and .isInline)
      | names("nglog"))
   else empty end),
  (.inner[]? | select(.kind == "NamespaceDecl")
   | if .name == "experimental" then names("nglog::experimental")
     elif .name == "base" and ($file | test("/log_setup\\.h$"))
     then names("nglog::base")
     else empty end)
