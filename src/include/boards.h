/* ************************************************************************
*   File: boards.h                                      Part of CircleMUD *
*  Usage: header file for bulletin boards                                 *
*                                                                         *
*  All rights reserved.  See license.doc for complete information.        *
*                                                                         *
*  Copyright (C) 1993, 94 by the Trustees of the Johns Hopkins University *
*  CircleMUD is based on DikuMUD, Copyright (C) 1990, 1991.               *
************************************************************************ */

//
//  File: boards.h                      -- Part of TempusMUD
//
// All modifications and additions are
// Copyright 1998 by John Watson, all rights reserved.
//

#define NUM_OF_BOARD_VNUMS		10	/* change if needed! */
#define NUM_OF_BOARDS	        53	/* change if needed! */
#define MAX_BOARD_MESSAGES 	60	/* arbitrary -- change if needed */
#define MAX_MESSAGE_LENGTH	16384	/* arbitrary -- change if needed */

#define INDEX_SIZE	   ((NUM_OF_BOARDS*MAX_BOARD_MESSAGES) + 5)

#define BOARD_MAGIC	1048575		/* arbitrary number - see modify.c */
#define VOTING_MAGIC 1048574	/* equally arbitrary, but must be lower */

#define LVL_CAN_POST  5

struct board_msginfo {
	int slot_num;				/* pos of message in "master index" */
	char *heading;				/* pointer to message's heading */
	int level;					/* level of poster */
	int heading_len;			/* size of header (for file write) */
	int message_len;			/* size of message text (for file write) */
};

struct board_info_type {
	int vnum[NUM_OF_BOARD_VNUMS];	/* possible vnums of this board */
	int read_lvl;				/* min level to read messages on this board */
	int write_lvl;				/* min level to write messages on this board */
	int remove_lvl;				/* min level to remove messages from this board */
	char filename[50];			/* file to save this board to */
	char groupname[50];			/* group membership required to write on this board. */
};

#define BOARD_VNUM(i, j) (board_info[i].vnum[j])
#define READ_LVL(i) (board_info[i].read_lvl)
#define WRITE_LVL(i) (board_info[i].write_lvl)
#define REMOVE_LVL(i) (board_info[i].remove_lvl)
#define FILENAME(i) (board_info[i].filename)

#define NEW_MSG_INDEX(i) (msg_index[i][num_of_msgs[i]])
#define MSG_HEADING(i, j) (msg_index[i][j].heading)
#define MSG_SLOTNUM(i, j) (msg_index[i][j].slot_num)
#define MSG_LEVEL(i, j) (msg_index[i][j].level)

int Board_display_msg(int board_type, struct char_data *ch,
	struct obj_data *obj, char *arg);
int Board_show_board(int board_type, struct char_data *ch,
	struct obj_data *obj, char *arg);
int Board_remove_msg(int board_type, struct char_data *ch,
	struct obj_data *obj, char *arg);
void Board_save_board(int board_type);
void Board_load_board(int board_type);
void Board_reset_board(int board_num);
void Board_write_message(int board_type, struct char_data *ch,
	struct obj_data *obj, char *arg);
