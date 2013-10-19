EXECUTABLE = iemcd

CC      = gcc
CFLAGS  = -Wall -L/usr/lib64/mysql -L/usr/lib 
LFLAGS  =-lmysqlclient -l'hidapi-hidraw'
C_SRCS	= $(EXECUTABLE).c

# gcc iemcd.c -Wall -L/usr/lib64/mysql -L/usr/lib -o iemcd  -lmysqlclient -l'hidapi-hidraw'

all: $(EXECUTABLE)

$(EXECUTABLE):
	$(CC) $(C_SRCS) $(CFLAGS) -o $(EXECUTABLE) $(LFLAGS)

install:
	install $(EXECUTABLE) /usr/bin/$(EXECUTABLE)
	install ./systemd/$(EXECUTABLE).service /usr/lib/systemd/system/$(EXECUTABLE).service
	install ./systemd/$(EXECUTABLE).conf /etc/$(EXECUTABLE).conf
	install ./systemd/BBB-UART4.service /usr/lib/systemd/system/BBB-UART4.service
	install ./systemd/enable-BBB-UART4.sh /usr/bin/enable-BB-UART4.sh
	systemctl daemon-reload
	systemctl restart $(EXECUTABLE).service

clean:
	rm -f $(EXECUTABLE)

.PHONY: clean libs
