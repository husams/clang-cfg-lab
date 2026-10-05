// Part 5.5 -- thread-safety analysis: annotated mutex, one mistake per function.
#define CAPABILITY(x) __attribute__((capability(x)))
#define GUARDED_BY(x) __attribute__((guarded_by(x)))
#define ACQUIRE(...) __attribute__((acquire_capability(__VA_ARGS__)))
#define ACQUIRE_SHARED(...) __attribute__((acquire_shared_capability(__VA_ARGS__)))
#define RELEASE(...) __attribute__((release_capability(__VA_ARGS__)))
#define REQUIRES(...) __attribute__((requires_capability(__VA_ARGS__)))
#define EXCLUDES(...) __attribute__((locks_excluded(__VA_ARGS__)))
#define SCOPED __attribute__((scoped_lockable))
#define ACQUIRED_BEFORE(...) __attribute__((acquired_before(__VA_ARGS__)))

struct CAPABILITY("mutex") Mutex {
  void Lock() ACQUIRE();
  void LockShared() ACQUIRE_SHARED();
  void Unlock() RELEASE();
};

struct SCOPED Guard {
  Guard(Mutex &m) ACQUIRE(m);
  ~Guard() RELEASE();
};

Mutex mu;
Mutex first;
Mutex second ACQUIRED_BEFORE(first);
int counter GUARDED_BY(mu);

void needs_mu() REQUIRES(mu);
void wants_free() EXCLUDES(mu);

// Correct: lock, write, unlock.
void ok() {
  mu.Lock();
  counter = 1;
  mu.Unlock();
}

// Correct: the RAII guard acquires and releases.
void ok_scoped() {
  Guard g(mu);
  counter = 2;
}

// Write without the lock.
void unguarded_write() { counter = 3; }

// Read-only lock, but the access is a write.
void shared_write() {
  mu.LockShared();
  counter = 4;
  mu.Unlock();
}

// Release something that is not held.
void unmatched_unlock() { mu.Unlock(); }

// Acquire twice.
void double_lock() {
  mu.Lock();
  mu.Lock();
  mu.Unlock();
}

// Return with the mutex still held.
void held_at_end() { mu.Lock(); }

// Lock on one path only: the join has two different states.
void one_path(bool c) {
  if (c)
    mu.Lock();
  counter = 5;
  if (c)
    mu.Unlock();
}

// Call a REQUIRES function without the lock.
void call_without() { needs_mu(); }

// Call an EXCLUDES function with the lock held.
void call_excluded() {
  mu.Lock();
  wants_free();
  mu.Unlock();
}

// Lock order: 'second' must be taken before 'first'.
void bad_order() {
  first.Lock();
  second.Lock();
  second.Unlock();
  first.Unlock();
}
