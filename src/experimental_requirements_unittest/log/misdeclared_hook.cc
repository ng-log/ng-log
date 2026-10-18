// SPDX-FileCopyrightText: 2026 The ng-log contributors
//
// SPDX-License-Identifier: BSD-3-Clause
//
// Author: Sergiu Deitsch

// A member named after a hook whose signature does not match it must be
// rejected instead of being ignored silently.

#include <ng-log/experimental/log.h>

// Admit() lacks const, so it would never run.
struct Gate {
  bool Admit(nglog::experimental::Context& context);
  void Prefix(nglog::experimental::Message& message) const;
};

int main() { nglog::experimental::Log(Gate{}, "message"); }
