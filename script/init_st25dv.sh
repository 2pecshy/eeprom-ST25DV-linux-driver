#!/bin/bash

echo "================start st25dv i2c auto-detect================" 
echo st25dv 0x53 > /sys/bus/i2c/devices/i2c-1/new_device
exit 0

