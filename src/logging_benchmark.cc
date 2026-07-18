// Copyright (c) 2008, Google Inc.
// Copyright (c) 2026, The ng-log contributors
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
// Microbenchmarks for the cost of CHECK/LOG/VLOG calls.

#include <benchmark/benchmark.h>

#include "ng-log/logging.h"

#ifdef NGLOG_USE_GFLAGS
#  include <gflags/gflags.h>
using namespace GFLAGS_NAMESPACE;
#endif

using namespace nglog;

namespace {

// Non-constant so the compiler cannot fold the comparisons below away.
int x = -1;

void CheckFailure(int, int, const char* /* file */, int /* line */,
                  const char* /* msg */) {}

void BM_Check1(benchmark::State& state) {
  int n = 0;
  for (auto _ : state) {
    CHECK_GE(n, x);
    CHECK_GE(n, x);
    CHECK_GE(n, x);
    CHECK_GE(n, x);
    CHECK_GE(n, x);
    CHECK_GE(n, x);
    CHECK_GE(n, x);
    CHECK_GE(n, x);
    ++n;
  }
}
BENCHMARK(BM_Check1);

void BM_Check2(benchmark::State& state) {
  int n = 0;
  for (auto _ : state) {
    CHECK(n >= x);
    CHECK(n >= x);
    CHECK(n >= x);
    CHECK(n >= x);
    CHECK(n >= x);
    CHECK(n >= x);
    CHECK(n >= x);
    CHECK(n >= x);
    ++n;
  }
}
BENCHMARK(BM_Check2);

void BM_Check3(benchmark::State& state) {
  int n = 0;
  for (auto _ : state) {
    if (n < x) CheckFailure(n, x, __FILE__, __LINE__, "n < x");
    if (n < x) CheckFailure(n, x, __FILE__, __LINE__, "n < x");
    if (n < x) CheckFailure(n, x, __FILE__, __LINE__, "n < x");
    if (n < x) CheckFailure(n, x, __FILE__, __LINE__, "n < x");
    if (n < x) CheckFailure(n, x, __FILE__, __LINE__, "n < x");
    if (n < x) CheckFailure(n, x, __FILE__, __LINE__, "n < x");
    if (n < x) CheckFailure(n, x, __FILE__, __LINE__, "n < x");
    if (n < x) CheckFailure(n, x, __FILE__, __LINE__, "n < x");
    ++n;
  }
}
BENCHMARK(BM_Check3);

void BM_logspeed(benchmark::State& state) {
  for (auto _ : state) {
    LOG(INFO) << "test message";
  }
}
BENCHMARK(BM_logspeed);

void BM_vlog(benchmark::State& state) {
  for (auto _ : state) {
    VLOG(1) << "test message";
  }
}
BENCHMARK(BM_vlog);

}  // namespace

int main(int argc, char** argv) {
  InitializeLogging(argv[0]);
#ifdef NGLOG_USE_GFLAGS
  ParseCommandLineFlags(&argc, &argv, true);
#endif

  benchmark::Initialize(&argc, argv);
  if (benchmark::ReportUnrecognizedArguments(argc, argv)) {
    return 1;
  }
  benchmark::RunSpecifiedBenchmarks();
  benchmark::Shutdown();
  return 0;
}
