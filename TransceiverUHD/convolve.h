#ifndef _CONVOLVE_H_
#define _CONVOLVE_H_

#ifdef __cplusplus
extern "C" {
#endif

extern void *convolve_h_alloc(int num);

extern int convolve_real(
	float *x, int x_len, float *h, int h_len, float *y, int y_len, int start, int len, int step, int offset);

extern int convolve_complex(
	float *x, int x_len, float *h, int h_len, float *y, int y_len, int start, int len, int step, int offset);

extern int base_convolve_real(
	float *x, int x_len, float *h, int h_len, float *y, int y_len, int start, int len, int step, int offset);

extern int base_convolve_complex(
	float *x, int x_len, float *h, int h_len, float *y, int y_len, int start, int len, int step, int offset);

#ifdef __cplusplus
};
#endif

#endif /* _CONVOLVE_H_ */
