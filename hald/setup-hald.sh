#!/bin/sh

information_fdidir="../../hal-info/fdi"

case `uname -s` in
    FreeBSD)	backend=freebsd ;;
    SunOS)	backend=solaris ;;
    *)		backend=linux ;;
esac
export HALD_RUNNER_PATH=`pwd`/$backend:`pwd`/$backend/probing:`pwd`/$backend/addons:`pwd`/.:`pwd`/../tools:`pwd`/../tools/$backend
export PATH=`pwd`/../hald-runner:$PATH

cleanup()
{
    [ -n "$HALD_TMPDIR" ] && rm -rf $HALD_TMPDIR
}

trap cleanup EXIT INT ABRT QUIT TERM

HALD_TMPDIR=`mktemp -t -d run-hald-XXXXXXXXXX` || exit 1

chmod 755 $HALD_TMPDIR

if [ "$1" = "--skip-fdi-install" ] ; then
    shift
else
    make -C ../policy install DESTDIR=$HALD_TMPDIR prefix=/
    make -C ../fdi install DESTDIR=$HALD_TMPDIR prefix=/ && \
    if [ -d $information_fdidir ] ; then
        make -C $information_fdidir install DESTDIR=$HALD_TMPDIR prefix=/
    else
        echo "WARNING: You need to checkout hal-info in the same level"
    	echo "directory as hal to get the information fdi files."
    fi
fi
export HAL_FDI_SOURCE_PREPROBE=$HALD_TMPDIR/share/hal/fdi/preprobe
export HAL_FDI_SOURCE_INFORMATION=$HALD_TMPDIR/share/hal/fdi/information
export HAL_FDI_SOURCE_POLICY=$HALD_TMPDIR/share/hal/fdi/policy
export HAL_FDI_CACHE_NAME=$HALD_TMPDIR/hald-local-fdi-cache
export POLKIT_POLICY_DIR=$HALD_TMPDIR/share/PolicyKit/policy

