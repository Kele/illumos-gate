#!/bin/bash
while true
do
	./fsdtestadm /rpool/test5/play/fsdtestadm /rpool/test5/play/copy && \
	diff /rpool/test5/play/fsdtestadm /rpool/test5/play/copy && \
	sleep 1
done
