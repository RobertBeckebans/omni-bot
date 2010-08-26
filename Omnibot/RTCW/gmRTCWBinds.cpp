////////////////////////////////////////////////////////////////////////////////
// 
// $LastChangedBy: drevil $
// $LastChangedDate: 2010-02-09 23:32:10 -0600 (Tue, 09 Feb 2010) $
// $LastChangedRevision: 4671 $
//
////////////////////////////////////////////////////////////////////////////////

#include "PrecompRTCW.h"
#include "gmRTCWBinds.h"

#include "gmConfig.h"
#include "gmThread.h"
#include "gmMachine.h"
#include "gmBot.h"
#include "gmBotLibrary.h" // crapshoot: for GM_CHECK_GAMEENTITY_FROM_PARAM in GetWeaponTag

#define CHECK_THIS_BOT() \
	Client *native = gmBot::GetThisObject( a_thread ); \
	if(!native) \
	{ \
	GM_EXCEPTION_MSG("Script Function on NULL object"); \
	return GM_EXCEPTION; \
	}

// Title: RTCW Script Bindings
//
//////////////////////////////////////////////////////////////////////////

// function: SendPrivateMessage
//		This function executes a private message for this bot.
//
// Parameters:
//	
//		char   - partial name match for target client(s)
//		string - message to send
//
//
// Returns:
//		None
static int GM_CDECL gmfSendPrivateMessage(gmThread *a_thread)
{
	CHECK_THIS_BOT();
	if(a_thread->GetNumParams() > 1)
	{
		const int bufferSize = 512;
		char buffer[bufferSize];

		int iMsgPos = 0;
		const int chatMsgSize = 2048;
		char chatMsg[chatMsgSize] = {0};
		char targName[chatMsgSize] = {0};

		const char *pAsString = a_thread->Param(0).AsString(a_thread->GetMachine(), buffer, bufferSize);
		if(pAsString)
		{
			int len = strlen(pAsString);
			if(chatMsgSize - iMsgPos > len)
			{
				Utils::StringCopy(&targName[iMsgPos], pAsString, len);
				iMsgPos += len;
			}
		}

		iMsgPos = 0;
		// and for the message...
		for(int i = 1; i < a_thread->GetNumParams(); ++i)
		{
			const char *pAsString = a_thread->Param(i).AsString(a_thread->GetMachine(), buffer, bufferSize);
			if(pAsString)
			{
				int len = strlen(pAsString);
				if(chatMsgSize - iMsgPos > len)
				{
					Utils::StringCopy(&chatMsg[iMsgPos], pAsString, len);
					iMsgPos += len;
				}
			}
		}

		bool bSucess = InterfaceFuncs::SendPrivateMessage(native, targName, chatMsg);
		a_thread->PushInt(bSucess ? 1 : 0);
		return GM_OK;
	}

	GM_EXCEPTION_MSG("Expected 2+ parameters");
	return GM_EXCEPTION;
}

//////////////////////////////////////////////////////////////////////////

// function: ChangePrimaryWeapon
//		Sets the bots primary weapon to a new weapon to use upon respawn
//
// Parameters:
//
//		int - weapon id to choose for primary weapon
//
// Returns:
//		int - true if success, false if error
static int GM_CDECL gmfBotPickPrimaryWeapon(gmThread *a_thread)
{
	CHECK_THIS_BOT();
	GM_CHECK_NUM_PARAMS(1);
	GM_CHECK_INT_PARAM(weaponId, 0);

	bool bSucess = InterfaceFuncs::SelectPrimaryWeapon(native, (RTCW_Weapon)weaponId);	
	a_thread->PushInt(bSucess ? 1 : 0);
	return GM_OK;
}

//////////////////////////////////////////////////////////////////////////

// function: ChangeSecondaryWeapon
//		Sets the bots secondary weapon to a new weapon to use upon respawn
//
// Parameters:
//
//		int - weapon id to choose for secondary weapon
//
// Returns:
//		int - true if success, false if error
static int GM_CDECL gmfBotPickSecondaryWeapon(gmThread *a_thread)
{
	CHECK_THIS_BOT();
	GM_CHECK_NUM_PARAMS(1);
	GM_CHECK_INT_PARAM(weaponId, 0);

	bool bSucess = InterfaceFuncs::SelectSecondaryWeapon(native, (RTCW_Weapon)weaponId);	
	a_thread->PushInt(bSucess ? 1 : 0);
	return GM_OK;
}

//////////////////////////////////////////////////////////////////////////

// function: GetReinforceTime
//		Gets the current reinforcement time for this bots team
//
// Parameters:
//
//		none
//
// Returns:
//		int - reinforce timer
static int GM_CDECL gmfGetReinforceTime(gmThread *a_thread)
{
	CHECK_THIS_BOT();
	GM_CHECK_NUM_PARAMS(0);
	
	a_thread->PushFloat(InterfaceFuncs::GetReinforceTime(native));
	return GM_OK;
}

//////////////////////////////////////////////////////////////////////////

// function: GetCursorHint
//		Gets the current hint and hint value for the client
//
// Parameters:
//
//		table - table to store results. function sets 'type' and 'value'
//
// Returns:
//		none
static int GM_CDECL gmfGetCurrentCursorHint(gmThread *a_thread)
{
	CHECK_THIS_BOT();
	GM_CHECK_NUM_PARAMS(1);
	GM_CHECK_TABLE_PARAM(hint, 0);


	int iHintType = 0, iHintValue = 0;
	InterfaceFuncs::GetCurrentCursorHint(native, iHintType, iHintValue);

	hint->Set(a_thread->GetMachine(), "type", gmVariable(iHintType));
	hint->Set(a_thread->GetMachine(), "value", gmVariable(iHintValue));

	return GM_OK;
}

//////////////////////////////////////////////////////////////////////////

// function: ChangeSpawnPoint
//		Changes the bots active spawn point
//
// Parameters:
//
//		int - Spawn point to change to
//
// Returns:
//		none
static int GM_CDECL gmfChangeSpawnPoint(gmThread *a_thread)
{
	CHECK_THIS_BOT();
	GM_CHECK_NUM_PARAMS(1);
	GM_CHECK_INT_PARAM(spawnpoint, 0);

	InterfaceFuncs::ChangeSpawnPoint(native, spawnpoint);
	return GM_OK;
}




//////////////////////////////////////////////////////////////////////////

// function: CanSnipe
//		Checks the bot sniping capability. Playerclass is covert ops and has sniper weapon.
//
// Parameters:
//
//		void
//
// Returns:
//		none
static int GM_CDECL gmfCanSnipe(gmThread *a_thread)
{
	CHECK_THIS_BOT();
	GM_CHECK_NUM_PARAMS(0);

	a_thread->PushInt(InterfaceFuncs::CanSnipe(native) ? 1 : 0);
	return GM_OK;
}

//////////////////////////////////////////////////////////////////////////

// function: Snipe
//		Gives the bot a snipe goal.
//
// Parameters:
//
//		<Vector3> - The <Vector3> position to snipe from
//		<Vector3> - The <Vector3> facing direction to look while sniping
//		<float> - OPTIONAL - Radius of the goal
//
// Returns:
//		none
static int GM_CDECL gmfSnipe(gmThread *a_thread)
{
	CHECK_THIS_BOT();
	GM_CHECK_NUM_PARAMS(2);
/*	GM_CHECK_VECTOR_PARAM(x1,y1,z1,0);
	GM_CHECK_VECTOR_PARAM(x2,y2,z2,1);
	GM_FLOAT_PARAM(rad, 2, 32.f);

	Client *native = gmBot::GetThisObject( a_thread );
	if(!native)
	{
		GM_EXCEPTION_MSG("Script Function on NULL object"); 
		return GM_EXCEPTION;
	}
	
	RTCW_Goal_Snipe::GoalInfo goalInfo;

	MapGoalPtr mg(new ET_SniperGoal());
	mg->SetPosition(Vector3f(x1,y1,z1));
	mg->SetFacing(Vector3f(x2,y2,z2));
	mg->SetRadius(rad);

	native->GetBrain()->ResetSubgoals("Script Snipe");
	GoalPtr g(new ET_Goal_Snipe(native, RTCW_WP_NONE, mg, goalInfo));
	g->SignalStatus(true);
	native->GetBrain()->InsertSubgoal(g);*/

	return GM_OK;
}

//////////////////////////////////////////////////////////////////////////

// function: GetGameType
//		Gets the current game type
//
// Parameters:
//
//		none
//
// Returns:
//		int - game type
static int GM_CDECL gmfGetGameType(gmThread *a_thread)
{
	GM_CHECK_NUM_PARAMS(0);
	
	a_thread->PushInt(InterfaceFuncs::GetGameType());
	return GM_OK;
}

//////////////////////////////////////////////////////////////////////////

// function: SetCvar
//		This function will set a game cvar
//
// Parameters:
//	
//		char   - the cvar to set
//		char   - the value of the cvar to be set
//
//
// Returns:
//		None
static int GM_CDECL gmfSetCvar(gmThread *a_thread)
{
	if(a_thread->GetNumParams() > 1)
	{
		const int bufferSize = 512;
		char buffer[bufferSize];

		int iPos = 0;
		const int cvarSize = 2048;
		const int valueSize = 2048;
		char cvar[cvarSize] = {0};
		char value[valueSize] = {0};

		const char *pAsString = a_thread->Param(0).AsString(a_thread->GetMachine(), buffer, bufferSize);
		if(pAsString)
		{
			int len = strlen(pAsString);
			if(cvarSize - iPos > len)
			{
				Utils::StringCopy(&cvar[iPos], pAsString, len);
				iPos += len;
			}
		}

		iPos = 0;
		// and for the message...
		for(int i = 1; i < a_thread->GetNumParams(); ++i)
		{
			const char *pAsString = a_thread->Param(i).AsString(a_thread->GetMachine(), buffer, bufferSize);
			if(pAsString)
			{
				int len = strlen(pAsString);
				if(valueSize - iPos > len)
				{
					Utils::StringCopy(&value[iPos], pAsString, len);
					iPos += len;
				}
			}
		}

		bool bSucess = InterfaceFuncs::SetCvar(cvar, value);
		a_thread->PushInt(bSucess ? 1 : 0);
		return GM_OK;
	}

	GM_EXCEPTION_MSG("Expected 2+ parameters");
	return GM_EXCEPTION;
}

//////////////////////////////////////////////////////////////////////////

// function: GetCvar
//		This function will get the value of a game cvar
//
// Parameters:
//	
//		char   - the cvar to get
//
//
// Returns:
//		The value of the cvar
static int GM_CDECL gmfGetCvar(gmThread *a_thread)
{
	if(a_thread->GetNumParams() > 0)
	{
		const int bufferSize = 512;
		char buffer[bufferSize];

		int iPos = 0;
		const int cvarSize = 2048;
		char cvar[cvarSize] = {0};

		const char *pAsString = a_thread->Param(0).AsString(a_thread->GetMachine(), buffer, bufferSize);
		if(pAsString)
		{
			int len = strlen(pAsString);
			if(cvarSize - iPos > len)
			{
				Utils::StringCopy(&cvar[iPos], pAsString, len);
				iPos += len;
			}
		}

		a_thread->PushInt(InterfaceFuncs::GetCvar(cvar));
		return GM_OK;
	}

	GM_EXCEPTION_MSG("Expected 1 parameter");
	return GM_EXCEPTION;
}

//////////////////////////////////////////////////////////////////////////

// function: GetSpawnPoint
//		Gets the bots current spawn point
//
// Parameters:
//
//		none
//
// Returns:
//		int - spawn point
static int GM_CDECL gmfGetSpawnPoint(gmThread *a_thread)
{
	CHECK_THIS_BOT();
	GM_CHECK_NUM_PARAMS(0);
	
	a_thread->PushInt(InterfaceFuncs::GetSpawnPoint(native));
	return GM_OK;
}

//////////////////////////////////////////////////////////////////////////

// function: Suicide
//		Set the bots suicide flag.
//		
//
// Parameters:
//
//		int suicide - 0 or 1
//		int persist - 0 or 1
//
// Returns:
//		none
static int GM_CDECL gmfSetSuicide(gmThread *a_thread)
{
	CHECK_THIS_BOT();
	GM_CHECK_NUM_PARAMS(2);
	GM_CHECK_INT_PARAM(suicide, 0);
	GM_CHECK_INT_PARAM(persist, 1);

	InterfaceFuncs::SetSuicide(native, suicide, persist);
	return GM_OK;
}

//////////////////////////////////////////////////////////////////////////

// function: BotPush
//		Set the bots push flag.
//		
//
// Parameters:
//
//		int push - 0 or 1
//
// Returns:
//		none
static int GM_CDECL gmfDisableBotPush(gmThread *a_thread)
{
	CHECK_THIS_BOT();
	GM_CHECK_NUM_PARAMS(1);
	GM_CHECK_INT_PARAM(botPush, 0);

	InterfaceFuncs::DisableBotPush(native, botPush);
	return GM_OK;
}

//////////////////////////////////////////////////////////////////////////

// function: GetExplosiveState
//		Return the state of the explosive; unarmed, armed, invalid.
//		
//
// Parameters:
//
//		GameEntity
//
// Returns:
//		explosive state
static int GM_CDECL gmfGetExplosiveState(gmThread *a_thread)
{
	CHECK_THIS_BOT();
	GM_CHECK_NUM_PARAMS(1);
	GameEntity gameEnt;
	GM_CHECK_GAMEENTITY_FROM_PARAM(gameEnt, 0);
	OBASSERT(gameEnt.IsValid(), "Bad Entity");
	a_thread->PushInt(InterfaceFuncs::GetExplosiveState(native,gameEnt));
	return GM_OK;
}

//////////////////////////////////////////////////////////////////////////

// function: IsWaitingForMedic
//		Returns true if entity is waiting for a medic
//		
//
// Parameters:
//
//		GameEntity
//
// Returns:
//		IsWaitingForMedic
static int GM_CDECL gmfIsWaitingForMedic(gmThread *a_thread)
{
	GM_CHECK_NUM_PARAMS(1);
	GameEntity gameEnt;
	GM_CHECK_GAMEENTITY_FROM_PARAM(gameEnt, 0);
	OBASSERT(gameEnt.IsValid(), "Bad Entity");

	if(InterfaceFuncs::IsWaitingForMedic(gameEnt))
		a_thread->PushInt(1);
	else
		a_thread->PushInt(0);

	return GM_OK;
}

//////////////////////////////////////////////////////////////////////////

static gmFunctionEntry s_ExtendedBotTypeLib[] =
{ 
	{"ChangePrimaryWeapon",		gmfBotPickPrimaryWeapon},
	{"ChangeSecondaryWeapon",	gmfBotPickSecondaryWeapon},	
	{"GetReinforceTime",		gmfGetReinforceTime},
	{"GetCursorHint",			gmfGetCurrentCursorHint},
	{"ChangeSpawnPoint",		gmfChangeSpawnPoint},		
	{"CanSnipe",				gmfCanSnipe},	
	{"Snipe",					gmfSnipe},	
	{"SendPrivateMessage",		gmfSendPrivateMessage},
	{"GetSpawnPoint",			gmfGetSpawnPoint},
	{"Suicide",					gmfSetSuicide},
	{"DisableBotPush",			gmfDisableBotPush},
	{"GetExplosiveState",		gmfGetExplosiveState},
};

static gmFunctionEntry s_ExtendedBotLib[] =
{
	{"GetGameType",				gmfGetGameType},
	{"SetCvar",					gmfSetCvar},
	{"GetCvar",					gmfGetCvar},
	{"IsWaitingForMedic",		gmfIsWaitingForMedic},
};

void gmBindRTCWBotLibrary(gmMachine *_machine)
{
	// Register the bot functions.
	_machine->RegisterLibrary(s_ExtendedBotLib, sizeof(s_ExtendedBotLib) / sizeof(s_ExtendedBotLib[0]));
	//////////////////////////////////////////////////////////////////////////	
	_machine->RegisterTypeLibrary(gmBot::GetType(), s_ExtendedBotTypeLib, sizeof(s_ExtendedBotTypeLib) / sizeof(s_ExtendedBotTypeLib[0]));

	// Register additional bot properties

	// var: TargetBreakableDistance
	//		The distance the bot will target breakable entities. Targets beyond this range will be ignored.
	gmBot::RegisterAutoProperty("TargetBreakableDist", GM_FLOAT, offsetof(RTCW_Client, m_BreakableTargetDistance), 0);
}