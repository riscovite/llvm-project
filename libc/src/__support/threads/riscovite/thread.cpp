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
                          ThreadTracker *tracker) {
  (void)arg;
  (void)runner;
  (void)tracker;
  // TODO: Implement
}

void thread_startup_stdc(void *arg, ThreadRunnerStdc runner,
                         ThreadTracker *tracker) {
  (void)arg;
  (void)runner;
  (void)tracker;
  // TODO: Implement
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

// The implementation of the "idle thread" that becomes current whenever
// there are no normal runnable threads.
__attribute__((noreturn)) void idle_thread() {
  // TODO: Implement
  // Pseudocode something like:
  // infinite loop {
  //    mask all interrupts
  //    if there is at least one runnable thread {
  //      set the force-thread-switch software interrupt pending
  //      unmask interrupts (probably causing this thread to suspend)
  //      return to start of loop (once there are no runnable threads again)
  //    }
  //    wfi
  //    unmask interrupts (potentially allowing interrupt handlers to make other
  //    threads runnable)
  // }
  for (;;) {
  }
}

static ThreadState *switch_threads(ThreadState *current_state) {
  // We assume that global_state will always be non-nil because this function
  // should only be called by a thread-switching interrupt handler and those
  // should be activated only after we've switched to multithreaded mode.
  auto global_state = app.multithreading_state();
  global_state->running_thread->state = current_state;

  // Before we try to pull from the runnable list, we'll check whether
  // any of our time-waiters have reached their desired time.
  auto now_result = syscall_impl<uint64_t>(RISCOVITE_SYS_GET_CURRENT_TIMESTAMP);
  // Getting the current timestamp should never fail, but if it does
  // then we'll just let the time-waiters stall in the time-wait list.
  if (now_result.error == 0) {
    uint64_t now = now_result.value;
    ThreadTracker *current = global_state->time_waiting_threads.head;
    while (current != NULL) {
      if (current->wake_time > now) {
        break; // The time-wait list is ordered by wake time, so the rest must
               // be in the future
      }
      // TODO: Move this task to the end of the runnable thread list, since
      // its wait time has elapsed. This will also remove it from the waiter
      // list for any synchronization primitive it's currently waiting on,
      // since it has reached its waiting deadline.
      // TODO: Also set the thread's a0 register to a value representing
      // "timeout" so that when it resumes the lowest-level yield function
      // can distinguish that from successfully acquiring whatever the
      // thread was waiting for, if anything.
      current = current->next_timed;
    }
  }

  ThreadTracker *next = global_state->runnable_threads.head;
  if (next != nullptr) {
    global_state->running_thread = next;
  } else {
    // Nothing is runnable, so we'll switch to the idle task.
    global_state->running_thread = &global_state->idle;
  }
  return global_state->running_thread->state;
}

// The ThreadStateInterruptHandler called by the software interrupt we
// use to force thread switching.
static ThreadState *force_thread_switch_handler(uint64_t a0,
                                         ThreadState *current_state) {
  (void)a0;
  // By the time we get here the code that activated this thread switch should
  // have already put the current thread into at least one of the following
  // so that it can potentially run again in future:
  // - The runnable thread list (if it's just yielding its current timeslice)
  // - The wait list for some synchronization primitive
  // - The time-waiting threads list
  //
  // Therefore we have nothing special to do here and can just run our usual
  // bookkeeping and new thread selection code.
  return switch_threads(current_state);
}

int Thread::run(ThreadStyle style, ThreadRunner runner, void *arg, void *stack,
                size_t stacksize, size_t guardsize, bool detached) {
  // The first call to Thread::run switches the app into multithreaded mode.
  int activate_err = app.ensure_multithreaded();
  if (activate_err != 0)
    return activate_err;

  (void)style;
  (void)runner;
  (void)arg;
  (void)stack;
  (void)stacksize;
  (void)guardsize;
  (void)detached;

  // In addition to the stack space requested by the application, we also
  // include some extra space for the ThreadTracker object for the thread and
  // for the ThreadState object that we'll leave at the top of its stack
  // whenever the thread is suspended.
  auto round_or_err = round_for_stack(get_tls_alloc_size(&app.tls));
  if (!round_or_err)
    return round_or_err.error();
  size_t tls_alloc_size = round_or_err.value();
  size_t overhead_size = THREAD_TRACKING_OVERHEAD + tls_alloc_size;

  round_or_err = round_to_page(stacksize + overhead_size);
  if (!round_or_err)
    return round_or_err.error();
  stacksize = round_or_err.value();
  round_or_err = round_to_page(guardsize);
  if (!round_or_err)
    return round_or_err.error();
  guardsize = round_or_err.value();

  size_t guard_pages = guardsize >> 12;
  if (guard_pages > 65535) {
    return EINVAL; // create_memory_block requires this packed into 16 bytes
  }

  // We'll request a memory block that is readable and writable but not
  // executable, and that has the requested number of guard pages.
  uint64_t memblock_flags = 0b011 | ((uint64_t)((uint16_t)guard_pages) << 32);

  auto result = syscall_impl<uint64_t>(RISCOVITE_SYS_CREATE_MEMORY_BLOCK,
                                       (uint64_t)stacksize, memblock_flags);
  if (result.error != 0) {
    return (int)result.error;
  }
  uint64_t memblock_hnd_num = (uint64_t)result.value;
  // void *memblock_start = (void *)result.value;
  void *memblock_end = (void *)(result.value + stacksize);

  // At the _end_ of our memory block we'll place the ThreadTracker object
  // for this thread, with the TLS area just below it, and then the rest of
  // the block will be the downward-growing stack, which will include the
  // ThreadState object when the thread is suspended later.
  ThreadTracker *tracker = ((ThreadTracker *)memblock_end) - 1;
  tracker->memblock_hnd_num = memblock_hnd_num;
  void *tls_base = ((char *)tracker) - tls_alloc_size;
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
  auto state = (ThreadState *)(((char *)tls_base) - sizeof(ThreadState));
  state->a[0] = (uint64_t)arg;
  state->a[2] = (uint64_t)tracker;
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

  return ENOTSUP;
}

int Thread::join(ThreadReturnValue &retval) {
  // TODO: Implement
  (void)retval;
  return ENOENT;
}

int Thread::detach() {
  // TODO: Implement
  return int(DetachType::SIMPLE);
}

void Thread::wait() {
  // TODO: Implement
}

bool Thread::operator==(const Thread &thread) const {
  // TODO: Implement
  (void)thread;
  return false;
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

// Reads the current value of the "gp" register, which we need to copy into
// each new thread we create since it's supposed to be constant for the
// whole runtime of the program.
__attribute__((always_inline)) static uint64_t get_gp_register() {
  uint64_t ret;
  asm("mv %[RET], gp" : [RET] "=r"(ret) : : "gp");
  return ret;
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

  // The main thread now gets a ThreadTracker allocated to represent it in
  // the multithreading system. This will be the first ThreadTracker and
  // initially the only one, though it's likely that whoever called this
  // function is about to allocate another.
  {
    AllocChecker ac;
    state->running_thread = new (ac) ThreadTracker();
    if (!ac) {
      delete state; // don't leak the AppThreading we already allocated
      return ENOMEM;
    }
  }
  // The main thread doesn't have a memory block because its stack
  // is the main stack provided by the RISCovite supervisor, and
  // its ThreadTracker object lives on the main heap.
  state->running_thread->memblock_hnd_num = 0;

  // We also need a software interrupt to use to force entry into the
  // thread-switching code when a thread becomes non-runnable and so needs
  // to be suspended immediately.
  auto intr_hnd_num_result =
      syscall_impl<uint64_t>(RISCOVITE_SYS_CREATE_TASK_SOFTWARE_INTERRUPT,
                             (uint64_t)&thread_switching_interrupt_handler<
                                 force_thread_switch_handler>);
  if (intr_hnd_num_result.error != 0) {
    // We need to free the two memory objects we already allocated.
    delete state->running_thread;
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

  // If we got this far then we've successfully set up the multithreading
  // state, so we can store the pointer to record that multithreading is
  // now active. Once we've stored a non-null pointer here we must never
  // change it again.
  this->threading.store(state);
  return 0;
}

} // namespace LIBC_NAMESPACE_DECL
