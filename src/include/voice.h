#ifndef _VOICE_H_
#define _VOICE_H_

/*************************************************************************
 *  File: voice.h                                                     *
 *  Usage: Source file for multi-voice support                        *
 *                                                                       *
 *  All rights reserved.  See license.doc for complete information.      *
 *                                                                       *
 *********************************************************************** */

//
// File: voice.h                            -- Part of TempusMUD
//

#include <stdio.h>
#include <map>
#include <string>
#include <list>
#include "creature.h"

enum voice_situation {
    VOICE_TAUNTING = 0,         // NPC remembers creature in room
    VOICE_ATTACKING,            // NPC attacking remembered creature
    VOICE_PANICKING,            // NPC is running away from remembered
    VOICE_HUNT_FOUND,           // Hunter found his prey
    VOICE_HUNT_LOST,            // Hunter lost his prey
    VOICE_HUNT_GONE,            // Hunter's prey disappeared
    VOICE_HUNT_TAUNT,           // Hunter is busy hunting
    VOICE_HUNT_UNSEEN,          // Hunter found invisible prey
    VOICE_HUNT_OPENAIR,         // Hunter can't track over air
    VOICE_HUNT_WATER,           // Hunter can't track over water
    VOICE_FIGHT_WINNING,        // NPC is winning the fight
    VOICE_FIGHT_LOSING,         // NPC is losing the fight
    VOICE_FIGHT_HELPING,        // NPC is assisting another NPC
    VOICE_OBEYING,              // NPC is obeying a command
};

enum {
    VOICE_NONE = 0,
    VOICE_MOBILE = 1,
    VOICE_ANIMAL = 2
};

struct Voice {
    struct  {
        char situation;
        char *command;
    };
    Voice();
    Voice(const Voice &o);
    ~Voice(void);
    Voice &operator=(const Voice &o);

    void clear(void);
    bool load(xmlNodePtr node);
    void perform(struct creature *ch, void *vict, voice_situation situation);

    int _idnum;
    std_string _name;
    std_map<int, std::vector< std::string > > _emits;
};

extern std_map<int,Voice> voices;

#define GET_VOICE(ch) ((ch)->mob_specials.shared->voice)

const char *voice_name(int voice_idx);
int find_voice_idx_by_name(const char *voice_name);
void emit_voice(struct creature *ch, void *vict, voice_situation situation);
void show_voices(struct creature *ch);

#endif
