#!/bin/sh

# The following checks that a given binary only defines the symbols
# specified
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

if test $# != 2; then
    echo "  Usage: $0 <binary> <symbol_file>"
    echo "  symbol_file is a file where each line is the name of an exported symbol"
    exit 1
fi

BINARY="$1"
SYMBOL_FILE="$2"

${NM:-nm} --dynamic --defined-only "$BINARY" | \
    cut -f 3 -d ' ' | \
    grep -v ^_ | \
    grep -v ^environ | \
    grep -v __progname | \
    grep -v ^mangle_path | \
    diff "$SYMBOL_FILE" -
