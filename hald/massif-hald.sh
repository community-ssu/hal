#!/bin/sh

. ./setup-hald.sh

#delete all old memory outputs, else we get hundreds
rm massif.*

G_SLICE="always-malloc" valgrind --num-callers=24 --tool=massif --depth=24 --format=html \
        --alloc-fn=g_malloc --alloc-fn=g_realloc \
	--alloc-fn=g_try_malloc --alloc-fn=g_malloc0 --alloc-fn=g_mem_chunk_all \
        --alloc-fn=g_slice_alloc0 --alloc-fn=g_slice_alloc \
	--alloc-fn=dbus_realloc \
	 ./hald --daemon=no --verbose=yes --retain-privileges --exit-after-probing

#massif uses the pid file, which is hard to process.
mv massif.*.html massif.html
mv massif.*.ps massif.ps
#convert to pdf, and make readable by normal users
ps2pdf massif.ps massif.pdf
chmod a+r massif.*
