# What is this?
This application listens on the specified serial tty device for a barcode to
come in. When a barcode is accepted, the SQL database will be queried and the 
drink ingredients are sent to the Control Board through the CB tty device.
Upon successful dispensing, the SQL database will be updated. Program logging 
is sent to /var/log/messages on RHEL systems and is marked by iemcd.

Packages:

### Compile Instructions
```bash
    make
```

### Run Instructions
```bash
    ./iemcd -u root -p password -d SD -c /dev/ttyUSB0 -b B38400 -s 4608 -v 1504
```
### Command Arguments
-u :: database username
-p :: database user's password
-d :: database name
-c :: Control Board tty device
-b :: Control Board tty device baud rate
-v :: USB Vendor ID
-s :: USB Product ID

### Sample Barcode
The required length of a barcode is currently set to 40 characters long. Valid characters are 0-9 and a-z.

```bash
1234567890123456789012345678901234567890
```

### SQL INSERT
```bash
INSERT INTO `SD`.`orderTable`
(`id`,
`orderID`,
`orderTime`,
`expired`,
`pickedUp`,
`Ing0`,
`Ing1`,
`Ing2`,
`Ing3`,
`Ing4`,
`Ing5` )
VALUES
(
0,
0,
0,
0,
false,
50,
50,
50,
50,
50,
50,
false
);
```

### SQL CREATE
```bash
CREATE TABLE `orderTable` (
  `id` int(11) NOT NULL AUTO_INCREMENT,
  `orderID` varchar(50) DEFAULT NULL,
  `orderTime` int(11) DEFAULT '0',
  `expired` varchar(10) DEFAULT 'false',
  `pickedUp` varchar(10) DEFAULT 'false',
  `Ing0` int(11) DEFAULT NULL,
  `Ing1` int(11) DEFAULT NULL,
  `Ing2` int(11) DEFAULT NULL,
  `Ing3` int(11) DEFAULT NULL,
  `Ing4` int(11) DEFAULT NULL,
  `Ing5` int(11) DEFAULT NULL,
  PRIMARY KEY (`id`)
);
```