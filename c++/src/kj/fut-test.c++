// Copyright (c) 2025 Cloudflare, Inc. and contributors
// Licensed under the MIT License:
//
// Permission is hereby granted, free of charge, to any person obtaining a copy
// of this software and associated documentation files (the "Software"), to deal
// in the Software without restriction, including without limitation the rights
// to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
// copies of the Software, and to permit persons to whom the Software is
// furnished to do so, subject to the following conditions:
//
// The above copyright notice and this permission notice shall be included in
// all copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
// AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
// OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
// THE SOFTWARE.

#include "kj/async.h"
#include "kj/common.h"
#include "kj/debug.h"
#include "kj/exception.h"
#include <kj/fut.h>
#include <kj/test.h>

namespace kj {
namespace {

kj::Promise<void> readyNow() { return kj::READY_NOW; }

kj::Promise<void> delayedPromise() {
  return kj::evalLater([]() {});
}

Fut<void> empty() { co_return; }

Fut<void> delayedFut() { co_await delayedPromise(); }

KJ_TEST("empty") {
  kj::EventLoop loop;
  kj::WaitScope waitScope(loop);
  empty().wait(waitScope);
}

Fut<size_t> immediate() { co_return 42; }

KJ_TEST("immediate") {
  kj::EventLoop loop;
  kj::WaitScope waitScope(loop);

  KJ_ASSERT(42 == immediate().wait(waitScope));
}

KJ_TEST("co_await immediate") {
  kj::EventLoop loop;
  kj::WaitScope waitScope(loop);

  auto fut = []() -> Fut<size_t> { co_return co_await immediate(); };
  KJ_ASSERT(42 == fut().wait(waitScope));
}

KJ_TEST("Fut coroutines are eager") {
  kj::EventLoop loop;
  kj::WaitScope waitScope(loop);

  size_t val = 0;
  // create a future but don't wait on it
  auto fut = [](size_t* ptr) -> Fut<void> { *ptr = 42; co_return ; }(&val);
  KJ_EXPECT(val == 42);
}

KJ_TEST("Fut coroutines can be lazy") {
  kj::EventLoop loop;
  kj::WaitScope waitScope(loop);

  size_t val = 0;

  auto fut = [](size_t* ptr) -> Fut<void, true /* lazy */> { *ptr = 42; co_return ; }(&val);
  // - coroutine gets allocated
  // - p = FutPromise<void, true> is initialized in the frame
  // - p.get_return_object is called
  //      - f = Fut<void>(handle_from_promise(p)) is created and returned
  // - p.initial_suspend is called, we return suspend_always
  // - coroutine gets suspended

  KJ_EXPECT(val == 0);

  KJ_DBG("====> wait");
  fut.wait(waitScope);
  KJ_DBG("<<====>>> wait", val);
  KJ_EXPECT(val == 42);
}

KJ_TEST("co_await discard result") {
  kj::EventLoop loop;
  kj::WaitScope waitScope(loop);

  auto fut = []() -> Fut<void> { co_await immediate(); };
  fut().wait(waitScope);
}

KJ_TEST("co_await empty") {
  kj::EventLoop loop;
  kj::WaitScope waitScope(loop);

  auto fut = []() -> Fut<void> {
    co_await empty();
  };
  fut().wait(waitScope);
}

KJ_TEST("co_await readyNow") {
  kj::EventLoop loop;
  kj::WaitScope waitScope(loop);

  auto fut = []() -> Fut<void> { co_await readyNow(); };

  // - coro frame gets allocated
  //  - p = FutPromise<void> initialized in the frame
  //    - p.get_return_object is called
  //      - f = Fut<void>(handle_from_promise(p)) is created and returned
  //    - p.initial_suspend is called, we return suspend_never
  //    - p.await_transform is called on readyNow()
  //    - a = PromiseFutAwaiter is initialized
  //     - a.await_ready is called, we return false since promises always
  //     suspend first
  //     - a.await_suspend(handle(p)) is called to suspend the coro frame, it
  //     returns true (suspend)
  //     - kj event loop calls a.fire()
  //     - a.fire() resumes handle(p)
  //     - a.await_resume is called
  //     - a is destroyed
  //    - p.return_void() is called
  //       - f.resolve() is called
  //    - p is destroyed
  // - coro frame is destroyed

  fut().wait(waitScope);
}

KJ_TEST("co_await delayed promise") {
  kj::EventLoop loop;
  kj::WaitScope waitScope(loop);

  auto fut = []() -> Fut<void> { co_await delayedPromise(); };
  fut().wait(waitScope);
}

Fut<void> throwException() {
  if (true) {
    kj::throwFatalException(KJ_EXCEPTION(DISCONNECTED, "request canceled"));
  }
  co_return;
}

KJ_TEST("throw exception") {
  kj::EventLoop loop;
  kj::WaitScope waitScope(loop);

  try {
    throwException().wait(waitScope);
    KJ_UNREACHABLE;
  } catch (...) {
    auto e = kj::getCaughtExceptionAsKj();
    KJ_ASSERT(e.getType() == kj::Exception::Type::DISCONNECTED);
    KJ_ASSERT(e.getDescription() == "request canceled");
  }
}

// KJ_TEST("co_await delayed fut") {
//   kj::EventLoop loop;
//   kj::WaitScope waitScope(loop);

//   auto fut = []() -> Fut<void> {
//     co_await delayedFut();
//   };
//   fut().wait(waitScope);
// }

} // namespace
} // namespace kj