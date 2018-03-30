# eeprom-ST25DV-linux-driver

ST25DV I2C/NFC EEPROM driver

This module is a simple driver to drive the ST25DV eeprom. The ST25DV is a EEPROM
readable and writeable by i2c and also by wireless Near Field Communication (NFC). 
Because the EEPROM can also be write by NFC, no cache of the eeprom is done, to get 
the right data even if a write has been done by NFC.

This module has been tested on RaspberryPi Model B:
	
	OS: Raspbian
	Kernel: linux 4.14.30+

The STV25DV have two areas:

The system configuration at address 0x57:

	The system configuration is the area to configure the ST25DV for NFC and I2C access.
	The system configuration is available at /sys/bus/i2c/devices/X-0057/st25dv_sys

The user area at address 0x53:

	The user area is the area to store user data like NDEF file.
	The user area is available at /sys/bus/i2c/devices/X-0053/st25dv_user

Setup on RaspberryPi:

To compile a module, linux headers are requested:

	sudo apt install raspberrypi-kernel-headers

Be sure to have I2C interface enable:

	sudo raspi-config

Install:

Compile the module:

	make

Insert the module into the kernel:

	sudo insmod st25dv.ko

Tell the presence of the ST25DV to the kernel:

	echo st25dv 0x53 > /sys/bus/i2c/devices/i2c-X/new_device

Test the driver:

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

write test of the user area:

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

read data write by i2c with android smartphone using NFC:

	Those screenshots have been done with STMicroelectronics ST25 Android app:
![alt text](https://raw.githubusercontent.com/2pecshy/eeprom-ST25DV-linux-driver/blob/master/res/android1.png) ![alt text](https://raw.githubusercontent.com/2pecshy/eeprom-ST25DV-linux-driver/blob/master/res/android2.png) ![alt text](https://raw.githubusercontent.com/2pecshy/eeprom-ST25DV-linux-driver/blob/master/res/android3.png)
