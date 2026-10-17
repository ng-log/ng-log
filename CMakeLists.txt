# SPDX-FileCopyrightText: 2015-2024 Google Inc.
# SPDX-FileCopyrightText: 2026 The ng-log contributors
#
# SPDX-License-Identifier: BSD-3-Clause

cmake_minimum_required (VERSION 3.28...4.4)

if (CMAKE_VERSION VERSION_GREATER_EQUAL 4.3)
  if (CMAKE_VERSION VERSION_LESS 4.4)
    set (CMAKE_EXPERIMENTAL_GENERATE_SBOM
      ca494ed3-b261-4205-a01f-603c95e4cae0)
  else (CMAKE_VERSION VERSION_LESS 4.4)
    set (CMAKE_EXPERIMENTAL_GENERATE_SBOM
      2d856d6d-53e8-488b-a17f-d486d2cac317)
  endif (CMAKE_VERSION VERSION_LESS 4.4)
endif (CMAKE_VERSION VERSION_GREATER_EQUAL 4.3)

project (ng-log
  VERSION 0.9.0
  DESCRIPTION "C++ logging module"
  HOMEPAGE_URL https://github.com/ng-log/ng-log
  LANGUAGES CXX
)

list (APPEND CMAKE_MODULE_PATH ${ng-log_SOURCE_DIR}/cmake)

include (CheckCXXSourceCompiles)
include (CheckCXXSourceRuns)
include (CheckCXXSymbolExists)
include (CheckIncludeFileCXX)
include (CheckStructHasMember)
include (CheckTypeSize)
include (CMakePackageConfigHelpers)
include (CMakePushCheckState)
include (CTest)
include (GenerateExportHeader)
include (GNUInstallDirs)

# An embedding project owns the global library type. Default to a shared
# library within this directory only.
if (PROJECT_IS_TOP_LEVEL)
  option (BUILD_SHARED_LIBS "Build shared libraries" ON)
elseif (NOT DEFINED BUILD_SHARED_LIBS)
  set (BUILD_SHARED_LIBS ON)
endif (PROJECT_IS_TOP_LEVEL)

option (BUILD_EXAMPLES "Build examples" ON)
option (WITH_LOCK_METRICS
  "Enable internal lock contention metrics" OFF)
option (PRINT_UNSYMBOLIZED_STACK_TRACES
  "Print file offsets in traces instead of symbolizing" OFF)
option (WITH_EXPERIMENTAL
  "Build the experimental logging functions if the compiler supports them" ON)
option (WITH_PKGCONFIG "Enable pkg-config support" OFF)
option (WITH_SYMBOLIZE "Enable symbolize module" ON)

set (WITH_UNWIND libunwind CACHE STRING "unwind driver")
set_property (CACHE WITH_UNWIND PROPERTY STRINGS none unwind libunwind)

# Backend for resolving file names and line numbers in symbolized stack
# frames. "auto" uses libbacktrace when available, since it resolves
# in-process without spawning a subprocess per frame, and falls back to
# addr2line otherwise. "addr2line" and "libbacktrace" force one specific
# backend. "none" disables both. A CMake boolean false (OFF, FALSE, 0, NO,
# an empty string, ...) is treated the same as "none", and anything else
# that is not one of the four names above (including a boolean true such
# as ON) is treated the same as "auto".
# When built with MinGW, "addr2line" replaces the DbgHelp-based backend
# entirely rather than supplementing it: DbgHelp cannot read the DWARF
# debug info a MinGW build emits by default, so addr2line resolves the
# symbol name too, not just the file and line. MSVC builds always use
# DbgHelp, since addr2line requires MinGW.
set (WITH_LINE_INFO auto CACHE STRING
  "Backend for resolving file names and line numbers in stack traces")
set_property (CACHE WITH_LINE_INFO PROPERTY STRINGS none auto addr2line libbacktrace)

if (NOT WITH_LINE_INFO)
  set (WITH_LINE_INFO none)
elseif (NOT WITH_LINE_INFO MATCHES "^(none|auto|addr2line|libbacktrace)$")
  set (WITH_LINE_INFO auto)
endif (NOT WITH_LINE_INFO)

set (WITH_FUZZING none CACHE STRING "Fuzzing engine")
set_property (CACHE WITH_FUZZING PROPERTY STRINGS none libfuzzer ossfuzz)

if (NOT WITH_UNWIND OR WITH_UNWIND STREQUAL none)
  set (CMAKE_DISABLE_FIND_PACKAGE_Unwind ON)
endif (NOT WITH_UNWIND OR WITH_UNWIND STREQUAL none)

set (_ng-log_try_addr2line OFF)
set (_ng-log_try_libbacktrace OFF)

if (WITH_LINE_INFO STREQUAL auto OR WITH_LINE_INFO STREQUAL addr2line)
  set (_ng-log_try_addr2line ON)
endif (WITH_LINE_INFO STREQUAL auto OR WITH_LINE_INFO STREQUAL addr2line)

if (WITH_LINE_INFO STREQUAL auto OR WITH_LINE_INFO STREQUAL libbacktrace)
  set (_ng-log_try_libbacktrace ON)
endif (WITH_LINE_INFO STREQUAL auto OR WITH_LINE_INFO STREQUAL libbacktrace)

if (NOT _ng-log_try_libbacktrace)
  set (CMAKE_DISABLE_FIND_PACKAGE_Libbacktrace ON)
endif (NOT _ng-log_try_libbacktrace)

set (CMAKE_CXX_VISIBILITY_PRESET hidden)
set (CMAKE_POSITION_INDEPENDENT_CODE ON)
set (CMAKE_VISIBILITY_INLINES_HIDDEN ON)

set (CMAKE_DEBUG_POSTFIX d)

if (BUILD_TESTING)
  # Tests always use the system-provided GTest/GMock. When unavailable, the
  # unit test section below is skipped entirely rather than falling back to a
  # bundled reimplementation.
  find_package (GTest 1.11 COMPONENTS GTest GMock NO_MODULE)
endif (BUILD_TESTING)

# Benchmarks are optional and are skipped when Google Benchmark is not
# available.
find_package (benchmark 1.6 NO_MODULE)

find_package (Threads REQUIRED)
find_package (Unwind)
find_package (Libbacktrace)

if (Unwind_FOUND)
  cmake_push_check_state (RESET)
  set (CMAKE_REQUIRED_LIBRARIES unwind::unwind)

  # Check whether linking actually succeeds. ARM toolchains of LLVM unwind
  # implementation do not necessarily provide the _Unwind_Backtrace function
  # which causes the previous check to succeed but the linking to fail.
  check_cxx_symbol_exists (_Unwind_Backtrace unwind.h HAVE__UNWIND_BACKTRACE)
  check_cxx_symbol_exists (_Unwind_GetIP unwind.h HAVE__UNWIND_GETIP)

  check_cxx_symbol_exists (unw_get_reg libunwind.h HAVE_UNW_GET_REG)
  check_cxx_symbol_exists (unw_getcontext libunwind.h HAVE_UNW_GETCONTEXT)
  check_cxx_symbol_exists (unw_init_local libunwind.h HAVE_UNW_INIT_LOCAL)
  check_cxx_symbol_exists (unw_step libunwind.h HAVE_UNW_STEP)

  if (HAVE__UNWIND_BACKTRACE AND HAVE__UNWIND_GETIP)
    set (_HAVE_UNWIND 1)
  endif (HAVE__UNWIND_BACKTRACE AND HAVE__UNWIND_GETIP)

  if (HAVE_UNW_GET_REG AND HAVE_UNW_GETCONTEXT AND HAVE_UNW_INIT_LOCAL AND HAVE_UNW_STEP)
    set (_HAVE_LIBUNWIND 1)
  endif (HAVE_UNW_GET_REG AND HAVE_UNW_GETCONTEXT AND HAVE_UNW_INIT_LOCAL AND HAVE_UNW_STEP)

  if (WITH_UNWIND STREQUAL unwind)
    if (_HAVE_UNWIND)
      set (HAVE_UNWIND 1)
    endif (_HAVE_UNWIND)
  elseif (WITH_UNWIND STREQUAL libunwind)
    if (_HAVE_LIBUNWIND)
      set (HAVE_LIBUNWIND 1)
    endif (_HAVE_LIBUNWIND)
  endif (WITH_UNWIND STREQUAL unwind)

  unset (_HAVE_LIBUNWIND)
  unset (_HAVE_UNWIND)

  cmake_pop_check_state ()
endif (Unwind_FOUND)

check_include_file_cxx (dlfcn.h HAVE_DLFCN_H)
check_include_file_cxx (elf.h HAVE_ELF_H)
check_include_file_cxx (glob.h HAVE_GLOB_H)
check_include_file_cxx (link.h HAVE_LINK_H)
check_include_file_cxx (pwd.h HAVE_PWD_H)
check_include_file_cxx (sys/exec_elf.h HAVE_SYS_EXEC_ELF_H)
check_include_file_cxx (sys/syscall.h HAVE_SYS_SYSCALL_H)
check_include_file_cxx (sys/time.h HAVE_SYS_TIME_H)
check_include_file_cxx (sys/types.h HAVE_SYS_TYPES_H)
check_include_file_cxx (sys/utsname.h HAVE_SYS_UTSNAME_H)
check_include_file_cxx (sys/wait.h HAVE_SYS_WAIT_H)
check_include_file_cxx (syscall.h HAVE_SYSCALL_H)
check_include_file_cxx (syslog.h HAVE_SYSLOG_H)
check_include_file_cxx (ucontext.h HAVE_UCONTEXT_H)
check_include_file_cxx (unistd.h HAVE_UNISTD_H)

check_type_size (mode_t HAVE_MODE_T LANGUAGE CXX)
check_type_size (ssize_t HAVE_SSIZE_T LANGUAGE CXX)

check_cxx_symbol_exists (_chsize_s io.h HAVE__CHSIZE_S)
check_cxx_symbol_exists (backtrace execinfo.h HAVE_EXECINFO_BACKTRACE)
check_cxx_symbol_exists (backtrace_symbols execinfo.h
  HAVE_EXECINFO_BACKTRACE_SYMBOLS)
check_cxx_symbol_exists (dladdr dlfcn.h HAVE_DLADDR)
check_cxx_symbol_exists (execve unistd.h HAVE_EXECVE)
check_cxx_symbol_exists (execv unistd.h HAVE_EXECV)
check_cxx_symbol_exists (fcntl fcntl.h HAVE_FCNTL)
check_cxx_symbol_exists (fork unistd.h HAVE_FORK)
check_cxx_symbol_exists (getauxval sys/auxv.h HAVE_GETAUXVAL)
check_cxx_symbol_exists (issetugid unistd.h HAVE_ISSETUGID)
check_cxx_symbol_exists (_Fork unistd.h HAVE__FORK)
check_cxx_symbol_exists (nanosleep ctime HAVE_NANOSLEEP)
check_cxx_symbol_exists (pipe unistd.h HAVE_PIPE)
check_cxx_symbol_exists (pipe2 unistd.h HAVE_PIPE2)
check_cxx_symbol_exists (poll poll.h HAVE_POLL)
check_cxx_symbol_exists (posix_fadvise fcntl.h HAVE_POSIX_FADVISE)
check_cxx_symbol_exists (posix_spawn spawn.h HAVE_POSIX_SPAWN)
check_cxx_symbol_exists (posix_spawn_file_actions_addopen spawn.h
  HAVE_POSIX_SPAWN_FILE_ACTIONS_ADDOPEN)
check_cxx_symbol_exists (pread unistd.h HAVE_PREAD)
check_cxx_symbol_exists (pthread_getname_np pthread.h HAVE_PTHREAD_GETNAME_NP)
cmake_push_check_state (RESET)
set (CMAKE_REQUIRED_DEFINITIONS -D_GNU_SOURCE)
check_cxx_source_compiles ([=[
  #include <pthread.h>
  int main() {
#if defined(__APPLE__)
    return pthread_setname_np("test");
#else
    return pthread_setname_np(pthread_self(), "test");
#endif
  }
]=] HAVE_PTHREAD_SETNAME_NP)
cmake_pop_check_state ()
check_cxx_symbol_exists (pwrite unistd.h HAVE_PWRITE)
check_cxx_symbol_exists (sched_yield sched.h HAVE_SCHED_YIELD)
check_cxx_symbol_exists (sigaction signal.h HAVE_SIGACTION)
check_cxx_symbol_exists (sigaltstack signal.h HAVE_SIGALTSTACK)
check_cxx_symbol_exists (waitpid sys/wait.h HAVE_WAITPID)

# Emscripten emulates descriptor-relative operations by resolving complete
# pathnames, which remain subject to the same length limit.
if (NOT EMSCRIPTEN)
  check_cxx_source_compiles ([=[
#ifndef _GNU_SOURCE
#  define _GNU_SOURCE 1
#endif

#include <dirent.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

int main() {
  struct stat status;
  int fd = open(".", O_RDONLY | O_DIRECTORY);
  int child_fd = openat(fd, ".", O_RDONLY | O_DIRECTORY);
  static_cast<void>(fstatat(fd, ".", &status, 0));
  static_cast<void>(faccessat(fd, ".", F_OK, 0));
  static_cast<void>(unlinkat(fd, "missing", 0));
  static_cast<void>(symlinkat("missing", fd, "link"));
  DIR* directory = fdopendir(fd);
  static_cast<void>(directory);
  static_cast<void>(close(child_fd));
  return 0;
}
]=] HAVE_POSIX_LONG_PATH_SUPPORT)
endif (NOT EMSCRIPTEN)

if (WIN32)
  check_cxx_symbol_exists (GetThreadDescription
    "windows.h;processthreadsapi.h" HAVE_GET_THREAD_DESCRIPTION)
endif (WIN32)

cmake_push_check_state (RESET)
set (CMAKE_REQUIRED_LIBRARIES dbghelp)
check_cxx_symbol_exists (UnDecorateSymbolName "windows.h;dbghelp.h" HAVE_DBGHELP)
cmake_pop_check_state ()

if (NOT WITH_FUZZING OR WITH_FUZZING STREQUAL none)
  # Detect the optional system demangler for explicit on-demand use.
  check_cxx_symbol_exists (abi::__cxa_demangle cxxabi.h HAVE___CXA_DEMANGLE)
endif (NOT WITH_FUZZING OR WITH_FUZZING STREQUAL none)

check_cxx_symbol_exists (__argv cstdlib HAVE___ARGV)
check_cxx_symbol_exists (getprogname cstdlib HAVE_GETPROGNAME)
check_cxx_symbol_exists (program_invocation_short_name cerrno HAVE_PROGRAM_INVOCATION_SHORT_NAME)
check_cxx_source_compiles ([=[
#include <cstdlib>
extern char* __progname;
int main() { return __progname != nullptr ? EXIT_SUCCESS : EXIT_FAILURE; }
]=] HAVE___PROGNAME)

set (_PC_FIELDS
  "uc_mcontext.gregs[REG_PC]"          # Solaris x86 (32 + 64 bit)
  "uc_mcontext.gregs[REG_EIP]"         # Linux (i386)
  "uc_mcontext.gregs[REG_RIP]"         # Linux (x86_64)
  "uc_mcontext.sc_ip"                  # Linux (ia64)
  "uc_mcontext.pc"                     # Linux (mips)
  "uc_mcontext.uc_regs->gregs[PT_NIP]" # Linux (ppc)
  "uc_mcontext.gregs[R15]"             # Linux (arm old [untested])
  "uc_mcontext.arm_pc"                 # Linux (arm arch 5)
  "uc_mcontext.gp_regs[PT_NIP]"        # Suse SLES 11 (ppc64)
  "uc_mcontext.mc_eip"                 # FreeBSD (i386)
  "uc_mcontext.mc_rip"                 # FreeBSD (x86_64 [untested])
  "uc_mcontext.__gregs[_REG_EIP]"      # NetBSD (i386)
  "uc_mcontext.__gregs[_REG_RIP]"      # NetBSD (x86_64)
  "uc_mcontext->ss.eip"                # OS X (i386, <=10.4)
  "uc_mcontext->__ss.__eip"            # OS X (i386, >=10.5)
  "uc_mcontext->ss.rip"                # OS X (x86_64)
  "uc_mcontext->__ss.__rip"            # OS X (>=10.5 [untested])
  "uc_mcontext->ss.srr0"               # OS X (ppc, ppc64 [untested])
  "uc_mcontext->__ss.__srr0"           # OS X (>=10.5 [untested])
)

if (HAVE_UCONTEXT_H AND NOT DEFINED PC_FROM_UCONTEXT)
  cmake_push_check_state (RESET)

  set (CMAKE_REQUIRED_DEFINITIONS -D_GNU_SOURCE)
  set (_PC_HEADERS ucontext.h signal.h)

  foreach (_PC_FIELD IN LISTS _PC_FIELDS)
    foreach (_PC_HEADER IN LISTS _PC_HEADERS)
      # Replace non-alphanumeric characters by underscores since the name will be
      # used as preprocessor definition.
      string (REGEX REPLACE "[^a-zA-Z0-9]" "_" HAVE_UCONTEXT_FIELD_NAME
        "HAVE_PC_FROM_UCONTEXT_${_PC_FIELD}")
      # Strip trailing underscores for readability
      string (REGEX REPLACE "_+$" "" HAVE_UCONTEXT_FIELD_NAME
        "${HAVE_UCONTEXT_FIELD_NAME}")

      check_struct_has_member (ucontext_t ${_PC_FIELD} ${_PC_HEADER}
        ${HAVE_UCONTEXT_FIELD_NAME} LANGUAGE CXX)

      if (${HAVE_UCONTEXT_FIELD_NAME})
        set (PC_FROM_UCONTEXT ${_PC_FIELD} CACHE STRING
          "<${_PC_HEADER}> ucontext_t PC member")
        mark_as_advanced (PC_FROM_UCONTEXT)
        break ()
      endif (${HAVE_UCONTEXT_FIELD_NAME})
    endforeach (_PC_HEADER)

    if (${HAVE_UCONTEXT_FIELD_NAME})
      break ()
    endif (${HAVE_UCONTEXT_FIELD_NAME})
  endforeach (_PC_FIELD)

  cmake_pop_check_state ()
endif (HAVE_UCONTEXT_H AND NOT DEFINED PC_FROM_UCONTEXT)

# Excluded on Emscripten: it has no real process model, so neither
# posix_spawn() nor fork()+exec() can actually run a subprocess there,
# regardless of what the symbol checks below report. CreateProcess() is
# always available on Windows, unlike posix_spawn()/fork(), which are each
# optional individually.
if (NOT EMSCRIPTEN AND
    (WIN32 OR
     (
       (
         (HAVE_POSIX_SPAWN AND HAVE_POSIX_SPAWN_FILE_ACTIONS_ADDOPEN) OR
         (HAVE_FORK AND HAVE_EXECVE)
       ) AND HAVE_PIPE AND HAVE_WAITPID AND HAVE_POLL
     )
    ))
  set (HAVE_SUBPROCESS 1)
endif ()

if (WITH_SYMBOLIZE)
  if (WIN32 OR CYGWIN)
    if (MINGW AND _ng-log_try_addr2line AND HAVE_SUBPROCESS)
      # addr2line resolves both the symbol name and the file/line from
      # DWARF debug info (what MinGW emits by default) on its own,
      # without going through DbgHelp at all, so this does not need the
      # DbgHelp probe below. MSVC builds keep using DbgHelp: they emit
      # CodeView, not DWARF, and addr2line does not read that format.
      set (HAVE_SYMBOLIZE 1)
      set (HAVE_ADDR2LINE 1)
    elseif (MINGW AND _ng-log_try_libbacktrace AND
            TARGET libbacktrace::libbacktrace)
      # Likewise, libbacktrace resolves both the symbol name and the
      # file/line from DWARF debug info (what MinGW emits by default) via
      # its own PE/COFF and DWARF readers, without going through DbgHelp.
      set (HAVE_SYMBOLIZE 1)
    else (MINGW AND _ng-log_try_addr2line AND HAVE_SUBPROCESS)
      cmake_push_check_state (RESET)
      set (CMAKE_REQUIRED_LIBRARIES DbgHelp)

      check_cxx_source_runs ([=[
      #include <windows.h>
      #include <dbghelp.h>
      #include <cstdlib>

      void foobar() { }

      int main()
      {
          HANDLE process = GetCurrentProcess();

          if (!SymInitialize(process, NULL, TRUE))
              return EXIT_FAILURE;

          char buf[sizeof(SYMBOL_INFO) + MAX_SYM_NAME];
          SYMBOL_INFO *symbol = reinterpret_cast<SYMBOL_INFO *>(buf);
          symbol->SizeOfStruct = sizeof(SYMBOL_INFO);
          symbol->MaxNameLen = MAX_SYM_NAME;

          void* const pc = reinterpret_cast<void*>(&foobar);
          BOOL ret = SymFromAddr(process, reinterpret_cast<DWORD64>(pc), 0, symbol);

          return ret ? EXIT_SUCCESS : EXIT_FAILURE;
      }
      ]=] HAVE_SYMBOLIZE)

      cmake_pop_check_state ()

    endif (MINGW AND _ng-log_try_addr2line AND HAVE_SUBPROCESS)
  elseif (APPLE AND HAVE_DLADDR)
    set (HAVE_SYMBOLIZE 1)
  elseif (UNIX)
    if (HAVE_ELF_H OR HAVE_SYS_EXEC_ELF_H)
      set (HAVE_SYMBOLIZE 1)
    endif (HAVE_ELF_H OR HAVE_SYS_EXEC_ELF_H)
  endif (WIN32 OR CYGWIN)
endif (WITH_SYMBOLIZE)

# Excluded on Emscripten: it has no real process model, so neither
# posix_spawn() nor fork()+exec() can actually run an addr2line subprocess
# there. POSIX addr2line additionally requires _Fork()+execv() because it can
# run from a signal handler.
if (_ng-log_try_addr2line AND HAVE_SYMBOLIZE AND UNIX AND NOT EMSCRIPTEN AND
    (HAVE_ELF_H OR HAVE_SYS_EXEC_ELF_H) AND HAVE_SIGACTION AND
    HAVE_SUBPROCESS AND HAVE__FORK AND HAVE_EXECV)
  set (HAVE_ADDR2LINE 1)
endif ()

if (_ng-log_try_libbacktrace AND HAVE_SYMBOLIZE AND TARGET libbacktrace::libbacktrace
    AND (UNIX OR MINGW))
  set (HAVE_LIBBACKTRACE 1)
endif (_ng-log_try_libbacktrace AND HAVE_SYMBOLIZE AND
       TARGET libbacktrace::libbacktrace AND (UNIX OR MINGW))

if (WITH_LINE_INFO STREQUAL addr2line AND NOT HAVE_ADDR2LINE)
  message (WARNING "WITH_LINE_INFO is 'addr2line' but addr2line support "
    "is not available. This setting has no effect in this configuration.")
elseif (WITH_LINE_INFO STREQUAL libbacktrace AND NOT HAVE_LIBBACKTRACE)
  message (WARNING "WITH_LINE_INFO is 'libbacktrace' but libbacktrace "
    "support is not available. This setting has no effect in this "
    "configuration.")
endif (WITH_LINE_INFO STREQUAL addr2line AND NOT HAVE_ADDR2LINE)

unset (_ng-log_try_addr2line)
unset (_ng-log_try_libbacktrace)

# Both can only end up available at once when WITH_LINE_INFO is "auto" (any
# other setting only ever tries one backend). HAVE_ADDR2LINE and
# HAVE_LIBBACKTRACE are otherwise mutually exclusive, so utilities.cc and
# symbolize.cc never need to reconcile the two backends themselves. Prefer
# libbacktrace: it resolves in-process, without spawning a subprocess per
# frame.
if (HAVE_ADDR2LINE AND HAVE_LIBBACKTRACE)
  unset (HAVE_ADDR2LINE)
endif (HAVE_ADDR2LINE AND HAVE_LIBBACKTRACE)

check_cxx_symbol_exists (gmtime_r "cstdlib;ctime" HAVE_GMTIME_R)
check_cxx_symbol_exists (localtime_r "cstdlib;ctime" HAVE_LOCALTIME_R)

set (SIZEOF_VOID_P ${CMAKE_SIZEOF_VOID_P})

configure_file (src/config.h.cmake.in config.h)

# Probe each stacktrace implementation independently. Select the first
# available implementation according to the established backend priority.
set (_ng-log_STACKTRACE_SOURCE)
cmake_push_check_state (RESET)
set (CMAKE_REQUIRED_INCLUDES
  ${ng-log_BINARY_DIR}
  ${ng-log_SOURCE_DIR}/src
)

file (READ ${ng-log_SOURCE_DIR}/src/internal/stacktrace_x86.cc
  _ng-log_STACKTRACE_X86_SOURCE)
string (APPEND _ng-log_STACKTRACE_X86_SOURCE
  "\nint main() {\n"
  "  void* frames[1];\n"
  "  return nglog::GetStackTrace(frames, 1, 0) < 0;\n"
  "}\n"
)
check_cxx_source_compiles ("${_ng-log_STACKTRACE_X86_SOURCE}"
  _ng-log_HAVE_STACKTRACE_X86)

file (READ ${ng-log_SOURCE_DIR}/src/internal/stacktrace_powerpc.cc
  _ng-log_STACKTRACE_POWERPC_SOURCE)
string (APPEND _ng-log_STACKTRACE_POWERPC_SOURCE
  "\nint main() {\n"
  "  void* frames[1];\n"
  "  return nglog::GetStackTrace(frames, 1, 0) < 0;\n"
  "}\n"
)
check_cxx_source_compiles ("${_ng-log_STACKTRACE_POWERPC_SOURCE}"
  _ng-log_HAVE_STACKTRACE_POWERPC)

file (READ ${ng-log_SOURCE_DIR}/src/internal/stacktrace_windows.cc
  _ng-log_STACKTRACE_WINDOWS_SOURCE)
string (APPEND _ng-log_STACKTRACE_WINDOWS_SOURCE
  "\nint main() {\n"
  "  void* frames[1];\n"
  "  return nglog::GetStackTrace(frames, 1, 0) < 0;\n"
  "}\n"
)
check_cxx_source_compiles ("${_ng-log_STACKTRACE_WINDOWS_SOURCE}"
  _ng-log_HAVE_STACKTRACE_WINDOWS)

cmake_pop_check_state ()

if (HAVE_LIBUNWIND)
  set (_ng-log_STACKTRACE_SOURCE src/internal/stacktrace_libunwind.cc)
elseif (HAVE_UNWIND)
  set (_ng-log_STACKTRACE_SOURCE src/internal/stacktrace_unwind.cc)
elseif (_ng-log_HAVE_STACKTRACE_X86)
  set (_ng-log_STACKTRACE_SOURCE src/internal/stacktrace_x86.cc)
elseif (_ng-log_HAVE_STACKTRACE_POWERPC)
  set (_ng-log_STACKTRACE_SOURCE src/internal/stacktrace_powerpc.cc)
elseif (_ng-log_HAVE_STACKTRACE_WINDOWS)
  set (_ng-log_STACKTRACE_SOURCE src/internal/stacktrace_windows.cc)
elseif (HAVE_EXECINFO_BACKTRACE)
  set (_ng-log_STACKTRACE_SOURCE src/internal/stacktrace_generic.cc)
endif (HAVE_LIBUNWIND)

if (_ng-log_STACKTRACE_SOURCE)
  set (HAVE_STACKTRACE 1)
endif (_ng-log_STACKTRACE_SOURCE)

configure_file (src/config.h.cmake.in config.h)

set (_ng-log_CMake_INSTALLDIR ${CMAKE_INSTALL_LIBDIR}/cmake/ng-log)

# The package configuration locates the find modules relative to its own
# location, both in the build tree and in the installation.
set (_ng-log_FIND_MODULES)

if (HAVE_UNWIND OR HAVE_LIBUNWIND)
  list (APPEND _ng-log_FIND_MODULES FindUnwind.cmake)
endif (HAVE_UNWIND OR HAVE_LIBUNWIND)

if (HAVE_LIBBACKTRACE)
  list (APPEND _ng-log_FIND_MODULES FindLibbacktrace.cmake)
endif (HAVE_LIBBACKTRACE)

# The package configuration checks whether the compiler of the consumer
# supports the experimental logging functions.
list (APPEND _ng-log_FIND_MODULES CheckExperimentalLogging.cmake)

foreach (_ng-log_FIND_MODULE IN LISTS _ng-log_FIND_MODULES)
  configure_file (cmake/${_ng-log_FIND_MODULE}
    ${ng-log_BINARY_DIR}/modules/${_ng-log_FIND_MODULE} COPYONLY)
endforeach (_ng-log_FIND_MODULE)

set (NGLOG_PUBLIC_H
  ${ng-log_BINARY_DIR}/ng-log/export.h
  src/ng-log/flags.h
  src/ng-log/internal/color.h
  src/ng-log/internal/color_spec.h
  src/ng-log/internal/hyperlink.h
  src/ng-log/internal/style_markers.h
  src/ng-log/internal/styled_value.h
  src/ng-log/internal/text_attributes.h
  src/ng-log/internal/text_style.h
  src/ng-log/log_message.h
  src/ng-log/log_setup.h
  src/ng-log/log_severity.h
  src/ng-log/log_sink.h
  src/ng-log/logging.h
  src/ng-log/platform.h
  src/ng-log/raw_logging.h
  src/ng-log/stl_logging.h
  src/ng-log/vlog_is_on.h
)

# Unless WITH_EXPERIMENTAL is disabled, the experimental logging functions are
# built whenever the compiler supports them.
include (cmake/CheckExperimentalLogging.cmake)

if (WITH_EXPERIMENTAL)
  nglog_check_experimental_logging (HAVE_EXPERIMENTAL_LOGGING
    INCLUDES ${ng-log_SOURCE_DIR}/src)
else (WITH_EXPERIMENTAL)
  # Shadows the cached result of an earlier check.
  set (HAVE_EXPERIMENTAL_LOGGING OFF)
endif (WITH_EXPERIMENTAL)

if (HAVE_EXPERIMENTAL_LOGGING)
  set (NGLOG_EXPERIMENTAL_PUBLIC_H
    src/ng-log/experimental/attributes.h
    src/ng-log/experimental/ensure.h
    src/ng-log/experimental/format.h
    src/ng-log/experimental/log.h
    src/ng-log/experimental/log_call.h
    src/ng-log/experimental/logging.h
    src/ng-log/experimental/requirements.h
    src/ng-log/experimental/signal_safe_log.h
    src/ng-log/experimental/sink.h
  )
  list (APPEND NGLOG_PUBLIC_H ${NGLOG_EXPERIMENTAL_PUBLIC_H})
endif (HAVE_EXPERIMENTAL_LOGGING)

set (NGLOG_SRCS
  ${NGLOG_PUBLIC_H}
  src/addr2line.cc
  src/addr2line.h
  src/demangle.cc
  src/demangle.h
  src/flags.cc
  src/initializer.h
  src/internal/attributes.h
  src/internal/checked_arithmetic.h
  src/internal/color_format.cc
  src/internal/emscripten_console.cc
  src/internal/emscripten_console.h
  src/internal/flag_display.h
  src/internal/flag_registry.cc
  src/internal/hyperlink.cc
  src/internal/levenshtein.cc
  src/internal/lock_metrics.cc
  src/internal/lock_metrics.h
  src/internal/log_cleaner.cc
  src/internal/log_cleaner.h
  src/internal/log_message_access.h
  src/internal/path.cc
  src/internal/path.h
  src/internal/process_flag_registry.cc
  src/internal/process_flag_registry.h
  src/internal/raw_log.h
  src/internal/source_location.cc
  src/internal/source_location.h
  src/internal/style_recorder.cc
  src/internal/style_recorder.h
  src/internal/styled_output.cc
  src/internal/styled_output.h
  src/internal/terminal_capabilities.cc
  src/internal/terminal_capabilities.h
  src/internal/theme.cc
  src/internal/theme.h
  src/internal/utf8.cc
  src/internal/utf8.h
  src/libbacktrace.cc
  src/libbacktrace.h
  src/logging.cc
  src/raw_logging.cc
  src/signalhandler.cc
  src/stacktrace.h
  src/subprocess.cc
  src/subprocess_fork.cc
  src/subprocess_fork.h
  src/subprocess.h
  src/symbolize.cc
  src/symbolize.h
  src/utilities.cc
  src/utilities.h
  src/vlog_is_on.cc
)

if (_ng-log_STACKTRACE_SOURCE)
  list (APPEND NGLOG_SRCS ${_ng-log_STACKTRACE_SOURCE})
endif (_ng-log_STACKTRACE_SOURCE)

# NOTE MSYS2 defines both WIN32 and UNIX. Do not use windows port in this case.
if ((CYGWIN OR WIN32) AND NOT UNIX)
  list (APPEND NGLOG_SRCS
    src/windows/port.cc
    src/windows/port.h
  )
endif ((CYGWIN OR WIN32) AND NOT UNIX)

add_library (ng-log_internal OBJECT
  ${NGLOG_SRCS}
)
target_compile_features (ng-log_internal PUBLIC $<TARGET_PROPERTY:ng-log,COMPILE_FEATURES>)

# GCC emits GNU unique symbols for some standard library templates, and glibc
# never unloads a module that defines such a symbol. Without them, a module
# that links ng-log statically can be unloaded and releases its flags.
if (CMAKE_CXX_COMPILER_ID STREQUAL "GNU" AND NOT WIN32 AND NOT APPLE)
  target_compile_options (ng-log_internal PRIVATE -fno-gnu-unique)
endif (CMAKE_CXX_COMPILER_ID STREQUAL "GNU" AND NOT WIN32 AND NOT APPLE)

if (WITH_LOCK_METRICS)
  target_compile_definitions (ng-log_internal PRIVATE NGLOG_ENABLE_LOCK_METRICS)
endif (WITH_LOCK_METRICS)

# Some generators (such as Xcode) do not generate any output if the target does
# not reference at least one source file.
set (_ng-log_EMPTY_SOURCE ${ng-log_BINARY_DIR}/ng-log.cc)

file (CONFIGURE OUTPUT ${_ng-log_EMPTY_SOURCE} CONTENT "")

add_library (ng-log
  $<TARGET_OBJECTS:ng-log_internal>
  ${_ng-log_EMPTY_SOURCE}
)
target_compile_features (ng-log PUBLIC cxx_std_14)

add_library (ng-log::ng-log ALIAS ng-log)

if (HAVE_EXPERIMENTAL_LOGGING)
  add_library (ng-log_experimental_objects OBJECT
    src/experimental/attributes.cc
    src/experimental/context.h
    src/experimental/ensure.cc
    src/experimental/log.cc
    src/experimental/signal_safe_log.cc
    src/experimental/site.cc
  )

  target_compile_features (ng-log_experimental_objects PRIVATE cxx_std_20)

  target_include_directories (ng-log_experimental_objects PRIVATE
    $<TARGET_PROPERTY:ng-log_internal,INCLUDE_DIRECTORIES>)
  # Compiles like the remainder of the library, including its exports.
  target_compile_definitions (ng-log_experimental_objects PRIVATE
    $<TARGET_PROPERTY:ng-log_internal,COMPILE_DEFINITIONS>)
  target_compile_options (ng-log_experimental_objects PRIVATE
    $<TARGET_PROPERTY:ng-log_internal,COMPILE_OPTIONS>)

  target_sources (ng-log PRIVATE $<TARGET_OBJECTS:ng-log_experimental_objects>)

  # Consumers of the experimental logging functions require C++20, unlike
  # those of the remainder of the library. The package configuration defines
  # the target of an installation, since it depends on the consumer compiler.
  add_library (ng-log_experimental INTERFACE)
  target_compile_features (ng-log_experimental INTERFACE cxx_std_20)
  target_link_libraries (ng-log_experimental INTERFACE ng-log)

  add_library (ng-log::experimental ALIAS ng-log_experimental)
endif (HAVE_EXPERIMENTAL_LOGGING)

set (ng-log_libraries_options_for_static_linking)
set (ng-log_pkgconfig_compile_options)

if (WIN32)
  # Do not define min and max as macros
  target_compile_definitions (ng-log PRIVATE NOMINMAX)
  # Exclude unnecessary funcitonality
  target_compile_definitions (ng-log PRIVATE WIN32_LEAN_AND_MEAN)
endif (WIN32)

if (HAVE_UNWIND OR HAVE_LIBUNWIND)
  target_link_libraries (ng-log PRIVATE unwind::unwind)
  set (ng-log_libraries_options_for_static_linking "${ng-log_libraries_options_for_static_linking} -lunwind")
  set (Unwind_DEPENDENCY "find_dependency (Unwind ${Unwind_VERSION})")
endif (HAVE_UNWIND OR HAVE_LIBUNWIND)

if (HAVE_LIBBACKTRACE)
  target_link_libraries (ng-log PRIVATE libbacktrace::libbacktrace)
  set (ng-log_libraries_options_for_static_linking "${ng-log_libraries_options_for_static_linking} -lbacktrace")
  set (Libbacktrace_DEPENDENCY "find_dependency (Libbacktrace)")
endif (HAVE_LIBBACKTRACE)

if (HAVE_DBGHELP)
  target_link_libraries (ng-log PRIVATE dbghelp)
  set (ng-log_libraries_options_for_static_linking "${ng-log_libraries_options_for_static_linking} -ldbghelp")
endif (HAVE_DBGHELP)

if (WIN32)
  target_link_libraries (ng-log PRIVATE advapi32 ole32 pathcch shell32 uuid)
  set (ng-log_libraries_options_for_static_linking
    "${ng-log_libraries_options_for_static_linking} -ladvapi32 -lole32 -lpathcch -lshell32 -luuid")
endif (WIN32)

target_link_libraries (ng-log PRIVATE Threads::Threads)

if (CMAKE_THREAD_LIBS_INIT)
  set (ng-log_libraries_options_for_static_linking "${ng-log_libraries_options_for_static_linking} ${CMAKE_THREAD_LIBS_INIT}")
endif (CMAKE_THREAD_LIBS_INIT)

if (ANDROID)
  target_link_libraries (ng-log PRIVATE log)
  set (ng-log_libraries_options_for_static_linking "${ng-log_libraries_options_for_static_linking} -llog")
endif (ANDROID)

set_target_properties (ng-log PROPERTIES VERSION ${ng-log_VERSION})
set_target_properties (ng-log PROPERTIES SOVERSION 0)

set (_ng-log_SPDX_LICENSE BSD-3-Clause)
set_property (TARGET ng-log PROPERTY SPDX_LICENSE ${_ng-log_SPDX_LICENSE})

if (CYGWIN OR WIN32)
  target_compile_definitions (ng-log PUBLIC NGLOG_NO_ABBREVIATED_SEVERITIES)
endif (CYGWIN OR WIN32)

# The header set installs the headers with their directory layout and adds
# the installed include directory to the exported usage requirements.
target_sources (ng-log PUBLIC
  FILE_SET HEADERS
  BASE_DIRS ${ng-log_BINARY_DIR} ${ng-log_SOURCE_DIR}/src
  FILES ${NGLOG_PUBLIC_H}
)

target_include_directories (ng-log BEFORE PUBLIC
  "$<BUILD_INTERFACE:${ng-log_BINARY_DIR}>"
  "$<BUILD_INTERFACE:${ng-log_SOURCE_DIR}/src>"
  PRIVATE ${ng-log_BINARY_DIR}
  PRIVATE ${ng-log_SOURCE_DIR}/src)

if (CYGWIN OR WIN32)
  target_include_directories (ng-log_internal PUBLIC
    "$<BUILD_INTERFACE:${ng-log_SOURCE_DIR}/src/windows>"
    PRIVATE ${ng-log_SOURCE_DIR}/src/windows)

  target_include_directories (ng-log PUBLIC
    "$<BUILD_INTERFACE:${ng-log_SOURCE_DIR}/src/windows>"
    PRIVATE ${ng-log_SOURCE_DIR}/src/windows)
endif (CYGWIN OR WIN32)

target_include_directories (ng-log_internal PUBLIC
  $<TARGET_PROPERTY:ng-log,INCLUDE_DIRECTORIES>)
target_compile_definitions (ng-log_internal PUBLIC
  $<TARGET_PROPERTY:ng-log,COMPILE_DEFINITIONS>
  PRIVATE ng_log_EXPORTS)

# The build tree exposes the experimental headers even if the library lacks
# the experimental logging functions, which the headers then reject.
if (HAVE_EXPERIMENTAL_LOGGING)
  set (_ng-log_EXPORT_CONTENT)
else (HAVE_EXPERIMENTAL_LOGGING)
  set (_ng-log_EXPORT_CONTENT
    "#define NGLOG_INTERNAL_EXPERIMENTAL_LOGGING_UNAVAILABLE")
endif (HAVE_EXPERIMENTAL_LOGGING)

generate_export_header (ng-log
  BASE_NAME NGLOG
  EXPORT_FILE_NAME ${ng-log_BINARY_DIR}/ng-log/export.h
  CUSTOM_CONTENT_FROM_VARIABLE _ng-log_EXPORT_CONTENT)

string (STRIP "${ng-log_libraries_options_for_static_linking}" ng-log_libraries_options_for_static_linking)

if (WITH_PKGCONFIG)
  set (VERSION ${ng-log_VERSION})
  set (prefix ${CMAKE_INSTALL_PREFIX})
  set (exec_prefix ${CMAKE_INSTALL_FULL_BINDIR})
  set (libdir ${CMAKE_INSTALL_FULL_LIBDIR})
  set (includedir ${CMAKE_INSTALL_FULL_INCLUDEDIR})

  configure_file (
    "${ng-log_SOURCE_DIR}/cmake/libng-log.pc.in"
    "${ng-log_BINARY_DIR}/libng-log.pc"
    @ONLY
  )

  unset (VERSION)
  unset (prefix)
  unset (exec_prefix)
  unset (libdir)
  unset (includedir)
endif (WITH_PKGCONFIG)

# Unit testing

if (WITH_FUZZING AND NOT WITH_FUZZING STREQUAL none)
  add_executable (fuzz_demangle
    src/fuzz_demangle.cc
  )

  # The demangler is not exported from the shared library. Link the objects
  # directly instead.
  target_compile_definitions (fuzz_demangle PRIVATE NGLOG_STATIC_DEFINE
    $<TARGET_PROPERTY:ng-log,COMPILE_DEFINITIONS>)
  target_compile_features (fuzz_demangle PRIVATE
    $<TARGET_PROPERTY:ng-log,COMPILE_FEATURES>)
  target_include_directories (fuzz_demangle PRIVATE
    $<TARGET_PROPERTY:ng-log,INCLUDE_DIRECTORIES>)
  target_link_libraries (fuzz_demangle PRIVATE
    $<TARGET_OBJECTS:ng-log_internal>
    $<TARGET_PROPERTY:ng-log,LINK_LIBRARIES>)

  if (WITH_FUZZING STREQUAL "ossfuzz")
    set (LIB_FUZZING_ENGINE $ENV{LIB_FUZZING_ENGINE})
    target_link_libraries (fuzz_demangle PRIVATE ${LIB_FUZZING_ENGINE})
  elseif (WITH_FUZZING STREQUAL "libfuzzer")
    # The fuzzing engine provides main() and must therefore also be linked.
    target_compile_options (fuzz_demangle PRIVATE -fsanitize=fuzzer)
    target_link_options (fuzz_demangle PRIVATE -fsanitize=fuzzer)
  else (WITH_FUZZING STREQUAL "ossfuzz")
    message (FATAL_ERROR "Unsupported fuzzing engine ${WITH_FUZZING}")
  endif (WITH_FUZZING STREQUAL "ossfuzz")
endif (WITH_FUZZING AND NOT WITH_FUZZING STREQUAL none)

if (BUILD_TESTING AND GTest_FOUND)
  include (GoogleTest)

  # Discover tests when running them so that the build does not depend on
  # executing test programs.
  set (CMAKE_GTEST_DISCOVER_TESTS_DISCOVERY_MODE PRE_TEST)

  # Configures and builds a target of a separate project against the ng-log
  # build tree. The test optionally runs a command of that project afterwards.
  function (nglog_add_build_test _NAME)
    cmake_parse_arguments (PARSE_ARGV 1 _ARGS ""
      "SOURCE_DIR;TARGET;CONFIG;EXPECT_FAILURE" "TEST_COMMAND;OPTIONS")

    if (DEFINED _ARGS_UNPARSED_ARGUMENTS OR NOT DEFINED _ARGS_SOURCE_DIR OR
        NOT DEFINED _ARGS_TARGET)
      message (FATAL_ERROR "nglog_add_build_test (${_NAME}) expects "
        "SOURCE_DIR and TARGET but got: ${ARGN}")
    endif (DEFINED _ARGS_UNPARSED_ARGUMENTS OR NOT DEFINED _ARGS_SOURCE_DIR OR
        NOT DEFINED _ARGS_TARGET)

    if (NOT DEFINED _ARGS_CONFIG)
      set (_ARGS_CONFIG $<CONFIG>)
    endif (NOT DEFINED _ARGS_CONFIG)

    set (_OPTIONS ${_ARGS_OPTIONS})

    # Cross compiling needs the same toolchain in the nested build.
    if (DEFINED CMAKE_TOOLCHAIN_FILE)
      list (APPEND _OPTIONS -DCMAKE_TOOLCHAIN_FILE=${CMAKE_TOOLCHAIN_FILE})
    endif (DEFINED CMAKE_TOOLCHAIN_FILE)

    set (_TEST_COMMAND)

    if (DEFINED _ARGS_TEST_COMMAND)
      if (CMAKE_CROSSCOMPILING AND DEFINED CMAKE_CROSSCOMPILING_EMULATOR)
        # The nested CTest neither knows the executable suffix of the target
        # nor runs the program through the emulator.
        list (POP_FRONT _ARGS_TEST_COMMAND _PROGRAM)
        list (PREPEND _ARGS_TEST_COMMAND ${CMAKE_CROSSCOMPILING_EMULATOR}
          ${_PROGRAM}${CMAKE_EXECUTABLE_SUFFIX})
      endif (CMAKE_CROSSCOMPILING AND DEFINED CMAKE_CROSSCOMPILING_EMULATOR)

      set (_TEST_COMMAND --test-command ${_ARGS_TEST_COMMAND})
    endif (DEFINED _ARGS_TEST_COMMAND)

    # Empty values are omitted, since the driver of builds that must fail
    # passes the command on as a list, which drops empty elements.
    set (_GENERATOR_OPTIONS)

    if (CMAKE_GENERATOR_PLATFORM)
      list (APPEND _GENERATOR_OPTIONS
        --build-generator-platform ${CMAKE_GENERATOR_PLATFORM})
    endif (CMAKE_GENERATOR_PLATFORM)

    if (CMAKE_GENERATOR_TOOLSET)
      list (APPEND _GENERATOR_OPTIONS
        --build-generator-toolset ${CMAKE_GENERATOR_TOOLSET})
    endif (CMAKE_GENERATOR_TOOLSET)

    set (_COMMAND ${CMAKE_CTEST_COMMAND}
      --build-config ${_ARGS_CONFIG}
      --build-and-test
      "${ng-log_SOURCE_DIR}/${_ARGS_SOURCE_DIR}"
      "${ng-log_BINARY_DIR}/Tests/${_NAME}"
      --build-generator ${CMAKE_GENERATOR}
      ${_GENERATOR_OPTIONS}
      --build-makeprogram ${CMAKE_MAKE_PROGRAM}
      --build-target ${_ARGS_TARGET}
      --build-options
      -DCMAKE_BUILD_TYPE=${_ARGS_CONFIG}
      -DCMAKE_CXX_COMPILER=${CMAKE_CXX_COMPILER}
      -DCMAKE_FIND_PACKAGE_NO_PACKAGE_REGISTRY=ON
      -Dng-log_DIR=${ng-log_BINARY_DIR}
      ${_OPTIONS}
      ${_TEST_COMMAND}
    )

    # A build that must fail is required to fail with matching output, since
    # CTest ignores the exit status of a test it matches against an
    # expression.
    if (DEFINED _ARGS_EXPECT_FAILURE)
      add_test (NAME ${_NAME} COMMAND ${CMAKE_COMMAND}
        "-DEXPECTED=${_ARGS_EXPECT_FAILURE}"
        -P "${ng-log_SOURCE_DIR}/cmake/RunFailingBuildTest.cmake" --
        ${_COMMAND})
    else (DEFINED _ARGS_EXPECT_FAILURE)
      add_test (NAME ${_NAME} COMMAND ${_COMMAND})
    endif (DEFINED _ARGS_EXPECT_FAILURE)
  endfunction (nglog_add_build_test)

  add_library (ng-log_testing_utilities OBJECT
    src/testing_utilities.cc
  )

  target_compile_definitions (ng-log_testing_utilities PRIVATE NGLOG_STATIC_DEFINE
    $<TARGET_PROPERTY:ng-log,COMPILE_DEFINITIONS>)
  target_compile_features (ng-log_testing_utilities PRIVATE
    $<TARGET_PROPERTY:ng-log,COMPILE_FEATURES>)
  target_include_directories (ng-log_testing_utilities PRIVATE
    $<TARGET_PROPERTY:ng-log,INCLUDE_DIRECTORIES>)
  target_link_libraries (ng-log_testing_utilities PRIVATE GTest::gtest
    GTest::gmock)

  add_library (ng-log_test INTERFACE)
  target_link_libraries (ng-log_test INTERFACE $<TARGET_OBJECTS:ng-log_internal>
    $<TARGET_OBJECTS:ng-log_testing_utilities>
    $<TARGET_PROPERTY:ng-log,LINK_LIBRARIES> GTest::gtest GTest::gmock)
  target_compile_definitions (ng-log_test INTERFACE NGLOG_STATIC_DEFINE
    $<TARGET_PROPERTY:ng-log,COMPILE_DEFINITIONS>)
  if (WITH_LOCK_METRICS)
    target_compile_definitions (ng-log_test INTERFACE NGLOG_ENABLE_LOCK_METRICS)
  endif (WITH_LOCK_METRICS)
  target_compile_features (ng-log_test INTERFACE
    $<TARGET_PROPERTY:ng-log,COMPILE_FEATURES>)
  target_include_directories (ng-log_test INTERFACE $<TARGET_PROPERTY:ng-log,INCLUDE_DIRECTORIES>)

  # Collects the unprefixed macros of the public headers. Lines are not read
  # individually, since a list element that ends with the backslash of a
  # continuation line escapes the list separator.
  set (NGLOG_UNPREFIXED_MACROS)

  foreach (_ng-log_HEADER IN LISTS NGLOG_PUBLIC_H)
    file (READ ${_ng-log_HEADER} _ng-log_CONTENT)
    string (REGEX MATCHALL "\n#[ \t]*define[ \t]+[A-Za-z0-9_]+"
      _ng-log_DEFINES "${_ng-log_CONTENT}")

    foreach (_ng-log_DEFINE IN LISTS _ng-log_DEFINES)
      string (REGEX REPLACE "^\n#[ \t]*define[ \t]+" "" _ng-log_MACRO
        "${_ng-log_DEFINE}")
      if (NOT _ng-log_MACRO MATCHES "^(NGLOG_|_)")
        list (APPEND NGLOG_UNPREFIXED_MACROS ${_ng-log_MACRO})
      endif (NOT _ng-log_MACRO MATCHES "^(NGLOG_|_)")
    endforeach (_ng-log_DEFINE)
  endforeach (_ng-log_HEADER)

  list (REMOVE_DUPLICATES NGLOG_UNPREFIXED_MACROS)
  # Rescans the headers once they change.
  set_property (DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS
    ${NGLOG_PUBLIC_H})

  # Verifies that each header compiles without other includes and leaves the
  # unprefixed macros undefined. Such headers can be combined with libraries
  # that define the same names, such as the LOG and CHECK macros of Abseil.
  function (nglog_add_header_test _TARGET)
    set (_SOURCES)

    foreach (_HEADER IN LISTS ARGN)
      file (RELATIVE_PATH _INCLUDE ${ng-log_SOURCE_DIR}/src
        ${ng-log_SOURCE_DIR}/${_HEADER})
      string (MAKE_C_IDENTIFIER ${_INCLUDE} _NAME)
      set (_SOURCE ${ng-log_BINARY_DIR}/${_TARGET}/${_NAME}.cc)
      set (_CHECKS)

      foreach (_MACRO IN LISTS NGLOG_UNPREFIXED_MACROS)
        string (APPEND _CHECKS "#ifdef ${_MACRO}\n\
#  error \"${_INCLUDE} defines ${_MACRO}, which conflicts with other \
logging libraries\"\n#endif\n")
      endforeach (_MACRO)

      file (CONFIGURE OUTPUT ${_SOURCE} CONTENT
        "#include <${_INCLUDE}>  // IWYU pragma: keep\n\n${_CHECKS}" @ONLY)
      list (APPEND _SOURCES ${_SOURCE})
    endforeach (_HEADER)

    add_library (${_TARGET} OBJECT ${_SOURCES})
    target_link_libraries (${_TARGET} PRIVATE ng-log_test)
  endfunction (nglog_add_header_test)

  nglog_add_header_test (macro_free_headers_unittest
    src/ng-log/log_message.h
    src/ng-log/log_setup.h
    src/ng-log/log_severity.h
    src/ng-log/log_sink.h
  )

  add_executable (email_test_helper
    src/email_test_helper.cc
  )

  add_executable (logging_unittest
    src/logging_unittest.cc
    src/vlog_module_test_helper.cc
  )

  target_link_libraries (logging_unittest PRIVATE ng-log_test)
  add_dependencies (logging_unittest email_test_helper)
  target_compile_definitions (logging_unittest PRIVATE
    EMAIL_TEST_HELPER_PATH="$<TARGET_FILE:email_test_helper>"
  )

  if (MINGW)
    # Statically linked because this helper runs as a child process and must
    # not depend on MinGW runtime DLLs being available on the child's PATH.
    target_link_options (email_test_helper PRIVATE
      -static -static-libgcc -static-libstdc++
    )
  endif (MINGW)

  add_executable (platform_unittest
    src/platform_unittest.cc
  )

  target_link_libraries (platform_unittest PRIVATE
    ng-log_test GTest::gtest_main)

  gtest_discover_tests (platform_unittest
    TEST_PREFIX platform.
    DISCOVERY_TIMEOUT 30
  )

  add_executable (flags_unittest
    src/flags_unittest.cc
  )

  target_link_libraries (flags_unittest PRIVATE ng-log_test)

  add_executable (flags_external_unittest
    src/flags_external_unittest.cc
  )

  target_link_libraries (flags_external_unittest PRIVATE ng-log GTest::gtest)

  gtest_discover_tests (flags_external_unittest
    TEST_PREFIX "flags.external."
  )

  add_executable (flags_environment_unittest
    src/flags_environment_unittest.cc
  )

  target_link_libraries (flags_environment_unittest PRIVATE ng-log_test)

  # The environment must be in place before static initialization, and a list
  # valued property cannot be forwarded through gtest_discover_tests.
  add_test (NAME flags.environment COMMAND flags_environment_unittest)
  set_tests_properties (flags.environment PROPERTIES
    ENVIRONMENT
      "NGLOG_logtostderr=1;NGLOG_log_prefix=FaLsE;NGLOG_log_year_in_prefix=not-a-boolean;NGLOG_v=0x10;NGLOG_logfile_mode=0600"
  )

  # Separate copies of ng-log coordinate their flag names only where the
  # operating system provides an object that is private to the process.
  if (WIN32 OR CMAKE_SYSTEM_NAME STREQUAL "Linux")
    # Each module carries its own copy of the ng-log objects.
    foreach (_ng-log_library_copy first second)
      add_library (flags_library_copy_${_ng-log_library_copy} MODULE
        src/flags_library_copy.cc
        $<TARGET_OBJECTS:ng-log_internal>
      )
      # The objects were compiled for export, the module source was not.
      target_compile_definitions (flags_library_copy_${_ng-log_library_copy}
        PRIVATE NGLOG_STATIC_DEFINE)
    endforeach (_ng-log_library_copy)

    set (_ng-log_library_copy_value 0)
    foreach (_ng-log_library_copy first second)
      math (EXPR _ng-log_library_copy_value "${_ng-log_library_copy_value} + 1")
      set (_ng-log_library_copy_target flags_library_copy_${_ng-log_library_copy})
      target_compile_definitions (${_ng-log_library_copy_target} PRIVATE
        NGLOG_LIBRARY_COPY_VALUE=${_ng-log_library_copy_value}
        $<TARGET_PROPERTY:ng-log,COMPILE_DEFINITIONS>)
      target_compile_features (${_ng-log_library_copy_target} PRIVATE
        $<TARGET_PROPERTY:ng-log,COMPILE_FEATURES>)
      target_include_directories (${_ng-log_library_copy_target} PRIVATE
        $<TARGET_PROPERTY:ng-log,INCLUDE_DIRECTORIES>)
      target_link_libraries (${_ng-log_library_copy_target} PRIVATE
        $<TARGET_PROPERTY:ng-log,LINK_LIBRARIES>)
    endforeach (_ng-log_library_copy)

    # The loader must not link ng-log. Otherwise its own copy would collide
    # with every loaded module.
    add_executable (flags_library_copies_unittest
      src/flags_library_copies_unittest.cc
    )
    target_link_libraries (flags_library_copies_unittest PRIVATE GTest::gtest
      ${CMAKE_DL_LIBS})
    if (WIN32)
      # LoadLibrary finds the modules next to the executable.
      target_compile_definitions (flags_library_copies_unittest PRIVATE
        "NGLOG_LIBRARY_COPY_FIRST=\"$<TARGET_FILE_NAME:flags_library_copy_first>\""
        "NGLOG_LIBRARY_COPY_SECOND=\"$<TARGET_FILE_NAME:flags_library_copy_second>\"")
    else (WIN32)
      target_compile_definitions (flags_library_copies_unittest PRIVATE
        "NGLOG_LIBRARY_COPY_FIRST=\"$<TARGET_FILE:flags_library_copy_first>\""
        "NGLOG_LIBRARY_COPY_SECOND=\"$<TARGET_FILE:flags_library_copy_second>\"")
    endif (WIN32)
    add_dependencies (flags_library_copies_unittest flags_library_copy_first
      flags_library_copy_second)

    gtest_discover_tests (flags_library_copies_unittest
      TEST_PREFIX flags.copies.
      DISCOVERY_TIMEOUT 30
    )
  endif (WIN32 OR CMAKE_SYSTEM_NAME STREQUAL "Linux")

  gtest_discover_tests (flags_unittest
    TEST_PREFIX flags.
    DISCOVERY_TIMEOUT 30
  )

  add_executable (flags_static_initialization_unittest
    src/flags_static_initialization_unittest.cc
  )
  target_link_libraries (flags_static_initialization_unittest PRIVATE
    ng-log_test)
  add_test (NAME flags.static_initialization
    COMMAND flags_static_initialization_unittest)

  add_executable (preinit_warning_unittest
    src/preinit_warning_unittest.cc
  )

  target_link_libraries (preinit_warning_unittest PRIVATE ng-log_test)

  add_executable (vlog_is_on_unittest
    src/vlog_is_on_unittest.cc
    src/vlog_module_test_helper.cc
  )

  target_link_libraries (vlog_is_on_unittest PRIVATE ng-log_test)

  add_executable (color_attributes_unittest
    src/color_attributes_unittest.cc
  )

  target_link_libraries (color_attributes_unittest PRIVATE ng-log_test)

  add_executable (logtostdout_unittest
    src/logtostdout_unittest.cc
  )

  target_link_libraries (logtostdout_unittest PRIVATE ng-log_test)

  gtest_discover_tests (logtostdout_unittest
    TEST_PREFIX logtostdout.
    DISCOVERY_TIMEOUT 30
  )

  if (HAVE_EXPERIMENTAL_LOGGING)
    nglog_add_header_test (experimental_headers_unittest
      ${NGLOG_EXPERIMENTAL_PUBLIC_H})
    target_compile_features (experimental_headers_unittest PRIVATE
      cxx_std_20)

    foreach (_ng-log_TEST IN ITEMS attributes ensure log signal_safe_log)
      set (_ng-log_TARGET experimental_${_ng-log_TEST}_unittest)

      add_executable (${_ng-log_TARGET}
        src/experimental/${_ng-log_TEST}_unittest.cc
        src/experimental/testing.cc
        src/experimental/testing.h
      )

      target_compile_features (${_ng-log_TARGET} PRIVATE cxx_std_20)
      target_link_libraries (${_ng-log_TARGET} PRIVATE
        $<TARGET_OBJECTS:ng-log_experimental_objects> ng-log_test)

      gtest_discover_tests (${_ng-log_TARGET}
        TEST_PREFIX experimental_${_ng-log_TEST}.
        DISCOVERY_TIMEOUT 30
      )
    endforeach (_ng-log_TEST)

    # C++26 rejects comparing arrays, which the comparisons of That() must
    # therefore avoid, and provides extended floating-point types, which
    # RawLog() must reject.
    if (cxx_std_26 IN_LIST CMAKE_CXX_COMPILE_FEATURES)
      foreach (_ng-log_TEST IN ITEMS ensure signal_safe_log)
        set (_ng-log_TARGET experimental_${_ng-log_TEST}_cxx26_unittest)

        add_executable (${_ng-log_TARGET}
          src/experimental/${_ng-log_TEST}_unittest.cc
          src/experimental/testing.cc
          src/experimental/testing.h
        )

        target_compile_features (${_ng-log_TARGET} PRIVATE cxx_std_26)
        target_link_libraries (${_ng-log_TARGET} PRIVATE
          $<TARGET_OBJECTS:ng-log_experimental_objects> ng-log_test)

        gtest_discover_tests (${_ng-log_TARGET}
          TEST_PREFIX experimental_${_ng-log_TEST}_cxx26.
          DISCOVERY_TIMEOUT 30
        )
      endforeach (_ng-log_TEST)
    endif (cxx_std_26 IN_LIST CMAKE_CXX_COMPILE_FEATURES)

    foreach (_ng-log_STRIP_LOG IN ITEMS 0 2 10)
      set (_ng-log_TARGET experimental_striplog${_ng-log_STRIP_LOG}_unittest)

      add_executable (${_ng-log_TARGET}
        src/experimental/striplog_unittest.cc
      )

      target_compile_features (${_ng-log_TARGET} PRIVATE cxx_std_20)

      target_compile_definitions (${_ng-log_TARGET} PRIVATE
        NGLOG_STRIP_LOG=${_ng-log_STRIP_LOG})

      if (_ng-log_STRIP_LOG EQUAL 10)
        # Like the strip test of the macros, this also covers release mode and
        # a conflicting ERROR macro.
        target_compile_definitions (${_ng-log_TARGET} PRIVATE
          ERROR=0
          NDEBUG
          NGLOG_NO_ABBREVIATED_SEVERITIES
        )
      endif (_ng-log_STRIP_LOG EQUAL 10)

      target_link_libraries (${_ng-log_TARGET} PRIVATE
        $<TARGET_OBJECTS:ng-log_experimental_objects> ng-log_test)

      gtest_discover_tests (${_ng-log_TARGET}
        TEST_PREFIX experimental_striplog${_ng-log_STRIP_LOG}.
        DISCOVERY_TIMEOUT 30
      )
    endforeach (_ng-log_STRIP_LOG)
  endif (HAVE_EXPERIMENTAL_LOGGING)

  add_executable (stl_logging_unittest
    src/stl_logging_unittest.cc
  )

  target_link_libraries (stl_logging_unittest PRIVATE ng-log_test)

  if (HAVE_SYMBOLIZE)
    add_executable (symbolize_unittest
      src/symbolize_unittest.cc
    )

    target_link_libraries (symbolize_unittest PRIVATE ng-log_test)
  endif (HAVE_SYMBOLIZE)

  if (HAVE_ADDR2LINE)
    # Stand-ins for a fake "addr2line" on PATH exercising specific
    # behaviors (hanging, producing no output): a real executable is
    # needed rather than a POSIX shell script wherever CreateProcess()
    # is used to spawn it.
    add_executable (addr2line_fake_hang
      src/addr2line_fake_hang.cc
    )

    add_executable (addr2line_fake_empty
      src/addr2line_fake_empty.cc
    )

    add_executable (addr2line_fake_fail
      src/addr2line_fake_fail.cc
    )

    add_executable (addr2line_fake_slow
      src/addr2line_fake_slow.cc
    )

    add_executable (addr2line_fake_stream
      src/addr2line_fake_stream.cc
    )

    if (MINGW)
      add_executable (addr2line_fake_functions
        src/addr2line_fake_functions.cc
      )
      set_target_properties (addr2line_fake_functions PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY
          "${CMAKE_CURRENT_BINARY_DIR}/addr2line_fake_functions"
        RUNTIME_OUTPUT_NAME addr2line
      )
      add_dependencies (symbolize_unittest addr2line_fake_functions)
      # Keep the child independent of MinGW runtime DLLs on PATH.
      target_link_options (addr2line_fake_functions PRIVATE
        -static -static-libgcc -static-libstdc++)
    endif (MINGW)

    add_executable (addr2line_unittest
      src/addr2line_unittest.cc
    )

    target_link_libraries (addr2line_unittest PRIVATE ng-log_test)
    add_dependencies (addr2line_unittest addr2line_fake_hang addr2line_fake_empty
      addr2line_fake_fail addr2line_fake_slow addr2line_fake_stream)
    target_compile_definitions (addr2line_unittest PRIVATE
      ADDR2LINE_FAKE_HANG_PATH="$<TARGET_FILE:addr2line_fake_hang>"
      ADDR2LINE_FAKE_EMPTY_PATH="$<TARGET_FILE:addr2line_fake_empty>"
      ADDR2LINE_FAKE_FAIL_PATH="$<TARGET_FILE:addr2line_fake_fail>"
      ADDR2LINE_FAKE_SLOW_PATH="$<TARGET_FILE:addr2line_fake_slow>"
      ADDR2LINE_FAKE_STREAM_PATH="$<TARGET_FILE:addr2line_fake_stream>"
      # Configurations whose default flags ask for debug info, which a test
      # needs to expect file names and line numbers.
      NGLOG_TEST_HAS_DEBUG_INFO=$<BOOL:$<CONFIG:Debug,RelWithDebInfo>>)

    if (WIN32)
      # Statically linked so the runtime DLLs of whichever toolchain
      # built it (libwinpthread, libstdc++, ...) do not need to be on
      # PATH wherever this gets spawned as a child process.
      target_link_options (addr2line_fake_hang PRIVATE
        -static -static-libgcc -static-libstdc++)
      target_link_options (addr2line_fake_empty PRIVATE
        -static -static-libgcc -static-libstdc++)
      target_link_options (addr2line_fake_fail PRIVATE
        -static -static-libgcc -static-libstdc++)
      target_link_options (addr2line_fake_slow PRIVATE
        -static -static-libgcc -static-libstdc++)
      target_link_options (addr2line_fake_stream PRIVATE
        -static -static-libgcc -static-libstdc++)
    endif (WIN32)
  endif (HAVE_ADDR2LINE)

  if (HAVE_SUBPROCESS)
    add_executable (subprocess_test_helper
      src/subprocess_test_helper.cc
    )

    add_executable (subprocess_unittest
      src/subprocess_unittest.cc
    )

    target_link_libraries (subprocess_unittest PRIVATE ng-log_test)
    add_dependencies (subprocess_unittest subprocess_test_helper)
    target_compile_definitions (subprocess_unittest PRIVATE
      SUBPROCESS_HELPER_PATH="$<TARGET_FILE:subprocess_test_helper>")

    if (MINGW)
      # Keep the child independent of MinGW runtime DLLs on PATH.
      target_link_options (subprocess_test_helper PRIVATE
        -municode -static -static-libgcc -static-libstdc++)
    endif (MINGW)
  endif (HAVE_SUBPROCESS)

  if (HAVE_LIBBACKTRACE)
    add_executable (libbacktrace_unittest
      src/libbacktrace_unittest.cc
    )

    target_link_libraries (libbacktrace_unittest PRIVATE ng-log_test)
  endif (HAVE_LIBBACKTRACE)

  add_executable (demangle_unittest
    src/demangle_unittest.cc
  )

  target_link_libraries (demangle_unittest PRIVATE ng-log_test)

  gtest_discover_tests (demangle_unittest
    TEST_PREFIX demangle.
    DISCOVERY_TIMEOUT 30
  )

  add_executable (color_unittest
    src/color_unittest.cc
  )

  target_link_libraries (color_unittest PRIVATE ng-log_test)

  gtest_discover_tests (color_unittest
    TEST_PREFIX color.
    DISCOVERY_TIMEOUT 30
  )

  if (HAVE_STACKTRACE)
    add_executable (stacktrace_unittest
      src/stacktrace_unittest.cc
    )

    target_link_libraries (stacktrace_unittest PRIVATE ng-log_test)
  endif (HAVE_STACKTRACE)

  add_executable (testing_utilities_unittest
    src/testing_utilities_unittest.cc
  )

  target_link_libraries (testing_utilities_unittest PRIVATE ng-log_test
    GTest::gtest_main)

  gtest_discover_tests (testing_utilities_unittest
    TEST_PREFIX testing_utilities.
    DISCOVERY_TIMEOUT 30
  )

  add_executable (utilities_unittest
    src/utilities_unittest.cc
  )

  target_link_libraries (utilities_unittest PRIVATE ng-log_test)

  gtest_discover_tests (utilities_unittest
    TEST_PREFIX utilities.
    DISCOVERY_TIMEOUT 30
  )

  if (HAVE_STACKTRACE AND HAVE_SYMBOLIZE)
    add_executable (signalhandler_unittest
      src/signalhandler_unittest.cc
    )

    target_link_libraries (signalhandler_unittest PRIVATE ng-log_test)
  endif (HAVE_STACKTRACE AND HAVE_SYMBOLIZE)

  gtest_discover_tests (logging_unittest
    TEST_PREFIX logging.
    DISCOVERY_TIMEOUT 30
    PROPERTIES
      TIMEOUT 30
  )

  gtest_discover_tests (preinit_warning_unittest
    TEST_PREFIX preinit_warning.
    DISCOVERY_TIMEOUT 30
  )

  gtest_discover_tests (vlog_is_on_unittest
    TEST_PREFIX vlog_is_on.
    DISCOVERY_TIMEOUT 30
  )

  gtest_discover_tests (color_attributes_unittest
    TEST_PREFIX color_attributes.
    DISCOVERY_TIMEOUT 30
  )

  if (TARGET signalhandler_unittest)
    gtest_discover_tests (signalhandler_unittest
      TEST_PREFIX signalhandler.
      DISCOVERY_TIMEOUT 30
    )

    if (HAVE_SIGACTION)
      # Forks off the segv, reentrant, and multithread scenarios below
      # and checks each is killed by the expected signal within a
      # timeout, catching the reentrancy guard hanging the process
      # instead of letting it die.
      add_test (NAME signalhandler_self_test
        COMMAND signalhandler_unittest self_test)
      set_tests_properties (signalhandler_self_test PROPERTIES TIMEOUT 60)
    endif (HAVE_SIGACTION)
  endif (TARGET signalhandler_unittest)

  if (TARGET stacktrace_unittest)
    gtest_discover_tests (stacktrace_unittest
      TEST_PREFIX stacktrace.
      DISCOVERY_TIMEOUT 30
      PROPERTIES TIMEOUT 30
    )
  endif (TARGET stacktrace_unittest)

  gtest_discover_tests (stl_logging_unittest
    TEST_PREFIX stl_logging.
    DISCOVERY_TIMEOUT 30
  )

  if (TARGET symbolize_unittest)
    gtest_discover_tests (symbolize_unittest
      TEST_PREFIX symbolize.
      DISCOVERY_TIMEOUT 30
    )
  endif (TARGET symbolize_unittest)

  if (TARGET addr2line_unittest)
    gtest_discover_tests (addr2line_unittest
      TEST_PREFIX addr2line.
      DISCOVERY_TIMEOUT 30
    )
  endif (TARGET addr2line_unittest)

  if (TARGET subprocess_unittest)
    gtest_discover_tests (subprocess_unittest
      TEST_PREFIX subprocess.
      DISCOVERY_TIMEOUT 30
    )
  endif (TARGET subprocess_unittest)

  if (TARGET libbacktrace_unittest)
    gtest_discover_tests (libbacktrace_unittest
      TEST_PREFIX libbacktrace.
      DISCOVERY_TIMEOUT 30
    )
  endif (TARGET libbacktrace_unittest)

  add_executable (mock-log_unittest
    src/mock-log_unittest.cc
    src/mock-log.h
  )

  target_link_libraries (mock-log_unittest PRIVATE ng-log_test)

  gtest_discover_tests (mock-log_unittest
    TEST_PREFIX mock-log.
    DISCOVERY_TIMEOUT 30
    PROPERTIES
      # Reports deadlocks without failing slow runs on overloaded runners.
      TIMEOUT 60
  )

  nglog_add_build_test (cmake_package_config
    SOURCE_DIR src/package_config_unittest/working_config
    TARGET ng-log_package_config
  )

  if (HAVE_EXPERIMENTAL_LOGGING)
    set (_ng-log_EXPECT_EXPERIMENTAL TRUE)
  else (HAVE_EXPERIMENTAL_LOGGING)
    set (_ng-log_EXPECT_EXPERIMENTAL FALSE)
  endif (HAVE_EXPERIMENTAL_LOGGING)

  # Requiring the experimental component fails with a reason unless the
  # library was built with the experimental logging functions.
  if (HAVE_EXPERIMENTAL_LOGGING)
    nglog_add_build_test (cmake_package_config_experimental
      SOURCE_DIR src/package_config_unittest/components
      TARGET ng-log_package_components
      OPTIONS
        -DNGLOG_REQUIRED_COMPONENTS=experimental
        -DNGLOG_EXPECT_EXPERIMENTAL=TRUE
    )

    # C++98 orders after C++20 numerically, which must not keep the check of
    # the consumer compiler from selecting C++20.
    nglog_add_build_test (cmake_package_config_experimental_cxx98
      SOURCE_DIR src/package_config_unittest/components
      TARGET ng-log_package_components
      OPTIONS
        -DCMAKE_CXX_STANDARD=98
        -DNGLOG_OPTIONAL_COMPONENTS=experimental
        -DNGLOG_EXPECT_EXPERIMENTAL=TRUE
    )

    # A consumer whose compiler lacks the requirements cannot use the
    # experimental component even though the library provides it. Marking the
    # functions unavailable to the compiler of the consumer makes its check
    # fail like such a compiler does.
    # The flags are added to those of the environment, such as coverage flags,
    # instead of replacing them.
    set (_ng-log_UNSUPPORTED_COMPILER
      -DCMAKE_CXX_FLAGS_INIT=-DNGLOG_INTERNAL_EXPERIMENTAL_LOGGING_UNAVAILABLE)

    nglog_add_build_test (cmake_package_config_experimental_unsupported
      SOURCE_DIR src/package_config_unittest/components
      TARGET ng-log_package_components
      OPTIONS
        -DNGLOG_REQUIRED_COMPONENTS=experimental
        ${_ng-log_UNSUPPORTED_COMPILER}
      EXPECT_FAILURE
        "required component experimental.*C\\+\\+ compiler.*does not support"
    )

    nglog_add_build_test (cmake_package_config_optional_experimental_unsupported
      SOURCE_DIR src/package_config_unittest/components
      TARGET ng-log_package_components
      OPTIONS
        -DNGLOG_OPTIONAL_COMPONENTS=experimental
        -DNGLOG_EXPECT_EXPERIMENTAL=FALSE
        ${_ng-log_UNSUPPORTED_COMPILER}
    )
  else (HAVE_EXPERIMENTAL_LOGGING)
    if (WITH_EXPERIMENTAL)
      set (_ng-log_EXPERIMENTAL_REASON "does not support")
    else (WITH_EXPERIMENTAL)
      set (_ng-log_EXPERIMENTAL_REASON "WITH_EXPERIMENTAL=OFF")
    endif (WITH_EXPERIMENTAL)

    nglog_add_build_test (cmake_package_config_experimental
      SOURCE_DIR src/package_config_unittest/components
      TARGET ng-log_package_components
      OPTIONS -DNGLOG_REQUIRED_COMPONENTS=experimental
      EXPECT_FAILURE
        "required component experimental.*${_ng-log_EXPERIMENTAL_REASON}"
    )

    # The build tree exposes the experimental headers, which must then reject
    # their use instead of failing to link.
    nglog_add_build_test (experimental_unavailable
      SOURCE_DIR src/experimental_requirements_unittest
      TARGET log_requires_header
      EXPECT_FAILURE
        "ng-log was built without the experimental logging functions"
    )
  endif (HAVE_EXPERIMENTAL_LOGGING)

  nglog_add_build_test (cmake_package_config_optional_experimental
    SOURCE_DIR src/package_config_unittest/components
    TARGET ng-log_package_components
    OPTIONS
      -DNGLOG_OPTIONAL_COMPONENTS=experimental
      -DNGLOG_EXPECT_EXPERIMENTAL=${_ng-log_EXPECT_EXPERIMENTAL}
  )

  # Without components, the experimental component is reported as well.
  nglog_add_build_test (cmake_package_config_no_components
    SOURCE_DIR src/package_config_unittest/components
    TARGET ng-log_package_components
    OPTIONS -DNGLOG_EXPECT_EXPERIMENTAL=${_ng-log_EXPECT_EXPERIMENTAL}
  )

  nglog_add_build_test (cmake_package_config_unknown_component
    SOURCE_DIR src/package_config_unittest/components
    TARGET ng-log_package_components
    OPTIONS -DNGLOG_REQUIRED_COMPONENTS=unknown
    EXPECT_FAILURE "ng-log provides no component unknown"
  )

  add_executable (cleanup_immediately_unittest
    src/cleanup_immediately_unittest.cc)

  target_link_libraries (cleanup_immediately_unittest PRIVATE ng-log_test)

  add_executable (cleanup_with_absolute_prefix_unittest
    src/cleanup_with_absolute_prefix_unittest.cc)

  target_link_libraries (cleanup_with_absolute_prefix_unittest PRIVATE ng-log_test)

  add_executable (cleanup_with_relative_prefix_unittest
    src/cleanup_with_relative_prefix_unittest.cc)

  target_link_libraries (cleanup_with_relative_prefix_unittest PRIVATE ng-log_test)

  add_executable (cleanup_with_non_ascii_prefix_unittest
    src/cleanup_with_non_ascii_prefix_unittest.cc)

  target_link_libraries (cleanup_with_non_ascii_prefix_unittest PRIVATE ng-log_test)

  set (CLEANUP_LOG_DIR ${ng-log_BINARY_DIR}/cleanup_tests)

  add_test (NAME cleanup_init COMMAND
    ${CMAKE_COMMAND} -E make_directory ${CLEANUP_LOG_DIR})
  add_test (NAME cleanup_logdir COMMAND
    ${CMAKE_COMMAND} -E remove_directory ${CLEANUP_LOG_DIR})
  add_test (NAME cleanup_immediately COMMAND
    ${CMAKE_COMMAND}
    -DLOGCLEANUP=$<TARGET_FILE:cleanup_immediately_unittest>
    "-DEMULATOR=${CMAKE_CROSSCOMPILING_EMULATOR}"
    # NOTE The trailing slash is important
    -DTEST_DIR=${CLEANUP_LOG_DIR}/
    -P ${ng-log_SOURCE_DIR}/cmake/RunCleanerTest1.cmake
    WORKING_DIRECTORY ${ng-log_BINARY_DIR})
  add_test (NAME cleanup_with_absolute_prefix COMMAND
    ${CMAKE_COMMAND}
    -DLOGCLEANUP=$<TARGET_FILE:cleanup_with_absolute_prefix_unittest>
    "-DEMULATOR=${CMAKE_CROSSCOMPILING_EMULATOR}"
    # NOTE The trailing slash is important
    -DTEST_DIR=${CLEANUP_LOG_DIR}/absolute_prefix/
    -P ${ng-log_SOURCE_DIR}/cmake/RunCleanerTest2.cmake
    WORKING_DIRECTORY ${ng-log_BINARY_DIR})
  add_test (NAME cleanup_with_relative_prefix COMMAND
    ${CMAKE_COMMAND}
    -DLOGCLEANUP=$<TARGET_FILE:cleanup_with_relative_prefix_unittest>
    "-DEMULATOR=${CMAKE_CROSSCOMPILING_EMULATOR}"
    -DTEST_DIR=${ng-log_BINARY_DIR}/
    -DTEST_SUBDIR=test_subdir/
    -P ${ng-log_SOURCE_DIR}/cmake/RunCleanerTest3.cmake
    WORKING_DIRECTORY ${ng-log_BINARY_DIR})
  add_test (NAME cleanup_with_non_ascii_prefix COMMAND
    ${CMAKE_COMMAND}
    -DLOGCLEANUP=$<TARGET_FILE:cleanup_with_non_ascii_prefix_unittest>
    "-DEMULATOR=${CMAKE_CROSSCOMPILING_EMULATOR}"
    -DTEST_DIR=${ng-log_BINARY_DIR}/
    -DTEST_ON_WINDOWS=${WIN32}
    -P ${ng-log_SOURCE_DIR}/cmake/RunCleanerTest4.cmake
    WORKING_DIRECTORY ${ng-log_BINARY_DIR})

  # Fixtures setup
  set_tests_properties (cleanup_init PROPERTIES FIXTURES_SETUP logcleanuptest)
  ## Fixtures cleanup
  set_tests_properties (cleanup_logdir PROPERTIES FIXTURES_CLEANUP logcleanuptest)
  # Fixture requirements
  set_tests_properties (
    cleanup_immediately
    cleanup_with_absolute_prefix
    cleanup_with_non_ascii_prefix
    cleanup_with_relative_prefix
    PROPERTIES FIXTURES_REQUIRED logcleanuptest
  )

  add_executable (striplog0_unittest
    src/striplog_unittest.cc
  )
  # Without stripping, only compiling out debug logging hides its output.
  target_compile_definitions (striplog0_unittest PRIVATE
    NDEBUG
    NGLOG_STRIP_LOG=0
  )
  target_link_libraries (striplog0_unittest PRIVATE ng-log_test)

  gtest_discover_tests (striplog0_unittest
    TEST_PREFIX striplog0.
    DISCOVERY_TIMEOUT 30
  )

  add_executable (striplog2_unittest
    src/striplog_unittest.cc
  )
  target_compile_definitions (striplog2_unittest PRIVATE NGLOG_STRIP_LOG=2)
  target_link_libraries (striplog2_unittest PRIVATE ng-log_test)

  gtest_discover_tests (striplog2_unittest
    TEST_PREFIX striplog2.
    DISCOVERY_TIMEOUT 30
  )

  add_executable (striplog10_unittest
    src/striplog_unittest.cc
  )
  target_compile_definitions (striplog10_unittest PRIVATE
    ERROR=0
    NDEBUG
    NGLOG_NO_ABBREVIATED_SEVERITIES
    NGLOG_STRIP_LOG=10
  )
  target_link_libraries (striplog10_unittest PRIVATE ng-log_test)

  gtest_discover_tests (striplog10_unittest
    TEST_PREFIX striplog10.
    DISCOVERY_TIMEOUT 30
  )

  nglog_add_build_test (flags_declaration
    SOURCE_DIR src/flags_declaration_unittest
    TARGET ng-log_flags_declaration
  )

  nglog_add_build_test (flags_builtin_redeclaration
    SOURCE_DIR src/flags_declaration_unittest
    TARGET ng-log_flags_builtin_redeclaration
  )
  # The static assertion message is part of the diagnostic of every compiler.
  set_tests_properties (flags_builtin_redeclaration PROPERTIES
    PASS_REGULAR_EXPRESSION "is a built-in ng-log flag"
  )

  nglog_add_build_test (log_severity_constants
    SOURCE_DIR src/log_severity_unittest
    TARGET ng-log_log_severity_constants
  )
  set_tests_properties (log_severity_constants PROPERTIES
    PASS_REGULAR_EXPRESSION "NGLOG_COMPACT_LOG_[1-3]"
  )

  nglog_add_build_test (log_severity_conversion
    SOURCE_DIR src/log_severity_unittest
    TARGET ng-log_log_severity_conversion
  )

  if (CMAKE_CXX_COMPILER_ID STREQUAL GNU)
    set_tests_properties (log_severity_conversion PROPERTIES
      PASS_REGULAR_EXPRESSION "error: invalid conversion from (‘|')int(’|')"
    )
  elseif (CMAKE_CXX_COMPILER_ID MATCHES Clang)
    set_tests_properties (log_severity_conversion PROPERTIES
      PASS_REGULAR_EXPRESSION "no known conversion from 'int'"
    )
  elseif (MSVC)
    set_tests_properties (log_severity_conversion PROPERTIES
      PASS_REGULAR_EXPRESSION "error C2440"
    )
  else (CMAKE_CXX_COMPILER_ID STREQUAL GNU)
    message (AUTHOR_WARNING
      "Unsupported C++ compiler ${CMAKE_CXX_COMPILER_ID}: "
      "log_severity_conversion test will be disabled"
    )
    set_tests_properties (log_severity_conversion PROPERTIES DISABLED ON)
  endif (CMAKE_CXX_COMPILER_ID STREQUAL GNU)

  nglog_add_build_test (log_severity_qualified
    SOURCE_DIR src/log_severity_unittest
    TARGET ng-log_log_severity_qualified
  )
  if (CMAKE_CXX_COMPILER_ID STREQUAL GNU)
    set_tests_properties (log_severity_qualified PROPERTIES
      PASS_REGULAR_EXPRESSION "NGLOG_INFO.*is not a member.*nglog"
    )
  elseif (CMAKE_CXX_COMPILER_ID MATCHES Clang)
    set_tests_properties (log_severity_qualified PROPERTIES
      PASS_REGULAR_EXPRESSION "no member named 'NGLOG_INFO' in namespace 'nglog'"
    )
  elseif (MSVC)
    set_tests_properties (log_severity_qualified PROPERTIES
      PASS_REGULAR_EXPRESSION "NGLOG_INFO.*is not a member of 'nglog'"
    )
  else (CMAKE_CXX_COMPILER_ID STREQUAL GNU)
    message (AUTHOR_WARNING
      "Unsupported C++ compiler ${CMAKE_CXX_COMPILER_ID}: "
      "log_severity_qualified test will be disabled"
    )
    set_tests_properties (log_severity_qualified PROPERTIES DISABLED ON)
  endif (CMAKE_CXX_COMPILER_ID STREQUAL GNU)

  if (HAVE_EXPERIMENTAL_LOGGING)
    # Each case of src/experimental_requirements_unittest at <group>/<name>.cc
    # must fail to compile with a diagnostic that matches its expression and
    # names what is wrong. The headers reject unsupported language and library
    # levels by naming the missing requirement, misuse of the functions by
    # naming the rule.
    set (_ng-log_REJECTED_CASES
      requires/cxx20
      requires/format
      requires/source_location
      signal_safe/locale
      signal_safe/alternate
      signal_safe/dynamic
      signal_safe/precision
      signal_safe/long_double_fixed
      signal_safe/fill_brace
      signal_safe/fill_non_ascii
      signal_safe/missing_precision
      signal_safe/leading_zero_width
      signal_safe/width
      signal_safe/type
      compare/no_comparison
      compare/comma
      compare/comma_message
      compare/nodiscard
      that/no_comparison
      that/cstring_ordering
      that/cstring_range
      that/wide_cstring
      that/ignore_case_non_cstring
      that/index_non_integer
      that/bound_non_integer
      that/byte_cstring
      that/nodiscard
      rate_limit/every_floating
      rate_limit/every_bool
      rate_limit/first_floating
      rate_limit/first_duration
      log/requires_header
      log/two_stage_nodiscard
      log/repeated_severity
      log/conflicting_destinations
      log/skip_log_file_alone
      log/misdeclared_hook
      log/severity_out_of_range
      ensure/nodiscard
      ensure/constexpr
      ensure/constexpr_comparison
      ensure/constexpr_that
      ensure/constexpr_not_null
    )
    set (_ng-log_REJECTED_REASONS
      "requires C\\+\\+20"
      "requires std::format with compile-time checked format strings \\(__cpp_lib_format >= 202106L\\)"
      "requires std::source_location \\(__cpp_lib_source_location >= 201907L\\)"
      LocaleSpecificFormattingIsNotSignalSafe
      SignalSafeFloatAlternateFormIsNotSupported
      SignalSafeFloatDynamicFieldIsNotSupported
      SignalSafeFloatPrecisionExceedsLimit
      SignalSafeLongDoubleFixedNotationIsNotSupported
      SignalSafeFloatFillIsInvalid
      SignalSafeFloatFillIsNotAscii
      SignalSafeFloatPrecisionIsMissing
      SignalSafeFloatWidthIsInvalid
      SignalSafeFloatWidthExceedsLimit
      SignalSafeFloatTypeIsNotSupported
      "NGLOG_COMPARE\\(\\) expects a comparison such as a == b"
      "operator ?,"
      "operator ?,"
      "Compare the operand and pass the comparison to Ensure"
      "That\\(\\) expects a comparison such as That\\(a\\) == b"
      "That\\(\\) cannot order C strings"
      "That\\(\\) cannot order C strings"
      "That\\(\\) compares C strings of char only"
      "Cstring"
      "IntegerOperand"
      "IntegerOperand"
      "That\\(\\) compares C strings of char only"
      "Compare the operand and pass the comparison to Ensure"
      "Every expects a count of occurrences"
      "Every expects a count of occurrences"
      "First expects a count of occurrences"
      "First expects a count of occurrences"
      "requires including ng-log/experimental/log.h"
      "call the logger with the format string and arguments"
      "(satisfaction of |')[^ ]*SingleAttributesAreUnique<"
      "(satisfaction of |')[^ ]*CategoriesAreDistinct<"
      "SkipLogFile requires a ToSink destination"
      "Admit\\(\\) of an attribute must be const"
      SeverityIsOutOfRange
      "Pass the comparison to Ensure"
      EnsureFailedDuringConstantEvaluation
      EnsureFailedDuringConstantEvaluation
      EnsureFailedDuringConstantEvaluation
      EnsureFailedDuringConstantEvaluation
    )

    foreach (_ng-log_CASE _ng-log_REASON IN ZIP_LISTS _ng-log_REJECTED_CASES
        _ng-log_REJECTED_REASONS)
      string (REPLACE "/" "_" _ng-log_TARGET ${_ng-log_CASE})
      string (REPLACE "/" "." _ng-log_TEST ${_ng-log_CASE})
      nglog_add_build_test (experimental_${_ng-log_TEST}
        SOURCE_DIR src/experimental_requirements_unittest
        TARGET ${_ng-log_TARGET}
        EXPECT_FAILURE "${_ng-log_REASON}"
      )
    endforeach (_ng-log_CASE _ng-log_REASON)
  endif (HAVE_EXPERIMENTAL_LOGGING)

  nglog_add_build_test (includes_logging
    SOURCE_DIR src/includes_unittest
    TARGET ng-log_includes_logging
  )

  # The program checks the value of NGLOG_DFATAL_LEVEL.
  nglog_add_build_test (includes_log_severity
    SOURCE_DIR src/includes_unittest
    TARGET ng-log_includes_log_severity
    TEST_COMMAND ng-log_includes_log_severity
  )

  nglog_add_build_test (includes_vlog_is_on
    SOURCE_DIR src/includes_unittest
    TARGET ng-log_includes_vlog_is_on
  )

  nglog_add_build_test (includes_raw_logging
    SOURCE_DIR src/includes_unittest
    TARGET ng-log_includes_raw_logging
  )

  nglog_add_build_test (includes_stl_logging
    SOURCE_DIR src/includes_unittest
    TARGET ng-log_includes_stl_logging
  )

  nglog_add_build_test (dcheck_on
    SOURCE_DIR src/dcheck_unittest
    TARGET ng-log_dcheck
    CONFIG Debug
    TEST_COMMAND ng-log_dcheck
  )
  set_tests_properties (dcheck_on PROPERTIES
    DISABLED $<NOT:$<CONFIG:Debug,RelWithDebInfo>>
    ENVIRONMENT_MODIFICATION "PATH=path_list_prepend:$<TARGET_FILE_DIR:ng-log>"
    PASS_REGULAR_EXPRESSION "Assert failed: false"
  )

  nglog_add_build_test (dcheck_off
    SOURCE_DIR src/dcheck_unittest
    TARGET ng-log_dcheck
    CONFIG Release
    TEST_COMMAND ng-log_dcheck
  )
  # None of the debug-only statements may produce output. The build output
  # is part of the test output, so match the messages themselves.
  set_tests_properties (dcheck_off PROPERTIES
    DISABLED $<NOT:$<CONFIG:Release,MinSizeRel>>
    ENVIRONMENT_MODIFICATION "PATH=path_list_prepend:$<TARGET_FILE_DIR:ng-log>"
    FAIL_REGULAR_EXPRESSION "no output;Assert failed"
  )
endif (BUILD_TESTING AND GTest_FOUND)

if (benchmark_FOUND)
  add_executable (logging_benchmark
    src/logging_benchmark.cc
  )

  target_link_libraries (logging_benchmark PRIVATE ng-log benchmark::benchmark)
  if (WITH_LOCK_METRICS)
    target_compile_definitions (logging_benchmark PRIVATE NGLOG_ENABLE_LOCK_METRICS)
  endif (WITH_LOCK_METRICS)
endif (benchmark_FOUND)

if (BUILD_EXAMPLES)
  add_executable (custom_sink_example examples/custom_sink.cc)
  target_link_libraries (custom_sink_example PRIVATE ng-log::ng-log)

  add_executable (color_stacktrace_example examples/color_stacktrace.cc)
  target_link_libraries (color_stacktrace_example PRIVATE ng-log::ng-log)

  if (EMSCRIPTEN)
    target_link_options (custom_sink_example PRIVATE "-sEXIT_RUNTIME=1")
    target_link_options (color_stacktrace_example PRIVATE "-sEXIT_RUNTIME=1")
  endif (EMSCRIPTEN)
endif (BUILD_EXAMPLES)

install (TARGETS ng-log
  EXPORT ng-log-targets
  FILE_SET HEADERS
)

if (WITH_PKGCONFIG)
  install (
    FILES "${ng-log_BINARY_DIR}/libng-log.pc"
    DESTINATION "${CMAKE_INSTALL_LIBDIR}/pkgconfig"
  )
endif (WITH_PKGCONFIG)

set (ng-log_CMake_VERSION ${CMAKE_MINIMUM_REQUIRED_VERSION})

# The package config reports why the experimental component is missing.
if (HAVE_EXPERIMENTAL_LOGGING)
  set (_ng-log_EXPERIMENTAL_FOUND TRUE)
  set (_ng-log_EXPERIMENTAL_MISSING_REASON)
elseif (WITH_EXPERIMENTAL)
  set (_ng-log_EXPERIMENTAL_FOUND FALSE)
  set (_ng-log_EXPERIMENTAL_MISSING_REASON "the compiler used to build ng-log \
does not support the experimental logging functions")
else (HAVE_EXPERIMENTAL_LOGGING)
  set (_ng-log_EXPERIMENTAL_FOUND FALSE)
  set (_ng-log_EXPERIMENTAL_MISSING_REASON "ng-log was built with \
WITH_EXPERIMENTAL=OFF")
endif (HAVE_EXPERIMENTAL_LOGGING)

configure_package_config_file (cmake/ng-log-config.cmake.in
  ${ng-log_BINARY_DIR}/ng-log-config.cmake
  INSTALL_DESTINATION ${_ng-log_CMake_INSTALLDIR}
  NO_CHECK_REQUIRED_COMPONENTS_MACRO)

write_basic_package_version_file (
  ${ng-log_BINARY_DIR}/ng-log-config-version.cmake
  COMPATIBILITY SameMinorVersion)

export (EXPORT ng-log-targets NAMESPACE ng-log:: FILE ng-log-targets.cmake)
export (PACKAGE ng-log)

install (FILES
  ${ng-log_BINARY_DIR}/ng-log-config.cmake
  ${ng-log_BINARY_DIR}/ng-log-config-version.cmake
  DESTINATION ${_ng-log_CMake_INSTALLDIR})

list (TRANSFORM _ng-log_FIND_MODULES PREPEND cmake/)

install (FILES ${_ng-log_FIND_MODULES}
  DESTINATION ${_ng-log_CMake_INSTALLDIR}/modules
  COMPONENT Development)

install (EXPORT ng-log-targets NAMESPACE ng-log:: DESTINATION
  ${_ng-log_CMake_INSTALLDIR})

if (CMAKE_VERSION VERSION_GREATER_EQUAL 4.3
  AND CMAKE_EXPERIMENTAL_GENERATE_SBOM)
  if (CMAKE_VERSION VERSION_GREATER_EQUAL 4.4)
    set (_ng-log_SBOM_EXPORT_ARGUMENTS EXPORTS ng-log-targets)
  else (CMAKE_VERSION VERSION_GREATER_EQUAL 4.4)
    set (_ng-log_SBOM_EXPORT_ARGUMENTS EXPORT ng-log-targets)
  endif (CMAKE_VERSION VERSION_GREATER_EQUAL 4.4)

  set (_ng-log_SBOM_ARGUMENTS
    PROJECT ng-log
    DESTINATION ${CMAKE_INSTALL_DATADIR}/sbom
    VERSION ${ng-log_VERSION}
    LICENSE ${_ng-log_SPDX_LICENSE}
    DESCRIPTION ${ng-log_DESCRIPTION}
    HOMEPAGE_URL ${ng-log_HOMEPAGE_URL}
    FORMAT spdx-3.0+json
  )

  if (CMAKE_VERSION VERSION_GREATER_EQUAL 4.4)
    list (APPEND _ng-log_SBOM_ARGUMENTS
      PACKAGE_URL ${ng-log_HOMEPAGE_URL})
  endif (CMAKE_VERSION VERSION_GREATER_EQUAL 4.4)

  # SBOM generation is experimental and must not turn into a configuration
  # error when author warnings are treated as errors.
  if (COMMAND cmake_diagnostic)
    cmake_diagnostic (PUSH)
    cmake_diagnostic (DEMOTE CMD_EXPERIMENTAL WARN)
  endif (COMMAND cmake_diagnostic)

  install (SBOM ng-log
    ${_ng-log_SBOM_EXPORT_ARGUMENTS}
    ${_ng-log_SBOM_ARGUMENTS}
  )

  if (COMMAND cmake_diagnostic)
    cmake_diagnostic (POP)
  endif (COMMAND cmake_diagnostic)
endif (CMAKE_VERSION VERSION_GREATER_EQUAL 4.3
  AND CMAKE_EXPERIMENTAL_GENERATE_SBOM)

# CPack describes the installation of the top level project and takes the
# install components that are defined when it is included.
if (PROJECT_IS_TOP_LEVEL)
  set (CPACK_PACKAGE_NAME ng-log)
  set (CPACK_PACKAGE_DESCRIPTION_SUMMARY "C++ logging library")
  set (CPACK_PACKAGE_VERSION_MAJOR ${ng-log_VERSION_MAJOR})
  set (CPACK_PACKAGE_VERSION_MINOR ${ng-log_VERSION_MINOR})
  set (CPACK_PACKAGE_VERSION_PATCH ${ng-log_VERSION_PATCH})
  set (CPACK_PACKAGE_VERSION ${ng-log_VERSION})

  include (CPack)
endif (PROJECT_IS_TOP_LEVEL)
