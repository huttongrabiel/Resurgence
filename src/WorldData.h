#pragma once

#include "lidar/PointCloudProcessing.h"

class WorldData {
public:
	virtual PointXY getGPS() = 0;
	virtual float getHeading() = 0;
	virtual bool lidarSees() = 0;
	virtual float targetDistance() = 0;
};
