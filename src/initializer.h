// SPDX-FileCopyrightText: 2008-2023 Google Inc.
// SPDX-FileCopyrightText: 2026 The ng-log contributors
//
// SPDX-License-Identifier: BSD-3-Clause

// ---
// Author: Jacob Hoffman-Andrews

#ifndef NGLOG_INTERNAL_INITIALIZER_H
#define NGLOG_INTERNAL_INITIALIZER_H

class Initializer {
 public:
  using void_function = void (*)();
  Initializer(const char*, void_function f) { f(); }
};

#define REGISTER_MODULE_INITIALIZER(name, body)                          \
  namespace {                                                            \
  static void nglog_init_module_##name() { body; }                       \
  Initializer nglog_initializer_module_##name(#name,                     \
                                              nglog_init_module_##name); \
  }

#endif /* NGLOG_INTERNAL_INITIALIZER_H */
