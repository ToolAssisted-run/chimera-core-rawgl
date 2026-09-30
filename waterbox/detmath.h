/* detmath.h - the transcendental functions the core's decoders use, the same
 * in every build.
 *
 * stb_vorbis (the 20th Anniversary Edition's music) and TinySoundFont (Windows
 * 3.1's) call sin, cos, exp, log, pow and tan. The C library's versions are
 * accurate, but not to the same last bit in glibc (the native reference) and
 * musl (the sandbox), and a last bit is enough to change a decoded sample and
 * split the two builds. These are computed from + - * / and exact operations
 * only (IEEE double: the same result on every x86-64), with ordinary range
 * reduction and series: plenty for audio, and identical everywhere.
 *
 * Header-only, static: included by the translation units that compile the
 * decoders (music.cpp).
 */
#ifndef RAWGL_DETMATH_H
#define RAWGL_DETMATH_H

#include <math.h>

static const double DM_PI = 3.14159265358979323846;
static const double DM_LN2 = 0.69314718055994530942;
static const double DM_LN10 = 2.30258509299404568402;

/* sin and cos on [-pi/4, pi/4]: Taylor series to x^17 (error below 1e-17) */
static double dm_sin_kernel(double x)
{
	const double x2 = x * x;
	double t = x, s = x;
	for (int n = 2; n <= 16; n += 2)
	{
		t = -t * x2 / (double)(n * (n + 1));
		s += t;
	}
	return s;
}

static double dm_cos_kernel(double x)
{
	const double x2 = x * x;
	double t = 1.0, s = 1.0;
	for (int n = 1; n <= 16; n += 2)
	{
		t = -t * x2 / (double)(n * (n + 1));
		s += t;
	}
	return s;
}

/* x = k * pi/2 + r, |r| <= pi/4 (Cody-Waite, pi/2 in three parts) */
static int dm_reduce(double x, double *r)
{
	static const double PIO2_1 = 1.57079632673412561417e+00;
	static const double PIO2_2 = 6.07710050650619224932e-11;
	static const double PIO2_3 = 2.02226624879595063154e-21;
	const double k = floor(x / (DM_PI / 2) + 0.5);
	*r = ((x - k * PIO2_1) - k * PIO2_2) - k * PIO2_3;
	return (int)((long long)k & 3);
}

static double dm_sin(double x)
{
	double r;
	switch (dm_reduce(x, &r))
	{
	case 0: return dm_sin_kernel(r);
	case 1: return dm_cos_kernel(r);
	case 2: return -dm_sin_kernel(r);
	default: return -dm_cos_kernel(r);
	}
}

static double dm_cos(double x)
{
	double r;
	switch (dm_reduce(x, &r))
	{
	case 0: return dm_cos_kernel(r);
	case 1: return -dm_sin_kernel(r);
	case 2: return -dm_cos_kernel(r);
	default: return dm_sin_kernel(r);
	}
}

static double dm_tan(double x) { return dm_sin(x) / dm_cos(x); }

/* e^x = 2^k * e^r, |r| <= ln2/2: Taylor series to r^20 */
static double dm_exp(double x)
{
	if (x != x) return x;
	if (x > 709.0) return HUGE_VAL;
	if (x < -745.0) return 0.0;
	const double k = floor(x / DM_LN2 + 0.5);
	const double r = (x - k * 6.93147180369123816490e-01) - k * 1.90821492927058770002e-10;
	double t = 1.0, s = 1.0;
	for (int n = 1; n <= 20; n++)
	{
		t = t * r / (double)n;
		s += t;
	}
	return ldexp(s, (int)k);
}

/* ln x = e * ln2 + 2 atanh((m - 1) / (m + 1)), m in [sqrt(1/2), sqrt(2)) */
static double dm_log(double x)
{
	if (x != x || x < 0.0) return NAN;
	if (x == 0.0) return -HUGE_VAL;
	if (x == HUGE_VAL) return x;
	int e;
	double m = frexp(x, &e); /* m in [0.5, 1) */
	if (m < 0.70710678118654752440)
	{
		m *= 2.0;
		e -= 1;
	}
	const double z = (m - 1.0) / (m + 1.0), z2 = z * z;
	double t = z, s = z;
	for (int n = 3; n <= 41; n += 2)
	{
		t *= z2;
		s += t / (double)n;
	}
	return (double)e * 6.93147180369123816490e-01 + ((double)e * 1.90821492927058770002e-10 + 2.0 * s);
}

static double dm_log10(double x) { return dm_log(x) / DM_LN10; }

static double dm_pow(double x, double y)
{
	if (y == 0.0) return 1.0;
	if (x == 1.0) return 1.0;
	if (x == 0.0) return y > 0.0 ? 0.0 : HUGE_VAL;
	if (x < 0.0)
	{
		/* only an integer power of a negative number is real */
		if (floor(y) != y) return NAN;
		const double v = dm_exp(y * dm_log(-x));
		return fmod(y, 2.0) != 0.0 ? -v : v;
	}
	return dm_exp(y * dm_log(x));
}

static float dm_powf(float x, float y) { return (float)dm_pow(x, y); }
static float dm_expf(float x) { return (float)dm_exp(x); }

#endif
