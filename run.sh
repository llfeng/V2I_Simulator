#!/bin/bash

#for ((i=1; i<=100; i++))
#do
#    echo $i
#done

for((velocity=0;velocity<=4;velocity++));   #velocity
do
	for((lane_num=1;lane_num<=3;lane_num++));   #lane num
	do
	    for((k=10;k>=1;k--));   #tag interval
	    do
		    interval=`expr $k \* 10`
#            echo lane${lane_num}_velocity${velocity}_spacing${interval}
            mkdir lane${lane_num}_velocity${velocity}_spacing${interval}
            cd lane${lane_num}_velocity${velocity}_spacing${interval}
            cp ../tag ../reader ../single_case.sh ./
            ./single_case.sh $lane_num $velocity $interval & 
            sleep 1
            cd ..
#		    for((m=1;m<=100;m++)); 
#		    do
#		        ./tag $interval &
#		        sleep 1
#		        ./reader $lane_num $velocity $interval
#		        sleep 1
#		    done
	    done
	done
done

#./trace $1 $2 $3&
#sleep 1
#./tag $1 $2&
#
#sleep 2

#./reader $1 $2
