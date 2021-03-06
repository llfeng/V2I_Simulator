#pragma once

#ifndef IS_AVAILABLE_H
#define IS_AVAILABLE_H
#include <vector>
using std::vector;


#define SYS_FOV         120.0


#define VEHICLE_LENGTH  4.5
#define VEHICLE_WIDTH   2.5

#define TAG_POSY_OFFSET 1.0
#define LANE_WIDTH      3.5

#define MAX_LANE_NUM    3
#define TAG_ROAD_LENGTH 200

#define VELOCITY_NUM    5

#define VEHICLE_NUM_PER_LANE    10


//#define UPLINK_DISTANCE     65
#if (UPLINK_BITRATE == 128)       //128bps
#define SMALL_SIGN_UPLINK_DISTANCE  80
#define LARGE_SIGN_UPLINK_DISTANCE  96
#elif (UPLINK_BITRATE == 256)       //256bps
#define SMALL_SIGN_UPLINK_DISTANCE  67
#define LARGE_SIGN_UPLINK_DISTANCE  83
#else
ERROR
#endif


//#define DOWNLINK_DISTANCE   (2*UPLINK_DISTANCE)
//#define DOWNLINK_DISTANCE   (UPLINK_DISTANCE)
#define DOWNLINK_DISTANCE       150

#define TAG_SPACING_OFFSET  110


#define X get_x()
#define Y get_y()

struct Point {
	double x, y;
	Point(double x_ = 0, double y_ = 0) : x(x_), y(y_) {};
	Point(const Point &p) : x(p.x), y(p.y) {};
	Point operator+(const Point &p) const;
	Point operator-(const Point &p) const;
};

bool is_intersect(const vector<Point> &lights, 
	const Point &sign, 
	int which);

static bool interval_is_intersect(double a, double b, double c, double d);
#endif
