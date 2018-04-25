#!/bin/bash

echo "================remove st25dv i2c================" 
echo 0x53 > /sys/bus/i2c/devices/i2c-1/delete_device
exit 0
