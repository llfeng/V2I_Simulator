#!/bin/sh


./trace $1 $2 $3&
sleep 1
./tag $1 $2&

sleep 2

./reader $1 $2
