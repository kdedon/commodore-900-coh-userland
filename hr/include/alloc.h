/*
 * Copyright (c) 1977-1995 Robert Swartz.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#ifndef HANDLE
#define HANDLE int
#endif
typedef struct
{
	char	*ms_ptr;	/* pointer to data header area of block */
	char	ms_flag;	/* flags for master pointer 	        */
	char	ms_zone;	/* zone number for master pointer	*/
	char	ms_type;	/* object type for master pointer	*/
} MstrPtr;

typedef struct DataBlk
{
unsigned int	 dt_size;		/* size of block in bytes	*/
	 char	 dt_flag;		/* misc flags			*/
	 char	 dt_lockc;		/* number of times block locked */
	 HANDLE	 dt_ptr;		/* index to master pointer	*/
	 struct DataBlk *dt_front;	/* pointer to next block	*/
	 struct DataBlk *dt_back;	/* pointer to last block	*/
} DataBlk;

/*
 *	Object Types For Master Pointers
 */
#define	OT_ICON		(0)
#define OT_STRING	(1)
#define OT_REGION	(2)
#define OT_BITIMAGE	(3)
#define OT_MENU		(4)

/*
 *	Data Block Header Flag values
 */
#define	DB_FREE		(0)
#define DB_RELOC	(1)
#define	DB_NRELOC	(2)

/*
 *	Memory Management Routine values
 */
#define MaxZone		(1)	 /* number of zones       - testing value    */
#define SizeZone	(5000)	 /* size of zone in bytes - testing value    */
#define PtsPerBlk	(16)	 /* number of master pointers per data block */
				 /* this value MUST be a power of two	     */
#define LogPts		(4)	 /* power which 2 is raised to get PtsPerBlk */

/*
 *	Often called Memory Zone Routine
 */
#define RetZone(Z)	{Zone = first[Z]; EndZone = (DataBlk *)last[Z];};

/*
**	In the following defines,
**		P => ptr to ms_ptr field of MstrPtr structure
**		D => ptr to DataBlk structure
*/
#define ToDataBlk(P)	((DataBlk *)(((MstrPtr *)(P))->ms_ptr)-1)
#define SpecHandle(P)	(((MstrPtr *)(P))->ms_flag |= 0x01)
#define RegHandle(P)	(((MstrPtr *)(P))->ms_flag &= 0xfe)
#define IsSpecHandle(P)	((((MstrPtr *)(P))->ms_flag & 0x01) == 0x01)
#define	LockMstr(P)	(((MstrPtr *)(P))->ms_flag &= 0xfd)
#define UnlockMstr(P)	(((MstrPtr *)(P))->ms_flag |= 0x02)
#define IsLockMstr(P)	((((MstrPtr *)(P))->ms_flag & 0x02) == 0x00)
#define IsEmptyMstr(P)	( ((P) == NULL) || (((MstrPtr *)(P))->ms_ptr == NULL) )
#define IsFreeBlk(D)	((D)->dt_flag == DB_FREE)
#define IsRelocBlk(D)	(((D)->dt_flag == DB_RELOC) && ((D)->dt_lockc == 0))
#define IsNrelocBlk(D)	((D)->dt_flag == DB_NRELOC)
#define IsLockBlk(D)	(((D)->dt_flag == DB_RELOC) && ((D)->dt_lockc > 0))
#define NextBlk(D)	((DataBlk *)((char *)(D) + (D)->dt_size))
