/*************************************************************************
*   File: fight.c                                       Part of CircleMUD *
*  Usage: Combat system                                                   *
*                                                                         *
*  All rights reserved.  See license.doc for complete information.        *
*                                                                         *
*  Copyright ( C ) 1993, 94 by the Trustees of the Johns Hopkins University *
*  CircleMUD is based on DikuMUD, Copyright ( C ) 1990, 1991.               *
************************************************************************ */

//
// File: fight.c                      -- Part of TempusMUD
//
// All modifications and additions are
// Copyright 1998 by John Watson, all rights reserved.
//

#ifdef HAS_CONFIG_H
#include "config.h"
#endif

#define __fight_c__
#define __combat_code__

#include <signal.h>

#include "structs.h"
#include "utils.h"
#include "comm.h"
#include "handler.h"
#include "interpreter.h"
#include "db.h"
#include "spells.h"
#include "screen.h"
#include "char_class.h"
#include "vehicle.h"
#include "materials.h"
#include "flow_room.h"
#include "fight.h"
#include "bomb.h"
#include "guns.h"
#include "specs.h"
#include "mobact.h"
#include "security.h"
#include "quest.h"
#include "utils.h"
#include "prog.h"
#include "weather.h"

#define BAD_ATTACK_TYPE(attacktype) (attacktype == TYPE_BLEED || \
                                     attacktype == SPELL_POISON || \
                                     attacktype == TYPE_ABLAZE || \
                                     attacktype == TYPE_ACID_BURN || \
                                     attacktype == TYPE_TAINT_BURN || \
                                     attacktype == TYPE_PRESSURE || \
                                     attacktype == TYPE_SUFFOCATING || \
                                     attacktype == TYPE_ANGUISH || \
                                     attacktype == TYPE_OVERLOAD || \
                                     attacktype == TYPE_SUFFERING || \
                                     attacktype == SPELL_STIGMATA || \
                                     attacktype == TYPE_DROWNING || \
                                     attacktype == SPELL_SICKNESS || \
                                     attacktype == TYPE_RAD_SICKNESS || \
                                     attacktype == SKILL_HOLY_TOUCH)

int corpse_state = 0;

/* The Fight related routines */
struct obj_data *get_random_uncovered_implant(struct creature * ch, int type);
int calculate_weapon_probability(struct creature *ch, int prob,
	struct obj_data *weap);
int do_combat_fire(struct creature *ch, struct creature *vict);
int do_casting_weapon(struct creature *ch, struct obj_data *weap);
int calculate_attack_probability(struct creature *ch);
void do_emp_pulse_char(struct creature *ch, struct creature *vict);
void perform_autoloot(struct creature *ch, struct obj_data *corpse);

/*
   corrects position and removes combat related bits.
   Call ONLY from removeCombat()/removeAllCombat()
*/
void
remove_fighting_affects(struct creature *ch)
{
	if (ch->in_room && room_is_open_air(ch->in_room)) {
		GET_POSITION(ch) = POS_FLYING;
	} else if (!IS_NPC(ch)) {
		if (GET_POSITION(ch) >= POS_FIGHTING)
			GET_POSITION(ch) = POS_STANDING;
		else if (GET_POSITION(ch) >= POS_RESTING)
			GET_POSITION(ch) = POS_SITTING;
	} else {
		if (AFF_FLAGGED(ch, AFF_CHARM) && IS_UNDEAD(ch))
			GET_POSITION(ch) = POS_STANDING;
		else if (GET_POSITION(ch) > POS_SITTING)
			GET_POSITION(ch) = POS_STANDING;
	}

	update_pos(ch);

}

/* When ch kills victim */
void
change_alignment(struct creature *ch, struct creature *victim)
{
	GET_ALIGNMENT(ch) += -(GET_ALIGNMENT(victim) / 100);
	GET_ALIGNMENT(ch) = MAX(-1000, GET_ALIGNMENT(ch));
	GET_ALIGNMENT(ch) = MIN(1000, GET_ALIGNMENT(ch));
	check_eq_align(ch);
}

void
raw_kill(struct creature *ch, struct creature *killer, int attacktype)
{
    struct obj_data *corpse;

	if (attacktype != SKILL_GAROTTE)
		death_cry(ch);

    trigger_prog_dying(ch, killer);

    // Handle dying progs before creating the corpse
    if (GET_ROOM_PROG(ch->in_room) != NULL)
	    trigger_prog_death(ch->in_room, PROG_TYPE_ROOM, ch);

    void trigger_mobile_prog(struct creature *tch, gpointer ignore) {
		if (GET_MOB_PROGOBJ(tch) != NULL && tch != ch)
            trigger_prog_death(tch, PROG_TYPE_MOBILE, ch);
    }
    g_list_foreach(ch->in_room->people, trigger_mobile_prog, 0);

    // Create the corpse itself
	corpse = make_corpse(ch, killer, attacktype);

	struct affected_type *af = ch->affected;
    while (af) {
        if (clearAtDeath(af)) {
            affect_remove(ch, af);
            af = ch->affected;
        } else {
            af = af->next;
        }
    }

	REMOVE_BIT(AFF2_FLAGS(ch), AFF2_PETRIFIED);

	if (IS_NPC(ch))
		ch->mob_specials.shared->kills++;

	// Equipment dealt with in make_corpse.
	// Do not save it here.
	if (is_arena_combat(killer, ch))
		arena_die(ch);
    else if (is_npk_combat(killer, ch) && !ROOM_FLAGGED(ch->in_room, ROOM_DEATH))
        npk_die(ch);
	else
		creature_die(ch);

    if (killer && killer != ch && PRF2_FLAGGED(killer, PRF2_AUTOLOOT))
        perform_autoloot(killer, corpse);
}

extern bool LOG_DEATHS;

void
die(struct creature *ch, struct creature *killer, int attacktype)
{
	if (IS_NPC(ch) && GET_MOB_SPEC(ch)) {
		if (GET_MOB_SPEC(ch) (killer, ch, 0, NULL, SPECIAL_DEATH)) {
			mudlog(LVL_CREATOR, NRM, true,
				"ERROR: Mobile special for %s run in place of standard extraction.\n",
				GET_NAME(ch));
			return;
		}
	}

	if( LOG_DEATHS ) {
		if( IS_NPC(ch) ) {
			slog("DEATH: %s killed by %s. attacktype: %d SPEC[%p]",
				GET_NAME(ch),
				killer ? GET_NAME(killer) : "(NULL)",
				attacktype,
				GET_MOB_SPEC(ch));
		} else {
			slog("DEATH: %s killed by %s. attacktype: %d PC",
				GET_NAME(ch),
				killer ? GET_NAME(killer) : "(NULL)",
				attacktype);
		}
	}

	if( IS_PC(ch) && !is_arena_combat(killer, ch) && killer != NULL &&
			!PLR_FLAGGED(killer, PLR_KILLER) && !isNewbie(ch) ) {
		// exp loss capped at the beginning of the level.
		int loss = GET_EXP(ch) / 8;

		loss = MIN( loss, GET_EXP(ch) - exp_scale[(int)GET_LEVEL(ch)] );
		gain_exp(ch, -loss);
	}

	if (PLR_FLAGGED(ch, PLR_KILLER | PLR_THIEF) &&
			GET_LEVEL(ch) < LVL_AMBASSADOR)
        punish_killer_death(ch);

	if (!IS_NPC(ch) && ((!ch->in_room) || !is_arena_combat(killer, ch))) {
		if (ch != killer)
			REMOVE_BIT(PLR_FLAGS(ch), PLR_KILLER | PLR_THIEF);

		if (GET_LEVEL(ch) > 10 && !IS_NPC(ch) && (!killer || IS_NPC(killer))) {
			if (GET_LIFE_POINTS(ch) <= 0 && GET_MAX_HIT(ch) <= 1) {

				if (IS_EVIL(ch) || IS_NEUTRAL(ch))
					send_to_char(ch,
						"Your soul screeches in agony as it's torn from the mortal realms... forever.\r\n");

				else if (IS_GOOD(ch))
					send_to_char(ch,
						"The righteous rejoice as your soul departs the mortal realms... forever.\r\n");

				SET_BIT(PLR2_FLAGS(ch), PLR2_BURIED);
				mudlog(LVL_GOD, NRM, true,
					"%s died with no maxhit and no life points. Burying.",
					GET_NAME(ch));

			} else if (GET_LIFE_POINTS(ch) > 0) {
				GET_LIFE_POINTS(ch) =
					MAX(0, GET_LIFE_POINTS(ch) - number(1, (GET_LEVEL(ch) / 8)));
			} else if (!number(0, 3)) {
				GET_CON(ch) = MAX(3, GET_CON(ch) - 1);
			} else if (GET_LEVEL(ch) > number(20, 50)) {
				GET_MAX_HIT(ch) = MAX(1, GET_MAX_HIT(ch) - dice(3, 5));
			}
		}
		if (IS_CYBORG(ch)) {
			GET_TOT_DAM(ch) = 0;
			GET_BROKE(ch) = 0;
		}
		GET_PC_DEATHS(ch)++;

        // Tally kills for quest purposes
        if (GET_QUEST(ch)) {
            struct quest *quest;

            quest = quest_by_vnum(GET_QUEST(ch));
            if (quest)
                tallyDeath(quest, GET_IDNUM(ch));
        }
	}

	REMOVE_BIT(AFF2_FLAGS(ch), AFF2_ABLAZE);
	REMOVE_BIT(AFF3_FLAGS(ch), AFF3_SELF_DESTRUCT);
	raw_kill(ch, killer, attacktype);	// die
}

void perform_gain_kill_exp(struct creature *ch, struct creature *victim,
                           float multiplier);

void
group_gain(struct creature *ch, struct creature *victim)
{
	struct creature *leader;
	int total_levs = 0;
	int total_pc_mems = 0;
	float mult = 0;
	float mult_mod = 0;

	if (!(leader = ch->master))
		leader = ch;
    void count_pc_members(struct creature *tch, gpointer ignore) {
		if (AFF_FLAGGED(tch, AFF_GROUP) && (tch == leader
		|| leader == tch->master))
		{
			total_levs += GET_LEVEL(tch);
			if( IS_PC(tch) ) {
				total_levs += GET_REMORT_GEN(tch) * 8;
				total_pc_mems++;
			}
		}
	}
    g_list_foreach(ch->in_room->people, count_pc_members, 0);

    void dole_out_exp(struct creature *tch, gpointer ignore) {
		if (AFF_FLAGGED(tch, AFF_GROUP) &&
            (tch != victim) &&
            (tch == leader || leader == tch->master)) {
			mult = (float)GET_LEVEL(tch);
			if( IS_PC( tch ) )
				mult += GET_REMORT_GEN(tch) * 8;
			mult /= (float)total_levs;

			if (total_pc_mems) {
				mult_mod = 1 - mult;
				mult_mod *= mult;
				send_to_char(tch, "Your group gain is %d%% + bonus %d%%.\n",
					(int)((float)mult * 100), (int)((float)mult_mod * 100));
			}

			perform_gain_kill_exp(tch, victim, mult+mult_mod);
		}
	}
    g_list_foreach(ch->in_room->people, dole_out_exp, 0);
}

struct kill_record *tally_kill_record(struct creature *ch, struct creature *victim);

void
perform_gain_kill_exp(struct creature *ch, struct creature *victim,
                      float multiplier)
{
    int explore_bonus = 0;
	int exp = 0;

	if (IS_NPC(victim))
		exp = (int)MIN(max_exp_gain, (GET_EXP(victim) * multiplier) / 3);
	else
		exp = (int)MIN(max_exp_gain, (GET_EXP(victim) * multiplier) / 8);

	/* Calculate level-difference bonus */
	if (IS_NPC(ch))
		exp += MAX(0, (exp * MIN(4, (GET_LEVEL(victim) - GET_LEVEL(ch)))) / 8);
	else
		exp += MAX(0, (exp * MIN(8, (GET_LEVEL(victim) - GET_LEVEL(ch)))) / 8);

	exp = MAX(exp, 1);
	exp = MIN(max_exp_gain, exp);

	if (!IS_NPC(ch)) {
		exp = MIN(((exp_scale[(int)(GET_LEVEL(ch) + 1)] -
					exp_scale[(int)GET_LEVEL(ch)]) / 8), exp);
		/* exp is limited to 12.5% of needed from level to ( level + 1 ) */

		if ((exp + GET_EXP(ch)) > exp_scale[GET_LEVEL(ch) + 2])
			exp =
				(((exp_scale[GET_LEVEL(ch) + 2] - exp_scale[GET_LEVEL(ch) + 1])
					/ 2) + exp_scale[GET_LEVEL(ch) + 1]) - GET_EXP(ch);

        if (!IS_NPC(victim)) {
			exp /= 256;
		}

        if (is_arena_combat(ch, victim) ||
            ROOM_FLAGGED(victim->in_room, ROOM_HOUSE) ||
            ROOM_FLAGGED(victim->in_room, ROOM_CLAN_HOUSE))
        {
			exp = 1;
        }

	}

	exp = getPenalizedExperience(ch, exp, victim );

    if (IS_PC(ch) && IS_NPC(victim)) {
        struct kill_record *kill = tally_kill_record(ch, victim);

        exp += calc_explore_bonus(kill, exp);
    }

	if (IS_NPC(victim) && !IS_NPC(ch)
		&& (GET_EXP(victim) < 0 || exp > 5000000)) {
		slog("%s Killed %s(%d) for exp: %d.", GET_NAME(ch),
			GET_NAME(victim), GET_EXP(victim), exp);
	}
	if (exp > 0) {
        if (explore_bonus)
            send_to_char(ch, "%sYou've received an exploration bonus!%s\r\n",
                         CCYEL(ch, C_NRM), CCNRM(ch, C_NRM));
		send_to_char(ch, "%s%sYou have gained %d experience.%s\r\n",
			CCYEL(ch, C_NRM), CCBLD(ch, C_CMP), exp, CCNRM(ch, C_SPR));
	} else if (exp < 0) {
		send_to_char(ch, "%s%sYou have lost experience.%s\r\n",
			CCYEL(ch, C_NRM), CCBLD(ch, C_CMP), CCNRM(ch, C_SPR));
	} else
		send_to_char(ch, "You have gained trivial experience.\r\n");

	gain_exp(ch, exp);
	change_alignment(ch, victim);

}

void
gain_kill_exp(struct creature *ch, struct creature *victim)
{

	if (ch == victim)
		return;

	if (IS_NPC(victim) && MOB2_FLAGGED(victim, MOB2_UNAPPROVED)
		&& !isTester(ch))
		return;

	if ((IS_NPC(ch) && IS_PET(ch)) || (IS_NPC(victim) && IS_PET(victim)))
		return;

	if (AFF_FLAGGED(ch, AFF_GROUP)) {
		group_gain(ch, victim);
		return;
	}

    int distance = find_distance(ch->in_room, victim->in_room);
    if ((distance > 2) || (distance < 0))
        return;

	perform_gain_kill_exp(ch, victim, 1);
}

void
eqdam_extract_obj(struct obj_data *obj)
{

	struct obj_data *inobj = NULL, *next_obj = NULL;

	for (inobj = obj->contains; inobj; inobj = next_obj) {
		next_obj = inobj->next_content;

		obj_from_obj(inobj);

		if (IS_IMPLANT(inobj))
			SET_BIT(GET_OBJ_WEAR(inobj), ITEM_WEAR_TAKE);

		if (obj->in_room)
			obj_to_room(inobj, obj->in_room);
		else if (obj->worn_by)
			obj_to_char(inobj, obj->worn_by);
		else if (obj->carried_by)
			obj_to_char(inobj, obj->carried_by);
		else if (obj->in_obj)
			obj_to_obj(inobj, obj->in_obj);
		else {
			errlog("weird bogus shit.");
			extract_obj(inobj);
		}
	}
	extract_obj(obj);
}

struct obj_data *
destroy_object(struct creature *ch, struct obj_data *obj, int type)
{
	struct obj_data *new_obj = NULL, *inobj = NULL;
	struct room_data *room = NULL;
	struct creature *vict = NULL;
	const char *mat_name;
	const char *msg = NULL;
	int tmp;

	if (GET_OBJ_MATERIAL(obj))
		mat_name = material_names[GET_OBJ_MATERIAL(obj)];
	else
		mat_name = "material";

	new_obj = create_obj();
	new_obj->shared = null_obj_shared;
	GET_OBJ_MATERIAL(new_obj) = GET_OBJ_MATERIAL(obj);
	GET_OBJ_WEIGHT(new_obj) = GET_OBJ_WEIGHT(obj);
	GET_OBJ_TYPE(new_obj) = ITEM_TRASH;
	GET_OBJ_WEAR(new_obj) = ITEM_WEAR_TAKE;
	GET_OBJ_EXTRA(new_obj) = ITEM_NODONATE + ITEM_NOSELL;
	GET_OBJ_VAL(new_obj, 0) = obj->shared->vnum;
	GET_OBJ_MAX_DAM(new_obj) = 100;
	GET_OBJ_DAM(new_obj) = 100;
	if (type == SPELL_OXIDIZE && IS_FERROUS(obj)) {
		msg = "$p dissolves into a pile of rust!!";
		new_obj->aliases = strdup("pile rust");
		new_obj->name = strdup("a pile of rust");
		new_obj->line_desc = strdup(tmp_capitalize(tmp_strcat(new_obj->name, " is lying here.")));
		GET_OBJ_MATERIAL(new_obj) = MAT_RUST;
	} else if (type == SPELL_OXIDIZE && IS_BURNABLE_TYPE(obj)) {
		msg = "$p is incinerated!!";
		new_obj->aliases = strdup("pile ash");
		new_obj->name = strdup("a pile of ash");
		new_obj->line_desc = strdup(tmp_capitalize(tmp_strcat(new_obj->name, " is lying here.")));
		GET_OBJ_MATERIAL(new_obj) = MAT_ASH;

	} else if (type == SPELL_BLESS) {
		msg = "$p glows bright blue and shatters to pieces!!";
		new_obj->aliases = strdup(tmp_sprintf("%s %s shattered fragments",
			material_names[GET_OBJ_MATERIAL(obj)],
			obj->aliases));
		new_obj->name = strdup(tmp_strcat(
			"shattered fragments of ", mat_name));
		new_obj->line_desc = strdup(tmp_capitalize(
			tmp_strcat(new_obj->name, " are lying here.")));
	} else if (type == SPELL_DAMN) {
		msg = "$p glows bright red and shatters to pieces!!";
		new_obj->aliases = strdup(tmp_sprintf("%s %s shattered fragments",
			material_names[GET_OBJ_MATERIAL(obj)],
			obj->aliases));
		new_obj->name = strdup(tmp_strcat(
			"shattered pieces of ", mat_name));
		new_obj->line_desc = strdup(tmp_capitalize(
			tmp_strcat(new_obj->name, " are lying here.")));
	} else if (IS_METAL_TYPE(obj)) {
		msg = "$p is reduced to a mangled pile of scrap!!";
		new_obj->aliases = strdup(tmp_sprintf("%s %s mangled heap",
			material_names[GET_OBJ_MATERIAL(obj)],
			obj->aliases));
		new_obj->name = strdup(tmp_strcat("a mangled heap of ", mat_name));
		new_obj->line_desc = strdup(tmp_capitalize(tmp_strcat(new_obj->name, " is lying here.")));

	} else if (IS_STONE_TYPE(obj) || IS_GLASS_TYPE(obj)) {
		msg = "$p shatters into a thousand fragments!!";
		new_obj->aliases = strdup(tmp_sprintf("%s %s shattered fragments",
			material_names[GET_OBJ_MATERIAL(obj)],
			obj->aliases));
		new_obj->name = strdup(tmp_strcat(
			"shattered fragments of ", mat_name));
		new_obj->line_desc = strdup(tmp_capitalize(
			tmp_strcat(new_obj->name, " are lying here.")));

	} else {
		msg =  "$p has been destroyed!";
		new_obj->aliases = strdup(tmp_sprintf("%s %s mutilated heap",
			material_names[GET_OBJ_MATERIAL(obj)],
			obj->aliases));
		new_obj->name = strdup(tmp_strcat(
			"a mutilated heap of ", mat_name));
		new_obj->line_desc = strdup(tmp_capitalize(
			tmp_strcat(new_obj->name, " is lying here.")));

		if (IS_CORPSE(obj)) {
			GET_OBJ_TYPE(new_obj) = ITEM_CONTAINER;
			GET_OBJ_VAL(new_obj, 0) = GET_OBJ_VAL(obj, 0);
			GET_OBJ_VAL(new_obj, 1) = GET_OBJ_VAL(obj, 1);
			GET_OBJ_VAL(new_obj, 2) = GET_OBJ_VAL(obj, 2);
			GET_OBJ_VAL(new_obj, 3) = GET_OBJ_VAL(obj, 3);
			GET_OBJ_TIMER(new_obj) = GET_OBJ_TIMER(obj);
            if (CORPSE_IDNUM( obj ) > 0) {
                mudlog(LVL_DEMI, CMP, true, "%s destroyed by %s", obj->name, GET_NAME(ch));
            }
		}
	}

	if (IS_OBJ_STAT2(obj, ITEM2_IMPLANT))
		SET_BIT(GET_OBJ_EXTRA2(new_obj), ITEM2_IMPLANT);

	if ((room = obj->in_room) && (vict = obj->in_room->people)) {
		act(msg, false, vict, obj, 0, TO_CHAR);
		act(msg, false, vict, obj, 0, TO_ROOM);
	} else if ((vict = obj->worn_by))
		act(msg, false, vict, obj, 0, TO_CHAR);
	else
		act(msg, false, ch, obj, 0, TO_CHAR);

	if (!obj->shared->proto) {
		eqdam_extract_obj(obj);
		extract_obj(new_obj);
		return NULL;
	}

	if ((vict = obj->worn_by) || (vict = obj->carried_by)) {
		if (obj->worn_by && (obj != GET_EQ(obj->worn_by, obj->worn_on))) {
			tmp = obj->worn_on;
			eqdam_extract_obj(obj);
			if (equip_char(vict, new_obj, tmp, EQUIP_IMPLANT))
				return (new_obj);
		} else {
			eqdam_extract_obj(obj);
			obj_to_char(new_obj, vict);
		}
	} else if (room) {
		eqdam_extract_obj(obj);
		obj_to_room(new_obj, room);
	} else if ((inobj = obj->in_obj)) {
		eqdam_extract_obj(obj);
		obj_to_obj(new_obj, inobj);
	} else {
		errlog("Improper location of obj and new_obj in damage_eq.");
		eqdam_extract_obj(obj);
	}
	return (new_obj);
}

struct obj_data *
damage_eq(struct creature *ch, struct obj_data *obj, int eq_dam, int type)
{
	struct creature *vict = NULL;
	struct obj_data *inobj = NULL, *next_obj = NULL;

    /* test to see if item should take damage */
	if ((GET_OBJ_TYPE(obj) == ITEM_MONEY) || GET_OBJ_DAM(obj) < 0 ||
        GET_OBJ_MAX_DAM(obj) < 0 ||
		(ch && GET_LEVEL(ch) < LVL_IMMORT && !CAN_WEAR(obj, ITEM_WEAR_TAKE)) ||
		(ch && ch->in_room && ROOM_FLAGGED(ch->in_room, ROOM_ARENA)) ||
		(obj->in_room && ROOM_FLAGGED(obj->in_room, ROOM_ARENA)) ||
        	(obj->worn_by && GET_QUEST(obj->worn_by) &&
	 		QUEST_FLAGGED(quest_by_vnum(GET_QUEST(obj->worn_by)), QUEST_ARENA)) ||
		(GET_OBJ_TYPE(obj) == ITEM_KEY) || (GET_OBJ_TYPE(obj) == ITEM_SCRIPT)
		|| obj->in_room == zone_table->world)
		return NULL;

	/** damage has destroyed object */
	if ((GET_OBJ_DAM(obj) - eq_dam) < (GET_OBJ_MAX_DAM(obj) / 32)) {
		/* damage interior items */
		for (inobj = obj->contains; inobj; inobj = next_obj) {
			next_obj = inobj->next_content;

			damage_eq(NULL, inobj, (eq_dam / 2), type);
		}

		return destroy_object(ch, obj, type);
	}

    const char *damage_msg = NULL;

	if ((GET_OBJ_DAM(obj) > (GET_OBJ_MAX_DAM(obj) / 8))
			&& (GET_OBJ_DAM(obj) - eq_dam) < (GET_OBJ_MAX_DAM(obj) / 8)) {
		/* object has reached broken state */
		SET_BIT(GET_OBJ_EXTRA2(obj), ITEM2_BROKEN);
		damage_msg = "$p has been severely damaged!!";
	} else if ((GET_OBJ_DAM(obj) > (GET_OBJ_MAX_DAM(obj) / 4))
			&& ((GET_OBJ_DAM(obj) - eq_dam) < (GET_OBJ_MAX_DAM(obj) / 4))) {
		/* object looking rough ( 25% ) */
		damage_msg = tmp_sprintf("$p is starting to look pretty %s.",
                                 IS_METAL_TYPE(obj) ? "mangled" :
                                 (IS_LEATHER_TYPE(obj) || IS_CLOTH_TYPE(obj)) ? "ripped up" :
                                 "bad");
	} else if ((GET_OBJ_DAM(obj) > (GET_OBJ_MAX_DAM(obj) / 2)) &&
		((GET_OBJ_DAM(obj) - eq_dam) < (GET_OBJ_MAX_DAM(obj) / 2))) {
		/* object starting to wear ( 50% ) */
		damage_msg = "$p is starting to show signs of wear.";
	} else {
		/* just tally the damage and end */
		GET_OBJ_DAM(obj) -= eq_dam;
		return NULL;
	}

	/* send out messages and unequip if needed */
	if (obj->in_room && (vict = obj->in_room->people)) {
		act(damage_msg, false, vict, obj, 0, TO_CHAR);
		act(damage_msg, false, vict, obj, 0, TO_ROOM);
	} else if ((vict = obj->worn_by)) {
		act(damage_msg, false, vict, obj, 0, TO_CHAR);
		if (IS_OBJ_STAT2(obj, ITEM2_BROKEN)) {
			if (obj == GET_EQ(vict, obj->worn_on))
				obj_to_char(unequip_char(vict, obj->worn_on, EQUIP_WORN), vict);
			else {
				if (IS_DEVICE(obj))
					ENGINE_STATE(obj) = 0;
			}
		}
	} else
		act(damage_msg, false, ch, obj, 0, TO_CHAR);

	GET_OBJ_DAM(obj) -= eq_dam;
	return NULL;
}

int
SWAP_DAM_RETVAL(int val)
{
	return ((val & DAM_VICT_KILLED) ? DAM_ATTACKER_KILLED : 0) |
		((val & DAM_ATTACKER_KILLED) ? DAM_VICT_KILLED : 0);
}

//
// wrapper for damage() for damaging the attacker (swaps return values automagically)
//

inline int
damage_attacker(struct creature *ch, struct creature *victim, int dam,
	int attacktype, int location)
{
	int retval = damage(ch, victim, dam, attacktype, location);
	retval = SWAP_DAM_RETVAL(retval);
	return retval;
}

#define DAM_RETURN( flags ) { cur_weap = 0; return flags; }

//
// damage( ) returns true on a kill, false otherwise
// damage(  ) MUST return with DAM_RETURN(  ) macro !!!
// the return value bits can be a combination of:
// DAM_VICT_KILLED
// DAM_ATTACKER_KILLED
// or DAM_ATTACK_FAILED

int
damage(struct creature *ch, struct creature *victim, int dam,
	int attacktype, int location)
{
	int hard_damcap, is_humil = 0, eq_dam = 0, weap_dam = 0, i, impl_dam =
		0, mana_loss, feedback_dam, feedback_mana;
	struct obj_data *obj = NULL, *weap = cur_weap, *impl = NULL;
	struct room_affect_data rm_aff;
	struct affected_type *af = NULL;
	bool deflected = false;
    bool mshield_hit = false;
	struct creature *original_ch;

	original_ch = ch;

    memset(&rm_aff, 0x0, sizeof(struct room_affect_data));
	if (GET_POSITION(victim) <= POS_DEAD) {
		errlog("Attempt to damage a corpse--ch=%s,vict=%s,type=%d.",
			ch ? GET_NAME(ch) : "NULL", GET_NAME(victim), attacktype);
		DAM_RETURN(DAM_VICT_KILLED);
	}

	if (victim->in_room == NULL) {
		errlog("Attempt to damage a char with null in_room ch=%s,vict=%s,type=%d.",
			ch ? GET_NAME(ch) : "NULL", GET_NAME(victim), attacktype);
		raise(SIGSEGV);
	}
	// No more shall anyone be damaged in the void.
	if (victim->in_room == zone_table->world) {
		return DAM_ATTACK_FAILED;
	}

	if (GET_HIT(victim) < -10) {
		errlog("Attempt to damage a char with hps %d ch=%s,vict=%s,type=%d.",
			GET_HIT(victim), ch ? GET_NAME(ch) : "NULL", GET_NAME(victim),
			attacktype);
		DAM_RETURN(DAM_VICT_KILLED);
	}

	if (ch && (PLR_FLAGGED(victim, PLR_MAILING) ||
			PLR_FLAGGED(victim, PLR_WRITING) ||
			PLR_FLAGGED(victim, PLR_OLC)) && ch != victim) {
		mudlog(GET_INVIS_LVL(ch), BRF, true,
			"%s has attacked %s while writing at %d.", GET_NAME(ch),
			GET_NAME(victim), ch->in_room->number);
		removeCombat(ch, victim);
		removeAllCombat(victim);
		send_to_char(ch, "NO!  Do you want to be ANNIHILATED by the gods?!\r\n");
		DAM_RETURN(DAM_ATTACK_FAILED);
	}

	if (ch) {
		if( ( IS_NPC(ch) || IS_NPC(victim) ) &&
			  affected_by_spell(ch, SPELL_QUAD_DAMAGE) ) {
			dam *= 4;
		} else if (AFF3_FLAGGED(ch, AFF3_DOUBLE_DAMAGE)) {
			dam *= 2;
		}
		if (AFF3_FLAGGED(ch, AFF3_INST_AFF)) {	// In combat instant affects
			// Charging into combat gives a damage bonus
			if ((af = affected_by_spell(ch, SKILL_CHARGE))) {
				dam += (dam * af->modifier / 10);
			}
		}
		if ((af = affected_by_spell(ch, SPELL_SANCTIFICATION))) {
			if (IS_EVIL(victim) && !IS_SOULLESS(victim))
				dam += (dam * GET_REMORT_GEN(ch)) / 20;
			else if (ch != victim && IS_GOOD(victim)) {
				send_to_char(ch, "You have been de-sanctified!\r\n");
				affect_remove(ch, af);
			}
		}
	}

	if (CANNOT_DAMAGE(ch, victim, weap, attacktype))
		dam = 0;

	/* vendor protection */
	if (!ok_damage_vendor(ch, victim)) {
		DAM_RETURN(0); //we don't want to return DAM_ATTACK_FAILED because we shouldn't interfere with mass attacks
	}

	/* newbie protection and PLR_NOPK check */
	if (ch && ch != victim && ((!IS_NPC(ch) && !IS_NPC(victim)) ||
        (!IS_NPC(victim) && IS_NPC(ch) && ch->master && !IS_NPC(ch->master))))
	{
		if(!is_arena_combat(ch, victim)) {
			if (PLR_FLAGGED(ch, PLR_NOPK)) {
				send_to_char(ch, "A small dark shape flies in from the future and sticks to your eyebrow.\r\n");
				DAM_RETURN(DAM_ATTACK_FAILED);
			}
			if (PLR_FLAGGED(victim, PLR_NOPK)) {
				send_to_char(ch, "A small dark shape flies in from the future and sticks to your nose.\r\n");
				DAM_RETURN(DAM_ATTACK_FAILED);
			}

            if (checkReputations(ch, victim))
                DAM_RETURN(DAM_ATTACK_FAILED);
		}

		if (isNewbie(victim) &&
			!is_arena_combat(ch, victim) &&
			GET_LEVEL(ch) < LVL_IMMORT) {
			act("$N is currently under new character protection.",
				false, ch, 0, victim, TO_CHAR);
			act("You are protected by the gods against $n's attack!",
				false, ch, 0, victim, TO_VICT);
			slog("%s protected against %s ( damage ) at %d\n",
				GET_NAME(victim), GET_NAME(ch), victim->in_room->number);
            removeCombat(victim, ch);
            removeCombat(ch, victim);
			DAM_RETURN(DAM_ATTACK_FAILED);
		}

		if (isNewbie(ch) &&
			!is_arena_combat(ch, victim)) {
			send_to_char(ch,
				"You are currently under new player protection, which expires at level 41.\r\n"
				"You cannot attack other players while under this protection.\r\n");
			DAM_RETURN(DAM_ATTACK_FAILED);
		}
	}

	if (GET_POSITION(victim) < POS_FIGHTING)
		dam += (dam * (POS_FIGHTING - GET_POSITION(victim))) / 3;

	if (ch) {
		if (MOB2_FLAGGED(ch, MOB2_UNAPPROVED) && !isTester(victim))
			dam = 0;

		if (isTester(ch) && !IS_MOB(victim) && !isTester(victim))
			dam = 0;

		if (IS_MOB(victim) && GET_LEVEL(ch) >= LVL_AMBASSADOR &&
				GET_LEVEL(ch) < LVL_TIMEGOD && !mini_mud)
			dam = 0;

		if (victim->master == ch)
			stop_follower(victim);
		appear(ch, victim);
	}

	if (attacktype < MAX_SPELLS) {
		if (SPELL_IS_PSIONIC(attacktype) &&
			affected_by_spell(victim, SPELL_PSYCHIC_RESISTANCE))
			dam /= 2;
		if ((SPELL_IS_MAGIC(attacktype) || SPELL_IS_DIVINE(attacktype)) &&
			affected_by_spell(victim, SPELL_MAGICAL_PROT))
			dam -= dam / 4;
	}

	if (attacktype == SPELL_PSYCHIC_CRUSH)
		location = WEAR_HEAD;

	if (attacktype == TYPE_ACID_BURN && location == -1) {
		for (i = 0; i < NUM_WEARS; i++) {
			if (GET_EQ(victim, i))
				damage_eq(ch, GET_EQ(victim, i), (dam / 2), attacktype);
			if (GET_IMPLANT(victim, i))
				damage_eq(ch, GET_IMPLANT(victim, i), (dam / 2), attacktype);
		}
	}

	if (ch
			&& IS_WEAPON(attacktype)
			&& GET_EQ(victim, WEAR_SHIELD)
			&& CHECK_SKILL(victim, SKILL_SHIELD_MASTERY) > 20
			&& getSkillBonus(victim, SKILL_SHIELD_MASTERY) > number(0, 600)
            && GET_POSITION(victim) >= POS_FIGHTING) {
		act("$N deflects your attack with $S shield!", true,
			ch, GET_EQ(victim, WEAR_SHIELD), victim, TO_CHAR);
		act("You deflect $n's attack with $p!", true,
			ch, GET_EQ(victim, WEAR_SHIELD), victim, TO_VICT);
		act("$N deflects $n's attack with $S shield!", true,
			ch, GET_EQ(victim, WEAR_SHIELD), victim, TO_NOTVICT);
		location = WEAR_SHIELD;
		deflected = true;
	}

    if (ch
			&& IS_WEAPON(attacktype)
        	&& !SPELL_IS_PSIONIC(attacktype)
            && !SPELL_IS_BARD(attacktype)
			&& CHECK_SKILL(victim, SKILL_UNCANNY_DODGE) > 20
			&& getSkillBonus(victim, SKILL_UNCANNY_DODGE) > number(0, 350)
			&& GET_POSITION(victim) >= POS_FIGHTING) {
		act("$N smirks as $E easily sidesteps your attack!", true,
			ch, NULL, victim, TO_CHAR);
		act("You smirk as you easily sidestep $n's attack!", true,
			ch, NULL, victim, TO_VICT);
		act("$N smirks as $E easily sidesteps $n's attack!", true,
			ch, NULL, victim, TO_NOTVICT);

		DAM_RETURN(DAM_ATTACK_FAILED);
    }

    if (ch
			&& IS_WEAPON(attacktype)
        	&& !SPELL_IS_PSIONIC(attacktype)
            && !SPELL_IS_BARD(attacktype)
			&& CHECK_SKILL(victim, SKILL_TUMBLING) > 20
			&& getSkillBonus(victim, SKILL_TUMBLING) > number(0, 425)
			&& GET_POSITION(victim) >= POS_FIGHTING) {
		act("$N dexterously rolls away from your attack!", true,
			ch, NULL, victim, TO_CHAR);
		act("You dexterously roll away from $n's attack!", true,
			ch, NULL, victim, TO_VICT);
		act("$N dexterously rolls away from $n's attack!", true,
			ch, NULL, victim, TO_NOTVICT);

		DAM_RETURN(0);
    }

    // Mirror Image Melody
    if (ch && ch != victim && !IS_WEAPON(attacktype) &&
        !BAD_ATTACK_TYPE(attacktype) && !SPELL_IS_PSIONIC(attacktype)) {
        struct affected_type *paf;
        if ((paf = affected_by_spell(victim, SONG_MIRROR_IMAGE_MELODY))) {
            if ((number(0, 375) < paf->modifier*10 + getSkillBonus(victim, SONG_MIRROR_IMAGE_MELODY)) &&
                (GET_CLASS(victim) == CLASS_BARD) && paf->modifier > 0) {
                char *buf = tmp_sprintf("%sYour attack passes right through "
                                        "a mirror image of $N!%s", CCGRN_BLD(ch, C_NRM),
                                        CCNRM(ch, C_NRM));
                act(buf, false, ch, NULL, victim, TO_CHAR);

                act("$n's attack passes right through a mirror image of $N!", false,
                    ch, NULL, victim, TO_NOTVICT);

                buf = tmp_sprintf("%s$N's attack passes right through a mirror image "
                                  "of you!%s", CCGRN_BLD(ch, C_NRM), CCNRM(ch, C_NRM));
                act(buf, false, victim, NULL, ch, TO_CHAR);

                paf->modifier--;
                if (paf->modifier == 0) {
                    paf->duration = 0;
                }
                if (isOkToAttack(victim, ch)) {
                    addCombat(ch, victim, true);
                    addCombat(victim, ch, false);
                }

                DAM_RETURN(DAM_ATTACK_FAILED);
            }
        }
    }

    // Dimensional Shift
    if (ch && !(attacktype >= TYPE_EGUN_LASER && attacktype <= TYPE_EGUN_TOP) &&
    attacktype != SKILL_ENERGY_WEAPONS &&
    attacktype != SKILL_ARCHERY &&
    attacktype != SKILL_PROJ_WEAPONS &&
    !SPELL_IS_PSIONIC(attacktype) &&
    !SPELL_IS_BARD(attacktype) &&
    !SPELL_IS_PHYSICS(attacktype) &&
    !SPELL_IS_MAGIC(attacktype)) {

        struct affected_type *paf;
        if ((paf = affected_by_spell(victim, SPELL_DIMENSIONAL_SHIFT)) &&
        !mag_savingthrow(ch, GET_LEVEL(ch), SAVING_PHY) &&
        !number(0,100)) { //1% chance of working
            paf->duration--;

            const char *buf = "You become caught in a dimensional void trying to reach $N!";
            act(buf, false, ch, NULL, victim, TO_CHAR);

            act("$n becomes caught in a dimensional void trying to reach $N!",
            false, ch, NULL, victim, TO_NOTVICT);

            buf = "$N becomes caught in a dimensional void trying to reach you!";
            act(buf, false, victim, NULL, ch, TO_CHAR);

            //ok they hit the void and don't manage to land the attack
            //now, does anything else happen to them?

            struct affected_type shiftAf;
            shiftAf.is_instant = 0;
            shiftAf.bitvector = 0;
            shiftAf.location = APPLY_NONE;
            shiftAf.modifier = 0;
            shiftAf.aff_index = 0;
            shiftAf.owner = GET_IDNUM(victim);
            shiftAf.duration = 0;
            shiftAf.level = getSkillBonus(victim, SPELL_DIMENSIONAL_SHIFT);
            shiftAf.type = SPELL_DIMENSIONAL_VOID;
            bool applyAffect = false;

            if (isOkToAttack(victim, ch)) {
                switch (number(0,14)) {
                    case 0:
                    case 1:
                    case 2:
                    case 3:
                    act("You become disoriented!", false, ch, NULL, victim, TO_CHAR);
                    act("$n becomes disoriented!", false, ch, NULL, victim, TO_ROOM);
                    shiftAf.location = APPLY_DEX;
                    shiftAf.modifier = -(1+getSkillBonus(victim, SPELL_DIMENSIONAL_SHIFT)/40);
                    shiftAf.duration = number(1,-shiftAf.modifier);
                    applyAffect = true;
                    break;
                    case 4:
                    case 5:
                    case 6:
                    act("You lose your footing and fall down!", false, ch, NULL, victim, TO_CHAR);
                    act("$n stumbles and falls down!", false, ch, NULL, victim, TO_ROOM);
                    GET_POSITION(ch) = POS_SITTING;
                    break;
                    case 7:
                    case 8:
                    act("The interdimensional vastness is too much for your mind to handle!", false, ch, NULL, victim, TO_CHAR);
                    act("$n looks dazed and confused.", false, ch, NULL, victim, TO_ROOM);
                    shiftAf.bitvector = AFF_CONFUSION;
                    shiftAf.duration = 1+getSkillBonus(victim, SPELL_DIMENSIONAL_SHIFT)/40;
                    shiftAf.aff_index = 1;
                    applyAffect = true;
                    break;
                    //9-14 no extra affect
                }
                WAIT_STATE(ch, 2 RL_SEC); //couple seconds to recover from the experience
                if (applyAffect) {
                    affect_join(ch, &shiftAf, true, false, true, false);
                }

                addCombat(ch, victim, true);
                addCombat(victim, ch, false);
            }

            DAM_RETURN(DAM_ATTACK_FAILED);
        }
    } // end dimensional shift

	/** check for armor **/
	if (location != -1) {

		if (location == WEAR_RANDOM) {
			location = choose_random_limb(victim);
		}

		if ((location == WEAR_FINGER_R ||
				location == WEAR_FINGER_L) && GET_EQ(victim, WEAR_HANDS)) {
			location = WEAR_HANDS;
		}

		if ((location == WEAR_EAR_R ||
				location == WEAR_EAR_L) && GET_EQ(victim, WEAR_HEAD)) {
			location = WEAR_HEAD;
		}

		obj = GET_EQ(victim, location);
		impl = GET_IMPLANT(victim, location);

		// implants are protected by armor
		if (obj && impl && IS_OBJ_TYPE(obj, ITEM_ARMOR) &&
			!IS_OBJ_TYPE(impl, ITEM_ARMOR))
			impl = NULL;

		if (obj || impl) {

			if (obj && OBJ_TYPE(obj, ITEM_ARMOR)) {
				eq_dam = (GET_OBJ_VAL(obj, 0) * dam) / 100;
				if (location == WEAR_SHIELD)
					eq_dam *= 2;
			}
			if (impl && OBJ_TYPE(impl, ITEM_ARMOR))
				impl_dam = (GET_OBJ_VAL(impl, 0) * dam) / 100;

			weap_dam = eq_dam + impl_dam;

			if (obj && !eq_dam)
				eq_dam = (dam / 16);

			if ((!obj || !OBJ_TYPE(obj, ITEM_ARMOR)) && impl && !impl_dam)
				impl_dam = (dam / 32);

			/* here are the damage absorbing characteristics */
			if ((attacktype == TYPE_BLUDGEON || attacktype == TYPE_HIT ||
					attacktype == TYPE_POUND || attacktype == TYPE_PUNCH)) {
				if (obj) {
					if (IS_LEATHER_TYPE(obj) || IS_CLOTH_TYPE(obj) ||
						IS_FLESH_TYPE(obj))
						eq_dam = (int)(eq_dam * 0.7);
					else if (IS_METAL_TYPE(obj))
						eq_dam = (int)(eq_dam * 1.3);
				}
				if (impl) {
					if (IS_LEATHER_TYPE(impl) || IS_CLOTH_TYPE(impl) ||
						IS_FLESH_TYPE(impl))
						eq_dam = (int)(eq_dam * 0.7);
					else if (IS_METAL_TYPE(impl))
						eq_dam = (int)(eq_dam * 1.3);
				}
			} else if ((attacktype == TYPE_SLASH || attacktype == TYPE_PIERCE
					|| attacktype == TYPE_CLAW || attacktype == TYPE_STAB
					|| attacktype == TYPE_RIP || attacktype == TYPE_CHOP)) {
				if (obj && (IS_METAL_TYPE(obj) || IS_STONE_TYPE(obj))) {
					eq_dam = (int)(eq_dam * 0.7);
					weap_dam = (int)(weap_dam * 1.3);
				}
				if (impl && (IS_METAL_TYPE(impl) || IS_STONE_TYPE(impl))) {
					impl_dam = (int)(impl_dam * 0.7);
					weap_dam = (int)(weap_dam * 1.3);
				}
			} else if (attacktype == SKILL_PROJ_WEAPONS) {
				if (obj) {
					if (IS_MAT(obj, MAT_KEVLAR))
						eq_dam = (int)(eq_dam * 1.6);
					else if (IS_METAL_TYPE(obj))
						eq_dam = (int)(eq_dam * 1.3);
				}
			}

			dam = MAX(0, (dam - eq_dam - impl_dam));

			if (obj) {
				if (IS_OBJ_STAT(obj, ITEM_MAGIC_NODISPEL))
					eq_dam /= 2;
				if (IS_OBJ_STAT(obj,
						ITEM_MAGIC | ITEM_BLESS | ITEM_DAMNED))
					eq_dam = (int)(eq_dam * 0.8);
				if (IS_OBJ_STAT2(obj, ITEM2_GODEQ | ITEM2_CURSED_PERM))
					eq_dam = (int)(eq_dam * 0.7);
			}
			if (impl) {
				if (IS_OBJ_STAT(impl, ITEM_MAGIC_NODISPEL))
					impl_dam /= 2;
				if (IS_OBJ_STAT(impl,
						ITEM_MAGIC | ITEM_BLESS | ITEM_DAMNED))
					impl_dam = (int)(impl_dam * 0.8);
				if (IS_OBJ_STAT2(impl, ITEM2_GODEQ | ITEM2_CURSED_PERM))
					impl_dam = (int)(impl_dam * 0.7);
			}

			/* here are the object oriented damage specifics */
			if ((attacktype == TYPE_SLASH || attacktype == TYPE_PIERCE ||
					attacktype == TYPE_CLAW || attacktype == TYPE_STAB ||
					attacktype == TYPE_RIP || attacktype == TYPE_CHOP)) {
				if (obj && (IS_METAL_TYPE(obj) || IS_STONE_TYPE(obj))) {
					eq_dam = (int)(eq_dam * 0.7);
					weap_dam = (int)(weap_dam * 1.3);
				}
				if (impl && (IS_METAL_TYPE(impl) || IS_STONE_TYPE(impl))) {
					impl_dam = (int)(impl_dam * 0.7);
					weap_dam = (int)(weap_dam * 1.3);
				}
			} else if (attacktype == SPELL_PSYCHIC_CRUSH)
				eq_dam *= 127;
			// OXIDIZE Damaging equipment
			else if (attacktype == SPELL_OXIDIZE) {
				// Chemical stability stops oxidize dam on eq.
				if (affected_by_spell(victim, SPELL_CHEMICAL_STABILITY)) {
					eq_dam = 0;
				} else if ((obj && IS_FERROUS(obj))) {
					apply_soil_to_char(victim, GET_EQ(victim, location),
						SOIL_RUST, location);
					eq_dam *= 32;
				} else if (impl && IS_FERROUS(impl)) {
					apply_soil_to_char(victim, GET_IMPLANT(victim, location),
						SOIL_RUST, location);
					eq_dam *= 32;
				} else if ((obj && IS_BURNABLE_TYPE(obj))) {
					apply_soil_to_char(victim, GET_EQ(victim, location),
						SOIL_CHAR, location);
					eq_dam *= 8;
				} else if (impl && IS_BURNABLE_TYPE(impl)) {
					apply_soil_to_char(victim, GET_IMPLANT(victim, location),
						SOIL_CHAR, location);
					eq_dam *= 8;
				}
			}
			if (weap) {
				if (IS_OBJ_STAT(weap, ITEM_MAGIC_NODISPEL))
					weap_dam /= 2;
				if (IS_OBJ_STAT(weap,
						ITEM_MAGIC | ITEM_BLESS | ITEM_DAMNED))
					weap_dam = (int)(weap_dam * 0.8);
				if (IS_OBJ_STAT2(weap, ITEM2_CAST_WEAPON | ITEM2_GODEQ |
						ITEM2_CURSED_PERM))
					weap_dam = (int)(weap_dam * 0.7);
			}
		}
	}
	if (deflected) {
		// We have to do all the object damage for shields here, after
		// it's calculated
		if (obj && eq_dam)
			damage_eq(ch, obj, eq_dam, attacktype);
		if (impl && impl_dam)
			damage_eq(ch, impl, impl_dam, attacktype);

		if (weap && (attacktype != SKILL_PROJ_WEAPONS) &&
            attacktype >= TYPE_EGUN_LASER && attacktype <= TYPE_EGUN_TOP &&
			attacktype != SKILL_ENERGY_WEAPONS)
			damage_eq(ch, weap, MAX(weap_dam, dam / 64), attacktype);
		DAM_RETURN(0);
	}

    //
	// attacker is a character
	//

	if (ch) {
        //particle stream special
        if (attacktype == TYPE_EGUN_PARTICLE && dam && ch) {
            struct obj_data *tmp = NULL;
            if (impl) tmp=impl;
            if (obj) tmp=obj;
            eq_dam = (int)(eq_dam * 1.3);
            if (ch && do_gun_special(ch, weap)) {
                eq_dam *= 4;
                dam *= 2;
                //we add in eq_dam here because it is later subtracted
                //since the particles are penetrating the eq we don't want it to provide protection
                dam += eq_dam;
                if (tmp) {
                    act("Your particle stream penetrates $p and into $N!", false, ch, tmp, victim, TO_CHAR);
                    act("$n's particle stream penetrates $p and into you!", false, ch, tmp, victim, TO_VICT);
                    act("$n's particle stream penetrates $p and into $N!", false, ch, tmp, victim, TO_NOTVICT);
                } else {
                    act("Your particle stream penetrates deep into $N!", false, ch, obj, victim, TO_CHAR);
                    act("$n's particle stream penetrates deep into you!", false, ch, obj, victim, TO_VICT);
                    act("$n's particle stream penetrates deep into $N!", false, ch, obj, victim, TO_NOTVICT);
                }
            }
        }

		//
		// attack type is a skill type
		//

		if (!cur_weap && ch != victim && dam &&
			(attacktype < MAX_SKILLS || attacktype >= TYPE_HIT) &&
			(attacktype > MAX_SPELLS
				|| IS_SET(spell_info[attacktype].routines, MAG_TOUCH))
			&& !SPELL_IS_PSIONIC(attacktype) && !SPELL_IS_BARD(attacktype)) {

			//
			// vict has prismatic sphere
			//

			if (AFF3_FLAGGED(victim, AFF3_PRISMATIC_SPHERE) &&
				attacktype < MAX_SKILLS &&
				(CHECK_SKILL(ch, attacktype) + GET_LEVEL(ch))
				< (GET_INT(victim) + number(70, 130 + GET_LEVEL(victim)))) {
				act("You are deflected by $N's prismatic sphere!",
					false, ch, 0, victim, TO_CHAR);
				act("$n is deflected by $N's prismatic sphere!",
					false, ch, 0, victim, TO_NOTVICT);
				act("$n is deflected by your prismatic sphere!",
					false, ch, 0, victim, TO_VICT);
				int retval =
					damage_attacker(victim, ch, dice(30, 3) + (dam / 4),
					SPELL_PRISMATIC_SPHERE, -1);

				if (!IS_SET(retval, DAM_VICT_KILLED)) {
					gain_skill_prof(victim, SPELL_PRISMATIC_SPHERE);
				}
				SET_BIT(retval, DAM_ATTACK_FAILED);
				DAM_RETURN(retval);
			}
			//
			// vict has electrostatic field
			//

			if ((af = affected_by_spell(victim, SPELL_ELECTROSTATIC_FIELD))) {
				if (!CHAR_WITHSTANDS_ELECTRIC(ch)
					&& !mag_savingthrow(ch, af->level, SAVING_ROD)) {
					// reduces duration if it hits
					if (af->duration > 1) {
						af->duration--;
					}
					int retval =
						damage_attacker(victim, ch, dice(3, af->level),
						SPELL_ELECTROSTATIC_FIELD, -1);

					if (!IS_SET(retval, DAM_VICT_KILLED)) {
						gain_skill_prof(victim, SPELL_ELECTROSTATIC_FIELD);
					}
					SET_BIT(retval, DAM_ATTACK_FAILED);
					DAM_RETURN(retval);
				}
			}

            //
            // vict has thorn skin
            //

            if ((af = affected_by_spell(victim, SPELL_THORN_SKIN))) {
                if (!mag_savingthrow(ch, af->level, SAVING_BREATH) &&
                    !random_fractional_5()) {
                    if (af->duration > 1) {
                        af->duration--;
                    }
                    int retval = damage_attacker(victim, ch, dice(3, af->level),
                                                 SPELL_THORN_SKIN, -1);

                    if (!IS_SET(retval, DAM_VICT_KILLED)) {
                        gain_skill_prof(victim, SPELL_THORN_SKIN);
                    }

                    if (!IS_SET(retval, DAM_ATTACKER_KILLED)) {
                        if (!mag_savingthrow(ch, af->level, SAVING_BREATH) &&
                            !IS_POISONED(ch) && random_fractional_4()) {
                            struct affected_type af;
                            int level_bonus = get_skill_bonus(ch, SPELL_THORN_SKIN);
                            af.type = SPELL_POISON;
                            af.location = APPLY_STR;
                            af.duration = MAX(1, level_bonus / 10);
                            af.modifier = -(number(1, level_bonus / 20));
                            af.owner = GET_IDNUM(ch);

                            if (level_bonus > (85 + number(0, 30))) {
                                af.bitvector = AFF3_POISON_3;
                                af.aff_index = 3;
                            }
                            else if (level_bonus > (75 + number(0, 30))) {
                                af.bitvector = AFF3_POISON_2;
                                af.aff_index = 3;
                            }
                            else {
                                af.bitvector = AFF_POISON;
                                af.aff_index = 1;
                            }
                            affect_join(ch, &af, false, false, false, false);
                            act("$N begins to look sick as the poison from "
                                "your thorns invades $S body.", false, victim,
                                NULL, ch, TO_CHAR);
                            act("You begin to feel sick as the poison from "
                                "$n's thorns invades your body.", false, victim,
                                NULL, ch, TO_VICT);
                            act("$N begins to look sick as the poison from "
                                "$n's thorns invades $S body.", false, victim,
                                NULL, ch, TO_NOTVICT);
                        }
                    }
                    SET_BIT(retval, DAM_ATTACK_FAILED);
                    DAM_RETURN(retval);
                }
            }
			//
			// attack type is a nonweapon melee type
			//

			if (attacktype < MAX_SKILLS && attacktype != SKILL_BEHEAD &&
				attacktype != SKILL_SECOND_WEAPON
				&& attacktype != SKILL_IMPALE) {

				int retval = 0;	// damage result for end of block return

				//
				// vict has prismatic sphere
				//

				if (AFF3_FLAGGED(victim, AFF3_PRISMATIC_SPHERE) &&
					!mag_savingthrow(ch, GET_LEVEL(victim), SAVING_ROD)) {

					retval =
						damage_attacker(victim, ch, dice(30,
							3) + (IS_MAGE(victim) ? (dam / 4) : 0),
						SPELL_PRISMATIC_SPHERE, -1);

					if (!IS_SET(retval, DAM_ATTACKER_KILLED)) {
						WAIT_STATE(ch, PULSE_VIOLENCE);
					}

				}
				//
				// vict has blade barrier
				//

				else if (AFF2_FLAGGED(victim, AFF2_BLADE_BARRIER)) {
					retval = damage_attacker(victim, ch,
						GET_LEVEL(victim) + (dam / 16),
						SPELL_BLADE_BARRIER, -1);

				}

                else if (affected_by_spell(victim, SONG_WOUNDING_WHISPERS) &&
                         attacktype != SKILL_PSIBLAST) {
					retval = damage_attacker(victim, ch,
						(getSkillBonus(victim, SONG_WOUNDING_WHISPERS) / 2) + (dam / 20),
						SONG_WOUNDING_WHISPERS, -1);
                }
				//
				// vict has fire shield
				//

				else if (AFF2_FLAGGED(victim, AFF2_FIRE_SHIELD) &&
					attacktype != SKILL_BACKSTAB &&
					!mag_savingthrow(ch, GET_LEVEL(victim), SAVING_BREATH) &&
					!AFF2_FLAGGED(ch, AFF2_ABLAZE) &&
					!CHAR_WITHSTANDS_FIRE(ch)) {

					retval = damage_attacker(victim, ch, dice(8, 8) +
						(IS_MAGE(victim) ? (dam / 8) : 0),
						SPELL_FIRE_SHIELD, -1);

					if (!IS_SET(retval, DAM_ATTACKER_KILLED)) {
                        ignite(ch, ch);
					}

				}
				//
				// vict has energy field
				//

				else if (AFF2_FLAGGED(victim, AFF2_ENERGY_FIELD)) {
					af = affected_by_spell(victim, SKILL_ENERGY_FIELD);
					if (!mag_savingthrow(ch,
							af ? af->level : GET_LEVEL(victim),
							SAVING_ROD) && !CHAR_WITHSTANDS_ELECTRIC(ch)) {
						retval = damage_attacker(victim, ch,
							af ? dice(3, MAX(10, af->level)) : dice(3, 8),
							SKILL_ENERGY_FIELD, -1);

						if (!IS_SET(retval, DAM_ATTACKER_KILLED)) {
							GET_POSITION(ch) = POS_SITTING;
							WAIT_STATE(ch, 2 RL_SEC);
						}
					}
				}
				//
				// return if anybody got killed
				//

				if (retval) {
					DAM_RETURN(retval);
				}

			}
		}
	}
    /********* OHH SHIT!  Begin new damage reduction code --N **********/
	float dam_reduction = 0;

	if (affected_by_spell(victim, SPELL_MANA_SHIELD)
		&& !BAD_ATTACK_TYPE(attacktype)) {
		mana_loss = (dam * GET_MSHIELD_PCT(victim)) / 100;
		mana_loss = MIN(GET_MANA(victim) - GET_MSHIELD_LOW(victim), mana_loss);
		mana_loss = MAX(mana_loss, 0);
		dam = MAX(0, dam - mana_loss);
	    mana_loss -= (int)(mana_loss * MIN(getDamReduction(victim), 0.50));
		GET_MANA(victim) -= mana_loss;

		if (GET_MANA(victim) <= GET_MSHIELD_LOW(victim)) {
			send_to_char(victim, "Your mana shield has expired.\r\n");
			affect_from_char(victim, SPELL_MANA_SHIELD);
		}

        if (mana_loss && (GET_MSHIELD_PCT(victim) == 100)) {
            mshield_hit = true;
			if (ch) {
				if (IS_WEAPON(attacktype))
					dam_message(mana_loss, ch, victim, attacktype, WEAR_MSHIELD);
				else
					skill_message(mana_loss, ch, victim, attacktype);
			}
        }
	}

	if (ch && GET_CLASS(ch) == CLASS_CLERIC && IS_EVIL(ch)) {
		// evil clerics get an alignment-based damage bonus, up to 30% on
		// new moons, %10 otherwise.  It may look like we're subtracting
		// damage, but the alignment is negative by definition.
		if (get_lunar_phase(lunar_day) == MOON_NEW)
			dam -= (int)((dam * GET_ALIGNMENT(ch)) / 3000);
		else
			dam -= (int)((dam * GET_ALIGNMENT(ch)) / 10000);
	}

	//******************* Reduction based on the attacker ********************/
	//************ Knights, Clerics, and Monks out of alignment **************/
	if (ch) {
		if (IS_NEUTRAL(ch)) {
			if (IS_KNIGHT(ch) || IS_CLERIC(ch))
				dam = dam / 4;
		} else {
			if (IS_MONK(ch))
				dam = dam / 4;
		}
	}
	// ALL previous damage reduction code that was based off of character
	// attributes has been moved to the function below.  See structs/struct creature.cc
	dam_reduction = getDamReduction(victim, ch);

	dam -= (int)(dam * dam_reduction);

	/*********************** End N's coding spree ***********************/

	if (IS_PUDDING(victim) || IS_SLIME(victim) || IS_TROLL(victim))
		dam /= 4;

	switch (attacktype) {
	case TYPE_OVERLOAD:
	case TYPE_TAINT_BURN:
		break;
	case SPELL_POISON:
		if (IS_UNDEAD(victim))
			dam = 0;
		break;
	case SPELL_STIGMATA:
	case TYPE_HOLYOCEAN:
		if (IS_UNDEAD(victim) || IS_EVIL(victim))	// is_evil added for stigmata
			dam *= 2;
		if (AFF_FLAGGED(victim, AFF_PROTECT_GOOD))
			dam /= 2;
		break;
	case SPELL_OXIDIZE:
		if (IS_CYBORG(victim))
			dam *= 2;
		else if (affected_by_spell(victim, SPELL_CHEMICAL_STABILITY))
			dam /= 4;
		break;
	case SPELL_DISRUPTION:
		if (IS_UNDEAD(victim) || IS_CYBORG(victim) || IS_ROBOT(victim))
			dam *= 2;
		break;
	case SPELL_CALL_LIGHTNING:
	case SPELL_LIGHTNING_BOLT:
	case SPELL_LIGHTNING_BREATH:
	case JAVELIN_OF_LIGHTNING:
	case SKILL_ENERGY_FIELD:
	case SKILL_DISCHARGE:
    case SPELL_ELECTRIC_ARC:
    case TYPE_EGUN_LIGHTNING:
		if (IS_VAMPIRE(victim))
			dam /= 2;
		if (OUTSIDE(victim)
			&& victim->in_room->zone->weather->sky == SKY_LIGHTNING)
			dam *= 2;
		if (AFF2_FLAGGED(victim, AFF2_PROT_LIGHTNING))
			dam /= 2;
		if (IS_CYBORG(victim))
			dam *= 2;
		break;
	case SPELL_HAILSTORM:
		if (OUTSIDE(victim)
			&& victim->in_room->zone->weather->sky >= SKY_RAINING)
			dam *= 2;
		break;
	case SPELL_HELL_FIRE:
	case SPELL_TAINT:
	case SPELL_BURNING_HANDS:
	case SPELL_FIREBALL:
	case SPELL_FLAME_STRIKE:
	case SPELL_FIRE_ELEMENTAL:
	case SPELL_FIRE_BREATH:
	case TYPE_ABLAZE:
	case SPELL_FIRE_SHIELD:
	case TYPE_FLAMETHROWER:
    case TYPE_EGUN_PLASMA:
		if (IS_VAMPIRE(victim) && OUTSIDE(victim) &&
			victim->in_room->zone->weather->sunlight == SUN_LIGHT &&
			GET_PLANE(victim->in_room) < PLANE_ASTRAL)
			dam *= 4;

		if ((victim->in_room->sector_type == SECT_PITCH_PIT ||
				ROOM_FLAGGED(victim->in_room, ROOM_EXPLOSIVE_GAS)) &&
			!ROOM_FLAGGED(victim->in_room, ROOM_FLAME_FILLED)) {
			send_to_room("The room bursts into flames!!!\r\n",
				victim->in_room);
			if (victim->in_room->sector_type == SECT_PITCH_PIT)
				rm_aff.description =
					strdup("   The pitch is ablaze with raging flames!\r\n");
			else
				rm_aff.description =
					strdup("   The room is ablaze with raging flames!\r\n");

			rm_aff.level = (ch != NULL) ? GET_LEVEL(ch) : number(1, 49);
			rm_aff.duration = number(3, 8);
			rm_aff.type = RM_AFF_FLAGS;
			rm_aff.flags = ROOM_FLAME_FILLED;
            rm_aff.owner = (ch) ? GET_IDNUM(ch) : 0;
            rm_aff.spell_type = TYPE_ABLAZE;
			affect_to_room(victim->in_room, &rm_aff);
			sound_gunshots(victim->in_room, SPELL_FIREBALL, 8, 1);
		}

		if (CHAR_WITHSTANDS_FIRE(victim))
			dam /= 2;
		if (IS_TROLL(victim) || IS_PUDDING(victim) || IS_SLIME(victim))
			dam *= 4;
		break;
	case SPELL_CONE_COLD:
	case SPELL_CHILL_TOUCH:
	case SPELL_ICY_BLAST:
	case SPELL_FROST_BREATH:
	case TYPE_FREEZING:
		if (CHAR_WITHSTANDS_COLD(victim))
			dam /= 2;
		break;
	case TYPE_BLEED:
		if (!CHAR_HAS_BLOOD(victim))
			dam = 0;
		break;
	case SPELL_STEAM_BREATH:
	case TYPE_BOILING_PITCH:
		if (AFF3_FLAGGED(victim, AFF3_PROT_HEAT))
			dam /= 2;
	case TYPE_ACID_BURN:
	case SPELL_ACIDITY:
	case SPELL_ACID_BREATH:
		if (affected_by_spell(victim, SPELL_CHEMICAL_STABILITY))
			dam /= 2;
		break;
	}

	// rangers' critical hit
	if (ch && IS_RANGER(ch) && dam > 10 &&
		IS_WEAPON(attacktype) &&
        number(0, 650) <= get_skill_bonus(ch, GET_CLASS(ch) == CLASS_RANGER)) {
		send_to_char(ch, "CRITICAL HIT!\r\n");
		act("$n has scored a CRITICAL HIT!", false, ch, 0, victim,
			TO_VICT);
		dam += (dam * (GET_REMORT_GEN(ch) + 4)) / 14;
	}

	if (ch)
		hard_damcap = MAX(20 + GET_LEVEL(ch) + (GET_REMORT_GEN(ch) * 2),
			GET_LEVEL(ch) * 20 + (GET_REMORT_GEN(ch) * GET_LEVEL(ch) * 2));
	else
		hard_damcap = 7000;

	dam = MAX(MIN(dam, hard_damcap), 0);

    if (ch) {
        //lightning gun special
        if (attacktype == TYPE_EGUN_LIGHTNING && dam) {
            if (do_gun_special(ch, weap)) {
                void lightning_zap(struct creature *tch, gpointer ignore) {
                    if (tch == ch || !findCombat(tch, ch))
                        return;
                    damage(ch, tch, dam/2, TYPE_EGUN_SPEC_LIGHTNING, WEAR_RANDOM);
                }
                g_list_foreach(ch->in_room->people, lightning_zap, 0);
            }
        }

        //photon special
        if (attacktype == TYPE_EGUN_PHOTON && dam) {
            if (do_gun_special(ch, weap)) {
                mag_affects(get_skill_bonus(ch, SKILL_ENERGY_WEAPONS), ch, victim, NULL, SPELL_BLINDNESS, SAVING_ROD);
            }
        }
        //plasma special
        if (attacktype == TYPE_EGUN_PLASMA && dam) {
            if (do_gun_special(ch, weap) && !CHAR_WITHSTANDS_FIRE(victim) &&
                !AFF2_FLAGGED(victim, AFF2_ABLAZE)) {
				act("$n's body suddenly ignites into flame!",
					false, victim, 0, 0, TO_ROOM);
				act("Your body suddenly ignites into flame!",
					false, victim, 0, 0, TO_CHAR);
                ignite(victim, ch);
            }
        }
        //ion special
        if (attacktype == TYPE_EGUN_ION && dam) {
            if (do_gun_special(ch,weap)) {
                do_emp_pulse_char(ch, victim);
                act("$n's body suddenly ignites into flame!",
					false, victim, 0, 0, TO_ROOM);
				act("Your body suddenly ignites into flame!",
					false, victim, 0, 0, TO_CHAR);
            }
        }
        //gamma special
        if (attacktype == TYPE_EGUN_GAMMA && dam) {
            if (do_gun_special(ch,weap) && !CHAR_WITHSTANDS_RAD(victim)) {
                add_rad_sickness(victim, GET_LEVEL(ch));
                act("$n begins to look sick.",
					false, victim, 0, 0, TO_ROOM);
				act("You start to feel sick.",
					false, victim, 0, 0, TO_CHAR);
            }
        }
        //sonic special
        if (attacktype == TYPE_EGUN_SONIC && dam) {
            if (do_gun_special(ch,weap)) {
                struct affected_type sonicAf;
                sonicAf.is_instant = 0;
                sonicAf.bitvector = 0;
                sonicAf.location = APPLY_DEX;
                sonicAf.modifier = -1;
                sonicAf.aff_index = 0;
                sonicAf.owner = GET_IDNUM(ch);
                sonicAf.duration = 1;
                sonicAf.level = get_skill_bonus(ch, SKILL_ENERGY_WEAPONS);
                sonicAf.type = TYPE_EGUN_SPEC_SONIC;
                act("You become disoriented!", false, ch, NULL, victim, TO_VICT);
                act("$N becomes disoriented!", false, ch, NULL, victim, TO_NOTVICT);
                affect_join(victim, &sonicAf, false, false, true, false);
            }
        }
    }
	//
	// characters under the effects of vampiric regeneration
	//

	if ((af = affected_by_spell(victim, SPELL_VAMPIRIC_REGENERATION))) {
		// pc caster
		if (ch && !IS_NPC(ch) && GET_IDNUM(ch) == af->modifier) {
			act(tmp_sprintf("You drain %d hitpoints from $N!", dam), false, ch, 0, victim, TO_CHAR);
			GET_HIT(ch) = MIN(GET_MAX_HIT(ch), GET_HIT(ch) + dam);
			af->duration--;
			if (af->duration <= 0) {
				affect_remove(victim, af);
			}
		}
	}

	if ((af = affected_by_spell(victim, SPELL_LOCUST_REGENERATION)) && dam > 0) {
		// pc caster
		if (ch && !IS_NPC(ch) && GET_IDNUM(ch) == af->modifier) {
			if (victim && (GET_MANA(victim) > 0)) {
				int manadrain = MIN((int)(dam * 0.90), GET_MANA(victim));
				if (GET_MOVE(ch) > 0) {
				    act(tmp_sprintf("You drain %d mana from $N!", manadrain), false, ch, 0, victim, TO_CHAR);
				    GET_MOVE(ch) = MAX(0, GET_MOVE(ch) - (int)(dam * 0.10));
					GET_MANA(ch) = MIN(GET_MAX_MANA(ch),
                                       GET_MANA(ch) + manadrain);
                }
                else {
                    act("You don't have the stamina to drain any mana from $N!",
                        false, ch, 0, victim, TO_CHAR);
                }
				af->duration--;
				if (af->duration <= 0) {
					affect_remove(victim, af);
				}
			}
            else {
                act("$N is completely drained of mana!", false, ch,
                    0, victim, TO_CHAR);
            }
		}
	}

    if (weap && affected_by_spell(victim, SPELL_GAUSS_SHIELD) && dam > 0) {
        if (IS_METAL_TYPE(weap))
            dam -= (dam * (MAX(20, getSkillBonus(victim, SPELL_GAUSS_SHIELD) / 3)) / 100);
    }

	GET_HIT(victim) -= dam;
	if (!IS_NPC(victim))
		GET_TOT_DAM(victim) += dam;

	if (ch && ch != victim
		&& !(MOB2_FLAGGED(victim, MOB2_UNAPPROVED) ||
			isTester(ch) || IS_PET(ch) || IS_PET(victim))) {
		// Gaining XP for damage dealt.
		int exp = MIN(GET_LEVEL(ch) * GET_LEVEL(ch) * GET_LEVEL(ch), GET_LEVEL(victim) * dam);

		exp = getPenalizedExperience(ch,  exp, victim );

        // We shall not give exp for damage delt unless they're in
        // the same room
        if (ch->in_room == victim->in_room)
		    gain_exp(ch, exp);
	}
	// check for killer flags and remove sleep/etc...
	if (ch && ch != victim) {
		// remove sleep/flood on a damaging hit
		if (dam) {
			if (AFF_FLAGGED(victim, AFF_SLEEP)) {
				if (affected_by_spell(victim, SPELL_SLEEP))
					affect_from_char(victim, SPELL_SLEEP);
				if (affected_by_spell(victim, SPELL_MELATONIC_FLOOD))
					affect_from_char(victim, SPELL_MELATONIC_FLOOD);
				if (affected_by_spell(victim, SKILL_SLEEPER))
					affect_from_char(victim, SKILL_SLEEPER);
			}
		}
		// remove stasis even on a miss
		if (AFF3_FLAGGED(victim, AFF3_STASIS)) {
			send_to_char(victim, "Emergency restart of system processes...\r\n");
			REMOVE_BIT(AFF3_FLAGS(victim), AFF3_STASIS);
			WAIT_STATE(victim, (5 - (CHECK_SKILL(victim,
							SKILL_FASTBOOT) / 32)) RL_SEC);
		}
		// check for killer flags right here
		if (!findCombat(victim, ch) && !IS_DEFENSE_ATTACK(attacktype)) {
			check_attack(ch, victim);
		}
	}
	update_pos(victim);

	/*
	 * skill_message sends a message from the messages file in lib/misc.
	 * dam_message just sends a generic "You hit $n extremely hard.".
	 * skill_message is preferable to dam_message because it is more
	 * descriptive.
	 *
	 * If we are _not_ attacking with a weapon ( i.e. a spell ), always use
	 * skill_message. If we are attacking with a weapon: If this is a miss or a
	 * death blow, send a skill_message if one exists; if not, default to a
	 * dam_message. Otherwise, always send a dam_message.
	 */

	if (!IS_WEAPON(attacktype)) {
		if (ch && !mshield_hit)
			skill_message(dam, ch, victim, attacktype);

		// some "non-weapon" attacks involve a weapon, e.g. backstab
		if (weap)
			damage_eq(ch, weap, MAX(weap_dam, dam / 64), attacktype);

		if (obj)
			damage_eq(ch, obj, eq_dam, attacktype);

		if (impl && impl_dam)
			damage_eq(ch, impl, impl_dam, attacktype);

		// ignite the victim if applicable
		if (!AFF2_FLAGGED(victim, AFF2_ABLAZE) &&
			(attacktype == SPELL_FIREBALL ||
				attacktype == SPELL_FIRE_BREATH ||
				attacktype == SPELL_HELL_FIRE ||
				attacktype == SPELL_FLAME_STRIKE ||
				attacktype == SPELL_METEOR_STORM ||
				attacktype == TYPE_FLAMETHROWER ||
				attacktype == SPELL_FIRE_ELEMENTAL)) {
			if (!mag_savingthrow(victim, 50, SAVING_BREATH) &&
				!CHAR_WITHSTANDS_FIRE(victim)) {
				act("$n's body suddenly ignites into flame!",
					false, victim, 0, 0, TO_ROOM);
				act("Your body suddenly ignites into flame!",
					false, victim, 0, 0, TO_CHAR);
                ignite(victim, ch);
			}
		}
		// transfer sickness if applicable
		if (ch && ch != victim &&
			((attacktype > MAX_SPELLS && attacktype < MAX_SKILLS &&
					attacktype != SKILL_BEHEAD && attacktype != SKILL_IMPALE)
				|| (attacktype >= TYPE_HIT && attacktype < TYPE_SUFFERING))) {
			if ((IS_SICK(ch) || IS_ZOMBIE(ch)
					|| (ch->in_room->zone->number == 163 && !IS_NPC(victim)))
				&& !IS_SICK(victim) && !IS_UNDEAD(victim)) {
				call_magic(ch, victim, 0, NULL, SPELL_SICKNESS, GET_LEVEL(ch),
                           CAST_PARA, NULL);
			}
		}
	} else if (ch) {
		if (suppress_output) {
			errlog("DEBUG: Output suppressed during damage");
		}
		// it is a weapon attack
		if (GET_POSITION(victim) == POS_DEAD || dam == 0) {
			if (!mshield_hit && !skill_message(dam, ch, victim, attacktype))
				dam_message(dam, ch, victim, attacktype, location);
		} else if (!mshield_hit) {
			dam_message(dam, ch, victim, attacktype, location);
		}

		if (obj && eq_dam)
			damage_eq(ch, obj, eq_dam, attacktype);
		if (impl && impl_dam)
			damage_eq(ch, impl, impl_dam, attacktype);

		if (weap && (attacktype != SKILL_PROJ_WEAPONS) &&
            attacktype >= TYPE_EGUN_LASER && attacktype <= TYPE_EGUN_TOP &&
			attacktype != SKILL_ENERGY_WEAPONS)
			damage_eq(ch, weap, MAX(weap_dam, dam / 64), attacktype);

		//
		// aliens spray blood all over the room
		//

		if (IS_ALIEN_1(victim) && (attacktype == TYPE_SLASH ||
				attacktype == TYPE_RIP ||
				attacktype == TYPE_BITE ||
				attacktype == TYPE_CLAW ||
				attacktype == TYPE_PIERCE ||
				attacktype == TYPE_STAB ||
				attacktype == TYPE_CHOP ||
				attacktype == SPELL_BLADE_BARRIER)) {
            void spray_with_acidic_blood(struct creature *tch, gpointer ignore) {
				if (tch == victim || number(0, 8))
                    return;
				bool is_char = false;
				if (tch == ch)
					is_char = true;
				int retval =
					damage(victim, tch, dice(4, GET_LEVEL(victim)),
					TYPE_ALIEN_BLOOD, -1);

				if (is_char && IS_SET(retval, DAM_VICT_KILLED)) {
					DAM_RETURN(DAM_ATTACKER_KILLED);
				}
			}
            g_list_foreach(ch->in_room->people, spray_with_acidic_blood, 0);
		}
	} else if (original_ch != ch) {
		slog("ch was changed in the middle of damage()! original=%p (%s), ch=%p (%s)",
			original_ch,
			(original_ch) ? GET_NAME(original_ch):"NULL",
			ch,
			(ch) ? GET_NAME(ch):"NULL");
	}

    //psychic feedback - now that we've taken damage we return some of it
    if (ch && (af = affected_by_spell(victim, SPELL_PSYCHIC_FEEDBACK)) &&
      !mag_savingthrow(ch, GET_LEVEL(victim), SAVING_PSI) &&
      !ROOM_FLAGGED(victim->in_room, ROOM_NOPSIONICS) && !NULL_PSI(ch) &&
      !BAD_ATTACK_TYPE(attacktype) && attacktype != SPELL_PSYCHIC_FEEDBACK) {
        feedback_dam = (dam*getSkillBonus(victim, SPELL_PSYCHIC_FEEDBACK))/400;
        feedback_mana = feedback_dam/15;
        if (af->duration > 1 && feedback_dam > random_number_zero_low(400)) {
			af->duration--;
		}
        if (GET_MANA(victim) < feedback_mana) {
            feedback_mana = GET_MANA(victim);
            feedback_dam = feedback_mana*15;
            send_to_char(victim, "You are no longer providing psychic feedback to your attackers.\r\n");
            affect_from_char(victim, SPELL_PSYCHIC_FEEDBACK);
        }
        if (feedback_dam > 0) {
            GET_MANA(victim) -= MAX(feedback_mana,1);
            int retval = damage_attacker(victim, ch, feedback_dam, SPELL_PSYCHIC_FEEDBACK, -1);
            if (!IS_SET(retval, DAM_VICT_KILLED) && random_number_zero_low(400) < feedback_dam) {
                gain_skill_prof(victim, SPELL_PSYCHIC_FEEDBACK);
            }
            if (retval) {
                DAM_RETURN(retval);
            }
        }
    }

	// Use send_to_char -- act(  ) doesn't send message if you are DEAD.
	switch (GET_POSITION(victim)) {

		// Mortally wounded
	case POS_MORTALLYW:
		act("$n is badly wounded, but will recover very slowly.",
			true, victim, 0, 0, TO_ROOM);
		send_to_char(victim,
			"You are badly wounded, but will recover very slowly.\r\n");
		break;

		// Incapacitated
	case POS_INCAP:
		act("$n is incapacitated but will probably recover.", true,
			victim, 0, 0, TO_ROOM);
		send_to_char(victim,
			"You are incapacitated but will probably recover.\r\n");
		break;

		// Stunned
	case POS_STUNNED:
		act("$n is stunned, but will probably regain consciousness again.",
			true, victim, 0, 0, TO_ROOM);
		send_to_char(victim,
			"You're stunned, but will probably regain consciousness again.\r\n");
		break;

		// Dead
	case POS_DEAD:
		act("$n is dead!  R.I.P.", false, victim, 0, 0, TO_ROOM);
		send_to_char(victim, "You are dead!  Sorry...\r\n");
		break;

		// pos >= Sleeping ( Fighting, Standing, Flying, Mounted... )
	default:
		{
			if (dam > (GET_MAX_HIT(victim) / 4))
				act("That really did HURT!", false, victim, 0, 0, TO_CHAR);

			if (GET_HIT(victim) < (GET_MAX_HIT(victim) / 4) &&
				GET_HIT(victim) < 200) {
				send_to_char(victim,
                             "%sYou wish that your wounds would stop %sBLEEDING%s%s so much!%s\r\n",
                             CCRED(victim, C_SPR), CCBLD(victim, C_SPR),
                             CCNRM(victim, C_SPR), CCRED(victim, C_SPR), CCNRM(victim, C_SPR));
			}
			//
			// NPCs fleeing due to MORALE
			//

			if (ch && IS_NPC(victim) && ch != victim &&
				!KNOCKDOWN_SKILL(attacktype) &&
				GET_POSITION(victim) > POS_SITTING
				&& !MOB_FLAGGED(victim, MOB_SENTINEL) && !IS_DRAGON(victim)
				&& !IS_UNDEAD(victim) && GET_CLASS(victim) != CLASS_ARCH
				&& GET_CLASS(victim) != CLASS_DEMON_PRINCE
				&& GET_MOB_WAIT(ch) <= 0 && !MOB_FLAGGED(ch, MOB_SENTINEL)
				&& (100 - ((GET_HIT(victim) * 100) / GET_MAX_HIT(victim))) >
				GET_MORALE(victim) + number(-5, 10 + (GET_INT(victim) / 4))) {

				if (GET_HIT(victim) > 0) {
					int retval = 0;
					do_flee(victim, tmp_strdup(""), 0, 0, &retval);
					if (IS_SET(retval, DAM_ATTACKER_KILLED)) {
						DAM_RETURN(DAM_ATTACKER_KILLED);
					}

				} else {
					mudlog(LVL_DEMI, BRF, true,
						"ERROR: %s was at position %d with %d hit points and tried to flee.",
						GET_NAME(victim), GET_POSITION(victim),
						GET_HIT(victim));
				}
			}
			//
			// cyborgs sustaining internal damage and perhaps exploding
			//

			if (IS_CYBORG(victim) && !IS_NPC(victim) &&
				GET_TOT_DAM(victim) >= max_component_dam(victim)) {
				if (GET_BROKE(victim)) {
					if (!AFF3_FLAGGED(victim, AFF3_SELF_DESTRUCT)) {
						send_to_char(victim, "Your %s has been severely damaged.\r\n"
							"Systems cannot support excessive damage to this and %s.\r\n"
							"%sInitiating Self-Destruct Sequence.%s\r\n",
							component_names[number(1,
									NUM_COMPS)][GET_OLD_CLASS(victim)],
							component_names[(int)
								GET_BROKE(victim)][GET_OLD_CLASS(victim)],
							CCRED_BLD(victim, C_NRM), CCNRM(victim, C_NRM));
						act("$n has auto-initiated self destruct sequence!\r\n"
							"CLEAR THE AREA!!!!", false, victim, 0, 0,
							TO_ROOM | TO_SLEEP);
						MEDITATE_TIMER(victim) = 4;
						SET_BIT(AFF3_FLAGS(victim), AFF3_SELF_DESTRUCT);
						WAIT_STATE(victim, PULSE_VIOLENCE * 7);	/* Just enough to die */
					}
				} else {
					GET_BROKE(victim) = number(1, NUM_COMPS);
					act("$n staggers and begins to smoke!", false, victim, 0,
						0, TO_ROOM);
					send_to_char(victim, "Your %s has been damaged!\r\n",
						component_names[(int)
							GET_BROKE(victim)][GET_OLD_CLASS(victim)]);
					GET_TOT_DAM(victim) = 0;
				}

			}
			// this is the block that handles things that happen when
			// someone is damaging someone else.  ( not damaging self
			// or being damaged by a null char, e.g. bomb )
			if (ch && victim != ch) {

				//
				// see if the victim flees due to WIMPY
				//

				if (!IS_NPC(victim) &&
					GET_HIT(victim) < GET_WIMP_LEV(victim)
					&& GET_HIT(victim) > 0) {
					send_to_char(victim, "You wimp out, and attempt to flee!\r\n");
					if (KNOCKDOWN_SKILL(attacktype) && dam)
						setPosition(victim, POS_SITTING);

					int retval = 0;
					do_flee(victim, tmp_strdup(""), 0, 0, &retval);
					if (IS_SET(retval, DAM_ATTACKER_KILLED)) {
						DAM_RETURN(DAM_ATTACKER_KILLED);
					}

					break;
				}
				//
				// see if the vict flees due to FEAR
				//

				else if (affected_by_spell(victim, SPELL_FEAR) &&
					!number(0, (GET_LEVEL(victim) / 8) + 1)
					&& GET_HIT(victim) > 0) {
					if (KNOCKDOWN_SKILL(attacktype) && dam)
						setPosition(victim, POS_SITTING);

					int retval = 0;
					do_flee(victim, tmp_strdup(""), 0, 0, &retval);
					if (IS_SET(retval, DAM_ATTACKER_KILLED)) {
						DAM_RETURN(DAM_ATTACKER_KILLED);
					}

					break;
				}
				// this block handles things that happen IF and ONLY
				// IF the ch has initiated the attack.  We assume that
				// ch is attacking unless the damage is from a DEFENSE
				// ATTACK
				if (ch && !IS_DEFENSE_ATTACK(attacktype)) {

					// ch is initiating an attack ?  only if ch is not
					// "attacking" with a fireshield/energy
					// shield/etc...
					if (!ch->fighting) {

                        if (IS_NPC(victim)) {
                            // mages casting spells and shooters
                            // should be able to attack without
                            // initiating melee ( on their part at
                            // least )
                            if (attacktype != SKILL_ENERGY_WEAPONS &&
                                attacktype != SKILL_PROJ_WEAPONS &&
                                ch->in_room == victim->in_room &&
                                (!IS_MAGE(ch) || attacktype > MAX_SPELLS ||
                                    !SPELL_IS_MAGIC(attacktype))) {
                                addCombat(ch, victim, true);
                            }
                        }
                        else
                            addCombat(ch, victim, true);

					}
					// add ch to victim's shitlist( s )
					if (!IS_NPC(ch) && !PRF_FLAGGED(ch, PRF_NOHASSLE)) {
						if (MOB_FLAGGED(victim, MOB_MEMORY))
							remember(victim, ch);
						if (MOB2_FLAGGED(victim, MOB2_HUNT))
							startHunting(victim, ch);
					}
					// make the victim retailiate against the attacker
					if (findCombat(ch, victim)) {
						if (!findCombat(victim, ch)) {
                            addCombat(victim, ch, false);
                        }
					} else {
						if (!isFighting(victim) &&
                            ch->in_room == victim->in_room) {
                            addCombat(victim, ch, false);
                        }
					}
				}
			}
			break;
		}
	}							/* end switch ( GET_POSITION(victim) ) */

	// Victim is Linkdead
	// Send him to the void

	if (!IS_NPC(victim) && !(victim->desc)
		&& !is_arena_combat(ch, victim)) {

		act("$n is rescued by divine forces.", false, victim, 0, 0, TO_ROOM);
		GET_WAS_IN(victim) = victim->in_room;
		if (ch) {
			removeAllCombat(victim);
			removeCombat(ch, victim);
		}
		char_from_room(victim);
		char_to_room(victim, zone_table->world);
		act("$n is carried in by divine forces.", false, victim, 0, 0,
			TO_ROOM);
	}

	/* debugging message */
	if (ch && PRF2_FLAGGED(ch, PRF2_DEBUG))
		send_to_char(ch,
			"%s[DAMAGE] %s   dam:%d   wait:%d   pos:%d   reduct:%.2f%s\r\n",
			CCCYN(ch, C_NRM), GET_NAME(victim), dam,
			IS_NPC(victim) ? GET_MOB_WAIT(victim) :
				victim->desc ? victim->desc->wait : 0,
			GET_POSITION(victim), dam_reduction, CCNRM(ch, C_NRM));

	if (victim && ch != victim && PRF2_FLAGGED(victim, PRF2_DEBUG))
		send_to_char(victim,
			"%s[DAMAGE] %s   dam:%d   wait:%d   pos:%d   reduct:%.2f%s\r\n",
			CCCYN(victim, C_NRM), GET_NAME(victim), dam,
			IS_NPC(victim) ? GET_MOB_WAIT(victim) :
				victim->desc ? victim->desc->wait : 0,
			GET_POSITION(victim), dam_reduction, CCNRM(victim, C_NRM));

	//
	// Victim has been slain, handle all the implications
	// Exp, Kill counter, etc...
	//

	if (GET_POSITION(victim) == POS_DEAD) {
		bool arena = is_arena_combat(ch, victim);

		if (ch) {
			if (attacktype != SKILL_SNIPE) {
				gain_kill_exp(ch, victim);
			}

			if (!IS_NPC(victim)) {
				// Log the death
                char *logmsg = NULL;

				if (victim != ch) {
					const char *room_str;

					if (victim->in_room)
						room_str = tmp_sprintf("in room #%d (%s)",
							victim->in_room->number, victim->in_room->name);
					else
						room_str = "in NULL room!";
					if (IS_NPC(ch)) {
						logmsg = tmp_sprintf("%s killed by %s %s%s",
							GET_NAME(victim), GET_NAME(ch),
							affected_by_spell(ch, SPELL_QUAD_DAMAGE) ? "(quad)":"",
							room_str);
					} else {
						logmsg = tmp_sprintf("%s(%d:%d) pkilled by %s(%d:%d) %s",
							GET_NAME(victim), GET_LEVEL(victim),
							GET_REMORT_GEN(victim),
							GET_NAME(ch), GET_LEVEL(ch), GET_REMORT_GEN(ch),
							room_str);

                        // If this account has had an imm logged on in the
                        // last hour log it as such
                        int imm_idx = hasCharLevel(ch->account, LVL_AMBASSADOR);
                        if (imm_idx) {
                            struct creature *tmp_ch;
                            CREATE(tmp_ch, struct creature, 1);
                            loadFromXML(tmp_ch, get_char_by_index(ch->account, imm_idx));
                            if (GET_LEVEL(ch) < 70) {
                                int now = time(NULL);
                                int last_logon = tmp_ch->player.time.logon;
                                if ((now - last_logon) <= 3600) {
                                    mudlog(GET_INVIS_LVL(victim), BRF, true,
                                           "CHEAT:  %s(%d) logged within an hour!",
                                           tmp_ch->player.name, tmp_GET_LEVEL(ch));
                                }
                            }
                            free_creature(tmp_ch);
                        }
					}

                    // Tally kills for quest purposes
                    if (GET_QUEST(ch)) {
                        struct quest *quest;

                        quest = quest_by_vnum(GET_QUEST(ch));
                        if (quest)
                            tallyPlayerKill(quest, GET_IDNUM(ch));
                    }

					// If it's not arena, give em a pkill and adjust reputation
					if (!arena) {
						if (dam_object)
							check_object_killer(dam_object, victim);
						else
							count_pkill(ch, victim);
					} else {
						// Else adjust arena kills
						GET_ARENAKILLS(ch) += 1;
                    }
				} else {
					const char *attack_desc;

					if (attacktype <= TOP_NPC_SPELL)
						attack_desc = spell_to_str(attacktype);
					else {
						switch (attacktype) {
						case TYPE_TAINT_BURN:
							attack_desc = "taint burn"; break;
						case TYPE_PRESSURE:
							attack_desc = "pressurization"; break;
						case TYPE_SUFFOCATING:
							attack_desc = "suffocation"; break;
						case TYPE_ANGUISH:
							attack_desc = "soulless anguish"; break;
						case TYPE_BLEED:
							attack_desc = "hamstring bleeding"; break;
						case TYPE_OVERLOAD:
							attack_desc = "cybernetic overload"; break;
						case TYPE_SUFFERING:
							attack_desc = "blood loss"; break;
						default:
							attack_desc = NULL; break;
						}
					}
					if (attack_desc)
						logmsg = tmp_sprintf("%s died by %s in room #%d (%s}",
							GET_NAME(ch), attack_desc, ch->in_room->number,
							ch->in_room->name);
					else
						logmsg = tmp_sprintf("%s died in room #%d (%s}",
							GET_NAME(ch), ch->in_room->number,
							ch->in_room->name);
				}

				// If it's arena, log it for complete only
				// and tag it
				if (arena) {
					strcat(logmsg, " [ARENA]");
					qlog(NULL, logmsg, QLOG_COMP, GET_INVIS_LVL(victim), true);
				} else {
					mudlog(GET_INVIS_LVL(victim), BRF, true, "%s", logmsg);
				}
				if (MOB_FLAGGED(ch, MOB_MEMORY))
					forget(ch, victim);
				if (!IS_NPC(ch) && ch != victim)
					is_humil = true;
			} else {
				GET_MOBKILLS(ch) += 1;
                // Tally kills for quest purposes
                if (GET_QUEST(ch)) {
                    struct quest *quest;

                    quest = quest_by_vnum(GET_QUEST(ch));
                    if (quest)
                        tallyMobKill(quest, GET_IDNUM(ch));
                }

			}

			if (ch->char_specials.hunting && ch->char_specials.hunting == victim)
				stopHunting(ch);
			die(victim, ch, attacktype);
			DAM_RETURN(DAM_VICT_KILLED);

		}

		if (!IS_NPC(victim)) {
			mudlog(LVL_AMBASSADOR, BRF, true,
				"%s killed by NULL-char (type %d [%s]) in room #%d (%s)",
				GET_NAME(victim), attacktype,
				(attacktype > 0 && attacktype < TOP_NPC_SPELL) ?
				spell_to_str(attacktype) :
				(attacktype >= TYPE_HIT && attacktype <= TOP_ATTACKTYPE) ?
				attack_type[attacktype - TYPE_HIT] : "bunk",
				victim->in_room->number, victim->in_room->name);
		}
		die(victim, NULL, attacktype);
		DAM_RETURN(DAM_VICT_KILLED);
	}
	//
	// end if GET_POSITION(victim) == POS_DEAD
	//

	DAM_RETURN(0);
}

#undef DAM_RETURN

// Pick the next weapon that the creature will strike with
struct obj_data *
get_next_weap(struct creature *ch)
{
	struct obj_data *cur_weap;
	int dual_prob = 0;

	if (GET_EQ(ch, WEAR_WIELD_2) && GET_EQ(ch, WEAR_WIELD)) {
		dual_prob = (total_obj_weight(GET_EQ(ch, WEAR_WIELD)) -
                     total_obj_weight(GET_EQ(ch, WEAR_WIELD_2))) * 2;
	}

	// Check dual wield
	cur_weap = GET_EQ(ch, WEAR_WIELD_2);
	if (cur_weap && (IS_OBJ_TYPE(cur_weap, ITEM_WEAPON) || IS_ENERGY_GUN(cur_weap))) {
		if (affected_by_spell(ch, SKILL_NEURAL_BRIDGING)) {
			if ((CHECK_SKILL(ch, SKILL_NEURAL_BRIDGING) * 2 / 3) + dual_prob > number(50, 150)) {
				gain_skill_prof(ch, SKILL_NEURAL_BRIDGING);
				return cur_weap;
			}
		} else {
			if ((CHECK_SKILL(ch, SKILL_SECOND_WEAPON) * 2 / 3) + dual_prob > number(50, 150)) {
				gain_skill_prof(ch, SKILL_SECOND_WEAPON);
				return cur_weap;
			}
		}
	}

	// Check normal wield
	cur_weap = GET_EQ(ch, WEAR_WIELD);
	if (cur_weap && !number(0, 1))
		return cur_weap;

	// Check equipment on hands
	cur_weap = GET_EQ(ch, WEAR_HANDS);
	if (cur_weap && (IS_OBJ_TYPE(cur_weap, ITEM_WEAPON) || IS_ENERGY_GUN(cur_weap))
        && !number(0, 5))
		return cur_weap;

	// Check for implanted weapons in hands
	cur_weap = GET_IMPLANT(ch, WEAR_HANDS);
	if (cur_weap && !GET_EQ(ch, WEAR_HANDS) &&
			(IS_OBJ_TYPE(cur_weap, ITEM_WEAPON) || IS_ENERGY_GUN(cur_weap)) && !number(0, 2))
		return cur_weap;

	// Check for weapon on feet
	cur_weap = GET_EQ(ch, WEAR_FEET);
	if (cur_weap && (IS_OBJ_TYPE(cur_weap, ITEM_WEAPON) || IS_ENERGY_GUN(cur_weap)) &&
			CHECK_SKILL(ch, SKILL_KICK) > number(10, 170))
		return cur_weap;

	// Check equipment on head
	cur_weap = GET_EQ(ch, WEAR_HEAD);
	if (cur_weap && (IS_OBJ_TYPE(cur_weap, ITEM_WEAPON) || IS_ENERGY_GUN(cur_weap))
        && !number(0, 2))
		return cur_weap;

	cur_weap = GET_EQ(ch, WEAR_WIELD);
	if (cur_weap)
		return cur_weap;

	return GET_EQ(ch, WEAR_HANDS);
}

// Damage multiplier for the backstab skill.
// 7x max for levels.
// 6x max more for gens based on getLevelBonus.
static inline int BACKSTAB_MULT( struct creature *ch  ) {
	int mult = 2 + ( GET_LEVEL(ch) + 1 )/10;
	int bonus = MAX(0,get_skill_bonus(ch, SKILL_BACKSTAB) - 50);
	mult += ( 6 * bonus ) / 50;
	return mult;
}

//
// generic hit
//
// return values are same as damage()
//
// We should REALLY only return 0 from hit at the end of the
// function, indicating that everything went smoothly.  Otherwise, we should
// return the result of damage, or DAM_ATTACK_FAILED.

int
hit(struct creature *ch, struct creature *victim, int type)
{

	int w_type = 0, victim_ac, calc_thaco, dam, tmp_dam, diceroll, skill = 0;
	int i, metal_wt;
	byte limb;
	struct obj_data *weap = NULL;
	int retval;

	if (ch->in_room != victim->in_room) {
        removeCombat(ch, victim);
        removeCombat(victim, ch);
		return DAM_ATTACK_FAILED;
	}

    if (ch && victim && !ok_to_attack(ch, victim, true)) {
        return DAM_ATTACK_FAILED;
    }

	if (LVL_AMBASSADOR <= GET_LEVEL(ch) && GET_LEVEL(ch) < LVL_GOD &&
		IS_NPC(victim) && !mini_mud) {
		send_to_char(ch, "You are not allowed to attack mobiles!\r\n");
		return DAM_ATTACK_FAILED;
	}
	if (AFF2_FLAGGED(ch, AFF2_PETRIFIED) && GET_LEVEL(ch) < LVL_ELEMENT) {
		if (!number(0, 2))
			act("You want to fight back against $N's attack, but cannot!",
				false, ch, 0, victim, TO_CHAR | TO_SLEEP);
		else if (!number(0, 1))
			send_to_char(ch,
				"You have been turned to stone, and cannot fight!\r\n");
		else
			send_to_char(ch, "You cannot fight back!!  You are petrified!\r\n");

		return DAM_ATTACK_FAILED;
	}

	if (isNewbie(victim) && !IS_NPC(ch) && !IS_NPC(victim) &&
		!is_arena_combat(ch, victim) &&
		GET_LEVEL(ch) < LVL_IMMORT) {
		act("$N is currently under new character protection.",
			false, ch, 0, victim, TO_CHAR);
		act("You are protected by the gods against $n's attack!",
			false, ch, 0, victim, TO_VICT);
		slog("%s protected against %s ( hit ) at %d\n",
			GET_NAME(victim), GET_NAME(ch), victim->in_room->number);

		removeCombat(ch, victim);
		removeCombat(victim, ch);
		return DAM_ATTACK_FAILED;
	}

    if (checkReputations(ch, victim)) {
        removeCombat(ch, victim);
        removeCombat(victim, ch);
        return DAM_ATTACK_FAILED;
    }

	if (MOUNTED_BY(ch)) {
		if (MOUNTED_BY(ch)->in_room != ch->in_room) {
			REMOVE_BIT(AFF2_FLAGS(MOUNTED_BY(ch)), AFF2_MOUNTED);
			dismount(ch);
		} else
			send_to_char(ch, "You had better dismount first.\r\n");
		return DAM_ATTACK_FAILED;
	}
	if (isMounted(victim)) {
		REMOVE_BIT(AFF2_FLAGS(MOUNTED_BY(victim)), AFF2_MOUNTED);
		dismount(victim);
		act("You are knocked from your mount by $N's attack!",
			false, victim, 0, ch, TO_CHAR);
	}
	if (AFF2_FLAGGED(victim, AFF2_MOUNTED)) {
		REMOVE_BIT(AFF2_FLAGS(victim), AFF2_MOUNTED);
        void check_mount(struct creature *tch, gpointer ignore) {
			if (MOUNTED_BY(tch) && MOUNTED_BY(tch) == victim) {
				act("You are knocked from your mount by $N's attack!",
					false, tch, 0, ch, TO_CHAR);
				dismount(tch);
				setPosition(tch, POS_STANDING);
			}
		}
        g_list_foreach(victim->in_room->people, check_mount, 0);
	}

	for (i = 0, metal_wt = 0; i < NUM_WEARS; i++)
		if (ch->equipment[i] &&
			GET_OBJ_TYPE(ch->equipment[i]) == ITEM_ARMOR &&
			(IS_METAL_TYPE(ch->equipment[i]) ||
				IS_STONE_TYPE(ch->equipment[i])))
			metal_wt += GET_OBJ_WEIGHT(ch->equipment[i]);

	if ((type != SKILL_BACKSTAB && type != SKILL_CIRCLE &&
			type != SKILL_BEHEAD && type != SKILL_CLEAVE) || !cur_weap) {
		if (type == SKILL_IMPLANT_W || type == SKILL_ADV_IMPLANT_W)
			cur_weap = get_random_uncovered_implant(ch, ITEM_WEAPON);
		else
			cur_weap = get_next_weap(ch);
		if (cur_weap) {
			if ((IS_ENERGY_GUN(cur_weap)
                 && (!cur_weap->contains
                     || (cur_weap->contains
                         && CUR_ENERGY(cur_weap->contains) <= 0)))
                || IS_GUN(cur_weap)) {
				w_type = TYPE_BLUDGEON;
            } else if (IS_ENERGY_GUN(cur_weap) && cur_weap->contains) {
                w_type = GUN_TYPE(cur_weap) + TYPE_EGUN_LASER;
                int cost = MIN(CUR_ENERGY(cur_weap->contains), GUN_DISCHARGE(cur_weap));
                CUR_ENERGY(cur_weap->contains) -= cost;
                if (CUR_ENERGY(cur_weap->contains) <= 0) {
                    act("$p has been depleted of fuel.  Replace cell before further use.", false, ch, cur_weap, 0, TO_CHAR);
                }
            } else if (IS_OBJ_TYPE(cur_weap, ITEM_WEAPON))
				w_type = GET_OBJ_VAL(cur_weap, 3) + TYPE_HIT;
			else
				cur_weap = NULL;
		}
	}

	if (!cur_weap) {
		if (IS_NPC(ch) && (ch->mob_specials.shared->attack_type != 0))
			w_type = ch->mob_specials.shared->attack_type + TYPE_HIT;
		else if (IS_TABAXI(ch) && number(0, 1))
			w_type = TYPE_CLAW;
		else
			w_type = TYPE_HIT;
	}

	weap = cur_weap;
	calc_thaco = calculate_thaco(ch, victim, weap);

	victim_ac = MAX(GET_AC(victim), -(GET_LEVEL(ch) * 4)) / 15;

	diceroll = number(1, 20);

	if (PRF2_FLAGGED(ch, PRF2_DEBUG))
		send_to_char(ch, "%s[HIT] thac0:%d   roll:%d  AC:%d%s\r\n",
			CCCYN(ch, C_NRM), calc_thaco, diceroll, victim_ac,
			CCNRM(ch, C_NRM));

    if ((diceroll == 1)
			&& GET_POSITION(victim) >= POS_FIGHTING
			&& CHECK_SKILL(victim, SKILL_COUNTER_ATTACK) > 70) {
        act("You launch a counter attack!", false, victim, NULL, ch, TO_CHAR);
        return hit(victim, ch, TYPE_UNDEFINED);
    }
	/* decide whether this is a hit or a miss */
	if (diceroll < 20
			&& AWAKE(victim)
			&& ((diceroll == 1) || ((calc_thaco - diceroll)) > victim_ac)) {

		if (type == SKILL_BACKSTAB || type == SKILL_CIRCLE ||
			type == SKILL_SECOND_WEAPON || type == SKILL_CLEAVE )
			return (damage(ch, victim, 0, type, -1));
		else
			return (damage(ch, victim, 0, w_type, -1));
	}

	/* IT's A HIT! */

	if (CHECK_SKILL(ch, SKILL_DBL_ATTACK) > 60)
		gain_skill_prof(ch, SKILL_DBL_ATTACK);
	if (CHECK_SKILL(ch, SKILL_TRIPLE_ATTACK) > 60)
		gain_skill_prof(ch, SKILL_TRIPLE_ATTACK);
	if (affected_by_spell(ch, SKILL_NEURAL_BRIDGING)
		&& CHECK_SKILL(ch, SKILL_NEURAL_BRIDGING) > 60)
		gain_skill_prof(ch, SKILL_NEURAL_BRIDGING);

	/* okay, it's a hit.  calculate limb */

	limb = choose_random_limb(victim);

	if (type == SKILL_BACKSTAB || type == SKILL_CIRCLE)
		limb = WEAR_BACK;
	else if (type == SKILL_IMPALE || type == SKILL_LUNGE_PUNCH)
		limb = WEAR_BODY;
	else if (type == SKILL_BEHEAD)
		limb = WEAR_NECK_1;

	/* okay, we know the guy has been hit.  now calculate damage. */
	if (cur_weap && w_type >= TYPE_EGUN_LASER && w_type <= TYPE_EGUN_TOP) {
        dam = dex_app[GET_DEX(ch)].todam;
        dam += GET_HITROLL(ch);
	} else {
        dam = str_app[STRENGTH_APPLY_INDEX(ch)].todam;
        dam += GET_DAMROLL(ch);
	}

    // If a weapon is involved, apply bless/damn bonuses
    if (cur_weap) {
        if (IS_EVIL(victim) && IS_OBJ_STAT(cur_weap, ITEM_BLESS))
            dam += 5;
        if (IS_GOOD(victim) && IS_OBJ_STAT(cur_weap, ITEM_DAMNED))
            dam += 5;
    }

	tmp_dam = dam;

	if (cur_weap) {
		if (IS_OBJ_TYPE(cur_weap, ITEM_WEAPON) || IS_ENERGY_GUN(cur_weap)) {
			dam += dice(GET_OBJ_VAL(cur_weap, 1), GET_OBJ_VAL(cur_weap, 2));
			if (invalid_char_class(ch, cur_weap))
				dam /= 2;
			if (IS_NPC(ch)) {
				tmp_dam += dice(ch->mob_specials.shared->damnodice,
					ch->mob_specials.shared->damsizedice);
				dam = MAX(tmp_dam, dam);
			}
			if (GET_OBJ_VNUM(cur_weap) > 0) {
				for (i = 0; i < MAX_WEAPON_SPEC; i++)
					if (GET_WEAP_SPEC(ch, i).vnum == GET_OBJ_VNUM(cur_weap)) {
						dam += GET_WEAP_SPEC(ch, i).level;
						break;
					}
			}
			if (IS_TWO_HAND(cur_weap) && IS_BARB(ch) && !IS_ENERGY_GUN(cur_weap) &&
                cur_weap->worn_on == WEAR_WIELD)
            {
				int dam_add;
				dam_add = getWeight(cur_weap) / 4;
				if (CHECK_SKILL(ch, SKILL_DISCIPLINE_OF_STEEL) > 60) {
					int bonus = get_skill_bonus(ch,  SKILL_DISCIPLINE_OF_STEEL );
					int weight = getWeight(cur_weap);
					dam_add += ( bonus * weight ) / 100;
				}
				dam += dam_add;
			}
		} else if (IS_OBJ_TYPE(cur_weap, ITEM_ARMOR)) {
			dam += (GET_OBJ_VAL(cur_weap, 0) / 3);
		} else
			dam += dice(1, 4) + number(0, getWeight(cur_weap));
	} else if (IS_NPC(ch)) {
		tmp_dam += dice(ch->mob_specials.shared->damnodice,
			ch->mob_specials.shared->damsizedice);
		dam = MAX(tmp_dam, dam);
	} else {
		dam += number(0, 3);	/* Max. 3 dam with bare hands */
		if (IS_MONK(ch) || IS_VAMPIRE(ch))
			dam += number((GET_LEVEL(ch) / 4), GET_LEVEL(ch));
		else if (IS_BARB(ch))
			dam += number(0, (GET_LEVEL(ch) / 2));
	}

     //bludgeoning with our gun
    if (cur_weap && IS_ENERGY_GUN(cur_weap) &&
        !(w_type >= TYPE_EGUN_LASER && w_type <= TYPE_EGUN_TOP)) {
        dam /= 10;
    }

	dam = MAX(1, dam);			/* at least 1 hp damage min per hit */
	dam = MIN(dam, 30 + (GET_LEVEL(ch) * 8));	/* level limit */

	if( type == SKILL_CLEAVE ) {
		if (GET_SKILL(ch, SKILL_GREAT_CLEAVE) > 50)
			dam *= 5 + GET_REMORT_GEN(ch) / 5;
		else
			dam *= 4;
		w_type = SKILL_CLEAVE;
	}

	if (type == SKILL_BACKSTAB) {
		gain_skill_prof(ch, type);
		if (IS_THIEF(ch)) {
			dam *= BACKSTAB_MULT(ch);
			dam +=
				number(0, (CHECK_SKILL(ch,
						SKILL_BACKSTAB) - LEARNED(ch)) / 2);
		}

		retval = damage(ch, victim, dam, SKILL_BACKSTAB, WEAR_BACK);

		if (retval) {
			return retval;
		}
	} else if (type == SKILL_CIRCLE) {
		if (IS_THIEF(ch)) {
			dam *= MAX(2, (BACKSTAB_MULT(ch) / 2));
			dam +=
				number(0, (CHECK_SKILL(ch, SKILL_CIRCLE) - LEARNED(ch)) / 2);
		}
		gain_skill_prof(ch, type);

		retval = damage(ch, victim, dam, SKILL_CIRCLE, WEAR_BACK);

		if (retval) {
			return retval;
		}

	} else {
        int ablaze_level = 0;
		if (cur_weap && IS_OBJ_TYPE(cur_weap, ITEM_WEAPON) && !IS_ENERGY_GUN(cur_weap) ) {
			if( GET_OBJ_VAL(cur_weap, 3) >= 0 &&
                GET_OBJ_VAL(cur_weap, 3) < TOP_ATTACKTYPE - TYPE_HIT &&
                IS_BARB(ch)) {
                skill = weapon_proficiencies[GET_OBJ_VAL(cur_weap, 3)];
                if (skill)
                    gain_skill_prof(ch, skill);
            }
            if( IS_OBJ_STAT2(cur_weap, ITEM2_ABLAZE) ) {
                struct tmp_obj_affect *af = affectedBySpell(cur_weap, SPELL_FLAME_OF_FAITH);
                if( af != NULL ) {
                    dam += number(1,10);
                    ablaze_level = af->level;
                }
            }
		}
        if (w_type >= TYPE_EGUN_LASER && w_type <= TYPE_EGUN_TOP
            && GET_SKILL(ch, SKILL_ENERGY_WEAPONS) > 60) {
            gain_skill_prof(ch, SKILL_ENERGY_WEAPONS);
        }

		retval = damage(ch, victim, dam, w_type, limb);

		if (retval) {
			return retval;
		}

        // ignite the victim if applicable
        if( ablaze_level > 0 && !AFF2_FLAGGED(victim, AFF2_ABLAZE) ) {
            if (!mag_savingthrow(victim, 10, SAVING_SPELL)
             && !CHAR_WITHSTANDS_FIRE(victim))
            {
                act("$n's body suddenly ignites into flame!",
                    false, victim, 0, 0, TO_ROOM);
                act("Your body suddenly ignites into flame!",
                    false, victim, 0, 0, TO_CHAR);
                ignite(victim, ch);
            }
        }

		if (weap && IS_FERROUS(weap) && IS_NPC(victim)
			&& GET_MOB_SPEC(victim) == rust_monster) {

			if ((!IS_OBJ_STAT(weap, ITEM_MAGIC) ||
					mag_savingthrow(ch, GET_LEVEL(victim), SAVING_ROD)) &&
				(!IS_OBJ_STAT(weap, ITEM_MAGIC_NODISPEL) ||
					mag_savingthrow(ch, GET_LEVEL(victim), SAVING_ROD))) {

				act("$p spontaneously oxidizes and crumbles into a pile of rust!", false, ch, weap, 0, TO_CHAR);
				act("$p crumbles into rust on contact with $N!",
					false, ch, weap, victim, TO_ROOM);

				extract_obj(weap);

				if ((weap = read_object(RUSTPILE)))
					obj_to_room(weap, ch->in_room);
				return DAM_ATTACK_FAILED;
			}
		}

		if (weap && IS_OBJ_TYPE(weap, ITEM_WEAPON) &&
			weap == GET_EQ(ch, weap->worn_on)) {
			if (GET_OBJ_SPEC(weap)) {
				GET_OBJ_SPEC(weap) (ch, weap, 0, NULL, SPECIAL_COMBAT);
			} else if (IS_OBJ_STAT2(weap, ITEM2_CAST_WEAPON))
				retval = do_casting_weapon(ch, weap);
				if (retval)
					return retval;
		}
	}

	if (!IS_NPC(ch) && GET_MOVE(ch) > 20) {
		GET_MOVE(ch)--;
		if (IS_DROW(ch) && ch->in_room && OUTSIDE(ch) && PRIME_MATERIAL_ROOM(ch->in_room) &&
			ch->in_room->zone->weather->sunlight == SUN_LIGHT)
			GET_MOVE(ch)--;
		if (IS_CYBORG(ch) && GET_BROKE(ch))
			GET_MOVE(ch)--;
	}

	return 0;
}

int
do_casting_weapon(struct creature *ch, struct obj_data *weap)
{
	struct obj_data *weap2;

	if (GET_OBJ_VAL(weap, 0) < 0 || GET_OBJ_VAL(weap, 0) > TOP_SPELL_DEFINE) {
		slog("Invalid spell number detected on weapon %d", GET_OBJ_VNUM(weap));
		return 0;
	}

	if ((SPELL_IS_MAGIC(GET_OBJ_VAL(weap, 0)) ||
			IS_OBJ_STAT(weap, ITEM_MAGIC)) &&
			ROOM_FLAGGED(ch->in_room, ROOM_NOMAGIC))
		return 0;
	if (SPELL_IS_PHYSICS(GET_OBJ_VAL(weap, 0)) &&
			ROOM_FLAGGED(ch->in_room, ROOM_NOSCIENCE))
		return 0;
	if (SPELL_IS_PSIONIC(GET_OBJ_VAL(weap, 0)) &&
			ROOM_FLAGGED(ch->in_room, ROOM_NOPSIONICS))
		return 0;
	if (number(0, MAX(2, LVL_GRIMP + 28 - GET_LEVEL(ch) - GET_INT(ch) -
			(CHECK_SKILL(ch, GET_OBJ_VAL(weap, 0)) / 8))))
		return 0;

    const char *action_msg = NULL;
	if (weap->action_desc)
		action_msg = weap->action_desc;
	else
		action_msg = tmp_sprintf("$p begins to hum and shake%s!",
                                 weap->worn_on == WEAR_WIELD ||
                                 weap->worn_on == WEAR_WIELD_2 ? " in your hand" : "");
	send_to_char(ch, "%s", CCCYN(ch, C_NRM));
	act(action_msg, false, ch, weap, 0, TO_CHAR);
	send_to_char(ch, "%s", CCNRM(ch, C_NRM));

	act(tmp_sprintf("$p begins to hum and shake%s!",
                    weap->worn_on == WEAR_WIELD ||
                    weap->worn_on == WEAR_WIELD_2 ? " in $n's hand" : ""),
        true, ch, weap, 0, TO_ROOM);
	if ((((!IS_DWARF(ch) && !IS_CYBORG(ch)) ||
				!IS_OBJ_STAT(weap, ITEM_MAGIC) ||
				!SPELL_IS_MAGIC(GET_OBJ_VAL(weap, 0))) &&
			number(0, GET_INT(ch) + 3)) || GET_LEVEL(ch) > LVL_GRGOD) {
		if (IS_SET(spell_info[GET_OBJ_VAL(weap, 0)].routines, MAG_DAMAGE) ||
				spell_info[GET_OBJ_VAL(weap, 0)].violent ||
				IS_SET(spell_info[GET_OBJ_VAL(weap, 0)].targets,
					TAR_UNPLEASANT)) {
            if (ch->fighting) {
                struct creature *vict = random_opponent(ch);
			    call_magic(ch, vict, 0, NULL, GET_OBJ_VAL(weap, 0),
				           GET_LEVEL(ch), CAST_WAND, NULL);
            }
        }
		else if (!affected_by_spell(ch, GET_OBJ_VAL(weap, 0)))
			call_magic(ch, ch, 0, NULL, GET_OBJ_VAL(weap, 0), GET_LEVEL(ch),
                       CAST_WAND, NULL);
	} else {
		// drop the weapon
		if ((weap->worn_on == WEAR_WIELD ||
				weap->worn_on == WEAR_WIELD_2) &&
			GET_EQ(ch, weap->worn_on) == weap) {
			act("$p shocks you!  You are forced to drop it!",
				false, ch, weap, 0, TO_CHAR);

			// weapon is the 1st of 2 wielded
			if (weap->worn_on == WEAR_WIELD &&
				GET_EQ(ch, WEAR_WIELD_2)) {
				weap2 = unequip_char(ch, WEAR_WIELD_2, EQUIP_WORN);
				obj_to_room(unequip_char(ch, weap->worn_on, EQUIP_WORN),
					ch->in_room);
				if (equip_char(ch, weap2, WEAR_WIELD, EQUIP_WORN))
					return DAM_ATTACKER_KILLED;
			}
			// weapon should fall to ground
			else if (number(0, 20) > GET_DEX(ch))
				obj_to_room(unequip_char(ch, weap->worn_on, EQUIP_WORN),
					ch->in_room);
			// weapon should drop to inventory
			else
				obj_to_char(unequip_char(ch, weap->worn_on, EQUIP_WORN),
					ch);

		}
		// just shock the victim
		else {
			act("$p blasts the hell out of you!  OUCH!!",
				false, ch, weap, 0, TO_CHAR);
			GET_HIT(ch) -= dice(3, 4);
		}
		act("$n cries out as $p shocks $m!", false, ch, weap, 0,
			TO_ROOM);
	}
	return 0;
}

//this function only determines whether a gun special should be performed
//actual performance code is in damage because the different special types
//require access to different damage variables at different times
bool
do_gun_special(struct creature *ch, struct obj_data *obj)
{
    if (!IS_ENERGY_GUN(obj) || !EGUN_CUR_ENERGY(obj)) {
        return false;
    }

    //calculation code here, future skills could and should affect this
    if (GET_OBJ_VAL(obj, 3) == EGUN_PARTICLE) {
        int penetrate = MAX(3,35-(GET_HITROLL(ch)/10)-GET_DEX(ch));
        if (number(0, penetrate)) {
            return false;
        }

    } else if (GET_OBJ_VAL(obj, 3) == EGUN_LIGHTNING) {
        //basic chance to chain
        bool chain = !number(0, MAX(5, LVL_GRIMP + 28 - GET_LEVEL(ch) - GET_DEX(ch) -
                    (CHECK_SKILL(ch, SKILL_ENERGY_WEAPONS) / 8)));
        //in water we get a second chance
        if (room_is_watery(ch->in_room) && !number(0,3))
            chain = true;
        return chain;
    } else if (GET_OBJ_VAL(obj, 3) == EGUN_PLASMA) {
        return number(0, MAX(0,get_skill_bonus(ch, SKILL_ENERGY_WEAPONS))/20);//almost always ignite if applicable
    } else if (number(0, MAX(2, LVL_GRIMP + 28 - GET_LEVEL(ch) - GET_DEX(ch) -
                (CHECK_SKILL(ch, SKILL_ENERGY_WEAPONS) / 8)))) {
        return false;
    }

    return true;
}

void
perform_violence1(struct creature *ch, gpointer ignore)
{
	int prob, i, die_roll;

    if (!ch->in_room || !ch->fighting)
        return;
    if (ch == findCombat(ch, ch)) {	// intentional crash here.
        errlog("ch == findCombat(ch, ch) in perform_violence.");
        raise(SIGSEGV);
    }

    if (AFF2_FLAGGED(ch, AFF2_PETRIFIED) ||
        (IS_NPC(ch) && ch->in_room->zone->idle_time >= ZONE_IDLE_TIME)) {
        remove_all_combat(ch);
        return;
    }

    if (!ch->fighting)
        return;

    if (IS_NPC(ch)) {
        if (GET_MOB_WAIT(ch) > 0) {
            GET_MOB_WAIT(ch) = MAX(GET_MOB_WAIT(ch) - SEG_VIOLENCE, 0);
        } else if (GET_MOB_WAIT(ch) == 0) {
            update_pos(ch);
        }
        if (GET_POSITION(ch) <= POS_SITTING)
            return;
    }
    // Make sure they're fighting before they fight.
    if (GET_POSITION(ch) == POS_STANDING
        || GET_POSITION(ch) == POS_FLYING) {
        GET_POSITION(ch) = POS_FIGHTING;
        return;
    }

    // Moved all attack prob stuff to this function.
    // STOP THE INSANITY! --N
    prob = calculate_attack_probability(ch);

    die_roll = number(0, 300);

    if (PRF2_FLAGGED(ch, PRF2_DEBUG)) {
        send_to_char(ch,
                     "%s[COMBAT] %s   prob:%d   roll:%d   wait:%d%s\r\n",
                     CCCYN(ch, C_NRM), GET_NAME(ch), prob, die_roll,
                     IS_NPC(ch) ? GET_MOB_WAIT(ch) : (CHECK_WAIT(ch) ? ch->desc->
                                                      wait : 0),
                     CCNRM(ch, C_NRM));
    }
    //
    // it's an attack!
    //
    if (MIN(100, prob + 15) >= die_roll) {

        bool stop = false;

        for (i = 0; i < 4; i++) {
            if (!ch->fighting || GET_LEVEL(ch) < (i * 8))
                break;
            if (GET_POSITION(ch) < POS_FIGHTING) {
                if (CHECK_WAIT(ch) < 10)
                    send_to_char(ch, "You can't fight while sitting!!\r\n");
                break;
            }

            if (prob >= number((i * 16) + (i * 8), (i * 32) + (i * 8))) {
                int retval = hit(ch, random_opponent(ch), TYPE_UNDEFINED);
                if (IS_SET(retval, DAM_ATTACKER_KILLED) ||
                    IS_SET(retval, DAM_VICT_KILLED)) {
                    stop = true;
                    break;
                }
            }
        }

        if (stop)
            return;

        if (IS_CYBORG(ch)) {
            int implant_prob;

            if (!ch->fighting)
                return;

            if (GET_POSITION(ch) < POS_FIGHTING) {
                if (CHECK_WAIT(ch) < 10)
                    send_to_char(ch, "You can't fight while sitting!!\r\n");
                return;
            }

            if (number(1, 100) < CHECK_SKILL(ch, SKILL_ADV_IMPLANT_W)) {

                implant_prob = 25;

                if (CHECK_SKILL(ch, SKILL_ADV_IMPLANT_W) > 100) {
                    implant_prob +=
                        GET_REMORT_GEN(ch) + (CHECK_SKILL(ch,
                                                          SKILL_ADV_IMPLANT_W) - 100) / 2;
                }

                if (number(0, 100) < implant_prob) {
                    int retval =
                        hit(ch, random_opponent(ch), SKILL_ADV_IMPLANT_W);
                    if (retval)
                        return;
                }
            }

            if (!ch->fighting)
                return;

            if (IS_NPC(ch) && (GET_REMORT_CLASS(ch) == CLASS_UNDEFINED
                               || GET_CLASS(ch) != CLASS_CYBORG))
                return;

            if (number(1, 100) < CHECK_SKILL(ch, SKILL_IMPLANT_W)) {
                implant_prob = 25;

                if (CHECK_SKILL(ch, SKILL_IMPLANT_W) > 100) {
                    implant_prob +=
                        GET_REMORT_GEN(ch) + (CHECK_SKILL(ch,
                                                          SKILL_IMPLANT_W) - 100) / 2;
                }

                if (number(0, 100) < implant_prob) {
                    int retval = hit(ch, random_opponent(ch), SKILL_IMPLANT_W);
                    if (retval)
                        return;
                }
            }
        }
    } else if (IS_NPC(ch) && ch->in_room &&
             GET_POSITION(ch) == POS_FIGHTING &&
             GET_MOB_WAIT(ch) <= 0 && (MIN(100, prob) >= number(0, 300))) {

        if (MOB_FLAGGED(ch, MOB_SPEC) && ch->in_room &&
            ch->mob_specials.shared->func && !number(0, 2)) {

            (ch->mob_specials.shared->func) (ch, ch, 0, tmp_strdup(""), SPECIAL_TICK);
        } else if (ch->in_room && GET_MOB_WAIT(ch) <= 0 && ch->fighting) {
            mobile_battle_activity(ch, NULL);
        }
    }
}

/* control the fights.  Called every 0.SEG_VIOLENCE sec from comm.c. */
void
perform_violence(void)
{
    g_list_foreach(creatures, perform_violence1, 0);
}


#undef __combat_code__