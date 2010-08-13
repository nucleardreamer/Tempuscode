//
// File: editor.c                      -- Part of TempusMUD
//
// All modifications and additions are
// Copyright 1998 by John Watson, all rights reserved.
//
//

#ifdef HAS_CONFIG_H
#include "config.h"
#endif

#include <string>
#include <list>
using namespace std;
#include <ctype.h>
#include <fcntl.h>
// Tempus Includes
#include "screen.h"
#include "desc_data.h"
#include "comm.h"
#include "db.h"
#include "utils.h"
#include "login.h"
#include "interpreter.h"
#include "boards.h"
#include "mail.h"
#include "editor.h"
#include "tmpstr.h"
#include "accstr.h"
#include "help.h"
#include "comm.h"
#include "player_table.h"

extern struct descriptor_data *descriptor_list;
extern HelpCollection *Help;

// Constructor
// Params: Users descriptor, The final destination of the text,
//      the max size of the text.
CEditor_CEditor(struct descriptor_data *d, int max)
    :theText()
{
	desc = d;
    curSize = 0;
	maxSize = max;
    wrap = true;
}

void
CEditor_SendStartupMessage(void)
{
    send_to_desc(desc, "&C     * &YTEDII &b]&n Save and exit with @ on a new line. &&H for help             &C*\r\n");
    send_to_desc(desc, "     ");
	for (int i = 0; i < 7; i++)
        send_to_desc(desc, "&C%d&B---------", i);
    send_to_desc(desc, "&C7&n\r\n");
}

void
CEditor_SendPrompt(void)
{
    send_to_desc(desc, "%3zd&b]&n ", theText.size() + 1);
}

void
CEditor_Finish(bool save)
{

    if (save) {
        list <string>_iterator itr;
        int length;
        char *text;

        length = curSize + (theText.size() * 2);
        text = (char *)malloc(sizeof(char) * length + 3);
        strcpy(text, "");
        for (itr = theText.begin(); itr != theText.end(); itr++) {
            strcat(text, itr->c_str());
            strcat(text, "\r\n");
        }

        // Call the finalizer
        this->Finalize(text);

        free(text);
    } else {
        this->Cancel();
    }

	if (IS_PLAYING(desc) && desc->creature && !IS_NPC(desc->creature)) {
        // Remove "using the editor bits"
        REMOVE_BIT(PLR_FLAGS(desc->creature),
                   PLR_WRITING | PLR_OLC | PLR_MAILING);
        REMOVE_BIT(PRF2_FLAGS(desc->creature), PRF2_NOWRAP);
	}

    // Free the editor
	desc->text_editor = NULL;
	delete this;
}

void
CEditor_Cancel(void)
{
    // Do nothing
}

void
CEditor_Process(char *inStr)
{
	// 2 special chars, @ and &
	char inbuf[MAX_INPUT_LENGTH + 1];
    char *args;

	strncpy(inbuf, inStr, MAX_INPUT_LENGTH);

	if (*inbuf == '&') {		// Commands
        if (!inbuf[1]) {
            SendMessage("& is the command character. Type &h for help.\r\n");
            return;
        }
        args = inbuf + 2;
        while (*args && isspace(*args))
            args++;
        if (!this->PerformCommand(inbuf[1], args))
            SendMessage("Invalid Command. Type &h for help.\r\n");
	} else if (*inbuf == '@') {	// Finish up
        Finish(true);
	} else {					// Dump the text in
		Append(inbuf);
	}
}

void
CEditor_DisplayBuffer(unsigned int start_line, int line_count)
{
	list <string>_iterator itr;
	unsigned int linenum, end_line;

    acc_string_clear();

    // Calculate last line number we're going to show
    end_line = theText.size() + 1;
    if (line_count >= 0 && start_line + line_count < end_line)
        end_line = start_line + line_count;

    // Set the iterator to the beginning line
	itr = theText.begin();
    for (linenum = 1;linenum < start_line && itr != theText.end();linenum++)
         itr++;

    // Display the lines from the beginning line to the end, making
    // sure we don't overflow the LARGE_BUF desc buffer
	for (linenum = start_line;linenum < end_line;linenum++, itr++) {
		acc_sprintf("%3d%s%s]%s %s\r\n", linenum,
                    CCBLD(desc->creature, C_CMP),
                    CCBLU(desc->creature, C_NRM),
                    CCNRM(desc->creature, C_NRM),
                    itr->c_str());
		if (acc_get_length() > (LARGE_BUFSIZE - 1024))
			break;
	}

    acc_strcat("\r\n", NULL);
	if (acc_get_length() > (LARGE_BUFSIZE - 1024))
        acc_strcat("Output buffer limit reached. Use \"&r <line number>\" to specify starting line.\r\n", NULL);

	SendMessage(acc_get_string());
}

bool
CEditor_Full(char *inStr)
{
	if ((strlen(inStr) + curSize) + ((theText.size() + 1) * 2) > maxSize) {
		return true;
	}
	return false;
}

void
CEditor_Append(char *inStr)
{

	if (Full(inStr)) {
		SendMessage("Error: The buffer is full.\r\n");
		return;
	}
	// All tildes must die
	if (PLR_FLAGGED(desc->creature, PLR_OLC)) {
		char *readPt, *writePt;

		readPt = writePt = inStr;
		while (*readPt) {
			if (*readPt != '~')
				*writePt++ = *readPt;
			readPt++;
		}
		*writePt = '\0';
	}
	theText.push_back(inStr);
    if (wrap)
	    Wrap();
	UpdateSize();
}

bool
CEditor_Insert(unsigned int line, char *inStr)
{
	string text;
	list<string>_iterator s;

    if (line < 1)
        line = 1;
	if (*inStr)
		inStr++;
	text = inStr;
	if (line > theText.size()) {
		SendMessage("You can't insert before a line that doesn't exist.\r\n");
		return false;
	}
	if ((text.length() + curSize) + ((theText.size() + 1) * 2) > maxSize) {
		SendMessage("Error: The buffer is full.\r\n");
		return false;
	}

    s = theText.begin();
    advance(s, line - 1);

	// Insert the new text
	theText.insert(s, text);
    if (wrap)
	    Wrap();
	UpdateSize();

	return true;
}

bool
CEditor_ReplaceLine(unsigned int line, char *inStr)
{
	string text;
	list <string>_iterator s;

	if (*inStr && *inStr == ' ')
		inStr++;
	text = inStr;

	if (line < 1 || line > theText.size()) {
		SendMessage("There's no line to replace there.\r\n");
		return false;
	}
	// Find the line
    s = theText.begin();
    advance(s, line - 1);

	// Make sure we can fit the new stuff in
	if ((text.length() + curSize - s->length()) +
		((theText.size() + 1) * 2) > maxSize) {
		SendMessage("Error: The buffer is full.\r\n");
		return false;
	}
    *s = text;

    if (wrap)
	    Wrap();
	UpdateSize();
	return true;
}

bool
CEditor_MoveLines(unsigned int start_line,
                   unsigned int end_line,
                   unsigned int dest_line)
{
	list<string>_iterator dest, begin, end;

    if (start_line < 1 || start_line > theText.size()) {
        if (start_line == end_line)
            SendMessage("Line %d is an invalid line.\r\n");
        else
            SendMessage("Starting line %d is an invalid line.\r\n");
        return false;
    }
    if (end_line < 1 || end_line > theText.size()) {
        SendMessage("Ending line %d is an invalid line.\r\n");
        return false;
    }
    if (dest_line < 1)
        dest_line = 1;
    if (dest_line >= theText.size())
        dest_line = theText.size() + 1;

    begin = theText.begin();
    advance(begin, start_line - 1);
    end = theText.begin();
    advance(end, end_line);
    dest = theText.begin();
    advance(dest, dest_line - 1);

    theText.splice(dest, theText, begin, end);

    if (start_line == end_line) {
        SendMessage(tmp_sprintf("Moved line %d above line %d.\r\n",
                                start_line, dest_line));
    } else {
        SendMessage(tmp_sprintf("Moved lines %d-%d to the line above line %d.\r\n",
                                start_line, end_line, dest_line));
    }
	return true;
}

bool
CEditor_Find(char *args)
{
	list <string>_iterator itr;
    string pattern(args);
	unsigned int i;

    acc_string_clear();

	itr = theText.begin();

	for (i = 1; itr != theText.end(); i++, itr++) {
        if (itr->find(pattern, 0) >= itr->length())
            continue;
		acc_sprintf("%3d%s%s]%s %s\r\n", i,
                    CCBLD(desc->creature, C_CMP),
                    CCBLU(desc->creature, C_NRM),
                    CCNRM(desc->creature, C_NRM),
                    itr->c_str());
		// Overflowing the LARGE_BUF desc buffer.
		if (acc_get_length() > 10240)
			break;
	}

	if (acc_get_length() > 10240)
		acc_strcat("Search result limit reached.\r\n", NULL);
    acc_strcat("\r\n", NULL);
	SendMessage(acc_get_string());

    return true;
}

bool
CEditor_Substitute(char *args)
{
    const char *usage = "There are two formats for substitute:\r\n  &s [search pattern] [replacement]\r\n  &s /search pattern/replacement/\r\nIn the first form, you can use (), [], <>, or {}.\r\n";
	// Iterator to the current line in theText
	list <string>_iterator line;
	// The string containing the search pattern
	string pattern;
	// String containing the replace pattern
	string replacement;
	// Number of replacements made
	int replaced = 0;
	// read pointer and write pointer.
	char *temp, end_char;
    bool balanced;
    int size_delta;

    while (isspace(*args))
        args++;

	if (!*args) {
		SendMessage(usage);
		return false;
	}

    // First non-space character is the search delimiter.  If the
    // delimiter is something used with some kind of brace, it
    // terminates in the opposite brace
    balanced = true;
    switch (*args) {
    case '(':
        end_char = ')'; break;
    case '[':
        end_char = ']'; break;
    case '{':
        end_char = '}'; break;
    case '<':
        end_char = '>'; break;
    default:
        end_char = *args;
        balanced = false;
        break;
    }
    args++;

    temp = args;
    while (*args && *args != end_char)
        args++;
    if (!*args) {
        SendMessage(usage);
        return false;
    }

    *args++ = '\0';
    pattern = temp;

    // If the pattern was specified with a balanced delimiter, then
    // the replacement must be, too
    if (balanced) {
        while (*args
               && *args != '('
               && *args != '['
               && *args != '{'
               && *args != '<')
            args++;
        if (!*args) {
            SendMessage("If the search pattern uses a balanced delimiter, the replacement must use a balanced\r\ndelimiter as well.\r\n");
            return false;
        }

        switch (*args) {
        case '(':
            end_char = ')'; break;
        case '[':
            end_char = ']'; break;
        case '{':
            end_char = '}'; break;
        case '<':
            end_char = '>'; break;
            break;
        default:
            end_char = *args; break;
        }
        args++;
    }

    temp = args;
    while (*args && *args != end_char)
        args++;

    *args = '\0';
    replacement = temp;

    size_delta = replacement.length() - pattern.length();

	// Find pattern in theText a line at a time and replace each instance.
	unsigned int pos;			// Current position in the line
	bool overflow = false;		// Have we overflowed the buffer?

	for (line = theText.begin(); !overflow && line != theText.end(); line++) {
        pos = line->find(pattern, 0);
		while (pos < line->length()) {
            // Handle both buffer size and replacement overflows
            if (curSize + size_delta > maxSize || replaced >= 100) {
                overflow = true;
                break;
            }
            *line = line->replace(pos, pattern.length(), replacement);
            replaced++;
            curSize += size_delta;
            pos = line->find(pattern, pos + replacement.length());
		}
	}
    if (curSize + size_delta > maxSize)
        SendMessage("Error: The buffer is full.\r\n");
    if (replaced >= 100)
        SendMessage("Replacement limit of 100 reached.\r\n");
    if (replaced > 0) {
        SendMessage(tmp_sprintf(
                        "Replaced %d occurrence%s of '%s' with '%s'.\r\n",
                        replaced,
                        (replaced == 1) ? "":"s",
                        pattern.c_str(),
                        replacement.c_str()));
    } else {
        SendMessage("Search string not found.\r\n");
    }

    if (wrap)
	    Wrap();
	UpdateSize();
	return true;
}

bool
CEditor_Wrap(void)
{
	list <string>_iterator line;
	string_iterator s;
	string tempstr;
	int linebreak;

	if (PRF2_FLAGGED(desc->creature, PRF2_NOWRAP)) {
		return false;
	}
	for (line = theText.begin(); line != theText.end(); line++) {
		linebreak = 76;
		tempstr = "";
		// If its less than 77 chars, it don't need ta be wrapped.
		if (line->length() <= 76)
			continue;

		s = line->begin();

		// Find the first space <= 76th char.
		for (s += 75; s != line->begin() && *s != ' '; s--)
			linebreak--;

		if (linebreak == 1) {	// Linebreak is at 76
			s = line->begin();
			s += 75;
		}
		tempstr.append(s, line->end());
		if (linebreak > 1)		// If its a pos other than 1, its a space.
			tempstr.erase(tempstr.begin());
		line->erase(s, line->end());
		line++;
		theText.insert(line, 1, tempstr);
		line = theText.begin();
	}
	return true;
}

bool
CEditor_Remove(unsigned int start_line, unsigned int finish_line)
{
	list <string>_iterator start, finish;

	if (start_line < 1 || start_line > theText.size()) {
		SendMessage("Someone already deleted that line boss.\r\n");
		return false;
	}
	if (finish_line < 1 || finish_line > theText.size()) {
		SendMessage("Someone already deleted that line boss.\r\n");
		return false;
	}

    start = theText.begin();
    advance(start, start_line - 1);
    finish = theText.begin();
    advance(finish, finish_line);
	theText.erase(start, finish);

    if (start_line == finish_line)
        SendMessage(tmp_sprintf("Line %d deleted.\r\n", start_line));
    else
        SendMessage(tmp_sprintf("Lines %d-%d deleted.\r\n",
                                start_line,
                                finish_line));

    if (wrap)
	    Wrap();
	UpdateSize();
	return true;
}

bool
CEditor_Clear(void)
{

	theText.erase(theText.begin(), theText.end());

	UpdateSize();
	return true;
}

void
CEditor_ImportText(const char *str)
{
    char *line;

    origText.clear();
    if (str) {
        while ((line = tmp_getline(&str)) != NULL)
            origText.push_back(string(line));
    }

    theText = origText;

    UpdateSize();
    if (wrap)
	    Wrap();
}

void
CEditor_UndoChanges(void)
{
	if (origText.size()) {
        Clear();
        theText = origText;
        UpdateSize();
        SendMessage("Original buffer restored.\r\n");
	} else {
		SendMessage("There's no original to undo to.\r\n");
    }
}

void
CEditor_SendMessage(const char *message)
{
	if (!desc || !desc->creature) {
		errlog("TEDII Attempting to SendMessage with null desc or desc->creature\r\n");
		return;
	}

	// If the original message is too long, make a new one thats small
	if (strlen(message) >= LARGE_BUFSIZE) {
        char *temp = NULL;

		slog("SendMessage Truncating message. NAME(%s) Length(%zd)",
             GET_NAME(desc->creature),
             strlen(message));
        temp = new char[LARGE_BUFSIZE];
		strncpy(temp, message, LARGE_BUFSIZE - 2);
		send_to_char(desc->creature, "%s", temp);
        delete [] temp;
	} else {
        // If the original message is small enough, just let it through.
		send_to_char(desc->creature, "%s", message);
	}
}

static inline int
text_length(list <string> &theText)
{
	int length = 0;
	list <string>_iterator s;
	for (s = theText.begin(); s != theText.end(); s++) {
		length += s->length();
	}
	return length;
}

void
CEditor_UpdateSize(void)
{
	int linesRemoved = 0;

	// update the current size
	curSize = text_length(theText);

	// Buffer overflow state.
	// This is probably happening in response to word wrap
	while (curSize > maxSize) {
		theText.pop_back();
		curSize = text_length(theText);
		linesRemoved++;
	}

	// Warn the player if the buffer was truncated.
	if (linesRemoved > 0) {
		SendMessage(tmp_sprintf("Error: Buffer limit exceeded.  %d %s removed.\r\n",
                                linesRemoved,
                                linesRemoved == 1 ? "line" : "lines"));
		slog("TEDINF: UpdateSize removed %d lines from buffer. Name(%s) Size(%d) Max(%d)",
             linesRemoved, GET_NAME(desc->creature), curSize, maxSize);
	}
	// Obvious buffer flow state. This should never happen, but if it does, say something.
	if (curSize > maxSize) {
		slog("TEDERR: UpdateSize updated to > maxSize. Name(%s) Size(%d) Max(%d)",
			GET_NAME(desc->creature), curSize, maxSize);
	}
}

void
CEditor_SendModalHelp(void)
{
    // default offers the clear buffer and undo changes options
    send_to_desc(desc, "            &YC - &nClear Buffer         &YU - &nUndo Changes  \r\n");
}

void
CEditor_ProcessHelp(char *inStr)
{
	char command[MAX_INPUT_LENGTH];

    acc_string_clear();

	if (!*inStr) {
        send_to_desc(desc,
                     "     &C*&B-----------------------&Y H E L P &B-----------------------&C*\r\n"
                     "            &YR - &nRefresh Screen       &YH - &nHelp         \r\n"
                     "            &YE - &nSave and Exit        &YQ - &nQuit (Cancel)\r\n"
                     "            &YL - &nReplace Line         &YD - &nDelete Line  \r\n"
                     "            &YI - &nInsert Line          &YM - &nMove Line(s)\r\n"
                     "            &YF - &nFind                 &YS - &nSubstitute\r\n");
        this->SendModalHelp();
        send_to_desc(desc,
                     "     &C*&B-------------------------------------------------------&C*&n\r\n");
	} else {
        HelpItem *help_item;

		inStr = one_argument(inStr, command);
		*command = tolower(*command);
        help_item = _Help->FindItems(tmp_sprintf("tedii-%c", *command), false, 0, false);
        if (help_item) {
            help_item->LoadText();
            send_to_desc(desc, "&cTEDII Command '%c'&n\r\n", *command);
            SEND_TO_Q(help_item->text, desc);
        } else {
            send_to_desc(desc, "Sorry.  There is no help on that.\r\n");
        }
    }
}

bool
parse_optional_range(const char *arg, int &start, int &finish)
{
    const char *dash = strchr(arg, '-');

    if (dash) {
        char *str;

        // Parse range
        str = tmp_substr(arg, 0, dash - arg - 1);
        if (!isnumber(str))
            return false;
        start = atoi(str);

        str = tmp_substr(arg, dash - arg + 1);
        if (!isnumber(str))
            return false;
        finish = atoi(str);

        if (start > finish)
            swap(start, finish);

        return true;
    }

    // Ensure single arg is numeric
    if (!isnumber(arg))
        return false;

    // Single number
    start = finish = atoi(arg);
    return true;
}

bool
CEditor_PerformCommand(char cmd, char *args)
{
	int line, start_line, end_line, dest_line;
	char command[MAX_INPUT_LENGTH];

	switch (tolower(cmd)) {
	case 'h':					// Help
		ProcessHelp(args);
        break;
	case 'e':					// Save and Exit
        Finish(true);
		break;
	case 'q':					// Quit without saving
        Finish(false);
		break;
	case 's':					// Substitute
        Substitute(args);
		break;
    case 'f':                   // Find
        Find(args);
        break;
	case 'c':					// Clear Buffer
		Clear();
		SendMessage("Cleared.\r\n");
		break;
	case 'l':					// Replace Line
		args = one_argument(args, command);
		if (!isdigit(*command)) {
			SendMessage("Format for Replace Line is: &l <line #> <text>\r\n");
			break;
		}
		line = atoi(command);
		if (line < 1) {
			SendMessage("Format for Replace Line is: &l <line #> <text>\r\n");
			break;
		}
        ReplaceLine(line, args);
		break;
	case 'i':					// Insert Line
		args = one_argument(args, command);
		if (!isdigit(*command)) {
			SendMessage
				("Format for insert command is: &i <line #> <text>\r\n");
			break;
		}
		line = atoi(command);
		if (line < 1) {
			SendMessage("Format for insert command is: &i <line #><text>\r\n");
			break;
		}
        Insert(line, args);
		break;
	case 'd':					// Delete Line
		args = one_argument(args, command);
        if (!parse_optional_range(command, start_line, end_line)) {
			SendMessage("Format for delete command is: &d <line #>\r\n");
			break;
		}
        Remove(start_line, end_line);
		break;
    case 'm': {
        char *arg;

        arg = tmp_getword(&args);
        if (!*arg) {
            SendMessage("Format for move command is: &m (<start line>-<end line>|<linenum>) <destination>\r\n");
            break;
        }

        if (!parse_optional_range(arg, start_line, end_line)) {
            SendMessage("Format for move command is: &m (<start line>-<end line>|<linenum>) <destination>\r\n");
            break;
        }

        arg = tmp_getword(&args);
        if (!*arg || !isnumber(arg)) {
            SendMessage("Format for move command is: &m (<start line>-<end line>|<line #>) <destination>\r\n");
            break;
        }
        dest_line = atoi(arg);

        if (dest_line >= start_line && dest_line <= end_line + 1) {
            SendMessage("The line range you specified is already at the destination.\r\n");
            break;
        }

        MoveLines(start_line, end_line, dest_line);
        break;
    }
	case 'r':					// Refresh Screen
        if (!*args) {
            DisplayBuffer();
        } else if (isnumber(args) && (line = atoi(args)) > 0) {
            int line_count = desc->account->get_term_height();

            DisplayBuffer(MAX(1, line - line_count / 2), line_count);
		} else {
            SendMessage("Format for refresh command is: &r [<line #>]\r\nOmit line number to display the whole buffer.\r\n");
        }
		break;
	default:
		return false;
	}

	return true;
}