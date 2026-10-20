#ifndef SPINLOCK_H
#define SPINLOCK_H

#include <stdint.h>

typedef struct
{
	uint32_t lock;
	uint64_t rflags;
} spinlock_t;

extern void spinlock_acquire(volatile uint32_t* lock);
extern void spinlock_release(volatile uint32_t* lock);

static inline void spin_lock(spinlock_t* spinlock)
{
	uint64_t rflags;
	 __asm__ volatile("pushfq; pop %0" : "=r"(rflags));

	 __asm__ volatile("cli" ::: "memory");

	spinlock_acquire(&spinlock->lock);

	spinlock->rflags = rflags;
}

static inline void spin_unlock(spinlock_t* spinlock)
{
	uint64_t old_rflags = spinlock->rflags;

	spinlock_release(&spinlock->lock);

	if (old_rflags & (1ULL << 9))
	{
		__asm__ volatile("sti" ::: "memory");
	}
}

#endif
