#!/bin/bash

path=$(pwd)
echo "================Test ST25DV EEPROM driver================"
echo "==========>user area r/w test"
dd if=/dev/zero of=${path}/st25dv_img_test.img bs=1b count=1
dd if=${path}/st25dv_img_test.img of=/sys/bus/i2c/devices/1-0053/st25dv_user bs=1b count=1
result=$(diff ${path}/st25dv_img_test.img /sys/bus/i2c/devices/1-0053/st25dv_user)
if [ "$result" != "" ] 
then
	echo "Test FAIL."
	rm ${path}/st25dv_img_test.img
	exit -1
fi
echo "Test PASS."
rm ${path}/st25dv_img_test.img
exit 0
