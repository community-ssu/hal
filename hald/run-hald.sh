#!/bin/sh

. ./setup-hald.sh

./hald --daemon=no --verbose=yes $@
#./hald --daemon=no


