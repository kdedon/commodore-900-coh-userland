

#define	me	runq.jq_head
#define	my	me


struct job
{
	struct job	*j_next;
	jmp_buf		j_env;			/* cpu regs */
	uint		j_slength,		/* # words of stack */
			j_smax;			/* max # words of stack */
	int		*j_stack,		/* stack */
			(*j_startf)( );		/* baby j's first function */
	MESSAGE		j_m;
};

struct jqueue
{
	struct job	*jq_head,
			*jq_tail;
};


/*
struct jqueue	runq;
*/
 
extern	struct	jqueue	runq;

struct job	*jstart( );
