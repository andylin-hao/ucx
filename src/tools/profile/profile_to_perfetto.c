#include "profile_utils.h"
#include <getopt.h>
#include <errno.h>
#include <fcntl.h>

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

static void convert_to_perfetto(FILE *fp, const profile_data_t *data,
                                options_t *opts, int thread_idx)
{
    profile_thread_data_t        *thread      = &data->threads[thread_idx];
    size_t                        num_records = thread->header->num_records;
    const ucs_profile_record_t  **stack[UCS_PROFILE_STACK_MAX * 2];
    const ucs_profile_record_t  **scope_ends;
    const ucs_profile_location_t *loc;
    const ucs_profile_record_t   *rec, *se, **sep;
    trace_event_t                 event;
    int                           nesting;
    uint64_t start_time;

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

    if (num_records > 0) {
        start_time = thread->records[0].timestamp;
    } else {
        start_time = 0;
    }

    for (rec = thread->records; rec < thread->records + num_records; ++rec) {
        loc       = &data->locations[rec->location];
        event.pid = 1;
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
        case UCS_PROFILE_TYPE_REQUEST_NEW:
        case UCS_PROFILE_TYPE_REQUEST_EVENT:
        case UCS_PROFILE_TYPE_REQUEST_FREE:
        default:
            break;
        }
    }
}

static void write_perfetto_trace(const profile_data_t *data, options_t *opts)
{
    int   thread_idx;
    FILE *fp;
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

    WRITE_PERFETTO_HEADER(fp);
    for (thread_idx = 0; thread_idx < data->num_threads; thread_idx++) {
        convert_to_perfetto(fp, data, opts, thread_idx);
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