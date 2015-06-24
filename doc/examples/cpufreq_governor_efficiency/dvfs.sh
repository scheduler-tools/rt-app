#!/bin/sh

# $1 $2 $3 $4 $5: governor cpu run sleep loops
set -e

echo $1 > /sys/devices/system/cpu/cpu$2/cpufreq/scaling_governor
#echo $1 > /sys/devices/system/cpu/cpu1/cpufreq/scaling_governor
sed 's/"cpus" : \[.*\],/"cpus" : \['$2'\],/' -i dvfs.json
sleep 3

if [ $3 ] ; then
	sed 's/"run" : .*,/"run" : '$3',/' -i dvfs.json
fi

if [ $4 ] ; then
	sed 's/"sleep" : .*,/"sleep" : '$4',/' -i dvfs.json
fi

if [ $5 ] ; then
	sed 's/^"loop" : .*,/"loop" : '$5',/' -i dvfs.json
fi

rt-app dvfs.json 2> /dev/null

if [ $1 ] ; then
	mv -f rt-app-thread-0.log rt-app_$1_run$3us_sleep$4us.log

	sum=0
	loop=0
	for i in $(cat rt-app_$1_run$3us_sleep$4us.log | sed 'n;d' | sed '1d' |cut -f 3); do
		loop=$(expr $loop + 1)
		sum=$(expr $sum + $i)
	done
	sum=$(expr $sum / $loop)
	echo $sum
	rm -f rt-app_$1_run$3us_sleep$4us.log
fi

