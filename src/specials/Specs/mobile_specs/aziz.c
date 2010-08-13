//
// File: aziz.spec                     -- Part of TempusMUD
//
// Copyright 1998 by John Watson, all rights reserved.
//

ACMD(do_bash);

SPECIAL(Aziz)
{
	struct creature *vict = NULL;

	if (spec_mode != SPECIAL_TICK)
		return 0;
	if (!ch->fighting)
		return 0;

	/* pseudo-randomly choose a mage in the room who is fighting me */
    CombatDataList_iterator li = ch->getCombatList()->begin();
    for (; li != ch->getCombatList()->end(); ++li) {
        if (IS_MAGE(li->getOpponent()) && number(0, 1)) {
            vict = li->getOpponent();
            break;
        }
    }

	/* if I didn't pick any of those, then just slam the guy I'm fighting */
	if (vict == NULL)
		vict = random_opponent(ch);

	do_bash(ch, tmp_strdup(""), 0, 0, 0);

	return 0;
}