/* $Id$ */

#include "vehicle.h"


static inline bool IsRoadVehInDepot(const Vehicle* v)
{
	assert(v->type == VEH_Road);
	return v->u.road.state == 254;
}

static inline bool IsRoadVehInDepotStopped(const Vehicle* v)
{
	return IsRoadVehInDepot(v) && v->vehstatus & VS_STOPPED;
}
