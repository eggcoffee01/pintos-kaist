/* This file is derived from source code for the Nachos
   instructional operating system.  The Nachos copyright notice
   is reproduced in full below. */

/* Copyright (c) 1992-1996 The Regents of the University of California.
   All rights reserved.

   Permission to use, copy, modify, and distribute this software
   and its documentation for any purpose, without fee, and
   without written agreement is hereby granted, provided that the
   above copyright notice and the following two paragraphs appear
   in all copies of this software.

   IN NO EVENT SHALL THE UNIVERSITY OF CALIFORNIA BE LIABLE TO
   ANY PARTY FOR DIRECT, INDIRECT, SPECIAL, INCIDENTAL, OR
   CONSEQUENTIAL DAMAGES ARISING OUT OF THE USE OF THIS SOFTWARE
   AND ITS DOCUMENTATION, EVEN IF THE UNIVERSITY OF CALIFORNIA
   HAS BEEN ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

   THE UNIVERSITY OF CALIFORNIA SPECIFICALLY DISCLAIMS ANY
   WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
   WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
   PURPOSE.  THE SOFTWARE PROVIDED HEREUNDER IS ON AN "AS IS"
   BASIS, AND THE UNIVERSITY OF CALIFORNIA HAS NO OBLIGATION TO
   PROVIDE MAINTENANCE, SUPPORT, UPDATES, ENHANCEMENTS, OR
   MODIFICATIONS.
   */

#include "threads/synch.h"
#include <stdio.h>
#include <string.h>
#include "threads/interrupt.h"
#include "threads/thread.h"

/* Initializes semaphore SEMA to VALUE.  A semaphore is a
   nonnegative integer along with two atomic operators for
   manipulating it:

   - down or "P": wait for the value to become positive, then
   decrement it.

   - up or "V": increment the value (and wake up one waiting
   thread, if any). */
void
sema_init (struct semaphore *sema, unsigned value) {
	ASSERT (sema != NULL);

	sema->value = value;
	list_init (&sema->waiters);
}

/* Down or "P" operation on a semaphore.  Waits for SEMA's value
   to become positive and then atomically decrements it.

   This function may sleep, so it must not be called within an
   interrupt handler.  This function may be called with
   interrupts disabled, but if it sleeps then the next scheduled
   thread will probably turn interrupts back on. This is
   sema_down function. */

void
sema_down (struct semaphore *sema) {
	enum intr_level old_level;

	ASSERT(sema != NULL);
	ASSERT(!intr_context ());

	old_level = intr_disable();
	// sema의 값이 0이 됐을 때는 공유할 수 있는 자원이 없다는 것을 의미하기 때문에, 새롭게 할당하려는 스레드를 block해서 실행이 안되도록 한다.
	while (sema->value == 0) {
		// FIFO 형태의 Semaphore이기 때문에, Semaphore이 양수가 돼서 활용할 수 있는 공유 자원이 있을 때 해당 자원을 활용하기 위해서 기다리고 있는 프로세스를 할당하기 위한 리스트로 waiter를 활용한다.
		// list_push_back (&sema->waiters, &thread_current ()->elem);
		
		// Priority Semaphore
		// Priority 형태의 Sempahore를 구현하기 위해서, cmp_priority를 이용해서 공유 자원이 있을 때 해당 자원을 활용하기 위해서 기다리고 있는 프로세스를 우선순위 순서대로 정렬해서 waiters 리스트에 넣는다.
		list_insert_ordered( &sema->waiters, &thread_current()->elem , &cmp_priority, NULL);
		thread_block();
	}
	sema->value--;
	intr_set_level(old_level);
}



/* Down or "P" operation on a semaphore, but only if the
   semaphore is not already 0.  Returns true if the semaphore is
   decremented, false otherwise.

   This function may be called from an interrupt handler. */
bool
sema_try_down (struct semaphore *sema) {
	enum intr_level old_level;
	bool success;

	ASSERT (sema != NULL);

	old_level = intr_disable ();
	if (sema->value > 0)
	{
		sema->value--;
		success = true;
	}
	else
		success = false;
	intr_set_level (old_level);

	return success;
}

/* Up or "V" operation on a semaphore.  Increments SEMA's value
   and wakes up one thread of those waiting for SEMA, if any.

   This function may be called from an interrupt handler. */
void
sema_up(struct semaphore *sema){
	enum intr_level old_level;

	ASSERT (sema != NULL);

	old_level = intr_disable ();
	if (!list_empty (&sema->waiters)){
		// 깨우려는 semaphore를 지정하고 나서, 
		list_sort(&sema->waiters, cmp_priority, NULL);
		// 해당 스레드의 상태를 wait->ready로 바꿔준다.
		thread_unblock (list_entry (list_pop_front (&sema->waiters),struct thread, elem));
	}
	// 자원을 할당 받을 수 있는 스레드의 개수를 늘려주고,
	sema->value++;
	// 현재 실행하고 있는 스레드보다 waiter 리스트 내 스레드의 우선순위가 더 높다면, CPU를 waiters 리스트 내 스레드에게 할당한다.
	//check_max_priority();
	preempt_priority();
	intr_set_level (old_level);
}

static void sema_test_helper (void *sema_);

/* Self-test for semaphores that makes control "ping-pong"
   between a pair of threads.  Insert calls to printf() to see
   what's going on. */
void
sema_self_test (void) {
	struct semaphore sema[2];
	int i;

	printf ("Testing semaphores...");
	sema_init (&sema[0], 0);
	sema_init (&sema[1], 0);
	thread_create ("sema-test", PRI_DEFAULT, sema_test_helper, &sema);
	for (i = 0; i < 10; i++)
	{
		sema_up (&sema[0]);
		sema_down (&sema[1]);
	}
	printf ("done.\n");
}

/* Thread function used by sema_self_test(). */
static void
sema_test_helper (void *sema_) {
	struct semaphore *sema = sema_;
	int i;

	for (i = 0; i < 10; i++)
	{
		sema_down (&sema[0]);
		sema_up (&sema[1]);
	}
}

/* Initializes LOCK.  A lock can be held by at most a single
   thread at any given time.  Our locks are not "recursive", that
   is, it is an error for the thread currently holding a lock to
   try to acquire that lock.

   A lock is a specialization of a semaphore with an initial
   value of 1.  The difference between a lock and such a
   semaphore is twofold.  First, a semaphore can have a value
   greater than 1, but a lock can only be owned by a single
   thread at a time.  Second, a semaphore does not have an owner,
   meaning that one thread can "down" the semaphore and then
   another one "up" it, but with a lock the same thread must both
   acquire and release it.  When these restrictions prove
   onerous, it's a good sign that a semaphore should be used,
   instead of a lock. */
void
lock_init (struct lock *lock) {
	ASSERT (lock != NULL);

	lock->holder = NULL;
	sema_init (&lock->semaphore, 1);
}

/* Acquires LOCK, sleeping until it becomes available if
   necessary.  The lock must not already be held by the current
   thread.

   This function may sleep, so it must not be called within an
   interrupt handler.  This function may be called with
   interrupts disabled, but interrupts will be turned back on if
   we need to sleep. */
void
lock_acquire (struct lock *lock) {
	ASSERT (lock != NULL);
	ASSERT (!intr_context ());
	ASSERT (!lock_held_by_current_thread (lock));

	struct thread *curr = thread_current();
	// lock holder가 존재하는 것을 확인
	if(lock->holder != NULL){
		
		// 현재 스레드가 기다리고 있는 lock을, 해당되는 lock 으로 저장한다.
		curr->waiting_lock = lock;
		// lock을 보유하고 있는 스레드의 donation_list에 우선순위가 높은 순으로 스레드를 삽입한다.
		list_insert_ordered(&lock->holder->donation_list, &curr->donation_elem, cmp_don_priority, NULL);
		// lock을 보유하고 있는 스레드에게, 더 높은 우선순위의 스레드가 우선순위를 기부한다.
		// 그리고 해당 함수에서는 순위가 높은 스레드가 순위가 낮은 스레드한테서 lock을 얻는 과정만 수행하는 것이지, 
		// 우선순위가 낮은 스레드가 더 높은 우선순위를 기증 받아서 실행되는 과정은 없어서 따로 donate_priority 함수를 만든 것이다. 
		donate_priority();
	}
	
	// lock의 semaphore를 낮추면서, 원래 lock을 소유했던 스레드에서 현재의 스레드로 lock의 소유권을 이전했다는 것을 표시한다.
	sema_down(&lock->semaphore);
	// 현재 스레드가 기다리고 있는 스레드를 NULL값으로 바꿔주고,
	curr->waiting_lock = NULL;
	// lock을 보유하고 있는 스레드를 현재 스레드로 바꿔준다.
	lock->holder = thread_current();

	/* Priority-semaphore, Condition variable */
	// sema_down (&lock->semaphore);
	// lock->holder = thread_current ();

}

// 현재의 스레드가 자신이 원하는 lock을 가진 스레드에게, 자신의 우선순위를 재귀적으로 기부하는 함수이다.
void donate_priority(void){
	struct thread *curr = thread_current();
	struct thread *holder;

	int priority = curr->priority;

	for (int i=0; i<8; i++){		
		if (curr->waiting_lock == NULL){
		return;
		}
		
		// 현재의 스레드가 자신이 원하는 lock을 가진 스레드에게, 자신의 우선순위를 기부하고,
		// 현재의 스레드를 자신이 원하는 lock을 가진 스레드로 지정함으로써 재귀적으로 우선순위를 기부한다. 
		holder = curr -> waiting_lock -> holder;
		holder->priority = priority;
		curr = holder;

	}

}

/* Tries to acquires LOCK and returns true if successful or false
   on failure.  The lock must not already be held by the current
   thread.

   This function will not sleep, so it may be called within an
   interrupt handler. */
bool
lock_try_acquire (struct lock *lock) {
	bool success;

	ASSERT (lock != NULL);
	ASSERT (!lock_held_by_current_thread (lock));

	success = sema_try_down (&lock->semaphore);
	if (success)
		lock->holder = thread_current ();
	return success;
}

/* Releases LOCK, which must be owned by the current thread.
   This is lock_release function.

   An interrupt handler cannot acquire a lock, so it does not
   make sense to try to release a lock within an interrupt
   handler. */

// 현재 스레드에게 자신의 우선순위를 기부한 스레드 중, 현재의 스레드가 자신의 lock을 반환할 스레드를 donation_list에서 제거하는 작업이 필요하다. 
// 그리고 현재의 스레드가 자신의 lock을 반환할 스레드를 donation_list에서 지우고 난 뒤, donation_list를 재정렬하는 작업이 필요하다.
void
lock_release (struct lock *lock) {
	ASSERT (lock != NULL);
	ASSERT (lock_held_by_current_thread (lock));

	remove_donor(lock);
	update_priority_don_list();

	lock->holder = NULL;
	sema_up (&lock->semaphore);
}

void remove_donor(struct lock *lock){
	struct list *donations = &(thread_current() -> donation_list);
	struct list_elem *donor_elem;
	struct thread *donor_thread;

	if(list_empty(donations)){
		return;
	}
	// donation_list에서 가장 우선순위가 높은 스레드를 지정한다.
	donor_elem = list_front(donations);

	while(1){
		// donor_elem -> donor_thread로 자료구조를 바꿔주는 과정이다.
		donor_thread = list_entry(donor_elem, struct thread, donation_elem);
		// 현재 스레드가 보유한 lock과 우선순위가 높은 스레드가 요청한 lock이 동일할 때,
		// donation_list에서 해당 lock을 요청한 우선순위가 높은 스레드를 삭제한다.
		if(donor_thread -> waiting_lock == lock ){
			list_remove(&donor_thread -> donation_elem);
		}
		
		// donations 리스트의 끝까지 순회해서, 현재 스레드가 보유한 lock과 우선순위가 높은 스레드가 요청한 lock이 동일한지 확인한다.
		donor_elem = list_next(donor_elem);

		if(donor_elem == list_end(donations)){
			return;
		}
	}

}

// 현재 스레드가 보유한 lock과 우선순위가 더 높은 스레드가 요청한 lock이 동일할 때, donation_list의 순위를 재정렬할 필요가 있다.
// donation_list가 존재하지 않으면 원래 스레드의 우선순위로 우선순위 변수를 재지정하고,
// donation_list가 존재한다면 오름차순으로 정렬된 해당 리스트의 가장 앞에 있는 스레드로 우선순위 변수를 재지정한다.
void update_priority_don_list(void){
	struct thread *curr = thread_current();
	struct list *donations = &(thread_current() -> donation_list);
	struct thread *donations_first;

	if(list_empty(donations)){
		curr->priority = curr->init_priority;
		return;
	}

	donations_first = list_entry( list_front(donations), struct thread, donation_elem);
	curr->priority = donations_first -> priority;
}


/* Returns true if the current thread holds LOCK, false
   otherwise.  (Note that testing whether some other thread holds
   a lock would be racy.) */
bool
lock_held_by_current_thread (const struct lock *lock) {
	ASSERT (lock != NULL);

	return lock->holder == thread_current ();
}

/* One semaphore in a list. */
// semaphore waiter안에 들어 있는 semaphore를 나타내기 위해서 쓰는 자료 구조
struct semaphore_elem {
	struct list_elem elem;              /* List element. */
	struct semaphore semaphore;         /* This semaphore. */
};

/* Initializes condition variable COND.  A condition variable
   allows one piece of code to signal a condition and cooperating
   code to receive the signal and act upon it. */
void
cond_init (struct condition *cond) {
	ASSERT (cond != NULL);

	list_init (&cond->waiters);
}

/* Atomically releases LOCK and waits for COND to be signaled by
   some other piece of code.  After COND is signaled, LOCK is
   reacquired before returning.  LOCK must be held before calling
   this function.

   The monitor implemented by this function is "Mesa" style, not
   "Hoare" style, that is, sending and receiving a signal are not
   an atomic operation.  Thus, typically the caller must recheck
   the condition after the wait completes and, if necessary, wait
   again.

   A given condition variable is associated with only a single
   lock, but one lock may be associated with any number of
   condition variables.  That is, there is a one-to-many mapping
   from locks to condition variables.

   This function may sleep, so it must not be called within an
   interrupt handler.  This function may be called with
   interrupts disabled, but interrupts will be turned back on if
   we need to sleep. */

// 자원을 활용할 수 있는 상태가 될 때까지, 스레드를 wait 리스트에 저장하기 위한 함수이다.
void
cond_wait (struct condition *cond, struct lock *lock) {
	struct semaphore_elem waiter;
	
	// 자원을 기다리고 있는 스레드가 존재할 때 
	ASSERT(cond != NULL);
	// 비어 있는 자원을 접근하기 위한 잠금을 갖고 있을 때
	ASSERT(lock != NULL);

	ASSERT(!intr_context ());
	// 현재 thread가 lock을 가지고 있는지 여부를 확인하는 것은, 해당 thread가 resource를 활용할 수 있는지 여부 자체를 먼저 확인하고 thread가 resource를 접근해야 되기 때문이다.
	ASSERT(lock_held_by_current_thread (lock));

	sema_init (&waiter.semaphore, 0);
	
	// Round robin 방식에 따른 구현
	// list_push_back (&cond->waiters, &waiter.elem);
	
	// waiters 리스트 내 자원을 할당 받으려는 스레드의 우선순위 순서대로 정렬한다.
	list_insert_ordered(&cond->waiters, &waiter.elem, cmp_sema_priority, NULL);

	// 해당되는 조건을 만족하기 이전에는, 자원에 다른 스레드가 접근할 수 있게 lock을 해제하고 sleep 상태를 유지한다.
	lock_release(lock);
	
	// 해당되는 조건을 만족할 때, 자원을 활용해서 sema_down을 하고 lock을 다시 얻는다.
	sema_down(&waiter.semaphore);
	lock_acquire(lock);
}

/* If any threads are waiting on COND (protected by LOCK), then
   this function signals one of them to wake up from its wait.
   LOCK must be held before calling this function.

   An interrupt handler cannot acquire a lock, so it does not
   make sense to try to signal a condition variable within an
   interrupt handler. */

// 스레드가 자원을 사용할 수 있는 상태가 된다면, waiter 리스트에서 우선순위가 가장 높은 스레드를 깨우고 sema_up 함수를 통해서 실행시킨다.
void
cond_signal (struct condition *cond, struct lock *lock UNUSED) {
	ASSERT (cond != NULL);
	ASSERT (lock != NULL);
	ASSERT (!intr_context ());
	ASSERT (lock_held_by_current_thread (lock));

	if (!list_empty (&cond->waiters))
	{	// waiters 내 자원의 할당을 기다리고 있는 스레드를, 스레드의 우선순위 기준으로 정렬한다.
		list_sort(&cond->waiters, cmp_sema_priority, NULL);
		
		// 우선순위 기준으로 정렬된 스레드를, 가장 우선순위가 높은 순으로 깨워서 실행한다.
		sema_up (&list_entry (list_pop_front (&cond->waiters), struct semaphore_elem, elem)->semaphore);
	
	}				
}

/* Wakes up all threads, if any, waiting on COND (protected by
   LOCK).  LOCK must be held before calling this function.

   An interrupt handler cannot acquire a lock, so it does not
   make sense to try to signal a condition variable within an
   interrupt handler. */
void
cond_broadcast (struct condition *cond, struct lock *lock) {
	ASSERT (cond != NULL);
	ASSERT (lock != NULL);

	while (!list_empty (&cond->waiters))
		cond_signal (cond, lock);
}

// 인자로 전달 받는 elem으로 스레드에 바로 접근할 수 없기 때문에, semphore 자료구조 내의 스레드 간 우선순위를 비교하기 위해서 새로운 우선순위 비교 함수를 만들어줘야 한다.
// list elem -> sempahore_elem -> waiters -> thread

bool cmp_sema_priority(const struct list_elem *a, const struct list_elem *b, void *aux UNUSED) {
    struct semaphore_elem *sema_a = list_entry(a, struct semaphore_elem, elem);
    struct semaphore_elem *sema_b = list_entry(b, struct semaphore_elem, elem);

    struct list *waiters_a = &(sema_a->semaphore.waiters);
    struct list *waiters_b = &(sema_b->semaphore.waiters);

    struct thread *root_a = list_entry(list_begin(waiters_a), struct thread, elem);
    struct thread *root_b = list_entry(list_begin(waiters_b), struct thread, elem);

    return root_a->priority > root_b->priority;
}

bool cmp_don_priority(const struct list_elem *a, const struct list_elem *b, void *aux UNUSED){
		struct thread *thread_a = list_entry(a, struct thread, donation_elem);
		struct thread *thread_b = list_entry(b, struct thread, donation_elem);

		return thread_a->priority > thread_b->priority;
}