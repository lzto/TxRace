#!/bin/bash

echo "test prog"
time $PIN_ROOT/pin -t obj-intel64/tx_prof.so -- $1 #/bin/ls #~/tm/htm/test_rtm

