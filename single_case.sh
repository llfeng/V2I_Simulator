#!/bin/bash

lane_num=$1
velocity=$2
interval=$3


		    for((m=1;m<=20;m++)); 
		    do
		        ./tag $interval &
		        sleep 1
                readerpid=`ps -ef|grep reader|grep -v grep|wc -l`
                while [ $readerpid -gt 500 ]
                do
                    sleep 1
                    readerpid=`ps -ef|grep reader|grep -v grep|wc -l`
                    echo "readerpid:" $readerpid
                done
                echo "./reader" $lane_num $velocity $interval
		        ./reader $lane_num $velocity $interval 
		        sleep 1
		    done

#./trace $1 $2 $3&
#sleep 1
#./tag $1 $2&
#
#sleep 2

#./reader $1 $2
