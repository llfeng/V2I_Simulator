

#include "lambertian.h"

const double pi = acos(-1);

bool is_connected(double distance, double cos_angle, double max_distance, double fov = 9) {
	double m = -log(2) / log(cos(pi * fov / 180));
	// m \approx 55
	double threashold = 1 / (max_distance * max_distance);
	double intensity = 1 / (distance * distance) * pow(cos(fov), m) * cos_angle;
	return intensity >= threashold;
}