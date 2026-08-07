// SPDX-FileCopyrightText: 2026 The ng-log contributors
// SPDX-License-Identifier: BSD-3-Clause

#ifndef NGLOG_LONG_PATH_TEST_H_
#define NGLOG_LONG_PATH_TEST_H_

#include <windows.h>

#include <cstddef>
#include <string>
#include <vector>

#include "internal/utf8.h"

namespace nglog {
namespace testing_internal {

inline bool IsWine() {
  const HMODULE ntdll = GetModuleHandleW(L"ntdll.dll");
  return ntdll != nullptr &&
         GetProcAddress(ntdll, "wine_get_version") != nullptr;
}

class LongPathTestDirectory final {
 public:
  LongPathTestDirectory() = default;
  LongPathTestDirectory(const LongPathTestDirectory&) = delete;
  LongPathTestDirectory& operator=(const LongPathTestDirectory&) = delete;

  ~LongPathTestDirectory() {
    for (auto directory = directories_.rbegin();
         directory != directories_.rend(); ++directory) {
      RemoveDirectoryW(directory->c_str());
    }
  }

  bool Create(const std::string& name) {
    const DWORD required_length = GetCurrentDirectoryW(0, nullptr);
    if (required_length == 0) {
      return false;
    }

    std::vector<wchar_t> current_directory(required_length);
    const DWORD current_directory_length = GetCurrentDirectoryW(
        static_cast<DWORD>(current_directory.size()), current_directory.data());
    if (current_directory_length == 0 ||
        current_directory_length >= current_directory.size()) {
      return false;
    }

    std::wstring wide_name;
    if (!internal::Utf8ToWide(name.data(), name.size(), &wide_name)) {
      return false;
    }
    std::wstring absolute_path(current_directory.data(),
                               current_directory_length);
    absolute_path += L'\\';
    absolute_path += wide_name;
    return CreateAtPath(name, absolute_path);
  }

  bool CreateInTemporaryDirectory(const std::string& name) {
    std::string temporary_path;
    if (!internal::GetTempPathUtf8(&temporary_path)) {
      return false;
    }
    if (!temporary_path.empty() && temporary_path.back() != '\\' &&
        temporary_path.back() != '/') {
      temporary_path += '\\';
    }
    temporary_path += name;

    std::wstring wide_path;
    if (!internal::Utf8ToWide(temporary_path.data(), temporary_path.size(),
                              &wide_path)) {
      return false;
    }
    return CreateAtPath(temporary_path, wide_path);
  }

  const std::string& relative_path() const { return relative_path_; }
  const std::wstring& extended_path() const { return extended_path_; }

  std::string ChildPath(const std::string& name) const {
    return relative_path_ + '/' + name;
  }

  std::wstring ExtendedChildPath(const std::wstring& name) const {
    return extended_path_ + L'\\' + name;
  }

 private:
  bool CreateAtPath(const std::string& path, const std::wstring& wide_path) {
    relative_path_ = path;
    extended_path_ = ToExtendedPath(wide_path);
    if (!CreateCurrentDirectory()) {
      return false;
    }

    constexpr std::size_t kLongPathMargin = 64;
    constexpr wchar_t kWideComponent[] = L"long_path_component";
    constexpr char kComponent[] = "long_path_component";
    const std::size_t minimum_length = MAX_PATH + kLongPathMargin;
    while (relative_path_.size() < minimum_length) {
      relative_path_ += '/';
      relative_path_ += kComponent;
      extended_path_ += L'\\';
      extended_path_ += kWideComponent;
      if (!CreateCurrentDirectory()) {
        return false;
      }
    }

    constexpr char kUtf8Component[] = "unicode_\xE2\x82\xAC";
    constexpr wchar_t kWideUtf8Component[] = L"unicode_\u20AC";
    relative_path_ += '/';
    relative_path_ += kUtf8Component;
    extended_path_ += L'\\';
    extended_path_ += kWideUtf8Component;
    return CreateCurrentDirectory();
  }
  static std::wstring ToExtendedPath(const std::wstring& path) {
    constexpr wchar_t kUncPrefix[] = L"\\\\";
    constexpr wchar_t kExtendedPrefix[] = L"\\\\?\\";
    constexpr wchar_t kExtendedUncPrefix[] = L"\\\\?\\UNC\\";
    constexpr std::size_t kUncPrefixLength =
        sizeof(kUncPrefix) / sizeof(kUncPrefix[0]) - 1;
    if (path.compare(0, kUncPrefixLength, kUncPrefix) == 0) {
      return kExtendedUncPrefix + path.substr(kUncPrefixLength);
    }
    return kExtendedPrefix + path;
  }

  bool CreateCurrentDirectory() {
    if (!CreateDirectoryW(extended_path_.c_str(), nullptr)) {
      return false;
    }
    directories_.push_back(extended_path_);
    return true;
  }

  std::string relative_path_;
  std::wstring extended_path_;
  std::vector<std::wstring> directories_;
};

}  // namespace testing_internal
}  // namespace nglog

#endif  // NGLOG_LONG_PATH_TEST_H_
