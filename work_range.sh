#!/bin/bash

#for ((i=1; i<=100; i++))
#do
#    echo $i
#done


for((velocity=1;velocity<=1;velocity++));   #velocity
do
	for((lane_num=2;lane_num<=2;lane_num++));   #lane num
	do
	    for((k=1;k<=3;k++));   #tag interval
	    do
            if [ $k -eq 1 ]
            then
                interval=100
            fi
            if [ $k -eq 2 ]
            then
                interval=500
            fi
            if [ $k -eq 3 ]
            then
                interval=1000
            fi
#		    interval=`expr $k \* 10`
#            echo lane${lane_num}_velocity${velocity}_spacing${interval}
            mkdir lane${lane_num}_velocity${velocity}_spacing${interval}
            cd lane${lane_num}_velocity${velocity}_spacing${interval}
            cp ../tag ../reader ../single_case.sh ./
            ./single_case.sh $lane_num $velocity $interval & 
            sleep 1
            cd ..
	    done
	done
done

#./trace $1 $2 $3&
#sleep 1
#./tag $1 $2&
#
#sleep 2

#./reader $1 $2
