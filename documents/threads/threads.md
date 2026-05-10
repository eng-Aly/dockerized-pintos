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


```
struct thread
  {
    /* Owned by thread.c. */
    tid_t tid;                            /* Thread identifier. */
    enum thread_status status;            /* Thread state. */
    char name[16];                        /* Name (for debugging purposes). */
    uint8_t *stack;                       /* Saved stack pointer. */
    int priority;                         /* Priority. */
    int total_donated_original_priority;  /*donated priority indicate the total*/
    struct list_elem allelem;             /* List element for all threads list. */

    /* Shared between thread.c and synch.c. */
    struct list_elem elem;              /* List element. */
    /* Owned by thread.c and timer.c. */
    int64_t sleep_ticks;                /* Ticks until wakeup. */
   
    //added for priority donations
    struct lock *waiting_lock; /* Lock this thread is blocked on. */
    struct list donations;     /* Donors waiting on locks I hold. */
    struct list_elem donation_elem;

    int nice;                  /* Niceness value (-20 to 20) */
    int recent_cpu;            /* Recent CPU usage (fixed-point) */
    int64_t wakeup_tick;

   
      /* Owned by synch.c. */
#ifdef USERPROG
    /* Owned by userprog/process.c. */
    uint32_t *pagedir;                  /* Page directory. */
#endif

    /* Owned by thread.c. */
    unsigned magic;                     /* Detects stack overflow. */
  };

```

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

* **base priority** is stored in `total_donated_original_priority`   //name was needed to change
* **effective priority** is stored in `priority`
* donation is tracked through the `donations` list

This design avoids treating donation as a sum and instead keeps priority inheritance aligned with Pintos requirements.

---

## Lock Donation Flow

### `lock_acquire(struct lock *lock )`

When a thread attempts to acquire a lock:

1. If the lock is free, it acquires it directly.
2. If the lock is held, the current thread marks the lock as `waiting_lock`.
3. The current thread is added to the holder’s `donations` list.
4. Priority donation is propagated through the holder chain.
5. The thread blocks using `sema_down()`.
6. After waking up, it becomes the lock holder.


```
void
lock_acquire (struct lock *lock)
{
  ASSERT (lock != NULL);
  ASSERT (!intr_context ());
  ASSERT (!lock_held_by_current_thread (lock));

  struct thread *cur = thread_current ();

  if (!thread_mlfqs && lock->holder != NULL)
    {
      cur->waiting_lock = lock;

      list_push_back (&lock->holder->donations,
                      &cur->donation_elem);

      donate_priority_chain (lock->holder, cur->priority);
    }

  sema_down (&lock->semaphore);

  cur->waiting_lock = NULL;
  lock->holder = cur;
}
```

### `lock_release(struct lock *lock)`

When a thread releases a lock:

1. Donations related to that lock are removed from the holder’s `donations` list.
2. The holder’s priority is restored to its base priority.
3. Remaining donations are checked.
4. The highest remaining donated priority is applied.
5. The semaphore is released with `sema_up()`.

```
void
lock_release (struct lock *lock)
{
  ASSERT (lock != NULL);
  ASSERT (lock_held_by_current_thread (lock));

  struct thread *cur = thread_current ();

  /* remove donations tied to this lock */
  struct list_elem *e = list_begin (&cur->donations);

  while (e != list_end (&cur->donations))
    {
      struct thread *t = list_entry (e, struct thread, donation_elem);

      if (t->waiting_lock == lock)
        e = list_remove (e);
      else
        e = list_next (e);
    }

  /* restore base priority */
  cur->priority = cur->total_donated_original_priority;

  /* re-apply remaining donations */
  if (!list_empty (&cur->donations))
    {
      struct thread *max_t = list_entry (
          list_max (&cur->donations,
                    thread_priority_higher,
                    NULL),
          struct thread,
          donation_elem);

      if (max_t->priority > cur->priority)
        cur->priority = max_t->priority;
    }

  lock->holder = NULL;
  sema_up (&lock->semaphore);
  if (!thread_mlfqs) {
        /* all your priority restoration code stays here */
  }
}

```

### `donate_priority_chain (struct thread *t, int new_prio)`


donate_priority_chain (struct thread *t, int new_prio)
{
  int depth = 0;

  while (t != NULL && depth < donation_depth_num)
    {
      if (t->priority < new_prio)
        t->priority = new_prio;

      if (t->waiting_lock == NULL)
        break;

      t = t->waiting_lock->holder;
      depth++;
    }
}

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

The repository was forked, and development was divided into multiple feature branches derived from a common base:
* `phase1/devel`
* `phase1/priority_inheritance`
* `phase1/priority_scheduler`
* `phase1/alarm_clock`
* `phase1/MLFQS`

every branch was from the devel and merged to it after passing the tests


---
