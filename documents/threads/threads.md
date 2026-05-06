# Priority Donation

## Overview

  **priority donation** goal is to ensure that the highest-priority ready thread runs first, while also preventing **priority inversion** when a high-priority thread waits on a lock held by a lower-priority thread.

### priority inversion: 
is a case where a  high-priority thread gets indirectly blocked by a lower-priority thread 
Consider high, medium, and low priority threads H, M, and L, respectively. If H needs to wait for L (for instance, for a lock held by L), and M is on the ready list, then H will never get the CPU because the low priority thread will not get any CPU time. A partial fix for this problem is for H to "donate" its priority to L while L is holding the lock, then recall the donation once L releases (and thus H acquires) the lock. 



The implementation covers:

* priority donation for locks
* nested donation
* multiple donations
* priority restoration on lock release
* compatibility with the MLFQS scheduler mode

---

## Design Goals

The project was designed with the following goals:

1. Keep the scheduler behavior deterministic and priority-based.
2. Apply donation only when a thread is actually blocked on a lock.
3. Support donation chains such as `H -> M -> L`.
4. Support multiple donors waiting on the same lock holder.
5. Restore the correct priority after a lock is released.
6. Avoid interference with `thread_mlfqs` mode.

---

## Thread Structure Changes

The `struct thread` was extended to support donation and MLFQS scheduling.

### Donation-related fields

* `priority`: current effective priority used by the scheduler
* `total_donated_original_priority`: base/original priority set by the user or test
* `waiting_lock`: lock currently being waited on
* `donations`: list of donor threads waiting on locks held by this thread
* `donation_elem`: list element used inside the donations list

### MLFQS-related fields

* `nice`: thread niceness value
* `recent_cpu`: recent CPU usage value
* `wakeup_tick`: wakeup time for sleeping threads

---

## Core Priority Donation Model

The implementation follows this model:

```text
effective priority = max(base priority, highest donated priority)
```

Where:

* **base priority** is stored in `total_donated_original_priority`
* **effective priority** is stored in `priority`
* donation is tracked through the `donations` list

This design avoids treating donation as a sum and instead keeps priority inheritance aligned with Pintos requirements.

---

## Lock Donation Flow

### `lock_acquire()`

When a thread attempts to acquire a lock:

1. If the lock is free, it acquires it directly.
2. If the lock is held, the current thread marks the lock as `waiting_lock`.
3. The current thread is added to the holder’s `donations` list.
4. Priority donation is propagated through the holder chain.
5. The thread blocks using `sema_down()`.
6. After waking up, it becomes the lock holder.

### `lock_release()`

When a thread releases a lock:

1. Donations related to that lock are removed from the holder’s `donations` list.
2. The holder’s priority is restored to its base priority.
3. Remaining donations are checked.
4. The highest remaining donated priority is applied.
5. The semaphore is released with `sema_up()`.

---

## Nested Donation

Nested donation is supported through a donation chain.

Example:

* Thread `H` waits on a lock held by `M`
* Thread `M` waits on a lock held by `L`
* `H` donates to `M`
* `M` forwards the donation to `L`

This propagation continues up to a reasonable limit on nesting depth.

---

## Semaphore Scheduling

Semaphores were updated so that waiting threads are handled in priority order.

### `sema_down()`

* The current thread is inserted into the semaphore waiters list in priority order.
* The thread blocks until the semaphore becomes available.

### `sema_up()`

* The highest-priority waiting thread is selected.
* That thread is unblocked first.
* If the unblocked thread has a higher priority than the running thread, the CPU yields.

This ensures correct priority scheduling even outside donation cases.

---

## Thread Priority Update

The `thread_set_priority()` function was updated so that a thread can change its base priority without losing donation effects.

Behavior:

* If the thread has no donations, its priority becomes the new value directly.
* If the thread has donations, the scheduler keeps the higher of:

  * the new base priority
  * the highest donated priority

This prevents donated priority from being lost prematurely.

---

## MLFQS Compatibility

The project also includes support for the MLFQS scheduler mode.

Important rule:

* **priority donation must be disabled when `thread_mlfqs` is true**

This keeps the donation logic from interfering with the MLFQS computation of priority, recent CPU, and niceness.

---

## Important Data Structures

### Donation list

Each thread stores a list of donor threads waiting on locks it holds.

This list is used to:

* track active donors
* recompute priority after lock release
* support multiple simultaneous donations

### Waiting lock

Each blocked thread stores the lock it is waiting for.

This field is used to:

* detect donation chains
* propagate priority through nested lock dependencies
* remove donations related to a specific lock on release

---

## Comparator Functions

Comparator functions were added to support ordered thread lists.

### For semaphore waiters

Used to compare threads by `priority` using the `elem` list field.

### For donation lists

Used to compare donor threads by `priority` using the `donation_elem` list field.

These comparators allow:

* ordered insertion into wait queues
* selecting the highest-priority donor
* correct recomputation after donation removal

---

## Test Results

The implementation was validated using Pintos thread tests.

Passed tests include:

* `priority-change`
* `priority-donate-one`
* `priority-donate-multiple`
* `priority-donate-multiple2`
* `priority-donate-nest`
* `priority-donate-sema`
* `priority-donate-lower`
* `priority-fifo`
* `priority-preempt`
* `priority-sema`
* `priority-condvar`

These results confirm that the implementation supports:

* priority donation
* nested donation
* multiple donors
* correct scheduling behavior
* condition variable and semaphore priority ordering

---

## Merge and Git Notes

During development, a merge conflict occurred between the donation work and the MLFQS branch. The final structure keeps both features in the codebase, with clear runtime separation based on `thread_mlfqs`.

General merge strategy used:

* keep donation fields in `struct thread`
* keep MLFQS fields in `struct thread`
* guard donation logic with `if (!thread_mlfqs)`
* preserve semaphore and scheduling behavior

---

## Final Outcome

The project now supports:

* priority-based scheduling
* priority donation for locks
* nested donation chains
* multiple donors
* correct priority restoration
* compatibility with MLFQS mode

This makes the Pintos thread subsystem behave correctly under both lock contention and priority-based scheduling scenarios.
