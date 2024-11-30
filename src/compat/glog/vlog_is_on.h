// Copyright (c) 2024, Google Inc.
// All rights reserved.
//
// Redistribution and use in source and binary forms, with or without
// modification, are permitted provided that the following conditions are
// met:
//
//     * Redistributions of source code must retain the above copyright
// notice, this list of conditions and the following disclaimer.
//     * Redistributions in binary form must reproduce the above
// copyright notice, this list of conditions and the following disclaimer
// in the documentation and/or other materials provided with the
// distribution.
//     * Neither the name of Google Inc. nor the names of its
// contributors may be used to endorse or promote products derived from
// this software without specific prior written permission.
//
// THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
// "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
// LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
// A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
// OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
// SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
// LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
// DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
// THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
// (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
// OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
//
// Author: Ray Sidney and many others
//
// Defines the VLOG_IS_ON macro that controls the variable-verbosity
// conditional logging.
//
// It's used by VLOG and VLOG_IF in logging.h
// and by RAW_VLOG in raw_logging.h to trigger the logging.
//
// It can also be used directly e.g. like this:
//   if (VLOG_IS_ON(2)) {
//     // do some logging preparation and logging
//     // that can't be accomplished e.g. via just VLOG(2) << ...;
//   }
//
// The truth value that VLOG_IS_ON(level) returns is determined by
// the three verbosity level flags:
//   --v=<n>  Gives the default maximal active V-logging level;
//            0 is the default.
//            Normally positive values are used for V-logging levels.
//   --vmodule=<str>  Gives the per-module maximal V-logging levels to override
//                    the value given by --v.
//                    E.g. "my_module=2,foo*=3" would change the logging level
//                    for all code in source files "my_module.*" and "foo*.*"
//                    ("-inl" suffixes are also disregarded for this matching).
//
// SetVLOGLevel helper function is provided to do limited dynamic control over
// V-logging by overriding the per-module settings given via --vmodule flag.
//
// CAVEAT: --vmodule functionality is not available in non gcc compilers.
//

#ifndef NGLOG_COMPAT_VLOG_IS_ON_H
#define NGLOG_COMPAT_VLOG_IS_ON_H

#include "ng-log/vlog_is_on.h"

#pragma message( \
    "<glog/vlog_is_on.h> is deprecated and will be removed in ng-log 0.9.0. Please use <ng-log/vlog_is_on.h> instead.")

#endif  // NGLOG_COMPAT_VLOG_IS_ON_H
