////////////////////////////////////////////////////////////////////////////////
// 
// $LastChangedBy$
// $LastChangedDate$
// $LastChangedRevision$
//
////////////////////////////////////////////////////////////////////////////////

#ifndef __RTCWBaseStates_H__
#define __RTCWBaseStates_H__

#include "StateMachine.h"

namespace AiState
{
	//////////////////////////////////////////////////////////////////////////

	class MountMg42 : public StateChild, public FollowPathUser, public AimerUser
	{
	public:
		void GetDebugString(StringStr &out);
		void RenderDebug();

		obReal GetPriority();
		void Enter();
		void Exit();
		StateStatus Update(float fDt);

		// FollowPathUser functions.
		bool GetNextDestination(DestinationVector &_desination, bool &_final, bool &_skiplastpt);

		// AimerUser functions.
		bool GetAimPosition(Vector3f &_aimpos);
		void OnTarget();

		MountMg42();
	private:
		MapGoalPtr			m_MapGoal;
		Trackers			Tracker;

		enum ScanType
		{
			SCAN_DEFAULT,
			SCAN_MIDDLE,
			SCAN_LEFT,
			SCAN_RIGHT,
			SCAN_ZONES,

			NUM_SCAN_TYPES
		};
		int					m_ScanDirection;
		int					m_NextScanTime;

		TrackTargetZone		m_TargetZone;

		int					m_MinCampTime;
		int					m_MaxCampTime;
		int					m_Stance;

		//////////////////////////////////////////////////////////////////////////
		Vector3f			m_MG42Position;

		Vector3f			m_AimPoint;

		Vector3f			m_GunCenterArc;
		Vector3f			m_CurrentMountedAngles;
		
		Vector3f			m_ScanLeft;
		Vector3f			m_ScanRight;

		float				m_MinHorizontalArc;
		float				m_MaxHorizontalArc;
		float				m_MinVerticalArc;
		float				m_MaxVerticalArc;

		int					m_ExpireTime;

		bool				m_GotGunProperties;
		//////////////////////////////////////////////////////////////////////////

		bool				m_AdjustedPosition;
		bool				m_IgnoreTargets;

		bool				_GetMG42Properties();
	};

	//////////////////////////////////////////////////////////////////////////

	class RepairMg42 : public StateChild, public FollowPathUser, public AimerUser
	{
	public:
		void GetDebugString(StringStr &out);
		void RenderDebug();

		obReal GetPriority();
		void Enter();
		void Exit();
		StateStatus Update(float fDt);

		// FollowPathUser functions.
		bool GetNextDestination(DestinationVector &_desination, bool &_final, bool &_skiplastpt);

		// AimerUser functions.
		bool GetAimPosition(Vector3f &_aimpos);
		void OnTarget();

		RepairMg42();
	private:
		Vector3f			m_MG42Position;
		MapGoalPtr			m_MapGoal;
		Trackers			Tracker;

		bool				m_IgnoreTargets;
	};

	//////////////////////////////////////////////////////////////////////////

	class TakeCheckPoint : public StateChild, public FollowPathUser
	{
	public:

		void GetDebugString(StringStr &out);
		void RenderDebug();

		obReal GetPriority();
		void Enter();
		void Exit();
		StateStatus Update(float fDt);

		// FollowPathUser functions.
		bool GetNextDestination(DestinationVector &_desination, bool &_final, bool &_skiplastpt);

		TakeCheckPoint();
	private:
		Trackers		Tracker;

		MapGoalPtr		m_MapGoal;
		Vector3f		m_TargetPosition;
	};

	//////////////////////////////////////////////////////////////////////////

	class ReviveTeammate : public StateChild, public FollowPathUser, public AimerUser
	{
	public:
		typedef enum
		{
			REVIVING,
			HEALING,
		} GoalState;

		void GetDebugString(StringStr &out);
		void RenderDebug();

		obReal GetPriority();
		void Enter();
		void Exit();
		StateStatus Update(float fDt);

		// FollowPathUser functions.
		bool GetNextDestination(DestinationVector &_desination, bool &_final, bool &_skiplastpt);

		// AimerUser functions.
		bool GetAimPosition(Vector3f &_aimpos);
		void OnTarget();

		ReviveTeammate();
	private:
		GoalState			m_GoalState;

		GameTimer			m_CheckReviveTimer;

		Trackers			Tracker;
		MapGoalPtr			m_MapGoal;
		MapGoalList			m_List;

		bool AreThereNewReviveGoals();
	};

	//////////////////////////////////////////////////////////////////////////

	class DefuseDynamite : public StateChild, public FollowPathUser, public AimerUser
	{
	public:

		void GetDebugString(StringStr &out);
		void RenderDebug();

		obReal GetPriority();
		void Enter();
		void Exit();
		StateStatus Update(float fDt);

		// FollowPathUser functions.
		bool GetNextDestination(DestinationVector &_desination, bool &_final, bool &_skiplastpt);

		// AimerUser functions.
		bool GetAimPosition(Vector3f &_aimpos);
		void OnTarget();

		DefuseDynamite();
	private:
		Vector3f			m_TargetPosition;

		Trackers			Tracker;
		MapGoalPtr			m_MapGoal;
	};

	//////////////////////////////////////////////////////////////////////////

	class CallArtillery : public StateChild, public FollowPathUser, public AimerUser
	{
	public:

		void GetDebugString(StringStr &out);
		void RenderDebug();

		obReal GetPriority();
		void Enter();
		void Exit();
		StateStatus Update(float fDt);

		void ProcessEvent(const MessageHelper &_message, CallbackParameters &_cb);

		// FollowPathUser functions.
		bool GetNextDestination(DestinationVector &_desination, bool &_final, bool &_skiplastpt);

		// AimerUser functions.
		bool GetAimPosition(Vector3f &_aimpos);
		void OnTarget();

		CallArtillery();
	private:
		Trackers			Tracker;
		MapGoalPtr			m_MapGoal;
		MapGoalPtr			m_MapGoalTarget;
		GameEntity			m_TargetEntity;
		int					m_FireTime;

		FilterPtr			m_WatchFilter;

		bool				m_Fired;
		int					m_MinCampTime;
		int					m_MaxCampTime;
		int					m_Stance;

		int					m_ExpireTime;
	};

	//////////////////////////////////////////////////////////////////////////
};

#endif
