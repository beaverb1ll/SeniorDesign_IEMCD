# What is this?
This application listens on the specified serial tty device for a barcode to
come in. When a barcode is accepted, the SQL database will be queried and the 
drink ingredients are sent to the Control Board through the CB tty device.
Upon successful dispensing, the SQL database will be updated. Program logging 
is sent to /var/log/messages on RHEL systems and is marked by iemcd.

### Compile Instructions
```bash
    make
```

### Run Instructions
```bash
    ./iemcd -u root -p password -d new_schema -c /dev/ttyS0 -b /dev/ttyS1 -r B38400 -s B38400
```


-u :: database username
-p :: database user's password
-d :: database name
-c :: CB tty device
-b :: barcode tty device
-r :: CB tty device baud rate
-s :: barcode tty device baud rate

### SQL Schema
Currently the schema can be found at the top of the main.c file