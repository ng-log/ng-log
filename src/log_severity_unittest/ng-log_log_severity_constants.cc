// SPDX-FileCopyrightText: 2008-2024 Google Inc.
// SPDX-FileCopyrightText: 2026 The ng-log contributors
//
// SPDX-License-Identifier: BSD-3-Clause
//
// Author: Sergiu Deitsch

#include <ng-log/logging.h>

int main() {
  // Must not compile
  LOG(0) << "type unsafe info";
  LOG(1) << "type unsafe info";
  LOG(2) << "type unsafe info";
  LOG(3) << "type unsafe info";
}
