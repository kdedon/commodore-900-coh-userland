/*
 *  This file defines the interface between top and the machine-dependent
 *  module.  It is NOT machine dependent and should not need to be changed
 *  for any specific machine.
 */

/*
 * the statics struct is filled in by machine_init
 */
struct statics
{
    char **procstate_names;
    char **cpustate_names;
    char **memory_names;
};

/*
 * A load average is hundredths of a process, in a long.  The distribution
 * offers this fixed-point representation beside the floating-point one (see
 * loadavg.h, FIXED_LOADAVG) for kernels that keep the figure scaled; here it
 * is also what keeps the whole program clear of double.
 *
 * LOAD_NONE means the kernel supplies no load average.  It is displayed as
 * such rather than shown as a number, and it is below every threshold, so
 * comparisons against it read as an unloaded machine.
 */
#define LOAD_SCALE	100L
#define LOAD_NONE	(-1L)

/*
 * the system_info struct is filled in by a machine dependent routine.
 */

struct system_info
{
    int    last_pid;
    long   load_avg[NUM_AVERAGES];	/* load_avg, hundredths */
    int    p_total;
    int    p_active;     /* number of procs considered "active" */
    int    *procstates;
    int    *cpustates;
    int    *memory;
};

/* cpu_states is an array of percentages * 10.  For example, 
   the (integer) value 105 is 10.5% (or .105).
 */

/*
 * the process_select struct tells get_process_info what processes we
 * are interested in seeing
 */

struct process_select
{
    int idle;		/* show idle processes */
    int system;		/* show system processes */
    int uid;		/* only this uid (unless uid == -1) */
    char *command;	/* only this command (unless == NULL) */
};

/* routines defined by the machine dependent module */

char *format_header();
char *format_next_process();

/* non-int routines typically used by the machine dependent module */
char *printable();
