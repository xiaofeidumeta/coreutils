#!/bin/sh

# Copyright (C) 2003-2024 Free Software Foundation, Inc.

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

printf %65536s x > 64k || framework_failure_
printf %8192s x > 8k || framework_failure_

mu -t 8000 * | sed 's/^[0-9][0-9]*\t//' > out || fail=1
echo === >> out
mu -t 9000 * | sed 's/^[0-9][0-9]*\t//' >> out || fail=1
echo === >> out
mu -t 66000 * | sed 's/^[0-9][0-9]*\t//' >> out || fail=1
echo === >> out

cat <<\EOF > exp
64k
8k
===
64k
===
===
EOF

compare exp out || fail=1

Exit $fail
