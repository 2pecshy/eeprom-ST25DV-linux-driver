# eeprom-ST25DV-linux-driver

## ST25DV I2C/NFC EEPROM driver

This module is a simple driver to drive the ST25DV eeprom. The ST25DV is a EEPROM
readable and writeable by i2c and also by wireless Near Field Communication (NFC). 
Because the EEPROM can also be write by NFC, no cache of the eeprom is done, to get 
the right data even if a write has been done by NFC.

### This module has been tested on RaspberryPi Model B:
	
	OS: Raspbian
	Kernel: linux 4.14.30+

#### The STV25DV is aviable on two address:
#### At address 0x57:

The system configuration is the area to configure the ST25DV for NFC and I2C access.

	The system configuration is available at /sys/bus/i2c/devices/X-0057/st25dv_sys

Aviable in write only, this file descriptor is to present the password of the security session

	The file descriptor is available at /sys/bus/i2c/devices/X-0057/st25dv_present_pwd

Aviable in write only, this file descriptor is to change the password of the security session

	The file descriptor is available at /sys/bus/i2c/devices/X-0057/st25dv_write_pwd

#### At address 0x53:

The user area is the area to store user data like NDEF file.

	The user area is available at /sys/bus/i2c/devices/X-0053/st25dv_user

The dynamics registers give information about the curent state of the chip(ex: if the chip is in the NFC Field).

	Dynamics registers are available at /sys/bus/i2c/devices/X-0053/st25dv_dyn_reg

### Setup on RaspberryPi:

To compile a module, linux headers are requested:

	sudo apt install raspberrypi-kernel-headers

Be sure to have I2C interface enable:

	sudo raspi-config

### Install:

Compile the module:

	make

Insert the module into the kernel:

	sudo insmod st25dv.ko

Tell the presence of the ST25DV to the kernel:

for 4Kb memory size

	echo st25dv04k 0x53 > /sys/bus/i2c/devices/i2c-X/new_device

for 16Kb memory size

    	echo st25dv16k 0x53 > /sys/bus/i2c/devices/i2c-X/new_device

for 64Kb memory size

    	echo st25dv64k 0x53 > /sys/bus/i2c/devices/i2c-X/new_device

#### Test the driver:

Read test of the system configuration area:

	xxd /sys/bus/i2c/devices/X-0057/st25dv_sys
	00000000: 8803 0100 000f 000f 000f 0000 0001 0700  ................
	00000010: 0000 0000 7f00 0324 c662 0703 0024 02e0  .......$.b...$..
	00000020: 12ff ffff                                ....

Read test of the user area:

	xxd /sys/bus/i2c/devices/X-0053/st25dv_user
	00000000: 0000 0000 0000 0000 0000 0000 0000 0000  ................
	00000010: 0000 0000 0000 0000 0000 0000 0000 0000  ................
	00000020: 0000 0000 0000 0000 0000 0000 0000 0000  ................
	00000030: 0000 0000 0000 0000 0000 0000 0000 0000  ................
	00000040: 0000 0000 0000 0000 0000 0000 0000 0000  ................
	00000050: 0000 0000 0000 0000 0000 0000 0000 0000  ................
	00000060: 0000 0000 0000 0000 0000 0000 0000 0000  ................
	00000070: 0000 0000 0000 0000 0000 0000 0000 0000  ................
	00000080: 0000 0000 0000 0000 0000 0000 0000 0000  ................
	00000090: 0000 0000 0000 0000 0000 0000 0000 0000  ................
	....

Write test of the user area:

	uname -a > /sys/bus/i2c/devices/X-0053/st25dv_user	
	xxd /sys/bus/i2c/devices/X-0053/st25dv_user
	00000000: 4c69 6e75 7820 7261 7370 6265 7272 7970  Linux raspberryp
	00000010: 6920 342e 3134 2e33 302b 2023 3131 3032  i 4.14.30+ #1102
	00000020: 204d 6f6e 204d 6172 2032 3620 3136 3a32   Mon Mar 26 16:2
	00000030: 303a 3035 2042 5354 2032 3031 3820 6172  0:05 BST 2018 ar
	00000040: 6d76 366c 2047 4e55 2f4c 696e 7578 0a00  mv6l GNU/Linux..
	00000050: 0000 0000 0000 0000 0000 0000 0000 0000  ................
	00000060: 0000 0000 0000 0000 0000 0000 0000 0000  ................
	00000070: 0000 0000 0000 0000 0000 0000 0000 0000  ................
	00000080: 0000 0000 0000 0000 0000 0000 0000 0000  ................
	00000090: 0000 0000 0000 0000 0000 0000 0000 0000  ................
	....

Enable security session status by present password:

	sudo dd if=/dev/zero of=/sys/bus/i2c/devices/X-0057/st25dv_present_pwd bs=8c count=1

Check security session status:

	xxd -s4 -l1 -p /sys/bus/i2c/devices/X-0053/st25dv_dyn_reg
	01

Disable security session status by present bad password:

	sudo dd if=/dev/urandom of=/sys/bus/i2c/devices/X-0057/st25dv_present_pwd bs=8c count=1

Change password for security session (need security session status = 0x01):

	dd if=/dev/urandom of=pwd_test.bin bs=8c count=1
	sudo dd if=pwd_test.bin of=/sys/bus/i2c/devices/X-0057/st25dv_write_pwd

#### read data write by i2c with android smartphone using NFC:

Those screenshots have been done with STMicroelectronics ST25 Android app:
[STMicroelectronics ST25 APP](https://play.google.com/store/apps/details?id=com.st.st25nfc)

![](https://github.com/2pecshy/eeprom-ST25DV-linux-driver/raw/master/res/android1.png) ![](https://github.com/2pecshy/eeprom-ST25DV-linux-driver/raw/master/res/android%203.png)
