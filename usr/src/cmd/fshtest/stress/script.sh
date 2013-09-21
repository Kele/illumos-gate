#!/bin/bash
while true
do
	./stress /rpool/test1/cbtest1 100000 &
	./stress /rpool/test1/cbtest2 100000 &
	./stress /rpool/test1/cbtest3 100000 &
	./stress /rpool/test1 100000 &
	./stress /rpool/test1 100000
done
