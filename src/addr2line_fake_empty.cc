// SPDX-License-Identifier: BSD-3-Clause
//
// Author: Sergiu Deitsch
//
// A stand-in for an "addr2line" that exits immediately without printing
// anything, used by addr2line_unittest.cc to distinguish that case from
// addr2line being entirely absent from PATH (no process spawned) or hanging
// (RunAddr2Line() reaches EOF here well within the timeout).

int main() { return 0; }
