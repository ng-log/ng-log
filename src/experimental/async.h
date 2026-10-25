// SPDX-FileCopyrightText: 2026 The ng-log contributors
//
// SPDX-License-Identifier: BSD-3-Clause
//
// Author: Sergiu Deitsch

// Passes messages that the calling thread admitted to the logging thread,
// which composes them and sends them to their destinations.

#ifndef NGLOG_SRC_EXPERIMENTAL_ASYNC_H
#define NGLOG_SRC_EXPERIMENTAL_ASYNC_H

#include <cstddef>
#include <memory>
#include <optional>
#include <source_location>
#include <string>
#include <utility>

#include "context.h"
#include "internal/log_message_access.h"
#include "ng-log/experimental/log.h"

namespace nglog::experimental::internal {

inline constexpr std::size_t kMebibyte = 1024 * 1024;

// Bytes of copied messages that may wait for the logging thread. Callers wait
// for the queue to drain below it, like a destination that writes slowly lets
// a synchronous caller wait, instead of dropping messages or using unbounded
// memory. It far exceeds the size of any single message, so that one always
// fits.
inline constexpr std::size_t kMaxPendingBytes = 8 * kMebibyte;

// A message that the logging thread composes, together with the settings of
// its destinations and the origin of its call, which refer to copies the
// message owns instead of the state of the call.
class PendingMessage {
 public:
  PendingMessage(const ContextImpl& context,
                 std::unique_ptr<DeferredMessage> message,
                 nglog::internal::LogMessageOrigin origin)
      : location_{context.location()},
        context_{context},
        message_{std::move(message)},
        origin_{std::move(origin)} {
    context_.set_location(location_);
    if (context.tag() != nullptr) {
      tag_ = context.tag();
      context_.set_tag(tag_->c_str());
    }
  }

  PendingMessage(const PendingMessage& other) = delete;
  PendingMessage& operator=(const PendingMessage& other) = delete;

  ContextImpl& context() noexcept { return context_; }
  const DeferredMessage& message() const noexcept { return *message_; }
  const nglog::internal::LogMessageOrigin& origin() const noexcept {
    return origin_;
  }
  // Bytes the message counts against the capacity of the queue.
  std::size_t size() const noexcept { return message_->size(); }

 private:
  std::source_location location_;
  std::optional<std::string> tag_;
  ContextImpl context_;
  std::unique_ptr<DeferredMessage> message_;
  nglog::internal::LogMessageOrigin origin_;
};

// Composes a pending message and sends it to its destinations on the calling
// thread.
void Send(PendingMessage& message);

// Passes a message to the logging thread, which sends the messages in the
// order they were passed.
void Defer(std::unique_ptr<PendingMessage> message);

// Waits until the logging thread sent the messages passed so far.
void WaitForPendingMessages();

}  // namespace nglog::experimental::internal

#endif  // NGLOG_SRC_EXPERIMENTAL_ASYNC_H
