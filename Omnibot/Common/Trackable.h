////////////////////////////////////////////////////////////////////////////////
// 
// $LastChangedBy: drevil $
// $LastChangedDate: 2010-01-21 11:55:10 -0600 (Thu, 21 Jan 2010) $
// $LastChangedRevision: 4601 $
//
////////////////////////////////////////////////////////////////////////////////

#ifndef __TRACKABLE_H__
#define __TRACKABLE_H__

#include "Omni-Bot_Events.h"

// class: Trackable
class Trackable
{
public:

	void AddReference(obuint32 _type);
	void DelReference(obuint32 _type);

	obuint32 GetRefCount(obuint32 _type);

	bool IsReferenced();

	Trackable();
	virtual ~Trackable();
protected:
	void _CheckIndex(obuint32 _type);
	
	typedef std::vector<obint32> TrackList;
	TrackList	m_TrackList;
};

#endif