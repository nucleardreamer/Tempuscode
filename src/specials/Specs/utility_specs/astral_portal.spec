//
// File: astral_portal.spec                     -- Part of TempusMUD
//
// Copyright 1998 by John Watson, all rights reserved.
//

SPECIAL(astral_portal)
{
  struct obj_data *portal = (struct obj_data *) me;
  skip_spaces(&argument); 

    if( spec_mode != SPECIAL_CMD )
        return 0;

  if (!CMD_IS("enter") || portal->worn_by || 
                     (portal->carried_by && portal->carried_by != ch))
    return 0;
  if (!*argument)  {
    send_to_char(ch, "Enter what?\r\n");
    return 1;
  }
  if (isname(argument, portal->name)) {
    act("$n steps into $p.", FALSE, ch, portal, 0, TO_ROOM);
    act("You step into $p.", FALSE, ch, portal, 0, TO_CHAR);
    call_magic(ch, ch, 0, SPELL_ASTRAL_SPELL, GET_LEVEL(ch), CAST_SPELL); 
    return 1;
  }
  return 0;
}
