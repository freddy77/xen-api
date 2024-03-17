#!/bin/sh

# Use user-writable directories
#
# check weird char
# remove directory at beginning
# strace support
# look at leaks at the end
# define PROG
# killall at the beginning
export TMPDIR=${TMPDIR:-/tmp}
export XDG_RUNTIME_DIR=${XDG_RUNTIME_DIR:-$TMPDIR}
export FE_TEST=1

SOCKET=${XDG_RUNTIME_DIR}/xapi/forker/main
rm -f "$SOCKET"
killall fe_main.exe > /dev/null 2>&1

strace -f -o trace -s 800 ../src/fe_main.exe &
MAIN=$!
cleanup () {
    killall fe_main.exe > /dev/null 2>&1
    kill $MAIN
}
trap cleanup EXIT INT
for _ in $(seq 1 10); do
    test -S ${SOCKET} || sleep 1
done
echo "" | ./fe_test.exe 16
