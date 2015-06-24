#!/bin/sh

set -e

if [ ! $1 ] ; then
	echo "Please input one cpu"
	exit
fi

echo performance > /sys/devices/system/cpu/cpu$1/cpufreq/scaling_governor

sleep 1

sed 's/"calibration" : "CPU.*",/"calibration" : "CPU'$1'",/' -i calibration.json
pLoad=$(rt-app calibration.json 2>&1 |grep pLoad |sed 's/.*= \(.*\)ns.*/\1/')
sed 's/"calibration" : .*,/"calibration" : '$pLoad',/' -i dvfs.json
echo CPU$1\'s pLoad is $pLoad
