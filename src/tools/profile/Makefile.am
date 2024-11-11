#
# Copyright (c) NVIDIA CORPORATION & AFFILIATES, 2001-2016. ALL RIGHTS RESERVED.
#
# See file LICENSE for terms.
#

bin_PROGRAMS              = ucx_read_profile ucx_profile_to_perfetto
ucx_read_profile_CPPFLAGS = $(BASE_CPPFLAGS)
ucx_read_profile_CFLAGS   = $(BASE_CFLAGS)
ucx_read_profile_SOURCES  = \
    profile_utils.h \
    profile_utils.c \
    read_profile.c
ucx_read_profile_LDADD    = \
    $(abs_top_builddir)/src/ucs/libucs.la

ucx_profile_to_perfetto_CPPFLAGS = $(BASE_CPPFLAGS)
ucx_profile_to_perfetto_CFLAGS   = $(BASE_CFLAGS)
ucx_profile_to_perfetto_SOURCES  = \
    profile_utils.h \
    profile_utils.c \
    profile_to_perfetto.c
ucx_profile_to_perfetto_LDADD    = \
    $(abs_top_builddir)/src/ucs/libucs.la
