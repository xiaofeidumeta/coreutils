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

mkdir -p a/b || framework_failure_
printf %4096s x > a/b/f || framework_failure_

mu -a a | sed 's/\s/ /' > out || fail=1
echo === >> out
mu -a -S a | sed 's/\s/ /' >> out || fail=1
echo === >> out
mu -s a | sed 's/\s/ /' >> out || fail=1
echo === >> out
mu -a -b a | sed 's/\s/ /' >> out || fail=1
echo === >> out
mu -a -h a | sed 's/\s/ /' >> out || fail=1
echo === >> out
mu -a --block-size=512 a | sed 's/\s/ /' >> out || fail=1
echo === >> out


cat <<\EOF > exp
4 a/b/f
4 a/b
4 a
===
4 a/b/f
4 a/b
0 a
===
4 a
===
4096 a/b/f
4096 a/b
4096 a
===
4.0K a/b/f
4.0K a/b
4.0K a
===
8 a/b/f
8 a/b
8 a
===
EOF

compare exp out || fail=1

Exit $fail
