TARGET = home_smart_camera
CFILES = main.c
CC = gcc
LIB_INCLUDES = -I/usr/include/gstreamer-1.0 -I/usr/include/glib-2.0 -I/usr/include/libxml2
PKG_CONF = `pkg-config --cflags --libs gstreamer-1.0 gobject-2.0 glib-2.0`
BLD_OBJS = $(CC) $(CFILES) -o $(TARGET) $(LIB_INCLUDES) $(PKG_CONF)

all: $(TARGET)
	$(info    Build complete.)

$(TARGET): clean
	$(BLD_OBJS)

clean:
	-rm -f /tmp/jpg/*.jpg
	-rm -f /home/pi/home_smart_device/$(TARGET)
