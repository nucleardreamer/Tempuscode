#ifndef _HELP_H_
#define _HELP_H_

#include <vector>
#include <fstream>
using namespace std;

#define HFLAG_UNAPPROVED (1 << 0)	// Help topic cannot be found by searching
#define HFLAG_IMMHELP    (1 << 1)	// Help topic can only be found by gods
#define HFLAG_MODIFIED   (1 << 2)	// Help topic has been changed or is being
								   //    edited now.
#define HGROUP_OLC       (1 << 0)
#define HGROUP_MISC      (1 << 1)
#define HGROUP_NEWBIE    (1 << 2)
#define HGROUP_SKILL     (1 << 3)
#define HGROUP_SPELL     (1 << 4)
#define HGROUP_CLASS     (1 << 5)
#define HGROUP_CITY      (1 << 6)
#define HGROUP_PLAYER    (1 << 7)
#define HGROUP_MAGE      (1 << 8)
#define HGROUP_CLERIC    (1 << 9)
#define HGROUP_THIEF     (1 << 10)
#define HGROUP_WARRIOR   (1 << 11)
#define HGROUP_BARB      (1 << 12)
#define HGROUP_PSIONIC   (1 << 13)
#define HGROUP_PHYSIC    (1 << 14)
#define HGROUP_CYBORG    (1 << 15)
#define HGROUP_KNIGHT    (1 << 16)
#define HGROUP_RANGER    (1 << 17)
#define HGROUP_BARD      (1 << 18)
#define HGROUP_MONK      (1 << 19)
#define HGROUP_MERCENARY (1 << 20)
#define HGROUP_HELP_EDIT (1 << 21)
#define HGROUP_HELP_GODS (1 << 22)
#define HGROUP_IMMHELP   (1 << 23)
#define HGROUP_QCONTROL  (1 << 24)
#define HGROUP_MAX        25

struct creature;

#define MAX_HELP_NAME_LENGTH 128
#define MAX_HELP_TEXT_LENGTH 16384
struct HelpItem {
	// Swap two items where they sit in the items list.
	friend void SwapItems(HelpItem * A, HelpItem * Ap, HelpItem * B,
		HelpItem * Bp);

	// Most of this should be protected with help_collection as a friend class
	// Member Funcs
	 HelpItem();
	~HelpItem();

	// Returns the next HelpItem
	inline HelpItem *Next(void) {
		return next;
	}
	// Sets the next helpitem
	inline void SetNext(HelpItem * n) {
		next = n;
	}
	// Returns the next HelpItem in the list to be shown
	inline HelpItem *NextShow(void) {
		return next_show;
	}
	// Set next item in the list to be shown
	inline void SetNextShow(HelpItem * n) {
		next_show = n;
	}
	bool Edit(struct creature * ch);	// Begin editing an item
	bool Clear();				// Clear the item out.
	void SetName(char *argument);
	void SetKeyWords(char *argument);
	void SetGroups(char *argument);
	void SetDesc(char *argument);	// Actually sets "text" (body of the message)
	void SetFlags(char *argument);
	void EditText(void);
	bool LoadText(void);
	bool Save(void);			// Save the current entry to file.
	bool IsInGroup(int thegroup);

	// Show the entry.
	// 0 == One Line Listing.
	// 1 == One Line Stat
	// 2 == Entire Entry
	// 3 == Entire Entry Stat
	void Show(struct creature * ch, char *buffer, int mode = 0);

	// Data
	int idnum;					// Unique Identifier
	int counter;				// How many times has it been looked at
	// Groups and flags are both bitvectors.
	int groups;					//  The groups the item is in
	int flags;					// Approved/Unapproved etc.
	long owner;					// Last person to edit the topic
	char *keys;					// Key Words
	char *name;					// The listed name of the help topic
	char *text;					// The body of the help topic
	struct creature *editor;
	fstream help_file;
	HelpItem *next;
	HelpItem *next_show;
};
struct HelpCollection {

	friend void SwapItems(HelpItem * A, HelpItem * Ap, HelpItem * B,
		HelpItem * Bp);
	// Christ, most of this should probably be private...
	// Member Funcs
	 HelpCollection();
	~HelpCollection();
	// Calls FindItems then Show
	void GetTopic(struct creature * ch,	// The character that wants the topic
		char *args,				// the search arguments. (pattern)
		int mode = 2,			// How to show the item. (see HelpItem_Show())
		bool show_no_app = false,	// Show Unapproved items?
		int thegroup = HGROUP_PLAYER,	// Which help groups to search through
		bool searchmode = false);	// Should we search by keyword but still
	// return a list?

	void List(struct creature * ch, char *args);	// Show all the items
	// Create an item. (calls EditItem && SaveIndex)
	bool CreateItem(struct creature * ch);
	bool EditItem(struct creature * ch, int idnum);	// Begin editing an item
	void ApproveItem(struct creature * ch, char *argument);	// Approve an item
	void UnApproveItem(struct creature * ch, char *argument);	// Approve an item
	bool ClearItem(struct creature * ch);	// Clear an item
	bool SaveItem(struct creature * ch);	// Duh?
	bool SaveIndex(void);	// Save the entire index
	bool SaveAll(struct creature * ch);	// Save Everything. (cals saveitem and saveindex)
	bool Set(struct creature * ch, char *argument);
	bool LoadIndex(void);		// Load help index (at startup)
	void Sync(void);			// Delete unneeded item->text from memory.
	void Show(struct creature * ch);	// Show collection statistics
	inline int GetTop(void) {
		return top_id;
	} void Push(HelpItem * n);	//Add topic to help_collection
	HelpItem *find_item_by_id(int id);	// Linear search of the help items

	// Data
	HelpItem *items;			// The top of the list of help topics
	HelpItem *bottom;			// The bottom of the list of help topics

	// Returns a show list of items it found
	HelpItem * FindItems(char *args, bool find_no_approve =
		false, int thegroup = HGROUP_PLAYER, bool searchmode = false);
	int top_id;					// The highest id in use..
	bool need_save;				// Weather something has been changed or not.
};

#endif
