#!/bin/sh
export GDK_DEBUG="${GDK_DEBUG:+$GDK_DEBUG,}gl-disable"
export GSK_RENDERER=cairo
exec /usr/bin/snapshot "$@"
