# What is this?
This application listens on the specified serial tty device for a barcode to
come in. When a barcode is accepted, the SQL database will be queried and the 
drink ingredients are sent to the Control Board through the CB tty device.
Upon successful dispensing, the SQL database will be updated. Program logging 
is sent to /var/log/messages on RHEL systems and is marked by iemcd.

### Packages
#### Ubuntu
```bash
    apt-get install make gcc g++ libmysqlclient-dev mysql-server apache2 libapache2-mod-php5 libapache2-mod-auth-mysql php5-mysql vim git automake autoconf libtool libudev-dev pkg-config libusb-dev
```

### Compile Instructions
```bash
    make
    make install
```

### Run Instructions
```bash
    ./iemcd -u root -p password -d SD -c /dev/ttyUSB0 -b B38400 -s 4608 -v 1504 -t 500
```
### Command Arguments
-u :: database username
-p :: database user's password
-d :: database name
-c :: Control Board tty device
-b :: Control Board tty device baud rate
-v :: USB Vendor ID
-s :: USB Product ID
-t :: USB Read Timeout (ms)

### Sample Barcode
The required length of a barcode is currently set to 40 characters long. Valid characters are 0-9 inclusively.

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
false,
false,
50,
50,
50,
50,
50,
50
);
```

### SQL CREATE
```bash
CREATE TABLE `orderTable` (
  `id` int(11) NOT NULL AUTO,
  `orderID` varchar(50) DEFAULT NULL,
  `orderTime` int(11) DEFAULT '0',
  `pickupTime` int(11) DEFAULT '0',
  `pickedUp` varchar(10) DEFAULT 'false',
  `Ing0` int(11) DEFAULT NULL,
  `Ing1` int(11) DEFAULT NULL,
  `Ing2` int(11) DEFAULT NULL,
  `Ing3` int(11) DEFAULT NULL,
  `Ing4` int(11) DEFAULT NULL,
  `Ing5` int(11) DEFAULT NULL,
  `expired` varchar(10) DEFAULT 'false',
  PRIMARY KEY (`id`)
) ENGINE=InnoDB DEFAULT;
```