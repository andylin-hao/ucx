#include "profile_utils.h"

#include <sys/mman.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <errno.h>


int read_profile_data(const char *file_name, profile_data_t *data)
{
    size_t total_num_records = 0;
    size_t env_vars_size;
    int ret, fd;
    uint32_t thread_idx;
    struct stat stt;
    const void *ptr;
    const void *threads_start, *threads_end;
    profile_thread_data_t *thread;

    data->env_variables = NULL;
    fd                  = open(file_name, O_RDONLY);
    if (fd < 0) {
        print_error("failed to open %s: %m", file_name);
        ret = fd;
        goto out;
    }

    ret = fstat(fd, &stt);
    if (ret < 0) {
        print_error("fstat(%s) failed: %m", file_name);
        goto out_close;
    }

    data->length = stt.st_size;
    data->mem    = mmap(NULL, stt.st_size, PROT_READ, MAP_PRIVATE, fd, 0);
    if (data->mem == MAP_FAILED) {
        print_error("mmap(%s, length=%zd) failed: %m", file_name, data->length);
        ret = -1;
        goto out_close;
    }

    ptr          = data->mem;
    data->header = ptr;

    if (data->header->version < UCS_PROFILE_FILE_MIN_VERSION) {
        print_error("invalid file version, expected: %u or greater, actual: %u",
                    UCS_PROFILE_FILE_MIN_VERSION, data->header->version);
        ret = -EINVAL;
        goto err_munmap;
    }

    env_vars_size = data->header->env_vars.size;
    /* coverity[tainted_data] */
    data->env_variables = malloc(env_vars_size + 1);
    if (data->env_variables == NULL) {
        print_error("failed to allocate env variables");
        ret = -1;
        goto err_munmap;
    }

    /* coverity[tainted_data] */
    memcpy(data->env_variables,
           UCS_PTR_BYTE_OFFSET(data->mem, data->header->env_vars.offset),
           env_vars_size);
    data->env_variables[env_vars_size] = '\0';

    data->num_locations = data->header->locations.size /
                          sizeof(ucs_profile_location_t);
    data->locations = UCS_PTR_BYTE_OFFSET(data->mem,
                                          data->header->locations.offset);
    /* coverity[tainted_data] */
    data->threads = calloc(data->header->threads.size, 1);
    if (data->threads == NULL) {
        print_error("failed to allocate threads array");
        ret = -1;
        goto err_env_variables;
    }

    threads_start = UCS_PTR_BYTE_OFFSET(data->mem,
                                        data->header->threads.offset);
    threads_end   = UCS_PTR_BYTE_OFFSET(threads_start,
                                        data->header->threads.size);

    for (thread_idx = 0, ptr = threads_start; ptr < threads_end; ++thread_idx) {
        thread            = &data->threads[thread_idx];
        thread->header    = ptr;
        ptr               = thread->header + 1;
        thread->locations = ptr;
        ptr               = thread->locations + data->num_locations;
        thread->records   = ptr;
        ptr               = thread->records + thread->header->num_records;
        total_num_records += thread->header->num_records;
    }

    data->num_threads = ucs_profile_calc_num_threads(total_num_records,
                                                     data->header);

    ret = 0;

out_close:
    close(fd);
out:
    return ret;

err_env_variables:
    free(data->env_variables);
err_munmap:
    munmap(data->mem, data->length);
    goto out_close;
}

void release_profile_data(profile_data_t *data)
{
    free(data->threads);
    free(data->env_variables);
    munmap(data->mem, data->length);
}