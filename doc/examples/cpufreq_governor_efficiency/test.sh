#!/bin/sh

set -e

test_efficiency() {

	FILENAME="results_$RANDOM$$.txt"

	if [ -e /sys/devices/system/cpu/cpu0/cpufreq/scaling_available_governors ]; then
		for i in $(cat /sys/devices/system/cpu/cpu0/cpufreq/scaling_available_governors); do
			if [ $i = $1 ] ; then
				gov_target=$i
			fi
			export gov_$i=$(echo $i)
		done
	else
		echo "cpufreq sysfs is not available!"
		exit
	fi

	if [ ! $gov_target ] ; then
		echo " Can't find $1 governor!"
		exit
	fi

	if [ ! $gov_performance ] ; then
		echo "Can't find performance governor!"
		exit
	fi

	if [ ! $gov_powersave ] ; then
		echo "Can't find powersave governor!"
		exit
	fi

	if [ $gov_target = $gov_performance ] || [ $gov_target = $gov_powersave ] ; then
		echo "Please input a governor other than \"performance\" or \"powersave\""
		exit
	fi

	# Get target gov data first
	dvfs.sh $1 $2 $3 $4 $5 > $FILENAME
	target=$(cat $FILENAME |sed -n '$p' | cut -d " " -f 1)
	over_target=$(cat $FILENAME |sed -n '$p' | cut -d " " -f 2)
	# Get performance data
	dvfs.sh performance $2 $3 $4 $5 > $FILENAME
	performance=$(cat $FILENAME |sed -n '$p' | cut -d " " -f 1)
        over_perf=$(cat $FILENAME |sed -n '$p' | cut -d " " -f 2)
        # Get powersave data
        dvfs.sh powersave $2 $3 $4 $5 > $FILENAME
        powersave=$(cat $FILENAME |sed -n '$p' | cut -d " " -f 1)
        over_save=$(cat $FILENAME |sed -n '$p' | cut -d " " -f 2)
	if [ $performance -ge $powersave ] ; then
		echo "powersave: $powersave"
		echo "performance: $performance"
		echo "Error! performance spent more time than powersave!"
		exit
	fi

	echo "\"powersave\" efficiency: 0% overrun: "$over_save
	echo "\"performance\" efficiency: 100% overrun: "$over_perf

	denominator=$(($powersave - $performance))

	if [ $powersave -le $target ]; then
		target=0
	else
		numerator=$(($powersave - $target))
		numerator=$(($numerator * 100))
		target=$(($numerator / $denominator))
		if [ $target -gt 100 ]; then
			target=100
		fi
	fi

	echo "\"$gov_target\" efficiency: $target% overrun: "$over_target

	rm -f $FILENAME
}

if [ $# -lt 4 ]; then
	echo "Usage: ./test.sh <governor> <cpu> <runtime> <sleeptime> [<loops>]"
	echo "governor:	target CPUFreq governor you want to test"
	echo "cpu:		cpu number on which you want to run the test"
	echo "runtime:	running time in ms per loop of the workload pattern"
	echo "sleeptime:	sleeping time in ms per loop of the workload pattern"
	echo "loops:		repeat times of the workload pattern. default: 10"
	echo "\nExample:\n\"./test.sh ondemand 0 100 100 20\" means\nTest \"ondemand\" on CPU0 with workload pattern \"run 100ms + sleep 100ms\"(20 loops).\n"
	exit
fi

if [ $# = 4 ]; then
	loops=10
else
	loops=$5
fi

echo "Test \"$1\" on CPU$2 with workload pattern \"run $3ms + sleep $4ms\"($loops loops)."

sleep 1
PATH=$PATH:.

test_efficiency $1 $2 $(($3 * 1000)) $(($4 * 1000)) $loops

