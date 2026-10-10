// SPDX-FileCopyrightText: 2019-2023 Google Inc.
// SPDX-FileCopyrightText: 2026 The ng-log contributors
//
// SPDX-License-Identifier: BSD-3-Clause

#include "ng-log/logging.h"

int main(int /*argc*/, char** argv) { nglog::InitializeLogging(argv[0]); }
