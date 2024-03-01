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

printf %8192s x > testfile || framework_failure_
# make sure no dirty pages
sync testfile

mu -f '%c %d %w %e %r' testfile | sed 's/\stestfile/ testfile/' > out || fail=1
echo === >> out

# append 4k to the file. That 4k will be dirty
printf %4096s x >> testfile || framework_failure_

mu -f '%c %d %w %e %r' testfile | sed 's/\stestfile/ testfile/' >> out || fail=1
echo === >> out

# flush the dirty page
sync testfile
mu -f '%c %d %w %e %r' testfile | sed 's/\stestfile/ testfile/' >> out || fail=1
echo === >> out

# drop the file cache
dd of=testfile oflag=nocache conv=notrunc,fdatasync count=0
mu -f '%c %d %w %e %r' testfile | sed 's/\stestfile/ testfile/' >> out || fail=1
echo === >> out

# pull the pages into cache
cat testfile
mu -f '%c %d %w %e %r' testfile | sed 's/\stestfile/ testfile/' >> out || fail=1
echo === >> out

cat <<\EOF > exp
8 0 0 0 0 testfile
===
12 4 0 0 0 testfile
===
12 0 0 0 0 testfile
===
0 0 0 0 0 testfile
===
12 0 0 0 0 testfile
===
EOF

compare exp out || fail=1

Exit $fail
