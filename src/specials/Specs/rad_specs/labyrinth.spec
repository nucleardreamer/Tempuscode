//
// File: labyrinth.spec                     -- Part of TempusMUD
//
// Copyright 1998 by John Watson, all rights reserved.
//

/*****************************************************/

// Don't allow player into labyrinth with equipment or items
SPECIAL(labyrinth_portal)
{
	obj_data *portal = (obj_data *)me;
	room_data *room;
	int idx;

	if (spec_mode != SPECIAL_CMD)
		return 0;

	if (!CMD_IS("enter"))
		return 0;

	skip_spaces(&argument);
	if (!isname(argument, portal->name))
		return 0;

	for (idx = 0; idx < NUM_WEARS;idx++) 
		if (GET_EQ(ch, idx)) {
			send_to_char(ch, "The portal of the labyrinth repulses you.\r\n");
			return 1;
		}

	if (ch->carrying) {
		send_to_char(ch, "The portal of the labyrinth repulses you.\r\n");
		return 1;
	}

	room = real_room(GET_OBJ_VAL(portal, 0));
	if (!room) {
		send_to_char(ch, "The labyrinth is currently closed for remodeling.\r\n");
		return 1;
	}

	send_to_char(ch, "You enter the realm of the labyrinth.\r\n");
	act("$n steps into $p", true, ch, portal, 0, TO_ROOM);
	if (!IS_NPC(ch) && ch->in_room->zone != room->zone)
		room->zone->enter_count++;
	
	char_from_room(ch);
	char_to_room(ch, room);
	act("$n steps out of the portal.", true, ch, 0, 0, TO_ROOM);
	return 1;
}

// This is a fake special, used as a flag to the pendulum_timer mob
// the exits will "spin" clockwise every pendulum tick
SPECIAL(labyrinth_carousel)
{
	return 0;
}

SPECIAL(labyrinth_clock)
{
	/* object vnum is 66000 */

	struct obj_data *clock = (struct obj_data *)me;
	static int clock_status = 0;
	struct room_data *to_room = NULL;

	if (spec_mode != SPECIAL_CMD)
		return 0;

	if (!CMD_IS("enter") && !CMD_IS("wind"))
		return (0);

	skip_spaces(&argument);
	if (!isname(argument, clock->name))
		return 0;

	if (CMD_IS("enter")) {

		send_to_char(ch, "You step into the clock.\r\n\r\n");
		act("$n steps into the clock.", TRUE, ch, 0, 0, TO_ROOM);

		if (clock_status == 0) {

			/* clock is not wound, go to inside of clock */
			to_room = real_room(66103);

		} else if (clock_status >= 4 && clock_status <= 9) {

			/* clock is wound, go to specific room */
			to_room = real_room(66036 + 100 * clock_status);
			/* unwind clock */
			clock_status = 0;

		} else {

			/* clock is wound, go to one of 6 randomly-selected rooms */
			to_room = real_room(66436 + 100 * number(0, 5));
			/* unwind clock */
			clock_status = 0;

		}

		if (!to_room) {
			slog("SYSERR: Improper room in labyrinth.spec");
			return 0;
		}

		char_from_room(ch, false);
		char_to_room(ch, to_room, false);
		look_at_room(ch, ch->in_room, 0);

		act("$n suddenly appears near you.", TRUE, ch, 0, 0, TO_ROOM);
		return 1;
	}

	if (CMD_IS("wind")) {

		/* check ch is holding key */
		if (!ch->equipment[WEAR_HOLD] ||
			(GET_OBJ_VNUM(ch->equipment[WEAR_HOLD]) != 66001)) {
			send_to_char(ch, "You aren't holding the correct key.\r\n");
			return 1;
		}

		send_to_char(ch, "You wind the clock.\r\n");
		act("$n winds the clock.", TRUE, ch, 0, 0, TO_ROOM);
		clock_status++;
		return 1;

	}

	return 0;
}

/*****************************************************/
SPECIAL(cuckoo)
{
	/* mob vnum is 66001 */
	/* procedure to periodically make the cuckoo bird
	   appear near the clock for a little while */

	/* I made this into a character special.  It is called
	   every 10 seconds on the zone pulse.  also called every 2
	   seconds if the mob is in battle... but we return 0
	   if the mob is fighting. (room and obj specs are not
	   pulsed... only called when a command is entered in
	   thier vicinity. */

	struct Creature *bird = (struct Creature *)me;
	struct room_data *r_clock_room = NULL, *to_room = NULL;
	static int room_status = 0;

	if (spec_mode != SPECIAL_TICK)
		return 0;

	if (cmd || FIGHTING(bird) || !AWAKE(bird))
		return 0;

	/* check if cuckoo is in room 66236 */
	if (bird->in_room->number == 66236) {

		/* little check to prevent a crash */
		if ((to_room = real_room(66104)) == NULL) {
			slog("SYSERR: Clock to room nonexistant.  Removing spec.");
			bird->mob_specials.shared->func = NULL;
			return 0;
		}

		act("$n disappears back into the clock.", FALSE, bird, 0, 0, TO_ROOM);
		char_from_room(bird, false);
		char_to_room(bird, to_room, false);
		return 1;
	}

	/* otherwise increment timer (room_status).  when timer expires
	   its time to bring cuckoo back. */

	room_status++;
	if (room_status >= 7) {

		room_status = 0;

		/* check if cuckoo is in room 66104 */
		if (bird->in_room->number == 66104) {

			if ((r_clock_room = real_room(66236)) == NULL) {
				slog("SYSERR: Clock room nonexistant.  Removing spec.");
				bird->mob_specials.shared->func = NULL;
				return 0;
			}

			char_from_room(bird, false);
			char_to_room(bird, r_clock_room, false);
			act("$n suddenly pops out of the clock and says 'Cuckoo! Cuckoo!'.", TRUE, bird, 0, 0, TO_ROOM);
			return 1;
		}
	}
	return 0;
}

/*****************************************************/
SPECIAL(drink_me_bottle)
{
	/* procedure to shrink characters who drink from the rabbit's bottle */
	/* object vnum is 66003 */

	struct affected_type af;
	struct obj_data *bottle = (struct obj_data *)me;

	if (spec_mode != SPECIAL_CMD)
		return 0;

	if (!CMD_IS("drink"))
		return 0;

	if (ch->getPosition() == POS_SLEEPING) {
		send_to_char(ch, "You'll have to wake up first!\r\n");
		return 1;
	}

	/* check here for argument, and contents of bottle */
	skip_spaces(&argument);
	if (!isname(argument, bottle->name) || !GET_OBJ_VAL(bottle, 1))
		return 0;

	send_to_char(ch, "You drink %s from %s.  Mmmm, tasty.\r\n",
		fname(bottle->name), bottle->short_description);
	act("$n drinks from $p.", TRUE, ch, bottle, 0, TO_ROOM);

	/* increment the bottle contents */
	GET_OBJ_VAL(bottle, 1)--;

	if (affected_by_spell(ch, SPELL_SHRINKING)) {
		send_to_char(ch, "Nothing seems to happen.\r\n");
		return 1;
	}

	af.type = SPELL_SHRINKING;
	af.duration = 2;
	af.location = APPLY_CHAR_HEIGHT;
	af.modifier = -(GET_HEIGHT(ch) - 10);
	af.aff_index = 0;
	af.bitvector = 0;

	affect_to_char(ch, &af);

	send_to_char(ch, "You feel very strange... you're SHRINKING!\r\n");
	act("$n shrinks to the size of a small rabbit!", TRUE, ch, 0, ch, TO_ROOM);

	return 1;
}

/*****************************************************/
SPECIAL(rabbit_hole)
{

	struct obj_data *hole = (struct obj_data *)me;
	struct room_data *to_room = NULL;

	skip_spaces(&argument);

	if (!CMD_IS("enter") || !isname(argument, hole->name))
		return (0);

	if (ch->player.height >= 11) {
		send_to_char(ch, "You are way too big to fit in there!\r\n");
		act("$n makes a spectacle of $mself, trying to squeeze into $p.",
			TRUE, ch, hole, 0, TO_ROOM);
		return 1;
	}

	if (ch->in_room->number == 66233)
		to_room = real_room(66234);
	else if (ch->in_room->number == 66234)
		to_room = real_room(66233);
	if (to_room == NULL)
		return 0;

	send_to_char(ch, "You climb through the hole.\r\n\r\n");
	act("$n steps into the hole.", TRUE, ch, 0, 0, TO_ROOM);
	char_from_room(ch, false);
	char_to_room(ch, to_room, false);
	look_at_room(ch, ch->in_room, 0);
	act("$n steps out of the hole.", TRUE, ch, 0, 0, TO_ROOM);

	return 1;
}


/*****************************************************/
SPECIAL(gollum)
{
	/* mob vnum is 66009 */
	/* procedure to make gollum ask his riddle
	   and to release players when riddle is answered */

	/* im making this into a mob special */
	if (spec_mode != SPECIAL_CMD && spec_mode != SPECIAL_TICK)
		return 0;
	struct Creature *gollum = (struct Creature *)me;
	struct room_data *to_room = NULL;

	/* check if gollum is in room and not fighting */
	if (gollum->in_room->number != 66163 || FIGHTING(gollum))
		return 0;

	if (CMD_IS("say") || CMD_IS("'")) {
		skip_spaces(&argument);
		if (!*argument)
			return 0;
		half_chop(argument, buf, buf2);
		if (!strncasecmp(buf, "time", 4)) {

			/* take ch to room 66162 */

			if ((to_room = real_room(66162)) == NULL) {
				slog("SYSERR: Gollum room nonexistant. Removing spec.");
				gollum->mob_specials.shared->func = NULL;
				return 0;
			}

			act("$N starts hopping around and cursing at $n.\r\n"
				"$N stares at $n and gestures with his arms.\r\n",
				TRUE, ch, 0, gollum, TO_ROOM);
			act("$N starts hopping around and cursing at you.\r\n"
				"$N stares at you and gestures with his arms.\r\n",
				TRUE, ch, 0, gollum, TO_CHAR);
			act("$n disappears.", TRUE, ch, 0, 0, TO_ROOM);
			char_from_room(ch, false);
			char_to_room(ch, to_room, false);
			look_at_room(ch, ch->in_room, 0);
			act("$n suddenly appears near you.", TRUE, ch, 0, 0, TO_ROOM);
			return 1;
		}
	}
	if (cmd)
		return 0;

	if (number(0, 5) == 0) {

		do_say(gollum,
			"How about I let you go if you can answer my riddle?'.\r\n"
			"'This thing all things devours: Birds, beasts, trees, flowers;'.\r\n"
			"'Gnaws iron, bites steel; Grinds hard stone to meal;'.\r\n"
			"'Slays king, ruins town, And beats high mountain down.", 0, 0, 0);

		return 1;
	} else if (number(0, 2) == 0) {

		do_say(gollum,
			"Bless us and splash us, my precioussss!'.\r\n"
			"'Look what issssss caught in our trap, gollum!", 0, 0, 0);
		act("$n thinks for a while and asks you, 'Do you like riddles?'.\r\n",
			FALSE, ch, 0, 0, TO_ROOM);

		return 1;
	}
	return 0;
}

void
labyrinth_spin_carousels(zone_data *zone)
{
	room_data *cur_room;
	CreatureList::iterator it;
	room_direction_data *dir_save[NUM_DIRS];

	for (cur_room = zone->world;cur_room->number < zone->top;cur_room = cur_room->next) {
		if (cur_room->func != labyrinth_carousel)
			continue;

		// Spin the room!  Wheee!
		memcpy(dir_save, cur_room->dir_option, sizeof(room_direction_data) * NUM_DIRS);
		cur_room->dir_option[NORTH] = dir_save[WEST];
		cur_room->dir_option[EAST] = dir_save[NORTH];
		cur_room->dir_option[SOUTH] = dir_save[EAST];
		cur_room->dir_option[WEST] = dir_save[SOUTH];

		if (cur_room->people.empty())
			continue;

		// Knock people down and describe if there are observers
		act("The floor lurches beneath your feet!", true, 0, 0, 0, TO_ROOM);
		it = cur_room->people.begin();
		for (;it != cur_room->people.end();++it)
			if (number(1, 26) > GET_DEX(*it)) {
				send_to_char(*it, "You fall to the floor!  Oof!\r\n");
				(*it)->setPosition(POS_SITTING);
			}
	}
}

//*****************************************************
// Pendulum description
// There is a timer mob that does the actual swinging of the pendulum.  It 
// is a mobile primarily because rooms don't receive special ticks.
//
// There is a room special that blocks the exits depending on the state of
// the pendulum.  This special is set on the room containing the pendulum,
// and the adjacent rooms.
//
// The pendulum itself is comprised of two objects, 60007 and 60011.  The
// first blocks the way south, and the second blocks the northern exit.
//
//  room     #1   #2
//			 +-+  +-+
//	60126    |*|  | |
//			 +-+  +-+
//	60136    |*|  |*|
//			 +-+  +-+
//	60146    | |  |*|
//			 +-+  +-+
//

SPECIAL(pendulum_timer_mob)
{
	// mob vnum is 66010
	// procedure to make penddulum swing in room 66136
	if (spec_mode != SPECIAL_CMD && spec_mode != SPECIAL_TICK)
		return 0;

	struct Creature *pendulum_timer_mob = (struct Creature *)me;
	struct obj_data *test_obj = NULL;
	struct Creature *vict = NULL;
	struct room_data *in_room = real_room(66136),
		*to_room = NULL, *from_room = NULL;
	static int pendulum_time = 0;

	if (cmd)
		return 0;
	pendulum_time++;

	if (!in_room)
		return 0;

	if (pendulum_time >= 2) {
		pendulum_time = 0;

		labyrinth_spin_carousels(pendulum_timer_mob->in_room->zone );

		for (test_obj = in_room->contents; test_obj;
			test_obj = test_obj->next_content) {

			if (GET_OBJ_VNUM(test_obj) == 66007) {

				if ((to_room = real_room(66126)) == NULL) {
					slog("SYSERR: pendulum room nonexistant. Removing spec.");
					pendulum_timer_mob->mob_specials.shared->func = NULL;
					return 0;
				}
				if ((from_room = real_room(66146)) == NULL) {
					slog("SYSERR: pendulum room nonexistant. Removing spec.");
					pendulum_timer_mob->mob_specials.shared->func = NULL;
					return 0;
				}

				obj_from_room(test_obj);
				obj_to_room(test_obj, to_room);
				send_to_room
					("The pendulum swings north, clearing the floor only by a foot or two.\r\n",
					in_room);

				room_data *theRoom = in_room;
				CreatureList::iterator it = theRoom->people.begin();
				for (; it != theRoom->people.end(); ++it) {
					vict = *it;
					if (vict->getPosition() > POS_SITTING) {
						send_to_char(vict,
							"You are carried north by the pendulum.\r\n");
						act("$n is pushed from the room by the pendulum.",
							TRUE, vict, 0, 0, TO_ROOM);
						char_from_room(vict, false);
						char_to_room(vict, to_room, false);
						look_at_room(vict, vict->in_room, 0);
						act("$n is pushed into the room by the pendulum.",
							TRUE, vict, 0, 0, TO_ROOM);
					}
				}

				for (test_obj = from_room->contents; test_obj;
					test_obj = test_obj->next_content) {
					if (GET_OBJ_VNUM(test_obj) == 66011) {
						obj_from_room(test_obj);
						obj_to_room(test_obj, in_room);
						break;
					}
				}

				break;
			}

			if (GET_OBJ_VNUM(test_obj) == 66011) {

				if ((to_room = real_room(66146)) == NULL) {
					slog("SYSERR: pendulum room nonexistant. Removing spec.");
					pendulum_timer_mob->mob_specials.shared->func = NULL;
					return 0;
				}
				if ((from_room = real_room(66126)) == NULL) {
					slog("SYSERR: pendulum room nonexistant. Removing spec.");
					pendulum_timer_mob->mob_specials.shared->func = NULL;
					return 0;
				}

				obj_from_room(test_obj);
				obj_to_room(test_obj, to_room);
				send_to_room
					("The pendulum swings south, clearing the floor only by a foot or two.\r\n",
					in_room);

				room_data *theRoom = in_room;
				CreatureList::iterator it = theRoom->people.begin();
				for (; it != theRoom->people.end(); ++it) {
					vict = *it;
					if (vict->getPosition() > POS_SITTING) {
						send_to_char(vict,
							"You are carried south by the pendulum.\r\n");
						act("$n is pushed from the room by the pendulum.",
							TRUE, vict, 0, 0, TO_ROOM);
						char_from_room(vict, false);
						char_to_room(vict, to_room, false);
						look_at_room(vict, vict->in_room, 0);
						act("$n is pushed into the room by the pendulum.",
							TRUE, vict, 0, 0, TO_ROOM);
					}
				}

				for (test_obj = from_room->contents; test_obj;
					test_obj = test_obj->next_content) {
					if (GET_OBJ_VNUM(test_obj) == 66007) {
						obj_from_room(test_obj);
						obj_to_room(test_obj, in_room);
						break;
					}
				}

				break;
			}
		}
	}
	return 1;
}

/*****************************************************/
SPECIAL(pendulum_room)
{
	struct obj_data *test_obj = NULL;

	if (spec_mode != SPECIAL_CMD)
		return 0;

	if (!CMD_IS("south") && !CMD_IS("north"))
		return 0;

	if (CMD_IS("south")) {
		for (test_obj = ch->in_room->contents; test_obj;
			test_obj = test_obj->next_content) {
			if (GET_OBJ_VNUM(test_obj) == 66007) {
				act("$n tries to go south but can't get past the pendulum.\r\n", TRUE, ch, 0, 0, TO_ROOM);
				send_to_char(ch, "The pendulum blocks the way!\r\n");
				return 1;
			}
		}
	}

	if (CMD_IS("north")) {
		for (test_obj = ch->in_room->contents; test_obj;
			test_obj = test_obj->next_content) {
			if (GET_OBJ_VNUM(test_obj) == 66011) {
				act("$n tries to go north but can't get past the pendulum.\r\n", TRUE, ch, 0, 0, TO_ROOM);
				send_to_char(ch, "The pendulum blocks the way!\r\n");
				return 1;
			}
		}
	}

	return 0;
}

/*****************************************************/
SPECIAL(parrot)
{

	struct Creature *parrot = (struct Creature *)me;
	struct Creature *master = parrot->master;

	if (spec_mode != SPECIAL_CMD && spec_mode != SPECIAL_TICK)
		return 0;
	if (!master || !cmd)
		return (0);
	if (!CMD_IS("stun") && !CMD_IS("backstab") && !CMD_IS("steal"))
		return (0);
	skip_spaces(&argument);
	if (!*argument)
		return 0;

	if (CMD_IS("steal")) {
		act("$n says, '$N is a bloody thief!'", TRUE, parrot, 0, ch, TO_ROOM);
		act("You are slapped by $N.", TRUE, ch, 0, parrot, TO_CHAR);
		act("$N slaps $n.", TRUE, ch, 0, parrot, TO_ROOM);
		return (1);
	}

	half_chop(argument, buf, buf2);
	if (strcasecmp(buf, GET_NAME(master)))
		return 0;

	if (CMD_IS("stun")) {
		act("$N tries to stun you!", TRUE, master, 0, ch, TO_CHAR);
		act("You try to stun $N.", TRUE, ch, 0, master, TO_CHAR);
	} else if (CMD_IS("backstab")) {
		act("$N tries to backstab you!", TRUE, master, 0, ch, TO_CHAR);
		act("You try to backstab $N.", TRUE, ch, 0, master, TO_CHAR);
	}
	act("You look at $N and snicker.", TRUE, master, 0, ch, TO_CHAR);
	act("$N looks at you and snickers.", TRUE, ch, 0, master, TO_CHAR);
	return (1);
}

/*****************************************************/
#define NUM_USES(obj)   (GET_OBJ_VAL(obj, 1))
SPECIAL(astrolabe)
{

	struct obj_data *astrolabe = (struct obj_data *)me;
	struct Creature *wearer = astrolabe->worn_by;
	struct room_data *to_room = NULL;

	if (ch != wearer || !IS_OBJ_TYPE(astrolabe, ITEM_OTHER))
		return (0);
	if (!CMD_IS("adjust") && !CMD_IS("use"))
		return (0);

	skip_spaces(&argument);
	if (!isname(argument, astrolabe->name))
		return 0;

	if (CMD_IS("adjust")) {
		act("$n fiddles with some controls on $p.",
			TRUE, ch, astrolabe, 0, TO_ROOM);
		send_to_char(ch,
			"You fiddle with some controls on your astrolabe.\r\n");
		ROOM_NUMBER(astrolabe) = ch->in_room->number;
		sprintf(buf, "$p is good for %d more use%s.",
			MAX(1, NUM_USES(astrolabe)), NUM_USES(astrolabe) > 1 ? "s" : "");
		act(buf, FALSE, ch, astrolabe, 0, TO_CHAR);
	} else if (CMD_IS("use")) {
		act("$n lifts $p and peers through it.", TRUE, ch, astrolabe, 0,
			TO_ROOM);
		send_to_char(ch, "You lift your astrolabe and peer through it.\r\n");

		to_room = real_room(ROOM_NUMBER(astrolabe));

		if (!to_room) {
			send_to_char(ch, "Nothing seems to happen.\r\n");
			return 1;
		}

		if (to_room->zone->plane != ch->in_room->zone->plane ||
			(to_room->zone != ch->in_room->zone &&
				(ZONE_FLAGGED(to_room->zone, ZONE_ISOLATED) ||
					ZONE_FLAGGED(ch->in_room->zone, ZONE_ISOLATED))) ||
			ROOM_FLAGGED(to_room, ROOM_NOTEL | ROOM_NORECALL | ROOM_NOMAGIC) ||
			ROOM_FLAGGED(ch->in_room, ROOM_NORECALL | ROOM_NOMAGIC)) {
			send_to_char(ch,
				"You are unable to see your destination from here.\r\n");
			return 1;
		}

		char_from_room(ch, false);
		char_to_room(ch, to_room, false);
		look_at_room(ch, ch->in_room, 0);
		act("$n suddenly appears near you.", TRUE, ch, 0, 0, TO_ROOM);
		NUM_USES(astrolabe)--;
		if (NUM_USES(astrolabe) <= 0) {
			act("$p slowly fades from existance.", FALSE, ch, astrolabe, 0,
				TO_CHAR);
			extract_obj(astrolabe);
		}
	}
	return 1;
}
