// SPDX-FileCopyrightText: 2026 The ng-log contributors
//
// SPDX-License-Identifier: BSD-3-Clause
//
// Author: Sergiu Deitsch

// Runs the logging thread, which sends the messages of the Async attribute.

#include "async.h"

#include "config.h"

#if defined(HAVE_FORK)
#  include <pthread.h>
#endif

#include <condition_variable>
#include <cstddef>
#include <cstdlib>
#include <memory>
#include <mutex>
#include <queue>
#include <thread>
#include <utility>

#include "internal/log_message_access.h"
#include "ng-log/platform.h"

namespace nglog::experimental::internal {
namespace {

// Sends the messages passed to it on a thread of its own in the order they
// were passed.
class LogThread {
 public:
  void Push(std::unique_ptr<PendingMessage> message) {
    if (Enqueue(message)) {
      pending_.notify_one();
    } else {
      Send(*message);
    }
  }

  void Wait() {
    std::unique_lock lock{mutex_};
    // The logging thread would otherwise wait for the message it sends.
    if (OnLoggingThread()) {
      return;
    }
    idle_.wait(lock,
               [this] { return (queue_.empty() && !sending_) || stopped_; });
  }

  // Sends the pending messages before the program exits, after which each
  // message is sent by the thread that logs it.
  void Stop() {
    std::queue<std::unique_ptr<PendingMessage>> remaining;
    {
      std::unique_lock lock{mutex_};
#if !defined(NGLOG_OS_WINDOWS)
      // Windows terminates the other threads before a library runs its exit
      // handlers, so the logging thread might never become idle there. The
      // exiting thread sends the queued messages instead.
      if (!OnLoggingThread()) {
        idle_.wait(lock, [this] { return queue_.empty() && !sending_; });
      }
#endif
      stopped_ = true;
      remaining.swap(queue_);
      pending_bytes_ = 0;
    }
    space_.notify_all();
    idle_.notify_all();
    for (; !remaining.empty(); remaining.pop()) {
      Send(*remaining.front());
    }
  }

#if defined(HAVE_FORK)
  // Keeps the child of a fork from inheriting the lock while another thread
  // holds it.
  void PrepareFork() { mutex_.lock(); }
  void ResumeParentAfterFork() { mutex_.unlock(); }

  // The logging thread does not exist in the child, which starts its own on
  // first use. Joining the inherited handle would wait forever, so it is
  // abandoned instead. The pending messages are the parent's to send.
  void ResumeChildAfterFork() {
    [[maybe_unused]] std::thread* const abandoned = thread_.release();
    queue_ = {};
    pending_bytes_ = 0;
    sending_ = false;
    mutex_.unlock();
  }
#endif

 private:
  // Queues a message unless the calling thread sends it itself. The logging
  // thread would otherwise wait for space in the queue that only it frees, and
  // its messages would follow those logged later. Once the program exits, each
  // thread sends its messages.
  bool Enqueue(std::unique_ptr<PendingMessage>& message) {
    std::unique_lock lock{mutex_};
    if (OnLoggingThread()) {
      return false;
    }
    const std::size_t size = message->size();
    space_.wait(lock, [this, size] {
      return pending_bytes_ + size <= kMaxPendingBytes || stopped_;
    });
    if (stopped_) {
      return false;
    }
    if (thread_ == nullptr) {
      thread_ = std::make_unique<std::thread>(&LogThread::Run, this);
      nglog::internal::SetPendingMessagesWaiter(&WaitForPendingMessages);
    }
    pending_bytes_ += size;
    queue_.push(std::move(message));
    return true;
  }

  // Requires the lock.
  bool OnLoggingThread() const noexcept {
    return thread_ != nullptr &&
           thread_->get_id() == std::this_thread::get_id();
  }

  // Runs until the program ends, which leaves the thread blocked since it is
  // never joined.
  [[noreturn]] void Run() {
    for (;;) {
      std::unique_ptr<PendingMessage> message = Take();
      Send(*message);
      message.reset();
      Finish();
    }
  }

  // Waits for a message and takes it from the queue.
  std::unique_ptr<PendingMessage> Take() {
    std::unique_ptr<PendingMessage> message;
    {
      std::unique_lock lock{mutex_};
      pending_.wait(lock, [this] { return !queue_.empty(); });
      message = std::move(queue_.front());
      queue_.pop();
      pending_bytes_ -= message->size();
      sending_ = true;
    }
    space_.notify_all();
    return message;
  }

  // Marks the message taken last as sent.
  void Finish() {
    bool idle = false;
    {
      const std::scoped_lock lock{mutex_};
      sending_ = false;
      idle = queue_.empty();
    }
    if (idle) {
      idle_.notify_all();
    }
  }

  std::mutex mutex_;
  std::condition_variable pending_;
  std::condition_variable idle_;
  std::condition_variable space_;
  std::queue<std::unique_ptr<PendingMessage>> queue_;
  // Bytes of the text of the queued messages.
  std::size_t pending_bytes_ = 0;
  // Whether the thread sends a message it took from the queue.
  bool sending_ = false;
  // Whether the program exits, after which messages are no longer queued.
  bool stopped_ = false;
  // Started on first use, so that programs that log synchronously only do not
  // run the thread.
  std::unique_ptr<std::thread> thread_;
};

LogThread* CreateInstance();

// Never destroyed, so that messages can be deferred during the destruction of
// static objects.
LogThread& Instance() {
  static LogThread* const thread = CreateInstance();
  return *thread;
}

LogThread* CreateInstance() {
  auto* const thread = new LogThread;
  std::atexit([] { Instance().Stop(); });
#if defined(HAVE_FORK)
  pthread_atfork([] { Instance().PrepareFork(); },
                 [] { Instance().ResumeParentAfterFork(); },
                 [] { Instance().ResumeChildAfterFork(); });
#endif
  return thread;
}

}  // namespace

void Defer(std::unique_ptr<PendingMessage> message) {
  Instance().Push(std::move(message));
}

void WaitForPendingMessages() { Instance().Wait(); }

}  // namespace nglog::experimental::internal
