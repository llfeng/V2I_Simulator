
#include "is_available.h"
#include <algorithm>
#include <cassert>

using std::max;
using std::min;


const double EPS = 1e-8;

// 定义在cpp里面从而隐藏他们

struct Line {
	Point l, r;
	Line() {};
	Line(const Point &l_, const Point &r_) : l(l_), r(r_) {}
	Line(const Line &line) : l(line.l), r(line.r) {};

	bool is_intersect(const Line &line) const;
};

struct Vehicle {
	Point light;
	Line car_lines[4];

	Vehicle(const Point &light_);
	Vehicle(const Vehicle &vehicle);
	bool is_intersect(const Line &line) const;
};

// class Point

Point Point::operator+(const Point &p) const {
	return Point(x + p.x, y + p.y);
}

Point Point::operator-(const Point &p) const {
	return Point(x - p.x, y - p.y);
}

// class Line

bool Line::is_intersect(const Line &line) const {
	// 写那么多line.l显得很沙雕
	const Point &a = l, &b = r, &c = line.l, &d = line.r;

	// 快速排斥
	if ( !(
		interval_is_intersect(min(a.x, b.x), max(a.x, b.x), min(c.x, d.x), max(c.x, d.x))
		&&
		interval_is_intersect(min(a.y, b.y), max(a.y, b.y), min(c.y, d.y), max(c.y, d.y))
		))
		return false;

	// 是否跨立
	double u, v, w, z;
	u = (c.x - a.x)*(b.y - a.y) - (b.x - a.x)*(c.y - a.y);
	v = (d.x - a.x)*(b.y - a.y) - (b.x - a.x)*(d.y - a.y);
	w = (a.x - c.x)*(d.y - c.y) - (d.x - c.x)*(a.y - c.y);
	z = (b.x - c.x)*(d.y - c.y) - (d.x - c.x)*(b.y - c.y);

	return (u*v <= EPS && w*z <= EPS);
}

// class Vehicle

Vehicle::Vehicle(const Point &light_) {
	light = light_;
	Point l = light + Point(-EPS, 0);
	
#if 0    
	Point	a(l + Point(0, -1.5)),
		b(l + Point(0, 1.5)),
		c(l + Point(-5, 1.5)),
		d(l + Point(-5, -1.5));
#endif       
	Point	a(l + Point(0, -VEHICLE_WIDTH/2)),
		b(l + Point(0, VEHICLE_WIDTH/2)),
		c(l + Point(-VEHICLE_LENGTH, VEHICLE_WIDTH/2)),
		d(l + Point(-VEHICLE_LENGTH, -VEHICLE_WIDTH/2));
	car_lines[0] = Line(a, b);
	car_lines[1] = Line(b, c);
	car_lines[2] = Line(c, d);
	car_lines[3] = Line(d, a);
}

Vehicle::Vehicle(const Vehicle &vehicle) {
	for (int i = 0; i < 4; i++)
		car_lines[i] = vehicle.car_lines[i];
	light = vehicle.light;
}

bool Vehicle::is_intersect(const Line &line) const {
	for (auto &car_line : car_lines) {
		if (line.is_intersect(car_line))
			return true;
	}
	return false;
}

bool is_intersect(const vector<Point> &lights,
	const Point &sign,
	int which) {
	vector<Vehicle> vehicles;
	// 备好空间防止反复扩张
	vehicles.reserve(lights.size());
	// 初始化
	for (auto &light : lights)
		// 让车灯不和车的前端重合
		vehicles.push_back(Vehicle(light));
	// 车灯到路标的连线
	Line line(lights[which], sign);
	// 逐车判断
	for (auto &vehicle : vehicles) {
		if (vehicle.is_intersect(line))
			return true;
	}
	return false;
}

static bool interval_is_intersect(double a, double b, double c, double d) {
	assert(a <= b);
	assert(c <= d);
	if (b <= c)
		return false;
	if (d <= a)
		return false;
	return true;
}

#ifdef DEBUG_INTERSECT
int main() {
	// a little test
	vector<Point> lights;
	lights.push_back(Point(-12, 9));
	lights.push_back(Point(-8.1, 4.5));
	// should be false
	assert(is_intersect(lights, Point(0, 0), 0) == 0);
	lights.push_back(Point(-7.9, 4.5));
	// should be true
	assert(is_intersect(lights, Point(0, 0), 0) == 1);
}
#endif
