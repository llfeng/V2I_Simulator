#!/bin/bash

lane_num=$1
velocity=$2
interval=$3


		    for((m=1;m<=10;m++)); 
		    do
		        ./tag $interval &
		        sleep 1
		        ./reader $lane_num $velocity $interval
		        sleep 1
		    done

#./trace $1 $2 $3&
#sleep 1
#./tag $1 $2&
#
#sleep 2

#./reader $1 $2
