# SPDX-FileCopyrightText: 2026 The ng-log contributors
#
# SPDX-License-Identifier: BSD-3-Clause

cmake_minimum_required (VERSION 3.28)
project (ng-log_experimental_requirements LANGUAGES CXX)

find_package (ng-log REQUIRED NO_MODULE)

# Each case is a source file <group>/<name>.cc that must fail to compile as
# C++20. Its target is named <group>_<name>.
set (_cases
  compare/comma
  compare/comma_message
  compare/no_comparison
  compare/nodiscard
  ensure/constexpr
  ensure/constexpr_comparison
  ensure/constexpr_not_null
  ensure/constexpr_that
  ensure/nodiscard
  log/conflicting_destinations
  log/misdeclared_hook
  log/repeated_severity
  log/requires_header
  log/severity_out_of_range
  log/skip_log_file_alone
  log/two_stage_nodiscard
  rate_limit/every_bool
  rate_limit/every_floating
  rate_limit/first_duration
  rate_limit/first_floating
  requires/format
  requires/source_location
  signal_safe/alternate
  signal_safe/dynamic
  signal_safe/fill_brace
  signal_safe/fill_non_ascii
  signal_safe/leading_zero_width
  signal_safe/locale
  signal_safe/long_double_fixed
  signal_safe/missing_precision
  signal_safe/precision
  signal_safe/type
  signal_safe/width
  that/bound_non_integer
  that/byte_cstring
  that/cstring_ordering
  that/cstring_range
  that/ignore_case_non_cstring
  that/index_non_integer
  that/no_comparison
  that/nodiscard
  that/wide_cstring
)

foreach (_case IN LISTS _cases)
  string (REPLACE "/" "_" _target ${_case})
  add_executable (${_target} ${_case}.cc)
  target_compile_features (${_target} PRIVATE cxx_std_20)
  target_link_libraries (${_target} PRIVATE ng-log::ng-log)
endforeach (_case)

# Exactly C++17 since the case verifies that older standards are rejected.
add_executable (requires_cxx20 requires/cxx20.cc)
set_target_properties (requires_cxx20 PROPERTIES
  CXX_STANDARD 17
  CXX_STANDARD_REQUIRED ON
)
target_link_libraries (requires_cxx20 PRIVATE ng-log::ng-log)

# Discarded results are reported as warnings, which these cases turn into
# errors.
foreach (_target IN ITEMS compare_nodiscard ensure_nodiscard
    log_two_stage_nodiscard that_nodiscard)
  target_compile_options (${_target} PRIVATE
    $<IF:$<CXX_COMPILER_ID:MSVC>,/WX,-Werror>)
endforeach (_target)
