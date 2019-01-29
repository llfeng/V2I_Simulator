#!/bin/bash

#for ((i=1; i<=100; i++))
#do
#    echo $i
#done


./work_range.sh
sleep 1

./road_traffic.sh

sleep 1

./tag_density.sh
sleep 1

#./trace $1 $2 $3&
#sleep 1
#./tag $1 $2&
#
#sleep 2

#./reader $1 $2
