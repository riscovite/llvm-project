//===--- Implementation of Thread for RISCovite -----------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "src/__support/threads/thread.h"
#include "config/app.h"
#include "src/__support/CPP/atomic.h"
#include "src/__support/CPP/new.h"
#include "src/__support/CPP/string_view.h"
#include "src/__support/CPP/stringstream.h"
#include "src/__support/OSUtil/riscovite/sysfuncs.h"
#include "src/__support/OSUtil/syscall.h"
#include "src/__support/common.h"
#include "src/__support/error_or.h"
#include "src/__support/macros/config.h"
#include "src/errno/libc_errno.h" // For error macros

#include <assert.h>
#include <stdint.h>

namespace LIBC_NAMESPACE_DECL {

static constexpr ErrorOr<size_t> add_no_overflow(size_t lhs, size_t rhs) {
  if (lhs > SIZE_MAX - rhs)
    return Error{EINVAL};
  if (rhs > SIZE_MAX - lhs)
    return Error{EINVAL};
  return lhs + rhs;
}

static constexpr ErrorOr<size_t> round_to_page(size_t v) {
  auto vp_or_err = add_no_overflow(v, 4096 - 1);
  if (!vp_or_err)
    return vp_or_err;

  return vp_or_err.value() & -4096;
}

static constexpr ErrorOr<size_t> round_for_stack(size_t v) {
  auto vp_or_err = add_no_overflow(v, 16 - 1);
  if (!vp_or_err)
    return vp_or_err;

  return vp_or_err.value() & -16;
}

void thread_startup_posix(void *arg, ThreadRunnerPosix runner,
                          ThreadTracker *tracker, ThreadAttributes *attrs) {
  (void)tracker;
  attrs->atexit_callback_mgr = internal::get_thread_atexit_callback_mgr();
  self.attrib = attrs;
  void *result = runner(arg);
  attrs->retval.posix_retval = result;
  thread_exit(ThreadReturnValue(attrs->retval.posix_retval),
              ThreadStyle::POSIX);
}

void thread_startup_stdc(void *arg, ThreadRunnerStdc runner,
                         ThreadTracker *tracker, ThreadAttributes *attrs) {
  (void)tracker;
  attrs->atexit_callback_mgr = internal::get_thread_atexit_callback_mgr();
  self.attrib = attrs;
  int result = runner(arg);
  attrs->retval.stdc_retval = result;
  thread_exit(ThreadReturnValue(attrs->retval.stdc_retval), ThreadStyle::STDC);
}

// A pointer to a function implementing an interrupt handler for switching
// between threads. Use functions of this type with the
// `thread_switching_interrupt_handler` function template to produce a pointer
// that can be used as a RISCovite interrupt handler.
//
// The second argument is a pointer to the saved state of the currently-active
// thread, stored at the tip of it stack. The result is a pointer to the saved
// state of the thread to switch to, which can be the same pointer given in
// the second argument if no thread switching is actually needed.
//
// The first argument is whatever was passed as the first argument by the
// RISCovite supervisor when calling the interrupt, and so its meaning
// depends on what interrupt type the function is responding to.
using ThreadSwitchingInterruptHandler = ThreadState *(*)(uint64_t,
                                                         ThreadState *);

template <ThreadSwitchingInterruptHandler handler>
static __attribute__((naked)) __attribute__((noreturn)) void
thread_switching_interrupt_handler(uint64_t a0) {
  // The following riscv64 assembly code adapts from the situation created
  // by the RISCovite supervisor on entry into an interrupt handler into
  // the signature expected by a ThreadStateInterruptHandler function.
  // Instantiations of this template can therefore be used directly as interrupt
  // handlers as long as the supervisor-provided arguments are not needed.
  //
  // At entry the stack pointer refers to the first field of the interrupt
  // stack frame, whose layout matches the fields of ThreadState starting
  // at the interrupt_flags field. We therefore adjust the stack pointer
  // to make room for the ThreadState fields that precede interrupt_flags
  // (since the stack grows downwards) and then populate them before calling
  // "handler" with the stack pointer as its first argument. The additional
  // fields we populate here are dealing with the callee-saved registers from
  // the ABI, along with the global pointer and the thread pointer that
  // are unchanged by a normal function call but could potentially change
  // when switching to a different thread.
  asm volatile(
      // NOTE: The following MUST preserve a0 until the jalr instruction so that
      // it can pass as the first argument to the handler function.
      "\n\tlla a2, %[HANDLER]" // load the constant handler address for our jalr
                               // below
      ///////////////////////////////////////////
      // The handler gets the ThreadState pointer as its second argument, and
      // that's a constant offset from the stack pointer at entry. We also need
      // to adjust the stack pointer itself so that our call to the handler
      // (or another interrupt) won't clobber those extra fields.
      // (It's actually against GCC/Clang's rules to directly modify the stack
      // pointer from inline assembly, but this function is intended to be
      // called only directly by the supervisor to service an interrupt and
      // therefore we're not subject to that constraint here, and we're going
      // to restore sp before we return to the supervisor anyway.)
      "\n\taddi sp, sp, %[SP_OFFSET_BEFORE]"
      "\n\tmv a1, sp"
      // The remaining stores are all relative to sp, which now
      // points to the beginning of what will become our ThreadState object.
      // (Use sp instead of a1 because these can then be 2-byte instructions.)
      "\n\tsd tp, %[TP_FIELD_OFFSET](sp)"
      "\n\tsd gp, %[GP_FIELD_OFFSET](sp)"
      "\n\tsd s0, %[S0_FIELD_OFFSET](sp)"
      "\n\tsd s1, %[S1_FIELD_OFFSET](sp)"
      "\n\tsd s2, %[S2_FIELD_OFFSET](sp)"
      "\n\tsd s3, %[S3_FIELD_OFFSET](sp)"
      "\n\tsd s4, %[S4_FIELD_OFFSET](sp)"
      "\n\tsd s5, %[S5_FIELD_OFFSET](sp)"
      "\n\tsd s6, %[S6_FIELD_OFFSET](sp)"
      "\n\tsd s7, %[S7_FIELD_OFFSET](sp)"
      "\n\tsd s8, %[S8_FIELD_OFFSET](sp)"
      "\n\tsd s9, %[S9_FIELD_OFFSET](sp)"
      "\n\tsd s10, %[S10_FIELD_OFFSET](sp)"
      "\n\tsd s11, %[S11_FIELD_OFFSET](sp)"
      // And now we're finally ready to call the handler. We treat this as
      // a normal function call so that after the function returns we can
      // perform the RISCovite-specific "return from interrupt" custom
      // instruction to pass control back to the supervisor.
      // a0 should still contain the original value it had on entry to
      // this function, so that the handler can make use of it if needed.
      "\n\tjalr ra, a2, 0" // Call the handler address we loaded into a2 above
      // After return, a0 now points to the ThreadState for the thread we
      // want to switch back to, and thus also to the tip of its stack. sp
      // is currently pointing to the tip of the stack of the thread we're
      // switching _from_, and so there is a brief window here where another
      // interrupt could get its stack frame pushed onto either stack, but
      // that's okay as long as the interrupt handler is well-behaved by
      // restoring sp properly when it returns.
      "\n\tmv sp, a0" // sp now refers to the new thread's ThreadState
      // We'll restore all of the callee-saved and special registers now.
      "\n\tld tp, %[TP_FIELD_OFFSET](sp)"
      "\n\tld gp, %[GP_FIELD_OFFSET](sp)"
      "\n\tld s0, %[S0_FIELD_OFFSET](sp)"
      "\n\tld s1, %[S1_FIELD_OFFSET](sp)"
      "\n\tld s2, %[S2_FIELD_OFFSET](sp)"
      "\n\tld s3, %[S3_FIELD_OFFSET](sp)"
      "\n\tld s4, %[S4_FIELD_OFFSET](sp)"
      "\n\tld s5, %[S5_FIELD_OFFSET](sp)"
      "\n\tld s6, %[S6_FIELD_OFFSET](sp)"
      "\n\tld s7, %[S7_FIELD_OFFSET](sp)"
      "\n\tld s8, %[S8_FIELD_OFFSET](sp)"
      "\n\tld s9, %[S9_FIELD_OFFSET](sp)"
      "\n\tld s10, %[S10_FIELD_OFFSET](sp)"
      "\n\tld s11, %[S11_FIELD_OFFSET](sp)"
      // We need to adjust the stack pointer back before we return, since
      // the supervisor is expecting to find its own interrupt stack frame
      // at the stack pointer.
      "\n\taddi sp, sp, %[SP_OFFSET_AFTER]"
      // The following is the RISCovite custom instruction for "interrupt
      // return", encoded directly here just so that we don't need to rely on a
      // modified assembler to support the "riscovite.iret" mnemonic. (We could
      // potentially avoid encoding this directly by just restoring the original
      // return address register and using "ret", since the supervisor arranges
      // for an interrupt handler to return to an iret instruction encoded in a
      // read-only page, but doing this directly means that we can just clobber
      // ra without needing to saving it since we know what instruction would've
      // been at that address anyway.
      "\n.word 0xFFFF8FF3"
      :
      /* no output operands */
      : [HANDLER] "i"(handler),
        [SP_OFFSET_BEFORE] "i"(-offsetof(ThreadState, interrupt_flags)),
        [SP_OFFSET_AFTER] "i"(offsetof(ThreadState, interrupt_flags)),
        [TP_FIELD_OFFSET] "i"(offsetof(ThreadState, tp)),
        [GP_FIELD_OFFSET] "i"(offsetof(ThreadState, gp)),
        [S0_FIELD_OFFSET] "i"(offsetof(ThreadState, s[0])),
        [S1_FIELD_OFFSET] "i"(offsetof(ThreadState, s[1])),
        [S2_FIELD_OFFSET] "i"(offsetof(ThreadState, s[2])),
        [S3_FIELD_OFFSET] "i"(offsetof(ThreadState, s[3])),
        [S4_FIELD_OFFSET] "i"(offsetof(ThreadState, s[4])),
        [S5_FIELD_OFFSET] "i"(offsetof(ThreadState, s[5])),
        [S6_FIELD_OFFSET] "i"(offsetof(ThreadState, s[6])),
        [S7_FIELD_OFFSET] "i"(offsetof(ThreadState, s[7])),
        [S8_FIELD_OFFSET] "i"(offsetof(ThreadState, s[8])),
        [S9_FIELD_OFFSET] "i"(offsetof(ThreadState, s[9])),
        [S10_FIELD_OFFSET] "i"(offsetof(ThreadState, s[10])),
        [S11_FIELD_OFFSET] "i"(offsetof(ThreadState, s[11]))
      :
      // clobbers: not all of these are actually clobbered but we're mentioning
      // them here to prevent the compiler from using them for "HANDLER".
      "memory", "ra", "a0", "a1", "tp", "gp", "s0", "s1", "s2", "s3", "s4",
      "s5", "s6", "s7", "s8", "s9", "s10", "s11");
}

static LIBC_INLINE void yield_timeslice(AppThreading *s) {
  constexpr uint64_t SYS_SET_INTERRUPT_PENDING =
      syscall_iface_func_num(0, 0x001);
  uint64_t suspend_intr_hnd_num = s->thread_switch_hnd_num;
  syscall_impl<uint64_t>(SYS_SET_INTERRUPT_PENDING, suspend_intr_hnd_num, 1);
}

// The implementation of the "idle thread" that becomes current whenever
// there are no normal runnable threads.
__attribute__((noreturn)) void idle_thread() {
  auto s = app.multithreading_state();
  for (;;) {
    asm volatile("csrrwi x0, 0x800, 0"); // disable interrupts
    if (!s->runnable_threads.is_empty()) {
      // We'll set the "force-suspend" interrupt pending, so that once we
      // re-enable interrupts this idle thread should be suspended in favor
      // of whichever thread is at the head of the runnable list.
      yield_timeslice(s);
      asm volatile("csrrwi x0, 0x800, 1"); // enable interrupts
      // We should typically not resume executing here again until next time
      // there are no runnable threads.
      continue;
    }
    // Sleep until BIOS thinks we might need to service an interrupt.
    // We do this with interrupts disabled intentionally so that we can make
    // sure that the runnable threads list can't become non-empty again before
    // we get a chance to sleep.
    asm volatile("wfi");
    asm volatile("csrrwi x0, 0x800, 1"); // enable interrupts
    // Interrupt handlers might now make other threads runnable before we
    // check again on the next iteration. We might even yield to another
    // thread here if one had been in the time-wait list and has now reached
    // its re-awake time.
  }
}

// The implementation of the "cleanup thread" that becomes current whenever
// there are zombie threads that need their reources cleaned up.
//
// This is implemented like a thread mainly just to avoid doing this
// potentially-expensive work in the main task-switching function with a nonzero
// interrupt priority floor. In the current implementation it cannot actually
// be preempted, so no other normal thread can run until it explicitly yields,
// but interrupt handlers can still interrupt it.
__attribute__((noreturn)) void cleanup_thread() {
  auto s = app.multithreading_state();
  for (;;) {
    // Threads can only become "zombie" by exiting themselves, and since no
    // other threads can run while we're in the cleanup thread we can assume
    // that the zombie thread list is immutable whenever we have control here,
    // and so we can safely leave interrupts active.
    auto zombie = s->take_next_zombie();
    if (zombie == nullptr) {
      // If the zombie list is empty then we have nothing else to do, so
      // we'll yield and let other threads run again until another zombie
      // appears in the list.
      yield_timeslice(s);
      continue;
    }

    // All of a thread's privately-owned memory resources live together
    // in a single memory block, and so freeing that block is sufficient
    // to clean everything up. (The thread should have run all of its
    // atexit code, destructors, etc before registering itself as a zombie.)
    // We don't check the result of this call because if freeing the memory
    // block fails there isn't really anything we can do about it; it should
    // only be possible if our record of the thread's memory block has
    // been corrupted somehow.
    uint64_t memblock_hnd_num = zombie->memblock_hnd_num;
    if (memblock_hnd_num != 0) {
      syscall_impl<uint64_t>(RISCOVITE_SYS_CLOSE, memblock_hnd_num);
    }
  }
}

// Forward-declaration because switch_threads and preempt_thread_switch_handler
// are mutually-dependent.
static ThreadState *preempt_thread_switch_handler(uint64_t a0,
                                                  ThreadState *current_state);

static ThreadState *switch_threads(ThreadState *current_state,
                                   uint64_t current_time) {
  // We assume that global_state will always be non-nil because this function
  // should only be called by a thread-switching interrupt handler and those
  // should be activated only after we've switched to multithreaded mode.
  auto global_state = app.multithreading_state();
  global_state->running_thread->state = current_state;

  // The following code must be resilient to the following changes potentially
  // made by interrupt handlers with higher priority than our task-switching
  // ones:
  // - Items being removed from the time-waiting list.
  // - Items being added to the runnable threads list.
  //
  // Interrupt handlers other than our threading-related ones are prohibited
  // from taking any actions that would cause any other kinds of change,
  // however. The following code is probably more conservative than it really
  // needs to be based on the above, and so we might be able to reduce the
  // amount of time spent with interrupts disabled in future changes.

  bool have_zombies = false;
  {
    auto d = disable_interrupts();
    ThreadTracker *current = global_state->time_waiting_threads.first();
    while (current != nullptr) {
      if (current->wake_time > current_time) {
        break; // The time-wait list is ordered by wake time, so the rest must
               // be in the future
      }
      current = global_state->end_thread_wait_timeout(
          current, &global_state->time_waiting_threads);
    }
    have_zombies = global_state->have_zombies(d);
    (void)d; // interrupts re-enabled here
  }

  // This brief period with interrupts re-enabled can potentially allow
  // an interrupt to add a new thread to the runnable list before we
  // decide which thread to select next.

  ThreadTracker *successor_runnable = nullptr;
  {
    auto d = disable_interrupts();
    ThreadTracker *next = global_state->next_runnable();
    if (have_zombies) {
      // If there's at least one zombie thread awaiting cleanup
      // then the cleanup thread gets control regardless of
      // what else might be going on.
      global_state->running_thread = &global_state->cleanup;
    } else if (next != nullptr) {
      successor_runnable = global_state->runnable_threads.next(next);
      global_state->running_thread = next;
      global_state->runnable_threads.push_tail(next, d);
    } else {
      // Nothing is runnable, so we'll switch to the idle task.
      global_state->running_thread = &global_state->idle;
    }
    (void)d; // interrupts re-enabled here
  }

  // If there's either another runnable thread or a time-waiting thread
  // then we'll ask the supervisor to preempt the new thread at the
  // appropriate future time.
  uint64_t timed_wait_time = 0;
  if (!have_zombies) {
    auto d = disable_interrupts();
    auto successor_timed_wait = global_state->next_time_waiter();
    if (successor_timed_wait != nullptr) {
      timed_wait_time = successor_timed_wait->wake_time;
    }
    // Note that other interrupt handlers are not allowed to _add_ time-waiters
    // to the list, so our only potential concern is that the
    // successor_timed_wait thread might stop time-waiting before we return
    // from this function, but that just means we'll end up back in here earlier
    // than we really needed to once the timer interrupt becomes pending.
    (void)d; // interrupts re-enabled here
  }
  if (!have_zombies &&
      (successor_runnable != nullptr || timed_wait_time != 0)) {
    // 4ms is the default timeslice. TODO: Make this customizable?
    uint64_t preempt_time = current_time + 4000000;
    if (successor_runnable == nullptr && timed_wait_time > preempt_time) {
      // If there is no immediately-runnable task and the next time-waiter
      // is further than one standard timeslice in the future then we'll
      // extend the timeslice until the time-waiter's wait time.
      // TODO: Should we let a very-near-future waiter slightly truncate
      // the timeslice to let it wake up closer to the time it requested?
      preempt_time = timed_wait_time;
    }

    auto result =
        syscall_impl<uint64_t>(RISCOVITE_SYS_SET_TIMER_INTERRUPT, preempt_time,
                               (uint64_t)&thread_switching_interrupt_handler<
                                   preempt_thread_switch_handler>,
                               (uint64_t)PREEMPT_INTERRUPT_PRIORITY);
    (void)result; // Can't really do anything if the timer setup fails, but it
                  // ought not to
  } else {
    // Clear any timer we might have configured previously.
    auto result =
        syscall_impl<uint64_t>(RISCOVITE_SYS_SET_TIMER_INTERRUPT, 0, 0, 0);
    (void)result; // Can't really do anything if the timer setup fails, but it
                  // ought not to
  }

  assert(!global_state->running_thread->complete);
  return global_state->running_thread->state;
}

// The ThreadStateInterruptHandler called by the software interrupt we
// use to force thread switching.
static ThreadState *force_thread_switch_handler(uint64_t a0,
                                                ThreadState *current_state) {
  (void)a0;

  auto now_result = syscall_impl<uint64_t>(RISCOVITE_SYS_GET_CURRENT_TIMESTAMP);
  uint64_t current_time = now_result.value;
  if (now_result.error != 0) {
    // It would be very weird for us to fail to get the current timestamp,
    // but if this does happen somehow then we'll pretend we're right at the
    // system's epoch, which effectively means that time-waiting threads cannot
    // reach their wake time on this call.
    current_time = 0;
  }

  auto s = app.multithreading_state();
  constexpr uint64_t SYS_CLEAR_INTERRUPT_PENDING =
      syscall_iface_func_num(0, 0x002);
  uint64_t suspend_intr_hnd_num = s->thread_switch_hnd_num;
  syscall_impl<uint64_t>(SYS_CLEAR_INTERRUPT_PENDING, suspend_intr_hnd_num);

  // By the time we get here the code that activated this thread switch should
  // have already put the current thread into at least one of the following
  // so that it can potentially run again in future:
  // - The runnable thread list (if it's just yielding its current timeslice)
  // - The wait list for some synchronization primitive
  // - The time-waiting threads list
  //
  // Therefore we have nothing special to do here and can just run our usual
  // bookkeeping and new thread selection code.
  return switch_threads(current_state, current_time);
}

// The ThreadStateInterruptHandler called by the task's timer to preempt a
// thread that has been running longer than its timeslice.
static ThreadState *preempt_thread_switch_handler(uint64_t a0,
                                                  ThreadState *current_state) {
  (void)a0;
  auto now_result = syscall_impl<uint64_t>(RISCOVITE_SYS_GET_CURRENT_TIMESTAMP);
  uint64_t current_time = now_result.value;
  if (now_result.error != 0) {
    // It would be very weird for us to fail to get the current timestamp,
    // but if this does happen somehow then we'll pretend we're right at the
    // system's epoch, which effectively means that time-waiting threads cannot
    // reach their wake time on this call.
    current_time = 0;
  }

  // TODO: Perhaps we should decline to switch threads if the current task
  // hasn't used up its timeslice yet. However, right now we always set the
  // timer to an appropriate time for switching threads anyway, so that doesn't
  // seem necessary.
  return switch_threads(current_state, current_time);
}

// Reads the current value of the "gp" register, which we need to copy into
// each new thread we create since it's supposed to be constant for the
// whole runtime of the program.
__attribute__((always_inline)) static uint64_t get_gp_register() {
  uint64_t ret;
  asm("mv %[RET], gp" : [RET] "=r"(ret) : : "gp");
  return ret;
}

int Thread::run(ThreadStyle style, ThreadRunner runner, void *arg, void *stack,
                size_t stacksize, size_t guardsize, bool detached) {
  // The first call to Thread::run switches the app into multithreaded mode.
  int activate_err = app.ensure_multithreaded();
  if (activate_err != 0)
    return activate_err;

  bool owned_stack = (stack == nullptr);
  auto round_or_err = round_for_stack(get_tls_alloc_size(&app.tls));
  if (!round_or_err)
    return round_or_err.error();
  size_t tls_alloc_size = round_or_err.value();
  size_t overhead_size =
      THREAD_TRACKING_OVERHEAD + tls_alloc_size + sizeof(ThreadAttributes);
  size_t memblock_size = overhead_size;
  size_t memblock_guard_pages = 0;
  if (owned_stack) {
    memblock_size += stacksize;
    round_or_err = round_to_page(guardsize);
    if (!round_or_err)
      return round_or_err.error();
    guardsize = round_or_err.value();
    memblock_guard_pages = guardsize >> 12;
    if (memblock_guard_pages > 65535) {
      return EINVAL; // create_memory_block requires this packed into 16 bytes
    }
    guardsize = memblock_guard_pages << 12;
  }
  round_or_err = round_to_page(memblock_size);
  if (!round_or_err)
    return round_or_err.error();
  memblock_size = round_or_err.value();

  // We'll request a memory block that is readable and writable but not
  // executable, and that has the requested number of guard pages.
  uint64_t memblock_flags =
      0b011 | ((uint64_t)((uint16_t)memblock_guard_pages) << 32);
  auto result = syscall_impl<uint64_t>(RISCOVITE_SYS_CREATE_MEMORY_BLOCK,
                                       (uint64_t)memblock_size, memblock_flags);
  if (result.error != 0) {
    return (int)result.error;
  }
  uint64_t memblock_hnd_num = (uint64_t)result.value;
  void *memblock_start = (void *)result.value;
  void *memblock_end = (void *)(result.value + memblock_size);

  // Our allocated memory block starts with our owned stack space (if any),
  // followed by the TLS data, then ThreadAttributes, and then finally our
  // ThreadTracker object.
  ThreadTracker *tracker = ((ThreadTracker *)memblock_end) - 1;
  new (tracker) ThreadTracker();
  ThreadAttributes *attrs = ((ThreadAttributes *)tracker) - 1;
  new (attrs) ThreadAttributes();
  void *tls_base = ((char *)attrs) - tls_alloc_size;
  size_t effective_stack_size =
      owned_stack ? (char *)tls_base - (char *)memblock_start : stacksize;
  if (owned_stack) {
    stack = memblock_start;
  }
  effective_stack_size &=
      ~0xf; // round down to nearest 16 bytes for ABI-required stack alignment
  void *stack_end = (char *)stack + effective_stack_size;
  tracker->memblock_hnd_num = memblock_hnd_num;

  this->attrib = attrs;
  attrs->detach_state =
      uint32_t(detached ? DetachState::DETACHED : DetachState::JOINABLE);
  attrs->stack = stack;
  attrs->stacksize = effective_stack_size;
  attrs->guardsize = guardsize;
  attrs->tls = (uintptr_t)tls_base;
  attrs->tls_size = tls_alloc_size;
  attrs->owned_stack = owned_stack ? 1 : 0;
  attrs->style = style;
  attrs->platform_data = tracker;

  // We need to initialize the TLS area, including copying the executable's
  // TLS image into it.
  TLSDescriptor tls_desc;
  init_tls(tls_base, tls_desc);

  // Our initial stack content is a ThreadState representing the initial
  // state the thread should have when it begins running, mimicking what
  // we'd expect for a previously-running thread that has been suspended.
  //
  // We begin execution as if calling either thread_startup_posix or
  // thread_startup_stdc, passing the arg and the given "runner". Those
  // noreturn functions both arrange for the thread to be cleaned up properly
  // once the runner returns.
  auto state = (ThreadState *)((uintptr_t)stack_end - sizeof(ThreadState));
  tracker->state = state;
  state->gp = get_gp_register();
  state->tp = tls_desc.tp;
  state->sp = (uint64_t)stack_end;
  state->s[0] = state->sp; // optional frame pointer, in case something needs it
  state->a[0] = (uint64_t)arg;
  state->a[2] = (uint64_t)tracker;
  state->a[3] = (uint64_t)attrs;
  switch (style) {
  case ThreadStyle::POSIX:
    state->pc = (uint64_t)&thread_startup_posix;
    state->a[1] = (uint64_t)runner.posix_runner;
    break;
  case ThreadStyle::STDC:
    state->pc = (uint64_t)&thread_startup_stdc;
    state->a[1] = (uint64_t)runner.stdc_runner;
    break;
  }

  // Now we'll set the new thread as runnable and yield this thread's own
  // timeslice so that it can begin running.
  auto threading_state = app.multithreading_state();
  threading_state->set_thread_runnable(tracker);
  yield_timeslice(threading_state);

  return 0;
}

int Thread::join(ThreadReturnValue &retval) {
  this->wait();

  if (this->attrib->style == ThreadStyle::POSIX)
    retval.posix_retval = this->attrib->retval.posix_retval;
  else
    retval.stdc_retval = this->attrib->retval.stdc_retval;

  // When we join with a thread we assume responsibility for
  // recording it as a zombie so that its resources will be
  // cleanup up at the next task switch.
  auto mtstate = app.multithreading_state();
  auto tracker = (ThreadTracker *)this->attrib->platform_data;
  mtstate->set_thread_zombie(tracker);

  // Unfortunately we currently need to yield our timeslice
  // here because the current thread might be the only thread
  // currently runnable and so it won't get preempted and thus
  // the zombie cleanup thread might never run.
  // FIXME: Find a better solution for this, so that we don't
  // have to yield timeslice twice just to join another thread.
  yield_timeslice(mtstate);

  return 0;
}

int Thread::detach() {
  uint32_t joinable_state = uint32_t(DetachState::JOINABLE);
  if (attrib->detach_state.compare_exchange_strong(
          joinable_state, uint32_t(DetachState::DETACHED)))
    return int(DetachType::SIMPLE);

  // If the thread was already detached, then the detach method should not
  // be called at all. If the thread is exiting, then we wait for it to exit
  // and then register it as a zombie so that its owned data can be freed
  // on the next thread switch.
  this->wait();
  auto mtstate = app.multithreading_state();
  auto tracker = (ThreadTracker *)this->attrib->platform_data;
  mtstate->set_thread_zombie(tracker);
  return int(DetachType::CLEANUP);
}

void Thread::wait() {
  auto mtstate = app.multithreading_state();
  for (;;) {
    auto d = disable_interrupts();
    auto tracker = (ThreadTracker *)this->attrib->platform_data;
    if (tracker->complete) {
      (void)d; // interrupts re-enabled here
      return;
    }
    // If we get here then the thread isn't complete yet and so we'll
    // add the current thread as its waiter and yield our timeslice.
    auto current_tracker = (ThreadTracker *)self.attrib->platform_data;
    mtstate->set_thread_waiting(current_tracker, &tracker->exit_waiters);
    yield_timeslice(app.multithreading_state());
    (void)d; // interrupts re-enabled here
  }
}

bool Thread::operator==(const Thread &thread) const {
  return thread.attrib == this->attrib;
}

int Thread::set_name(const cpp::string_view &name) {
  // TODO: Implement
  (void)name;
  return ERANGE;
}

int Thread::get_name(cpp::StringStream &name) const {
  // TODO: Implement
  (void)name;
  return ERANGE;
}

void thread_exit(ThreadReturnValue retval, ThreadStyle style) {
  (void)style;
  auto attrib = self.attrib;
  auto tracker = (ThreadTracker *)attrib->platform_data;

  // The very first thing we do is to call the thread's atexit callbacks.
  // These callbacks could be the ones registered by the language runtimes,
  // for example, the destructors of thread local objects. They can also
  // be destructors of the TSS objects set using API like pthread_setspecific.
  // NOTE: We cannot call the atexit callbacks as part of the
  // cleanup thread because destructors of thread local and TSS objects must
  // be called by the thread which owns them.
  internal::call_atexit_callbacks(attrib);

  auto mtstate = app.multithreading_state();
  uint32_t joinable_state = uint32_t(DetachState::JOINABLE);
  if (!attrib->detach_state.compare_exchange_strong(
          joinable_state, uint32_t(DetachState::EXITING))) {
    // Thread is detached, so it becomes a zombie immediately, and
    // we'll yield our timeslice to let the cleanup thread free
    // the thread's privately-owned resources.
    for (;;) {
      // In practice we should execute this loop only once because
      // if we yield while zombie then we can't get scheduled again.
      // The loop is just a safety measure to keep damaged contained
      // if something goes wrong elsewhere.
      mtstate->set_thread_zombie(tracker);
      tracker->complete = true; // must not be made runnable again
      yield_timeslice(mtstate);
    }
  }

  // If we get here then our thread is joinable and so we just need to
  // remove it from all ready/wait lists and yield our timeslice, and
  // then our state will hang around until another thread joins with this
  // one, at which point _that_ thread will be responsible for marking
  // this one as a zombie so it can be cleaned up.
  {
    auto d = disable_interrupts();
    attrib->retval = retval;
    tracker->waiters.detach(d);
    tracker->timed_waiters.detach(d);
    tracker->complete = true; // must not be made runnable again
    // Any threads that were waiting on our completion now become runnable.
    mtstate->end_thread_wait_list_success(&tracker->exit_waiters);
    // The following won't actually yield until we reenable interrupts.
    yield_timeslice(mtstate);
    (void)d; // interrupts re-enabled here
  }
  // We removed ourselves from all waiting lists before yielding, so there
  // should be no way for the thread to become runnable again, but we'll
  // guard here just in case.
  for (;;) {
    yield_timeslice(mtstate);
  }
}

int AppProperties::ensure_multithreaded() {
  if (this->is_multithreaded()) {
    // Nothing to do then: we already set up multithreading in a
    // previous call to this function.
    return 0;
  }
  // If we get here then we can assume that the multithreading system
  // isn't running and so we cannot be preempted. We could still potentially
  // be interrupted by an interrupt handler, but thread creation is not
  // defined to be interrupt-safe and so an interrupt handler attempting to
  // create a thread would be undefined behavior.

  AppThreading *state;
  {
    AllocChecker ac;
    state = new (ac) AppThreading();
    if (!ac)
      return ENOMEM;
  }

  // The main thread's ThreadTracker lives inside our AppThreading object
  // just because all of the other per-thread data for it is already elsewhere
  // by the time we get here, and so the layout we'd use for non-main threads
  // doesn't make sense for this one.
  auto main_thread = &state->main;
  main_thread->memblock_hnd_num = 0;

  // The main thread is initially the running thread, and also the only
  // member of the runnable threads list. (It's likely that whoever called
  // this function is about to add another thread to the list, though.)
  state->running_thread = main_thread;
  state->set_thread_runnable(main_thread);
  // Since the main thread is currently running, it doesn't yet have a
  // ThreadState object. That'll get created the first time we thread-switch
  // away from the main thread, by the interrupt handler pushing it onto the
  // main thread's stack.

  // We also need a software interrupt to use to force entry into the
  // thread-switching code when a thread becomes non-runnable and so needs
  // to be suspended immediately.
  auto intr_hnd_num_result =
      syscall_impl<uint64_t>(RISCOVITE_SYS_CREATE_TASK_SOFTWARE_INTERRUPT,
                             (uint64_t)&thread_switching_interrupt_handler<
                                 force_thread_switch_handler>,
                             (uint64_t)PREEMPT_INTERRUPT_PRIORITY);
  if (intr_hnd_num_result.error != 0) {
    // We need to free the state objects we already allocated to avoid leaking
    // it.
    delete state;
    return (int)intr_hnd_num_result.error;
  }
  state->thread_switch_hnd_num = intr_hnd_num_result.value;

  // The idle thread initially has on its stack a ThreadState that will
  // cause it to enter into idle_thread if it is activated.
  auto idle_stack_sp = &state->idle_stack[sizeof(state->idle_stack) & ~15];
  state->idle.state = (ThreadState *)(idle_stack_sp - sizeof(ThreadState));
  auto idle_state = state->idle.state;
  idle_state->pc = (uint64_t)&idle_thread;
  idle_state->sp = (uint64_t)idle_stack_sp;
  idle_state->gp = get_gp_register();

  // The thread cleanup thread similarly starts with a ThreadState that will
  // cause it to enter into cleanup_thread once it's activated.
  auto cleanup_stack_sp =
      &state->cleanup_stack[sizeof(state->cleanup_stack) & ~15];
  state->cleanup.state =
      (ThreadState *)(cleanup_stack_sp - sizeof(ThreadState));
  auto cleanup_state = state->cleanup.state;
  cleanup_state->pc = (uint64_t)&cleanup_thread;
  cleanup_state->sp = (uint64_t)cleanup_stack_sp;
  cleanup_state->gp = get_gp_register();

  // We'll need to make the main thread's attributes now look a little more
  // "realistic" so that our threading system can treat it the same as any
  // other thread.
  self.attrib->platform_data = main_thread;
  self.attrib->owned_stack = false;

  // If we got this far then we've successfully set up the multithreading
  // state, so we can store the pointer to record that multithreading is
  // now active. Once we've stored a non-null pointer here we must never
  // change it again.
  this->threading.store(state);
  return 0;
}

static void detach_thread_list_item(ThreadListMember *m,
                                    const InterruptsDisabled &d) {
  if (m->next == nullptr) {
    // Not currently in a list, so nothing to do.
    return;
  }
  m->next->prev = m->prev;
  m->prev->next = m->next;
  m->next = nullptr;
  m->prev = nullptr;
  (void)d;
}

// The following three functions sometimes generate false-positive
// -Wunneeded-internal-declaration, possibly due to inlining?
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunneeded-internal-declaration"

static void insert_thread_list_item_before(ThreadListMember *to_insert,
                                           ThreadListMember *before,
                                           const InterruptsDisabled &d) {
  to_insert->next = before;
  to_insert->prev = before->prev;
  to_insert->next->prev = to_insert;
  to_insert->prev->next = to_insert;
  (void)d;
}

static void insert_thread_list_item_after(ThreadListMember *to_insert,
                                          ThreadListMember *after,
                                          const InterruptsDisabled &d) {
  to_insert->prev = after;
  to_insert->next = after->next;
  to_insert->next->prev = to_insert;
  to_insert->prev->next = to_insert;
  (void)d;
}

static void insert_thread_list_item_time_wait(ThreadTracker *to_insert,
                                              ThreadTimedWaitList *list,
                                              const InterruptsDisabled &d) {
  auto wake_time = to_insert->wake_time;
  auto to_insert_m = list->member_for_tracker(to_insert);
  // We need either to find the first thread in the list that has a later wake
  // time than the one we're inserting and insert just before it, or place
  // our item at the tail of the list.
  auto current = list->first();
  for (; current != nullptr; current = list->next(current)) {
    if (current->wake_time > wake_time) {
      // We'll insert before this one, then.
      auto insert_before = list->member_for_tracker(current);
      insert_thread_list_item_before(to_insert_m, insert_before, d);
      return;
    }
  }
  // If we fall out here then we didn't find any thread with a later
  // wake_time than ours, so we'll just place our item at the end of
  // the list, or (in other words) "before" the list head.
  insert_thread_list_item_before(to_insert_m, &list->head, d);
}

#pragma GCC diagnostic pop // no longer ignoring
                           // "-Wunneeded-internal-declaration"

template <size_t OFFSET>
void ThreadList<OFFSET>::push_head(ThreadTracker *t,
                                   const InterruptsDisabled &d) {
  auto m = ThreadList<OFFSET>::member_for_tracker(t);
  detach_thread_list_item(m, d);
  insert_thread_list_item_after(m, &this->head, d);
  (void)d;
}

template <size_t OFFSET>
void ThreadList<OFFSET>::push_tail(ThreadTracker *t,
                                   const InterruptsDisabled &d) {
  auto m = ThreadList<OFFSET>::member_for_tracker(t);
  detach_thread_list_item(m, d);
  insert_thread_list_item_before(m, &this->head, d);
  (void)d;
}

template <size_t OFFSET>
bool ThreadList<OFFSET>::push_sole_member(ThreadTracker *t,
                                          const InterruptsDisabled &d) {
  bool can_push = this->is_empty();
  if (can_push) {
    auto m = ThreadList<OFFSET>::member_for_tracker(t);
    detach_thread_list_item(m, d);
    insert_thread_list_item_after(m, &this->head, d);
  }
  (void)d;
  return can_push;
}

template <size_t OFFSET>
void ThreadList<OFFSET>::remove(ThreadTracker *t, const InterruptsDisabled &d) {
  auto m = ThreadList<OFFSET>::member_for_tracker(t);
  detach_thread_list_item(m, d);
  (void)d;
}

void ThreadListMember::detach(const InterruptsDisabled &d) {
  detach_thread_list_item(this, d);
  (void)d;
}

void AppThreading::set_thread_waiting(ThreadTracker *t,
                                      ThreadWaitList *wait_list) {
  auto d = disable_interrupts();
  wait_list->push_tail(t, d);
  t->timed_waiters.detach(d); // no deadline
  (void)d;
}

void AppThreading::set_thread_waiting_deadline(ThreadTracker *t,
                                               ThreadWaitList *wait_list,
                                               uint64_t deadline) {
  auto d = disable_interrupts();
  t->wake_time = deadline;
  wait_list->push_tail(t, d);
  insert_thread_list_item_time_wait(t, &this->time_waiting_threads, d);
  (void)d;
}

void AppThreading::set_thread_sleeping(ThreadTracker *t, uint64_t deadline) {
  auto d = disable_interrupts();
  t->wake_time = deadline;
  // not runnable or waiting for anything except the deadline
  t->waiters.detach(d);
  insert_thread_list_item_time_wait(t, &this->time_waiting_threads, d);
  (void)d;
}

void AppThreading::set_thread_runnable(ThreadTracker *t) {
  auto d = disable_interrupts();
  t->timed_waiters.detach(d);
  this->runnable_threads.push_tail(t, d);
  (void)d;
}

void AppThreading::set_thread_zombie(ThreadTracker *t) {
  auto d = disable_interrupts();
  t->timed_waiters.detach(d);
  this->zombie_threads.push_tail(t, d);
  (void)d;
}

ThreadTracker *
AppThreading::end_thread_wait_success(ThreadTracker *t,
                                      ThreadWaitList *from_list) {
  auto d = disable_interrupts();
  auto ret = from_list->next(t);
  t->timed_waiters.detach(d);
  this->runnable_threads.push_tail(t, d);
  t->state->a[0] = 1; // return value from the explicit yield function
  (void)d;
  return ret;
}

void AppThreading::end_thread_wait_list_success(ThreadWaitList *from_list) {
  auto d = disable_interrupts();
  auto current = from_list->first();
  while (current != nullptr) {
    // We must decide the next thread before we make any other changes because
    // making "current" runnable will remove it from the list implicitly.
    auto next = from_list->next(current);
    current->timed_waiters.detach(d);
    this->runnable_threads.push_tail(current, d);
    current->state->a[0] = 1; // return value from the explicit yield function
    current = next;
  }
  (void)d;
}

ThreadTracker *
AppThreading::end_thread_wait_timeout(ThreadTracker *t,
                                      ThreadTimedWaitList *from_list) {
  auto d = disable_interrupts();
  auto ret = from_list->next(t);
  t->timed_waiters.detach(d);
  this->runnable_threads.push_tail(t, d);
  t->state->a[0] = 0; // return value from the explicit yield function
  (void)d;
  return ret;
}

} // namespace LIBC_NAMESPACE_DECL
