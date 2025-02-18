//===- Classes to capture properties of RISCovite applications --*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIBC_CONFIG_RISCOVITE_APP_H
#define LLVM_LIBC_CONFIG_RISCOVITE_APP_H

#include "src/__support/CPP/atomic.h"
#include "src/__support/macros/config.h"
#include "src/__support/macros/properties/architectures.h"

#include <stddef.h>
#include <stdint.h>

namespace LIBC_NAMESPACE_DECL {

// Instances of this act as RAII guards for interrupts being disabled.
//
// Functions that must only be called with interrupts already disabled should
// take an argument of this type to help callers prove that they have indeed
// disabled interrupts.
class InterruptsDisabled {
private:
  // Memory of the interrupt enable state when this object was created,
  // to allow restoring that same state when this object is destroyed.
  uint8_t restore;

  // Moving and copying are not allowed. Pass InterruptDisabled by reference
  // to functions that require their caller to have disabled interrupts.
  InterruptsDisabled(const InterruptsDisabled &) = delete;
  InterruptsDisabled(InterruptsDisabled &&) = delete;
  InterruptsDisabled &operator=(const InterruptsDisabled &) = delete;
  InterruptsDisabled &operator=(InterruptsDisabled &&) = delete;

public:
  LIBC_INLINE InterruptsDisabled() {
    asm volatile("csrrwi %[RET], 0x800, 0" : [RET] "=r"(this->restore));
  }

  LIBC_INLINE ~InterruptsDisabled() {
    asm volatile("csrw 0x800, %[V]" : : [V] "r"(this->restore));
  }

  // Returns true if interrupts were enabled before instantiating this object,
  // and thus interrupts will be re-enabled again once this object is
  // destroyed.
  LIBC_INLINE bool previously_enabled() const { return this->restore != 0; }
};

// Disables interrupts and returns an RAII guard that will restore the
// interrupt enable flag to its previous value once destroyed.
LIBC_INLINE InterruptsDisabled disable_interrupts() {
  return InterruptsDisabled();
}

// The lowest-level building block for putting a thread to sleep while it
// waits for some event to occur.
//
// Once the thread becomes runnable again, returns true if the event-wait
// succeeded, or false if the timeout deadline was reached before the event
// occurred.
//
// Before calling this function the thread must have removed itself from the
// runnable list and added itself to at least one of a normal event wait list
// or the system's time-wait list, such that it will eventually be made
// runnable again with the a0 return value register changed to 1 to signal
// "event occurred", or "0" to signal "timeout reached".
bool wait_current_thread_raw();

// The saved state for a currently-suspended thread.
//
// Most of this structure is actually just the exception stack frame generated
// automatically by the RISCovite supervisor on entry into the task switching
// interrupt handler, but we extend it with some additional fields on the
// front to capture the remaining general-purpose register values that are
// not included in the exception stack frame due to being callee-saved.
struct __attribute__((aligned(16))) ThreadState {
  // The value of the thread pointer register (x4) for this thread.
  //
  // This points to the byte immediately after the thread control block,
  // and so at the beginning of the thread local storage blocks.
  uint64_t tp;

  // The value of the global pointer register (x3) for this thread.
  uint64_t gp;

  // The twelve callee-saved registers that are not normally captured
  // automatically in an exception stack frame, but which we add to the
  // stack just before suspending a thread.
  uint64_t s[12];

  // The flags bitfield from the RISCovite interrupt stack frame of the
  // interrupt handler responsible for task switching.
  uint32_t interrupt_flags;

  // The approximate number of nanoseconds of runtime this thread had
  // accumulated at the time of the task switch, since the last reset.
  // This is tracked automatically by the RISCovite supervisor and written
  // by the supervisor into the interrupt stack frame.
  //
  // This saturates at just over four seconds, but that's okay because
  // we only test whether it's greater than a much smaller threshold
  // value representing the thread's timeslice.
  uint32_t runtime_nanos;

  // The program counter value when the thread was suspended.
  uint64_t pc;

  // The value of the return address register (x1) when the thread was
  // suspended.
  uint64_t ra;

  // The value that the stack pointer register (x2) should be restored to when
  // this thread is resumed, which is also the stack pointer value at the time
  // the thread was suspended.
  uint64_t sp;

  // The values of the eight argument/result registers (x10-x17) at the time
  // the thread was suspended.
  uint64_t a[8];

  // The values of the seven temporary (caller-saved) registers (x5-x7, x28-x31)
  // at the time the thread was suspended.
  uint64_t t[7];

  // The raw values of the 32 floating point registers at the time the thread
  // was suspended, but only if the relevant flag is set in `interrupt_flags`.
  // For a thread that has never accessed the floating point registers the
  // flag is not set and the values of these are unspecified.
  uint64_t f[32];

  // The value of the floating-point CSR at the time the thread was suspended,
  // but only in the cases where the values in `f` are valid. Otherwise the
  // value is unspecified.
  uint64_t fcsr;

  // Returns the value that the stack pointer register (x2) should be set to
  // before performing an interrupt return in order to resume this thread
  // using the embedded interrupt stack frame.
  inline uint64_t stack_pointer_to_resume() const {
    return reinterpret_cast<uint64_t>(this) +
           offsetof(ThreadState, interrupt_flags);
  }
};

struct ThreadListMember {
  ThreadListMember *prev;
  ThreadListMember *next;

  LIBC_INLINE constexpr ThreadListMember() : prev(nullptr), next(nullptr) {}

  LIBC_INLINE constexpr ThreadListMember(ThreadListMember &self)
      : prev(&self), next(&self) {}

  void detach(const InterruptsDisabled &d);
};

// Forward-declaration because ThreadTracker and ThreadList refer to each other.
struct __attribute__((aligned(16))) ThreadTracker;

enum ThreadListDelayInit { THREAD_LIST_DELAY_INIT };

// The head of a double-linked list of [ThreadTracker] objects.
//
// The tail of the list is in head->prev. head is always equal to
// head->prev->next when the list is in a consistent state. Interrupts must
// always be disabled when accessing the list so that it can only be viewed
// in an inconsistent state by code that's currently modifying it.
template <size_t LIST_MEMBER_OFFSET> struct ThreadList {
  // The head.next field is the first item, while head.prev is the last item.
  //
  // The first and last items point to this list head to mark the start/end
  // of the list.
  ThreadListMember head;

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wuninitialized"
  LIBC_INLINE constexpr ThreadList() : head(this->head) {}
#pragma GCC diagnostic pop

  LIBC_INLINE constexpr ThreadList([[maybe_unused]] ThreadListDelayInit delay) : head() {}

  LIBC_INLINE void init() {
    this->head.next = &this->head;
    this->head.prev = &this->head;
  }

  LIBC_INLINE void ensure_init(InterruptsDisabled &d) {
    if (this->head.next == nullptr) {
      this->init();
    }
    (void)d;
  }

  // This must be called only with a ThreadListMember that actually represents
  // a list item, and NEVER with the special member in "head".
  static LIBC_INLINE ThreadTracker *tracker_for_member(ThreadListMember *m) {
    return (ThreadTracker *)(((char *)m) - LIST_MEMBER_OFFSET);
  }

  static LIBC_INLINE ThreadListMember *member_for_tracker(ThreadTracker *t) {
    return (ThreadListMember *)(((char *)t) + LIST_MEMBER_OFFSET);
  }

  LIBC_INLINE bool is_empty() { return this->head.next == &this->head; }
  LIBC_INLINE ThreadTracker *first() {
    if (this->is_empty()) {
      return nullptr;
    }
    return ThreadList<LIST_MEMBER_OFFSET>::tracker_for_member(this->head.next);
  }
  // It's valid to call next only with a ThreadTracker that currently belongs
  // to this list. Passing any other ThreadTracker has undefined behavior.
  LIBC_INLINE ThreadTracker *next(ThreadTracker *current) {
    auto m = ThreadList<LIST_MEMBER_OFFSET>::member_for_tracker(current);
    if (m->next == &this->head) {
      return nullptr; // current is the last item, so there is no next
    }
    return ThreadList<LIST_MEMBER_OFFSET>::tracker_for_member(m->next);
  }
  void push_head(ThreadTracker *t, const InterruptsDisabled &d);
  void push_tail(ThreadTracker *t, const InterruptsDisabled &d);
  bool push_sole_member(ThreadTracker *t, const InterruptsDisabled &d);
  void insert_with_wake_time(ThreadTracker *t, uint64_t wake_time,
                             const InterruptsDisabled &d);
  void remove(ThreadTracker *t, const InterruptsDisabled &d);
};

// Tracking information for a thread, allocated at the time of thread creation.
//
// RISCovite has no OS-level support for threads and instead we implement
// preemptive threading in userspace as part of libc, using the timer interrupt
// facility provided by the supervisor. This type is therefore effectively the
// information about a thread that would live inside the kernel on other OSes.
struct __attribute__((aligned(16))) ThreadTracker {
  // When a thread is participating in a wait queue (which includes the queue
  // of runnable-but-not-yet-running threads), this field forms an invasive
  // doubly-linked list where each thread points to the threads that are before
  // and after it in the queue.
  //
  // When a thread is running this field has an unspecified value and it's
  // unsound to defreference it.
  //
  // This must be the first field in ThreadTracker due to the hard-coded
  // offset parameter on the type of field "joiners", below.
  ThreadListMember waiters;

  // When a thread has asked to be suspended until some future time, this
  // field forms an invasive doubly-linked list over all such threads in
  // order of wake-up time, soonest first.
  //
  // This is used both when a thread has requested to sleep and when it is
  // in a wait queue for a synchronization primitive with an associated
  // timeout. In the latter case the next_waiter field is also active and
  // represents the position in the associated wait queue, so that the
  // task can be unsuspended either by acquiring a lock (for example) or
  // by time running out.
  ThreadListMember timed_waiters;

  // When `next_timed` is participating in the timed wakeup queue, this
  // field records the time that the thread requested to be woken up.
  //
  // This field is meaningless when not in the timed wakeup queue.
  uint64_t wake_time;

  // When the thread is suspended, points to its thread state structure,
  // which is always left at the top of the thread's stack when suspending it.
  //
  // Only the task switching code is allowed to access this field, and because
  // the task switcher cannot preempt itself it has exclusive access without
  // any locking.
  //
  // The value of this field when a thread is running is unspecified and it
  // is unsound to dereference any offset of this pointer in that case.
  ThreadState *state;

  // List of threads that are waiting for this thread to terminate.
  //
  // This is a ThreadWaitList, but instantiated directly using a hard-coded
  // offset because we can't compute the offset of "timed_waiters" using
  // offsetof until we've completed this decl.
  //
  // All threads in this list will become runnable with their a0 set
  // to 1 once the thread is complete. Adding a thread here after complete
  // is already set to true is incorrect usage.
  ThreadList<0> exit_waiters;

  // Set to true once the thread exits, and thus once its result value
  // (in the associated ThreadAttributes object) is ready to read.
  bool complete;

  // The handle number for the memory block containing this thread's stack,
  // thread local storage descriptor, and this very tracking object.
  //
  // For the main thread this is zero because in that case the relevant
  // data lives inside the default stack area provided by the RISCovite
  // supervisor, and so there is no separately-allocated block.
  uint64_t memblock_hnd_num;
};

using ThreadWaitList = ThreadList<offsetof(ThreadTracker, waiters)>;
using ThreadTimedWaitList = ThreadList<offsetof(ThreadTracker, timed_waiters)>;

// The number of additional bytes needed in the memory block containing a
// thread's stack for both the tracking object and the state information
// retained when a thread is suspended (which is pushed to the top of the
// thread's stack when thread-switching.)
constexpr size_t THREAD_TRACKING_OVERHEAD =
    sizeof(ThreadTracker) + sizeof(ThreadState);

// The interrupt priority that we'll use for the timer interrupt that can
// cause task preemption. Any interrupt with a higher priority than this
// can potentially interrupt the preemption handler, while equal or lower
// priorities cannot be activated while preemption is in progress.
//
// It might make sense to make this configurable in future but we just have
// it hard-coded right now for simplicity's sake.
constexpr uint8_t PREEMPT_INTERRUPT_PRIORITY = 15;

// Data structure to capture properties of the RISCovite/ELF TLS image.
struct TLSImage {
  // The load address of the TLS image.
  uintptr_t address;

  // The byte size of the TLS image consisting of both initialized and
  // uninitialized memory. In ELF executables, it is size of .tdata + size of
  // .tbss. Put in another way, it is the memsz field of the PT_TLS header.
  uintptr_t size;

  // The byte size of initialized memory in the TLS image. In ELF exectubles,
  // this is the size of .tdata. Put in another way, it is the filesz of the
  // PT_TLS header.
  uintptr_t init_size;

  // The alignment of the TLS layout. It assumed that the alignment
  // value is a power of 2.
  uintptr_t align;
};

using AuxEntryType = unsigned long;
// Using the naming convention from `proc(5)`.
// TODO: Would be nice to use the aux entry structure from elf.h when available.
struct AuxEntry {
  AuxEntryType id;
  AuxEntryType value;
};

struct Args {
  uintptr_t argc;

  // A flexible length array would be more suitable here, but C++ doesn't have
  // flexible arrays: P1039 proposes to fix this. So, for now we just fake it.
  // Even if argc is zero, "argv[argc] shall be a null pointer"
  // (ISO C 5.1.2.2.1) so one is fine. Also, length of 1 is not really wrong as
  // |argc| is guaranteed to be atleast 1, and there is an 8-byte null entry at
  // the end of the argv array.
  uintptr_t argv[1];
};

// Global state related to our userspace implementation of multithreading.
//
// An object of this type is allocated on the first call to pthread_create,
// and then that allocation lives for the rest of the application runtime
// in the "threading" field of the global "app" object and used for managing
// the multithreading system.
//
// An application that never creates another thread beyond its main thread
// never allocates an object of this type and thus also retains the use of
// its task's timer interrupt. However, once an application's AppThreading
// has been created libc assumes that it henceforth owns the timer interrupt
// and expects the application to exclusively use libc facilities for
// such timekeeping.
struct AppThreading {
  // The thread that is currently running.
  ThreadTracker *running_thread;

  // A doubly-linked list of threads that are ready to run, or nullptr if no
  // threads are ready to run.
  //
  // Any thread that's in this list must have its [ThreadState] object
  // ready to be used to resume the thread. If the thread was preempted due
  // to exhausting its timeslice then most of the thread state will just be
  // preserved unchanged, but if the thread was previously in a wait queue
  // then the a0/a1 registers must be populated with suitable results for
  // the low-level wait function to return.
  //
  // On entry into main there is only the one main thread which is tracked
  // in running_thread, and there are therefore no runnable threads.
  ThreadWaitList runnable_threads;

  // A doubly-linked list of threads that want to become runnable again at
  // a particular time in the future, in order of requested wakeup time with
  // the soonest first.
  //
  // A thread in this list may or may not also be in a wait queue for a
  // synchronization primitive. If it is then it could be awakened by either
  // the wait queue this time-wait list, depending on which event occurs first.
  ThreadTimedWaitList time_waiting_threads;

  // A doubly-linked list of threads that have finished executing but whose
  // resources have not yet been freed.
  //
  // If this thread is not empty during a thread switch then control transfers
  // to the thread cleanup thread, which then empties this list by disposing
  // of each thread's resources before yielding control back to the main
  // scheduler.
  ThreadWaitList zombie_threads;

  // The handle number of a software interrupt object used to force immediate
  // entry into the thread-switching code. The thread-switching code can also
  // separately be entered from the timer interrupt when more than one task is
  // either already runnable or would become runnable after a future instant.
  //
  // The software interrupt this refers to must have the same interrupt priority
  // as the timer interrupt, to ensure that one cannot preempt the other.
  uint64_t thread_switch_hnd_num;

  // ThreadTracker for the main thread, whose stack and thread control block
  // are both already active when we switch into threaded mode, and so we
  // only need the additional ThreadTracker to represent it.
  ThreadTracker main;

  // ThreadTracker for the idle thread, which runs when none of the real threads
  // are runnable. This thread's ThreadState pointer always points into
  // the idle_stack field, once initialized.
  ThreadTracker idle;

  // ThreadTracker for the thread cleanup thread, which takes control whenever
  // there's at least one thread in zombie_threads during a task switch.
  ThreadTracker cleanup;

  // A small buffer used as stack space for the idle thread.
  // This contains its ThreadState when it's suspended, along with any other
  // small content that might be placed in the stack frame of the idle thread's
  // function by the compiler.
  alignas(16) char idle_stack[64 + sizeof(ThreadState)];

  // A small buffer used as stack space for the cleanup thread.
  // This contains its ThreadState when it's suspended, along with any other
  // small content that might be placed in the stack frame of the cleanup
  // thread's function by the compiler.
  alignas(16) char cleanup_stack[64 + sizeof(ThreadState)];

  LIBC_INLINE ThreadTracker *next_runnable() {
    return this->runnable_threads.first();
  }
  LIBC_INLINE ThreadTracker *next_time_waiter() {
    return this->time_waiting_threads.first();
  }
  LIBC_INLINE bool have_zombies(InterruptsDisabled &d) {
    (void)d;
    return this->zombie_threads.first() != nullptr;
  }
  // This function must be called only from the thread cleanup thread, since
  // it assumes that it has exclusive control over the zombie list.
  LIBC_INLINE ThreadTracker *take_next_zombie() {
    auto ret = this->zombie_threads.first();
    if (ret == nullptr) {
      return nullptr;
    }
    // We're going to immediately remove this item from the zombie list
    // because the caller MUST clean it up after retrieving it.
    auto m = &ret->waiters;
    m->next->prev = m->prev;
    m->prev->next = m->next;
    m->next = nullptr;
    m->prev = nullptr;
    return ret;
  }

  void set_thread_waiting(ThreadTracker *t, ThreadWaitList *wait_list);
  void set_thread_waiting_deadline(ThreadTracker *t, ThreadWaitList *wait_list,
                                   uint64_t deadline);
  void set_thread_sleeping(ThreadTracker *t, uint64_t deadline);
  void set_thread_runnable(ThreadTracker *t);
  void set_thread_zombie(ThreadTracker *t);

  // Add the current thread to the given waitlist without any deadline and
  // then yield to allow other threads to run. (This is one of our low-level
  // building blocks for synchronization primitives.)
  //
  // The thread that called this function will not run again until something
  // on another thread or in an interrupt handler moves the thread back from
  // this wait list into the runnable list.
  void wait_for_event(ThreadWaitList *wait_list);

  // Add the current thread to the given waitlist with the given deadline and
  // then yield to allow other threads to run. (This is one of our low-level
  // building blocks for synchronization primitives.)
  //
  // The thread that called this function will not run again either something
  // on another thread or in an interrupt handler moves the thread back from
  // this wait list into the runnable list, or until the deadline time is
  // reached and thus the main thread scheduler will force it runnable again.
  //
  // Returns true if it returned due to the occurance of the event associated
  // with the given wait list, or false if it returned due to reaching the
  // deadline before the event occurred.
  bool wait_for_event_deadline(ThreadWaitList *wait_list, uint64_t deadline);

  // Sets up the first thread in the given list, if any, to return from
  // an explicit yield with a successful result, and marks it as runnable.
  //
  // After this function returns, the resumed thread is no longer in the
  // wait list and any deadline it was waiting for is cancelled.
  void end_thread_wait_success(ThreadWaitList *from_list);

  // Performs the effect of end_thread_wait_success on every thread in the
  // given list, leaving the list empty.
  //
  // This function encapsulates the common case of traversing through all
  // threads in a particular wait list and making them all runnable at once.
  void end_thread_wait_list_success(ThreadWaitList *from_list);

  // Sets up the given thread to return from an explicit yield with an
  // unsuccessful (timeout) result and marks it as runnable.
  //
  // The result is the given thread's successor in from_list, if any,
  // because by the time this function returns the thread will no longer
  // belong to that list.
  ThreadTracker *end_thread_wait_timeout(ThreadTracker *t,
                                         ThreadTimedWaitList *from_list);
};

// Data structure which captures properties of a RISCovite application.
struct AppProperties {
  // Page size used for the application.
  uintptr_t page_size;

  Args *args;

  // The properties of an application's TLS image.
  TLSImage tls;

  // Environment data.
  uintptr_t *env_ptr;

  // Auxiliary vector data.
  AuxEntry *auxv_ptr;

  // Pointer to state related to our userspace multithreading implementation,
  // initialized only once the main thread creates its first non-main thread.
  //
  // If this is nullptr then libc is currently running in single-threaded mode
  // and so no thread-switching will occur.
  cpp::Atomic<AppThreading *> threading;

  // Returns a pointer to the application's AppThreading object if the
  // application is in multithreaded mode, or nullptr if the application is
  // currently single-threaded.
  //
  // If the result indicates single-threaded mode then the caller can assume
  // it will not be preempted by another thread, but it could potentially still
  // be interrupted by an interrupt handler.
  inline AppThreading *multithreading_state() { return this->threading.load(); }

  // Returns true if there has previously been a successful call to
  // ensure_multithreaded, or false otherwise.
  //
  // If this returns false then the caller can assume that it will not be
  // preempted by any other thread, but it could potentially still be
  // interrupted by an interrupt handler.
  inline bool is_multithreaded() {
    return this->multithreading_state() != nullptr;
  }

  // Initializes the multithreading implementation for this application if
  // not already initialized.
  //
  // Returns zero on success, or an errno-style error value on failure.
  //
  // After this function successfully returns once it is guaranteed to succeed
  // on subsequent calls. A multithreaded application cannot become
  // single-threaded again.
  int ensure_multithreaded();
};

[[gnu::weak]] extern AppProperties app;

// The descriptor of a thread's TLS area.
struct TLSDescriptor {
  // The size of the TLS area.
  uintptr_t size = 0;

  // The address of the TLS area. If zero, the TLS area uses automatic storage
  // and so does not need to be freed. Otherwise, pass this address to free(...)
  // once the thread exits.
  uintptr_t addr = 0;

  // The value the thread pointer register should be initialized to.
  // Note that, dependending the target architecture ABI, it can be the
  // same as |addr| or something else.
  uintptr_t tp = 0;

  constexpr TLSDescriptor() = default;
};

uintptr_t get_tls_alloc_size(const TLSImage *tls);

// Create and initialize the TLS area for the current thread. Should not
// be called before app.tls has been initialized.
void init_tls(void *alloc, TLSDescriptor &tls);

// Cleanup the TLS area as described in |tls_descriptor|.
void cleanup_tls(uintptr_t tls_addr, uintptr_t tls_size);

// Set the thread pointer for the current thread.
bool set_thread_ptr(uintptr_t val);

} // namespace LIBC_NAMESPACE_DECL

#endif // LLVM_LIBC_CONFIG_RISCOVITE_APP_H
