#!/bin/sh

. ./setup-hald.sh

#valgrind --num-callers=20 --show-reachable=yes --leak-check=yes --tool=memcheck ./hald --daemon=no --verbose=yes $@
valgrind --show-reachable=yes --tool=memcheck --leak-check=full ./hald --daemon=no --verbose=yes $@
