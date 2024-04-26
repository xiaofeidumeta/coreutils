#!/bin/sh
# make sure mu's --exclude option works

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

mkdir -p a/b/c a/x/y a/u/v || framework_failure_


mu --exclude=x a | sed 's/^[0-9][0-9]*	//' | sort > out || fail=1
printf '===\n' >> out
printf 'b\n' > excl
mu --exclude-from=excl a | sed 's/^[0-9][0-9]*	//' | sort >> out || fail=1
printf '===\n' >> out
# Make sure that we can exclude an entire hierarchy.
mu --exclude=a a >> out || fail=1
# Make sure that we can exclude based on more than one component.
# Before coreutils-5.3.0, this part would fail.
printf '===\n' >> out
mu --exclude=a/u --exclude=a/b a \
  | sed 's/^[0-9][0-9]*	//' | sort >> out || fail=1
cat <<\EOF > exp
a
a/b
a/b/c
a/u
a/u/v
===
a
a/u
a/u/v
a/x
a/x/y
===
===
a
a/x
a/x/y
EOF

compare exp out || fail=1

Exit $fail
