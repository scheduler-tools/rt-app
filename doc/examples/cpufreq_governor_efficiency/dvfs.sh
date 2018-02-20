#!/bin/sh

# $1 $2 $3 $4 $5: governor cpu run sleep loops
set -e

echo $1 > /sys/devices/system/cpu/cpu$2/cpufreq/scaling_governor
#echo $1 > /sys/devices/system/cpu/cpu1/cpufreq/scaling_governor
sed 's/"cpus" : \[.*\],/"cpus" : \['$2'\],/' -i dvfs.json

if [ $3 ] ; then
	sed 's/"run" : .*,/"run" : '$3',/' -i dvfs.json
fi

if [ $4 ] ; then
	sed 's/"period" : .*,/"period" : '$4',/' -i dvfs.json
fi

if [ $5 ] ; then
	sed '0,/"loop"/s/"loop" : .*,/"loop" : '$5',/' -i dvfs.json
fi

sync

sleep 1

rt-app dvfs.json 2> /dev/null

if [ $1 ] ; then

	mv -f rt-app-thread-0.log rt-app_$1_run$3us_sleep$4us.log

	sum=0
	loop=0
	overrun=0
	for i in $(cat rt-app_$1_run$3us_sleep$4us.log | sed '1d;n;d' | sed '1d' | awk '{print $3}'); do
		loop=$(($loop + 1))
		sum=$(($sum + $i))
		if [ $4 -le $i ] ; then
			#echo $i"vs"$4
			overrun=$(($overrun + 1))
		fi
	done

	sum=$(($sum / $loop))
	echo $sum" "$overrun
	#rm -f rt-app_$1_run$3us_sleep$4us.log
fi

