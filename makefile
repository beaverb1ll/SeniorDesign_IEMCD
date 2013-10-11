EXECUTABLE = iemcd

CC      = gcc
CFLAGS  = -Wall -L/usr/lib64/mysql -L/usr/lib -lmysqlclient -l'hidapi-hidraw'
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
