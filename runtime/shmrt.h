
#ifndef SHALIMAR_RUNTIME_H
#define SHALIMAR_RUNTIME_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define SHM_INT   0
#define SHM_REAL  1
#define SHM_CHAR  2
#define SHM_REF   3

typedef struct ShmArray ShmArray;

void shm_begin(void);
int  shm_end(void);

void shm_user_main(void);

void shm_init_globals(void);

void shm_name_files(void);

void shm_line(int32_t unit, int32_t line);

void shm_name_file(int32_t unit, const char *name);

int32_t shm_int_add(int32_t a, int32_t b);
int32_t shm_int_sub(int32_t a, int32_t b);
int32_t shm_int_mul(int32_t a, int32_t b);
int32_t shm_int_div(int32_t a, int32_t b);
int32_t shm_int_mod(int32_t a, int32_t b);
int32_t shm_int_pow(int32_t a, int32_t b);
int32_t shm_int_neg(int32_t a);

int32_t shm_int_eq(int32_t a, int32_t b);
int32_t shm_int_ne(int32_t a, int32_t b);
int32_t shm_int_lt(int32_t a, int32_t b);
int32_t shm_int_gt(int32_t a, int32_t b);
int32_t shm_int_le(int32_t a, int32_t b);
int32_t shm_int_ge(int32_t a, int32_t b);
int32_t shm_int_and(int32_t a, int32_t b);
int32_t shm_int_or(int32_t a, int32_t b);

double shm_real_add(double a, double b);
double shm_real_sub(double a, double b);
double shm_real_mul(double a, double b);
double shm_real_div(double a, double b);
double shm_real_mod(double a, double b);
double shm_real_pow(double a, double b);

int32_t shm_real_eq(double a, double b);
int32_t shm_real_ne(double a, double b);
int32_t shm_real_lt(double a, double b);
int32_t shm_real_gt(double a, double b);
int32_t shm_real_le(double a, double b);
int32_t shm_real_ge(double a, double b);
int32_t shm_real_and(double a, double b);
int32_t shm_real_or(double a, double b);

double  shm_int_to_real(int32_t value);
int32_t shm_real_to_int(double value);
int32_t shm_int_to_char(int32_t value);
int32_t shm_real_to_char(double value);

int32_t shm_real_truth(double value);

void    shm_loop_int_check(int32_t step);
int32_t shm_loop_int_run(int64_t value, int32_t end, int32_t step);
int64_t shm_loop_int_advance(int64_t value, int32_t step);

void    shm_loop_real_check(double start, double end, double step);
double  shm_loop_real_value(double start, double step, double pass);
int32_t shm_loop_real_run(double value, double end, double step);

void shm_enter(int32_t id, int32_t limit, const char *name);
void shm_leave(int32_t id);

ShmArray *shm_array_make(int32_t element, int32_t rank, const int64_t *dims);
ShmArray *shm_array_from_text(const char *bytes, int32_t length);

int32_t shm_array_dim(const ShmArray *array, int32_t axis);

int32_t shm_get_int(const ShmArray *array, int32_t index);
double  shm_get_real(const ShmArray *array, int32_t index);
int32_t shm_get_char(const ShmArray *array, int32_t index);
ShmArray *shm_get_ref(const ShmArray *array, int32_t index);

void shm_set_int(ShmArray *array, int32_t index, int32_t value);
void shm_set_real(ShmArray *array, int32_t index, double value);
void shm_set_char(ShmArray *array, int32_t index, int32_t value);
void shm_set_ref(ShmArray *array, int32_t index, ShmArray *value);

void shm_array_fill(ShmArray *destination, const ShmArray *source);

ShmArray *shm_text_concat(const ShmArray *a, const ShmArray *b);
int32_t   shm_text_compare(const ShmArray *a, const ShmArray *b);

double  shm_fn_sqrt(double x);
double  shm_fn_log(double x);
double  shm_fn_exp(double x);
double  shm_fn_hypot(double x, double y);
double  shm_fn_sin(double x);
double  shm_fn_cos(double x);
double  shm_fn_tan(double x);
double  shm_fn_asin(double x);
double  shm_fn_acos(double x);
double  shm_fn_atan(double x);
double  shm_fn_atan2(double y, double x);
double  shm_fn_pow(double x, double y);
double  shm_fn_round(double x);
double  shm_fn_ceil(double x);
double  shm_fn_floor(double x);
double  shm_fn_trunc(double x);
double  shm_fn_abs_real(double x);
int32_t shm_fn_abs_int(int32_t x);
double  shm_fn_max_real(double a, double b);
double  shm_fn_min_real(double a, double b);
int32_t shm_fn_max_int(int32_t a, int32_t b);
int32_t shm_fn_min_int(int32_t a, int32_t b);

void shm_print_int(int32_t value);
void shm_print_real(double value);
void shm_print_char(int32_t value);
void shm_print_array(const ShmArray *array);
void shm_print_places(int32_t places);
void shm_line_end(void);

#ifdef __cplusplus
}
#endif

#endif
