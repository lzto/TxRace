#!/bin/bash

logfile="output.log"

if [ "$1" != "" ];then
	logfile=$1
fi

echo "Processing Log: ${logfile}"

exe=./blackscholes.tx.exe

while read line; do
	if [[ $line =~ ^0x* ]]; then

		fromaddr=`echo $line | cut -d'-' -f1`
		toaddr=`echo $line | cut -d'>' -f2`
		echo "FROM:$fromaddr"
		addr2line -e ${exe} -f -a $fromaddr
		echo "TO:$toaddr"
		addr2line -e ${exe} -f -a $toaddr
		echo "--------"
	else
		echo "RAW:" $line
	fi
done<"$logfile"

