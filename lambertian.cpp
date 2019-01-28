

#include "lambertian.h"
#include <cmath>
#include <algorithm>
#include <iostream>
using namespace std;


const double pi = acos(-1), EPS = 1e-6, INF = 1e30, step = 5e-2;

static double convert(double deg) {
	return pi * deg / 180;
}

bool is_connected(double distance, double cos_angle, double max_distance, double fov = 60) {
//	if (cos_angle < cos(convert(fov)))
	if (cos_angle < cos(fov))
		return false;
	//double m = -log(2) / log(cos(convert(fov)));
	double m = -log(2) / log(cos(fov));
	//int m = 1;	// fov = 60
	double threashold = 1 / (max_distance * max_distance);
	double intensity = 1 / (distance * distance) * pow(cos_angle, m + 1);	// reader³öÉä½Ç=readerÈëÉä½Ç
	return intensity >= threashold;
}

int get_xaxis_range(double y, double tagx, double tagy, double max_distance, double &l, double &r) {
	// Sanity check
	y -= tagy;
	if (y < 0)
		return -1;
	if (y > max_distance)
		return -1;
	l = 1;
	r = -max_distance - 1;
	for (double i = -max_distance - step; i < 2 * step; i += step) {
		double hypotenuse = sqrt(y * y + i * i);
		if (is_connected(hypotenuse, -i/hypotenuse, max_distance)) {
			l = min(l, i);
			r = max(r, i);
		}
	}
	r += step;
	l += tagx;
	r += tagx;
	return l <= r;
}

#ifdef DEBUG_LAMBERTIAN
int main() {
	double l, r;
	get_xaxis_range(0, 0, 0, 80, l, r);
	cout << l << ' ' << r << endl;
	get_xaxis_range(40, 0, 0, 80, l, r);
	cout << l << ' ' << r << endl;
	get_xaxis_range(5, 0, 0, 80, l, r);
	cout << l << ' ' << r << endl;
	system("pause");
	return 0;
}
#endif
