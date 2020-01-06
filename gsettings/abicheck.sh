#!/bin/sh

# The following checks that gsettings/libdconfsettings.so only has
# dconf_* symbols.
#
# We also make sure to ignore gcov symbols included when building with
# --coverage, which usually means the following:
#
# __gcov_error_file
# __gcov_master
# __gcov_sort_n_vals
# __gcov_var
#
# And starting with gcc-9, also this one:
#
# mangle_path

set -x

${NM:-nm} --dynamic --defined-only $GSETTINGS_LIB > public-abi

test "`\
    cat public-abi | \
    cut -f 3 -d ' ' | \
    grep -v ^_ | \
    grep -v ^mangle_path | \
    grep -v ^g_io_module | \
    wc -l`" -eq 0 && rm public-abi
