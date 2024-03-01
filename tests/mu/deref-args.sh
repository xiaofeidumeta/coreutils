#!/bin/sh
# Ensure that --dereference-args (-D) gives reasonable names.

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

mkdir -p dir/a || framework_failure_
ln -s dir slink || framework_failure_

mu -D slink | sed 's/^[0-9][0-9]*	//' > out
# Ensure that the trailing slash is preserved and handled properly.
mu -D slink/ | sed 's/^[0-9][0-9]*	//' >> out

cat <<\EOF > exp
slink/a
slink
slink/a
slink/
EOF

compare exp out || fail=1

Exit $fail
