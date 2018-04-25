#!/bin/bash
#Author: 2018 Loïc Boban <loic.boban@gmail.com>
#Test of ST25DV i2c EEPROM driver

check () {
	if [ "$1" != "$2" ] 
	then
		echo "Test FAIL."
		rm ${path}/*.img
		exit -1
	fi
	echo "Test PASS."
}

path=$(pwd)
result="000"
echo "================Test ST25DV EEPROM driver================"
dd if=/dev/zero of=${path}/st25dv_img_default.img bs=1b count=1 &> /dev/null
dd if=/dev/urandom of=${path}/st25dv_img_test.img bs=1b count=1 &> /dev/null
dd if=/dev/zero of=${path}/default_pwd.img bs=8c count=1 &> /dev/null
dd if=/dev/urandom of=${path}/bad_pwd.img bs=8c count=1 &> /dev/null

echo "==========>st25dv_probe test"
result=$(echo st25dv 0x53 2>&1 > /sys/bus/i2c/devices/i2c-1/new_device)
check $result ""

echo "==========>user area r/w test"
dd if=${path}/st25dv_img_test.img of=/sys/bus/i2c/devices/1-0053/st25dv_user bs=1b count=1
result=$(diff ${path}/st25dv_img_test.img /sys/bus/i2c/devices/1-0053/st25dv_user)
check $result ""

echo "==========>present password test"
dd if=${path}/default_pwd.img of=/sys/bus/i2c/devices/1-0057/st25dv_present_pwd
result=$(xxd -p -s4 -l1 /sys/bus/i2c/devices/1-0053/st25dv_dyn_reg)
check $result "01"

echo "==========>present bad password test"
dd if=${path}/bad_pwd.img of=/sys/bus/i2c/devices/1-0057/st25dv_present_pwd
result=$(xxd -p -s4 -l1 /sys/bus/i2c/devices/1-0053/st25dv_dyn_reg)
check $result "00"

echo "==========>st25dv_remove test"
result=$(echo 0x53 2>&1 > /sys/bus/i2c/devices/i2c-1/delete_device)
check $result ""

rm ${path}/*.img
exit 0
