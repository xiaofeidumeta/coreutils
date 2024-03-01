#!/bin/sh

# Copyright (C) 2002-2024 Free Software Foundation, Inc.

# This program is free software: you can redistribute it and/or modify
# it under the terms of the GNU General Public License as published by
# the Free Software Foundation, either version 3 of the License, or
# (at your option) any later version.

# This program is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# GNU General Public License for more details.

# You should have received a copy of the GNU General Public License
# along with this program.  If not, see <https://www.gnu.org/licenses/>.

. "${srcdir=.}/tests/init.sh"; path_prepend_ ./src
print_ver_ mu

mkdir -p a/sub || framework_failure_
ln -s a/sub slink || framework_failure_
touch b || framework_failure_
ln -s .. a/sub/dotdot || framework_failure_
ln -s nowhere dangle || framework_failure_


mu -sD slink b > /dev/null 2>&1 || fail=1

returns_ 1 mu -L dangle > /dev/null 2>&1 || fail=1

mu_L_output=$(mu -L a) || fail=1
mu_lL_output=$(mu -lL a) || fail=1
mu_x_output=$(mu --exclude=dotdot a) || fail=1
test "X$mu_L_output" = "X$mu_x_output" || fail=1
test "X$mu_lL_output" = "X$mu_x_output" || fail=1

Exit $fail
