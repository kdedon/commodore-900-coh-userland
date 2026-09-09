/*
inet/coh_clock.c -- COHERENT timer osdep for the inet daemon.

Replaces Minix clock.c.  The timer-chain management is unchanged; only two
points are OS-dependent:

  get_time()	 an UPTIME in HZ ticks -- ticks since the daemon's first call,
		 not a wall clock.  Minix asked the kernel for GET_UPTIME; here
		 the ticks are accumulated from ftime() (10 ms resolution), so
		 the epoch never enters the arithmetic and setting the clock
		 while the daemon runs does not move network time.
  the alarm	 Minix armed a kernel synchronous alarm (SET_SYNC_AL) and got a
		 tick message back; the COHERENT daemon instead lets its
		 select() loop wake at the next deadline.  clck_next_deadline()
		 hands the main loop that absolute tick time; set_timer() just
		 flags an already-due head timer via clck_call_expire.

Copyright 1995 Philip Homburg (original); COHERENT port.
*/

#include "inet.h"
#include "generic/assert.h"
#include "generic/buf.h"
#include "generic/clock.h"
#include "generic/type.h"

#include <sys/timeb.h>

THIS_FILE

PUBLIC int clck_call_expire;

PRIVATE time_t curr_time;
PRIVATE timer_t *timer_chain;

/*
 * The tick clock.  clck_ticks counts whole seconds of elapsed time in HZ ticks,
 * clck_lastsec is the ftime() second it was last advanced from, and clck_prev is
 * the last value handed out, which the clock is never allowed to go below.
 *
 * clck_ticks starts at the low 16 bits of the wall-clock second (in ticks) so
 * that two incarnations of the daemon do not start their tick clock, and hence
 * their TCP initial sequence numbers, at the same place.
 */
#define CLCK_SEED_MASK	0xFFFFL		/* wall-clock seconds kept as a seed */
#define CLCK_MAX_STEP	(24L*60*60)	/* a longer step = the clock was set  */

PRIVATE time_t clck_ticks;
PRIVATE time_t clck_lastsec;
PRIVATE time_t clck_prev;
PRIVATE int clck_running;

FORWARD void clck_fast_release ARGS(( timer_t *timer ));
FORWARD void set_timer ARGS(( void ));

PUBLIC void clck_init()
{
	clck_call_expire= 0;
	curr_time= 0;
	timer_chain= 0;
	/* The tick state is NOT reset here: get_time() starts the clock on its
	 * first call, which may precede this one, and restarting it would move
	 * time backwards under any timer already armed. */
}

PUBLIC time_t get_time()
{
	struct timeb tb;
	time_t step;

	if (!curr_time)
	{
		ftime(&tb);
		if (!clck_running)
		{
			clck_running= 1;
			clck_lastsec= tb.time;
			clck_ticks= (tb.time & CLCK_SEED_MASK) * HZ;
		}

		/* Accumulate elapsed seconds as ticks.  A backward or absurdly
		 * large step is the operator setting the clock, not elapsed
		 * time: it contributes nothing, so network time stays put and
		 * timers keep their meaning. */
		step= tb.time - clck_lastsec;
		if (step < 0 || step > CLCK_MAX_STEP)
			step= 0;
		clck_ticks += step * HZ;
		clck_lastsec= tb.time;

		/* ms -> ticks = ms*HZ/1000.  The +1 keeps the value non-zero:
		 * zero is the "no time recorded" sentinel here and in the
		 * stack's timestamp fields, and clck_next_deadline()'s "idle". */
		curr_time= clck_ticks + (time_t)tb.millitm * HZ / 1000 + 1;
		if (curr_time < clck_prev)
			curr_time= clck_prev;
		clck_prev= curr_time;
	}
	return curr_time;
}

PUBLIC void set_time(tim)
time_t tim;
{
	/* Some code assumes no time elapses while it runs.  `tim' is in the tick
	 * units get_time() hands out, not seconds. */
	if (!curr_time)
		curr_time= tim;
}

PUBLIC void reset_time()
{
	curr_time= 0;
}

PUBLIC void clck_timer(timer, timeout, func, fd)
timer_t *timer;
time_t timeout;
timer_func_t func;
int fd;
{
	timer_t *timer_index;

	if (timer->tim_active)
		clck_fast_release(timer);
	assert(!timer->tim_active);

	timer->tim_next= 0;
	timer->tim_func= func;
	timer->tim_ref= fd;
	timer->tim_time= timeout;
	timer->tim_active= 1;

	if (!timer_chain)
		timer_chain= timer;
	else if (timeout < timer_chain->tim_time)
	{
		timer->tim_next= timer_chain;
		timer_chain= timer;
	}
	else
	{
		timer_index= timer_chain;
		while (timer_index->tim_next &&
			timer_index->tim_next->tim_time < timeout)
			timer_index= timer_index->tim_next;
		timer->tim_next= timer_index->tim_next;
		timer_index->tim_next= timer;
	}
	set_timer();
}

PRIVATE void clck_fast_release(timer)
timer_t *timer;
{
	timer_t *timer_index;

	if (!timer->tim_active)
		return;

	if (timer == timer_chain)
		timer_chain= timer_chain->tim_next;
	else
	{
		timer_index= timer_chain;
		while (timer_index && timer_index->tim_next != timer)
			timer_index= timer_index->tim_next;
		assert(timer_index);
		timer_index->tim_next= timer->tim_next;
	}
	timer->tim_active= 0;
}

PRIVATE void set_timer()
{
	if (!timer_chain)
		return;
	if (timer_chain->tim_time <= get_time())
		clck_call_expire= 1;
}

/*
 * clck_next_deadline -- the absolute tick time the main loop's select() should
 * wake at, or 0 if no timer is pending (block indefinitely).
 */
PUBLIC time_t clck_next_deadline()
{
	return timer_chain ? timer_chain->tim_time : (time_t)0;
}

PUBLIC void clck_untimer(timer)
timer_t *timer;
{
	clck_fast_release(timer);
	set_timer();
}

PUBLIC void clck_expire_timers()
{
	time_t now;
	timer_t *timer_index;

	clck_call_expire= 0;

	if (timer_chain == NULL)
		return;

	now= get_time();
	while (timer_chain && timer_chain->tim_time <= now)
	{
		assert(timer_chain->tim_active);
		timer_chain->tim_active= 0;
		timer_index= timer_chain;
		timer_chain= timer_chain->tim_next;
		(*timer_index->tim_func)(timer_index->tim_ref, timer_index);
	}
	set_timer();
}
