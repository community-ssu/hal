#!/bin/sh

. ./setup-hald.sh

echo ========================================
echo Just type \'run\' to start debugging hald
echo ========================================
gdb run --args ./hald --daemon=no --verbose=yes $@

