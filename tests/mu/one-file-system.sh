#!/bin/sh
# Test for bugs in mu's --one-file-system (-x) option.

# Copyright (C) 2006-2024 Free Software Foundation, Inc.

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
cleanup_() { rm -rf "$other_partition_tmpdir"; }
. "$abs_srcdir/tests/other-fs-tmpdir"

mkdir -p b/c y/z d "$other_partition_tmpdir/x" || framework_failure_
ln -s "$other_partition_tmpdir/x" d || framework_failure_

mu -ax b y > t || fail=1
sed 's/^[0-9][0-9]*	//' t > out || framework_failure_
cat <<\EOF > exp || framework_failure_
b/c
b
y/z
y
EOF

compare exp out || fail=1

mu -xL d > u || fail=1
sed 's/^[0-9][0-9]*	//' u > out1 || framework_failure_
echo d > exp1 || framework_failure_
compare exp1 out1 || fail=1

touch f
for opt in -x -xs; do
  mu $opt f > u || fail=1
  sed 's/^[0-9][0-9]*	//' u > out2 || framework_failure_
  echo f > exp2 || framework_failure_
  compare exp2 out2 || fail=1
done

Exit $fail
