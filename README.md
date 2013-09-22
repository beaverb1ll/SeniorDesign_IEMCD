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
### Command Arguments
-u :: database username
-p :: database user's password
-d :: database name
-c :: Control Board tty device
-b :: barcode tty device
-r :: Control Board tty device baud rate
-s :: barcode tty device baud rate

### Sample Barcode
The required length of a barcode is currently set to 50 characters long. Valid characters are 0-9 inclusively.

```bash
12345678901234567890123456789012345678901234567890
```

### SQL Schema
orderTable ->
		Integer id,
		varchar(50) orderID,
		Integer orderTime,
		Integer pickupTime,
		Bool pickedup,
		Integer Ing0,
		Integer Ing1,
		Integer Ing2,
		Integer Ing3,
		Integer Ing4,
		Integer Ing5
