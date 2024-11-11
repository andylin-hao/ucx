#ifndef UCX_PROFILE_UTILS_H
#define UCX_PROFILE_UTILS_H

#include <ucs/profile/profile.h>

#include <stdio.h>

#define MAX_THREADS        256

#define print_error(_fmt, ...) \
    fprintf(stderr, "Error: " _fmt "\n", ## __VA_ARGS__)

typedef enum {
    TIME_UNITS_NSEC,
    TIME_UNITS_USEC,
    TIME_UNITS_MSEC,
    TIME_UNITS_SEC,
    TIME_UNITS_LAST
} time_units_t;


typedef struct {
    const ucs_profile_thread_header_t   *header;
    const ucs_profile_thread_location_t *locations;
    const ucs_profile_record_t          *records;
} profile_thread_data_t;


typedef struct {
    void                         *mem;
    size_t                       length;
    const ucs_profile_header_t   *header;
    const ucs_profile_location_t *locations;
    profile_thread_data_t        *threads;
    char                         *env_variables;
    uint32_t                     num_locations;
    unsigned                     num_threads;
} profile_data_t;


typedef struct {
    uint64_t                     total_time;
    size_t                       count;
    unsigned                     location_idx;
} profile_sorted_location_t;

int read_profile_data(const char *file_name, profile_data_t *data);
void release_profile_data(profile_data_t *data);

#endif /* UCX_PROFILE_UTILS_H */