////////////////////////////////////////////////////////////////////////////////
// 
// $LastChangedBy$
// $LastChangedDate$
// $LastChangedRevision$
//
////////////////////////////////////////////////////////////////////////////////

#ifndef __ETQW_VOICEMACROS_H__
#define __ETQW_VOICEMACROS_H__

class Client;

typedef enum
{
	VCHAT_NONE,

	// Team messages.
	VCHAT_TEAM_PATHCLEARED,
	VCHAT_TEAM_ENEMYWEAK,
	VCHAT_TEAM_ALLCLEAR,
	VCHAT_TEAM_INCOMING,	
	VCHAT_TEAM_FIREINTHEHOLE,
	VCHAT_TEAM_ONDEFENSE,
	VCHAT_TEAM_ONOFFENSE,
	VCHAT_TEAM_TAKINGFIRE,
	VCHAT_TEAM_MINESCLEARED,
	VCHAT_TEAM_ENEMYDISGUISED,

	VCHAT_TEAM_MEDIC,
	VCHAT_TEAM_NEEDAMMO,
	VCHAT_TEAM_NEEDBACKUP,
	VCHAT_TEAM_NEEDENGINEER,
	VCHAT_TEAM_COVERME,
	VCHAT_TEAM_HOLDFIRE,
	VCHAT_TEAM_WHERETO,
	VCHAT_TEAM_NEEDOPS,

	VCHAT_TEAM_FOLLOWME,
	VCHAT_TEAM_LETGO,
	VCHAT_TEAM_MOVE,
	VCHAT_TEAM_CLEARPATH,
	VCHAT_TEAM_DEFENDOBJECTIVE,
	VCHAT_TEAM_DISARMDYNAMITE,
	VCHAT_TEAM_CLEARMINES,
	VCHAT_TEAM_REINFORCE_OFF,
	VCHAT_TEAM_REINFORCE_DEF,

	VCHAT_TEAM_AFFIRMATIVE,
	VCHAT_TEAM_NEGATIVE,
	VCHAT_TEAM_THANKS,
	VCHAT_TEAM_WELCOME,
	VCHAT_TEAM_SORRY,
	VCHAT_TEAM_OOPS,

	// Command related
	VCHAT_TEAM_COMMANDACKNOWLEDGED,
	VCHAT_TEAM_COMMANDDECLINED,
	VCHAT_TEAM_COMMANDCOMPLETED,
	VCHAT_TEAM_DESTROYPRIMARY,
	VCHAT_TEAM_DESTROYSECONDARY,
	VCHAT_TEAM_DESTROYCONSTRUCTION,
	VCHAT_TEAM_CONSTRUCTIONCOMMENCING,
	VCHAT_TEAM_REPAIRVEHICLE,
	VCHAT_TEAM_DESTROYVEHICLE,
	VCHAT_TEAM_ESCORTVEHICLE,

	VCHAT_IMA_SOLDIER,
	VCHAT_IMA_MEDIC,
	VCHAT_IMA_ENGINEER,
	VCHAT_IMA_FIELDOPS,
	VCHAT_IMA_COVERTOPS,

	VCHAT_TEAM_NUMMESSAGES, // LEAVE THIS AFTER THE TEAM MESSAGES

	// Global messages
	VCHAT_GLOBAL_AFFIRMATIVE,
	VCHAT_GLOBAL_NEGATIVE,
	VCHAT_GLOBAL_ENEMYWEAK,
	VCHAT_GLOBAL_HI,
	VCHAT_GLOBAL_BYE,
	VCHAT_GLOBAL_GREATSHOT,
	VCHAT_GLOBAL_CHEER,

	VCHAT_GLOBAL_THANKS,
	VCHAT_GLOBAL_WELCOME,
	VCHAT_GLOBAL_OOPS,
	VCHAT_GLOBAL_SORRY,
	VCHAT_GLOBAL_HOLDFIRE,
	VCHAT_GLOBAL_GOODGAME,

	VCHAT_GLOBAL_NUMMESSAGES, // LEAVE THIS AFTER THE GLOBAL MESSAGES

	// This must stay last.
	NUM_ETQW_VCHATS
} eVChat;

// class: ETQW_VoiceMacros
class ETQW_VoiceMacros
{
public:

	static int GetVChatId(const char *_string);
	static void SendVoiceMacro(Client *_bot, int _msg);

protected:
};

#endif
