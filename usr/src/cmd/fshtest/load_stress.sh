#!/bin/sh
cd /kernel/drv
while true
do
	modload fshtest &&
	modunload -i `modinfo | grep fshtest | cut -c -3`
done
