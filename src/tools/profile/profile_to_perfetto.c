#include "profile_utils.h"
#include <getopt.h>
#include <assert.h>
#include <errno.h>
#include <fcntl.h>

#include <ucs/datastruct/khash.h>

typedef struct options {
    const char *filename;
} options_t;

typedef struct {
    char     name[32];
    char     cat[24];
    char     ph;
    uint32_t pid;
    uint32_t tid;
    uint64_t ts;
    uint64_t event_id;
} trace_event_t;

static void usage()
{
    printf("Usage: ucx_profile_to_perfetto [profile-file]\n");
    printf("Options are:\n");
    printf("  -h              Show this help message\n");
}

static int parse_args(int argc, char **argv, options_t *opts)
{
    int c;

    while ((c = getopt(argc, argv, "h")) != -1) {
        switch (c) {
        case 'h':
            usage();
            return -127;
        default:
            usage();
            return -1;
        }
    }

    if (optind >= argc) {
        print_error("missing profile file argument\n");
        usage();
        return -1;
    }

    opts->filename = argv[optind];
    return 0;
}

KHASH_MAP_INIT_INT64(request_ids, size_t);

static void convert_to_perfetto(FILE *fp, const profile_data_t *data,
                                options_t *opts, int thread_idx, uint64_t start_time)
{
    profile_thread_data_t        *thread      = &data->threads[thread_idx];
    size_t                        num_records = thread->header->num_records;
    const ucs_profile_record_t  **stack[UCS_PROFILE_STACK_MAX * 2];
    const ucs_profile_record_t  **scope_ends;
    const ucs_profile_location_t *loc;
    const ucs_profile_record_t   *rec, *se, **sep;
    trace_event_t                 event;
    int                           nesting;
    khash_t(request_ids) reqids;
    int hash_extra_status;
    khiter_t hash_it;
    size_t reqid;
    size_t reqid_ctr              = 1;

#define WRITE_TRACE_EVENT(_fp, _event)                                         \
    fprintf(_fp,                                                               \
            "{\"name\":\"%s\",\"cat\":\"%s\",\"ph\":\"%c\",\"pid\":%u,"        \
            "\"tid\":%u,\"ts\":%lu}%s\n",                                      \
            _event.name, _event.cat, _event.ph, _event.pid, _event.tid,        \
            _event.ts,                                                         \
            (rec == thread->records + num_records - 1 &&                       \
             thread_idx == data->num_threads - 1)                              \
                ? ""                                                           \
                : ",")

    scope_ends = calloc(num_records, sizeof(*scope_ends));
    if (scope_ends == NULL) {
        print_error("failed to allocate memory for scope ends");
        return;
    }

    memset(stack, 0, sizeof(stack));

    nesting = 0;
    for (rec = thread->records; rec < thread->records + num_records; ++rec) {
        loc = &data->locations[rec->location];
        switch (loc->type) {
        case UCS_PROFILE_TYPE_SCOPE_BEGIN:
            stack[nesting + UCS_PROFILE_STACK_MAX] =
                &scope_ends[rec - thread->records];
            ++nesting;
            break;
        case UCS_PROFILE_TYPE_SCOPE_END:
            --nesting;
            sep = stack[nesting + UCS_PROFILE_STACK_MAX];
            if (sep != NULL) {
                *sep = rec;
            }
            break;
        default:
            break;
        }
    }

    kh_init_inplace(request_ids, &reqids);

    event.pid = data->header->pid;
    for (rec = thread->records; rec < thread->records + num_records; ++rec) {
        loc       = &data->locations[rec->location];
        event.tid = thread->header->tid;
        event.ts  = (rec->timestamp - start_time) / 1000;
        strncpy(event.cat, "function", sizeof(event.cat));
        switch (loc->type) {
        case UCS_PROFILE_TYPE_SCOPE_BEGIN:
            se = scope_ends[rec - thread->records];
            if (se != NULL) {
                strncpy(event.name, data->locations[se->location].name,
                        sizeof(event.name));
                event.ph = 'B';
                WRITE_TRACE_EVENT(fp, event);
            }
            break;
        case UCS_PROFILE_TYPE_SCOPE_END:
            strncpy(event.name, loc->name, sizeof(event.name));
            event.ph = 'E';
            WRITE_TRACE_EVENT(fp, event);
            break;
        case UCS_PROFILE_TYPE_SAMPLE:
            strncpy(event.name, loc->name, sizeof(event.name));
            event.ph = 'i';
            WRITE_TRACE_EVENT(fp, event);
            break;
        case UCS_PROFILE_TYPE_REQUEST_NEW:
        case UCS_PROFILE_TYPE_REQUEST_EVENT:
        case UCS_PROFILE_TYPE_REQUEST_FREE:
            strncpy(event.name, loc->name, sizeof(event.name));
            if (loc->type == UCS_PROFILE_TYPE_REQUEST_NEW) {
                hash_it = kh_put(request_ids, &reqids, rec->param64,
                                 &hash_extra_status);
                if (hash_it == kh_end(&reqids)) {
                    if (hash_extra_status == UCS_KH_PUT_KEY_PRESENT) {
                        /* old request was not released, replace it */
                        hash_it = kh_get(request_ids, &reqids, rec->param64);
                        reqid = reqid_ctr++;
                        kh_value(&reqids, hash_it) = reqid;
                    } else {
                        reqid = 0; /* error inserting to hash */
                    }
                } else {
                    /* new request */
                    reqid = reqid_ctr++;
                    kh_value(&reqids, hash_it) = reqid;
                }
                event.ph = 'b';
            } else {
                hash_it = kh_get(request_ids, &reqids, rec->param64);
                if (hash_it == kh_end(&reqids)) {
                    reqid = 0; /* could not find request */
                } else {
                    assert(reqid_ctr > 1);
                    reqid = kh_value(&reqids, hash_it);
                    if (loc->type == UCS_PROFILE_TYPE_REQUEST_FREE) {
                        kh_del(request_ids, &reqids, hash_it);
                    }
                }
                if (loc->type == UCS_PROFILE_TYPE_REQUEST_FREE) {
                    event.ph = 'e';
                } else {
                    event.ph = 'n';
                }
            }
            event.event_id = reqid;
        default:
            break;
        }
    }
}

static void write_perfetto_trace(const profile_data_t *data, options_t *opts)
{
    int   thread_idx;
    profile_thread_data_t *thread;
    FILE *fp;
    uint64_t start_time = 0;
    char  perfetto_filename[1024] = {0};

#define WRITE_PERFETTO_HEADER(_fp) fprintf(_fp, "{\"traceEvents\":[\n")
#define WRITE_PERFETTO_FOOTER(_fp)                                              \
    fprintf(_fp, "],\n \"displayTimeUnit\": \"ns\"\n}")

    snprintf(perfetto_filename, sizeof(perfetto_filename), "%s.%s",
             opts->filename, "json");

    fp = fopen(perfetto_filename, "w");
    if (fp == NULL) {
        print_error("failed to open perfetto fail at '%s' with error '%s'",
                    perfetto_filename, strerror(errno));
        return;
    }

    for (thread_idx = 0; thread_idx < data->num_threads; thread_idx++) {
        thread = &data->threads[thread_idx];
        if (thread->header->num_records > 0 && (start_time == 0 ||
                                                thread->records[0].timestamp < start_time)) {
            start_time = thread->records[0].timestamp;
        }
    }

    WRITE_PERFETTO_HEADER(fp);
    for (thread_idx = 0; thread_idx < data->num_threads; thread_idx++) {
        convert_to_perfetto(fp, data, opts, thread_idx, start_time);
    }
    WRITE_PERFETTO_FOOTER(fp);

    fclose(fp);
}

int main(int argc, char **argv)
{
    profile_data_t data = {0};
    options_t      opts;
    int            ret;

    ret = parse_args(argc, argv, &opts);
    if (ret < 0) {
        return (ret == -127) ? 0 : ret;
    }

    /* coverity[tainted_argument] */
    ret = read_profile_data(opts.filename, &data);
    if (ret < 0) {
        return ret;
    }
    write_perfetto_trace(&data, &opts);

    release_profile_data(&data);
    return ret;
    return 0;
}