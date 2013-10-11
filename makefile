EXECUTABLE = iemcd

CC      = gcc
CFLAGS  = -Wall `mysql_config --cflags` -l'hidapi-hidraw' -g -fpic
C_SRCS	= $(EXECUTABLE).c

all: $(EXECUTABLE)


$(EXECUTABLE):
	$(CC) $(CFLAGS) $(C_SRCS) -o $(EXECUTABLE)

install:
	install $(EXECUTABLE) /usr/bin/$(EXECUTABLE)
#	install ./systemd/$(EXECUTABLE).service /usr/lib/systemd/system/$(EXECUTABLE).service
	install ./systemd/$(EXECUTABLE).conf /etc/$(EXECUTABLE).conf
#	systemctl daemon-reload
#	systemctl restart $(EXECUTABLE).service

clean:
	rm -f $(EXECUTABLE)

.PHONY: clean libs
