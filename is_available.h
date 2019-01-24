#pragma once

#include <vector>
using std::vector;

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