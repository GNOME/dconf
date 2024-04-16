#!/usr/bin/env bash
# Copyright 2020 Frederic Martinsons
# Copyright 2024 Collabora Ltd.
# SPDX-License-Identifier: LGPL-2.1-or-later

set -eu

if [ -z "${G_TEST_SRCDIR-}" ]; then
    me="$(readlink -f "$0")"
    G_TEST_SRCDIR="${me%/*}"
fi

export TEST_NAME=shellcheck
export TEST_REQUIRES_TOOLS="git shellcheck"

run_lint () {
    # shellcheck disable=SC2046
    shellcheck $(git ls-files '*.sh' 'bin/completion/dconf')
}

# shellcheck source=tests/lint-common.sh
. "$G_TEST_SRCDIR/lint-common.sh"
