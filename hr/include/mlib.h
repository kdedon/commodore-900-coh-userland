/*
 * Copyright (c) 1977-1995 Robert Swartz.
 * SPDX-License-Identifier: BSD-3-Clause
 */

 
/*
 *		Defines for getting to fields of messages 
 */
#define MsgSender	my->j_m.msg_Sender
#define MsgReceiver	my->j_m.msg_Sender
#define MsgCmd		my->j_m.msg_Cmd
#define MsgData0	(my->j_m.msg_Data[0])
#define MsgData1	(my->j_m.msg_Data[1])
#define MsgData2	(my->j_m.msg_Data[2])
#define MsgBytH0	(* ((char *)  &MsgData0) )
#define MsgBytL0	(* (((char *) &MsgData0) + 1) )
#define MsgBytH1	(* ((char *)  &MsgData1) )
#define MsgBytL1	(* (((char *) &MsgData1) + 1) )
#define MsgBytH2	(* ((char *)  &MsgData2) )
#define MsgBytL2	(* (((char *) &MsgData2) + 1) )
#define MsgWid		(MsgBytH0)
#define MsgPt		(* (POINT *) &MsgData1)
#define MsgPtr		(* (char **) &MsgData1)
 
