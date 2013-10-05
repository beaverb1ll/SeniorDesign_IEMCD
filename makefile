EXECUTABLE=iemcd

all: iemcd libs

libs: libhidapi-hidraw.so

CC       ?= gcc
CFLAGS   ?= -Wall -lmysqlclient -g -fpic

CXX      ?= g++
CXXFLAGS ?= -Wall -lmysqlclient -g -fpic

LDFLAGS  ?= -Wall -lmysqlclient -g


COBJS     = hid.o
CPPOBJS   = iemcd.o
OBJS      = $(COBJS) $(CPPOBJS)
LIBS_UDEV = `pkg-config libudev --libs` -lrt
LIBS      = $(LIBS_UDEV)
INCLUDES ?= -I../hidapi `pkg-config libusb-1.0 --cflags`


# Console Test Program
iemcd: $(COBJS) $(CPPOBJS)
	$(CXX) $(LDFLAGS) $^ $(LIBS_UDEV) -o $@

# Shared Libs
libhidapi-hidraw.so: $(COBJS)
	$(CC) $(LDFLAGS) $(LIBS_UDEV) -shared -fpic -Wl,-soname,$@.0 $^ -o $@

# Objects
$(COBJS): %.o: %.c
	$(CC) $(CFLAGS) -c $(INCLUDES) $< -o $@

$(CPPOBJS): %.o: %.cpp
	$(CXX) $(CXXFLAGS) -c $(INCLUDES) $< -o $@

install:
	install $(EXECUTABLE) /usr/bin/$(EXECUTABLE)
	install $(EXECUTABLE).service /usr/lib/systemd/system/$(EXECUTABLE).service
	install $(EXECUTABLE).conf /etc/$(EXECUTABLE).conf
	systemctl daemon-reload

clean:
	rm -f $(OBJS) hidtest-hidraw libhidapi-hidraw.so iemcd.o

.PHONY: clean libs

