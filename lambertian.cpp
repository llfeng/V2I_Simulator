

#include "lambertian.h"
#include <cmath>

const double pi = acos(-1), EPS = 1e-6;

bool is_connected(double distance, double cos_angle, double max_distance, double fov = 60) {
	double m = -log(2) / log(cos(pi * fov / 180));
	// m \approx 55
	double threashold = 1 / (max_distance * max_distance);
	double intensity = 1 / (distance * distance) * cos_angle;
	return intensity >= threashold;
}

int get_xaxis_range(double y, double tagx, double tagy, double max_distance, double &l, double &r) {
	// Sanity check
	y -= tagy;
	if (y < 0)
		return -1;
	if (y > max_distance)
		return -1;
	l = -max_distance, r = 0;
	while (l + EPS < r) {
		double mid = (l + r) / 2;
		double edge = sqrt(mid * mid + y * y);
		if (is_connected(edge, y / edge, max_distance))
			r = mid;
		else
			l = mid;
	}
	r = -l;
	l += tagx;
	r += tagx;
	return 0;
}
