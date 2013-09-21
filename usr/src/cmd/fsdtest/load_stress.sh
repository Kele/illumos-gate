#!/bin/sh
cd /kernel/drv
while true
do
	modload fsd &&
	modunload -i `modinfo | grep fsd | cut -c -3`
done
