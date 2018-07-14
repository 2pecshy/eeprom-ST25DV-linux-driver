CC=dtc
CFLAGS=-@ -I dts -O dtb -o
SOURCE=i2c-st25dv-overlay.dts
TARGET=i2c-st25dv-overlay.dtb

all: $(TARGET)

i2c-st25dv-overlay.dtb: $(SOURCE)
	$(CC) $(CFLAGS) $(TARGET) $(SOURCE)

clean:
	rm -rf $(TARGET)

