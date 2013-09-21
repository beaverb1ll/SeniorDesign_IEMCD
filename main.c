// how this will work:

	// parse arguments
	// open serial port
	// open barcode (serial or usb?) port
	// open sql database
	
	// loop
		// wait for barcode
		// query sql
		// 	|   |
		//  |   |
		//  |    -> If found, send commands to serial 
		//   ->  Else continue.



/*CREATE  TABLE `new_schema`.`new_table` (
  `id` INT NOT NULL ,
  `orderID` VARCHAR(45) NULL ,
  `orderTime` DATETIME NULL ,
  `pickupTime` DATETIME NULL ,
  `pickedUp` VARCHAR(45) NULL ,
  `Ing0` INT NULL ,
  `Ing1` INT NULL ,
  `Ing2` INT NULL ,
  `Ing3` INT NULL ,
  `Ing4` INT NULL ,
  `Ing5` INT NULL ,
  PRIMARY KEY (`id`) );
*/

/*

	SQL Schema Used:
	orderTable ->
		Integer id,
		varchar orderID, 
		DateTime orderTime,
		DateTime pickupTime,
		Bool pickedup,
		Integer Ing0,
		Integer Ing1,
		Integer Ing2,
		Integer Ing3,
		Integer Ing4,
		Integer Ing5
*/

/*
	-u <username> -p <password> -d <databaseName> -c <control board tty device name> -b <barcode tty device name> -rc <baudRate> -rb <baudRate>




*/

#include <termios.h>
#include <unistd.h>
#include <string.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <errno.h>
#include <unistd.h>
#include <syslog.h>
#include <signal.h>
#include <mysql/mysql.h>


#define TRUE 1
#define FALSE 0

#define BARCODE_LENGTH 50
#define NUM_INGREDIENTS 6

struct settings {
	char dbName[100];
	char dbUsername[100];
	char dbPasswd[100];
	char cbDevice[100];
	char barcodeDevice[100];
	int cbBaud;
	int barcodeBaud;
	int barcodeLength;
};

int openSerial(const char *ttyName, int speed, int parity, int blockingAmnt);
int set_interface_attribs (int fd, int speed, int parity);
void set_blocking (int fd, int block_numChars, int block_timeout);
MYSQL* openSQL(const char *db_username, const char *db_passwd, const char *db_name);
int readBarcodes(int commandsFD, int barcodeFD, MYSQL *con);
int dispenseDrink(int cb_fd, int *ingredArray);
int sendCommand_getAck(int fd, const char *command);
int* getIngredFromSQL(MYSQL *sql_con, const char *query);
struct settings* parseArgs(int argc, char const *argv[]);
int baudToInt(const char *bRate);

int main(int argc, char const *argv[])
{
	int fd_CB, fd_barcode;
	MYSQL *con_SQL;
	struct settings *currentSettings;

	openlog("IEMCD", LOG_PID|LOG_CONS, LOG_USER);
	syslog(LOG_INFO, "Daemon Started.\n");

	currentSettings = parseArgs(argc, argv);
	syslog(LOG_INFO, "Finished parsing input arguments.");
	fd_CB = openSerial(currentSettings->cbDevice, currentSettings->cbBaud, 0, 1);
	syslog(LOG_INFO, "Opened control board serial device %s", currentSettings->cbDevice);
	fd_barcode = openSerial(currentSettings->barcodeDevice, currentSettings->barcodeBaud, 0, BARCODE_LENGTH);
	syslog(LOG_INFO, "Opened barcode serial device %s", currentSettings->barcodeDevice);
	con_SQL = openSQL(currentSettings->dbUsername, currentSettings->dbPasswd, currentSettings->dbName);
	syslog(LOG_INFO, "Opened SQL database %s", currentSettings->dbName);
	readBarcodes(fd_CB, fd_barcode, con_SQL);

	close(fd_CB);
	close(fd_barcode);
	mysql_close(con_SQL);
	return 0;
}

int openSerial(const char *ttyName, int speed, int parity, int blockingAmnt) 
{
    int fd; 

    fd = open (ttyName, O_RDWR | O_NOCTTY | O_SYNC);
    if (fd < 0)
    {
        syslog(LOG_INFO, "error %d returned while opening serial device %s: %s", errno, ttyName, strerror (errno));
        exit(1);
    }
    set_interface_attribs (fd, speed, parity);
    set_blocking (fd, blockingAmnt, 1); 	// block for BARCODE_LENGTH chars or .1 sec
    return fd;
}

int set_interface_attribs (int fd, int speed, int parity)
{
        struct termios tty;
        memset (&tty, 0, sizeof tty);
        if (tcgetattr (fd, &tty) != 0)
        {
            syslog(LOG_INFO, "error %d from tcsetattr. Unable to set tty attributes. Exiting...\n", errno);
            exit(1);
        }

        cfsetospeed (&tty, speed);
        cfsetispeed (&tty, speed);

        tty.c_cflag = (tty.c_cflag & ~CSIZE) | CS8;     // 8-bit chars
        // disable IGNBRK for mismatched speed tests; otherwise receive break
        // as \000 chars
        tty.c_iflag &= ~IGNBRK;         // ignore break signal
        tty.c_lflag = 0;                // no signaling chars, no echo,
                                        // no canonical processing
        tty.c_oflag = 0;                // no remapping, no delays
        tty.c_cc[VMIN]  = 0;            // read doesn't block
        tty.c_cc[VTIME] = 5;            // 0.5 seconds read timeout

        tty.c_iflag &= ~(IXON | IXOFF | IXANY); // shut off xon/xoff ctrl

        tty.c_cflag |= (CLOCAL | CREAD);// ignore modem controls,
                                        // enable reading
        tty.c_cflag &= ~(PARENB | PARODD);      // shut off parity
        tty.c_cflag |= parity;
        tty.c_cflag &= ~CSTOPB;
        tty.c_cflag &= ~CRTSCTS;

        if (tcsetattr (fd, TCSANOW, &tty) != 0)
        {
            syslog(LOG_INFO, "error %d from tcsetattr. Unable to set tty attributes. Exiting...\n", errno);
            exit(1);
        }
        return 0;
}

void set_blocking (int fd, int block_numChars, int block_timeout)
{
        struct termios tty;
        memset (&tty, 0, sizeof tty);
        if (tcgetattr (fd, &tty) != 0)
        {
            syslog(LOG_INFO, "error %d from tggetattr. Unable to set tty attributes. Exiting...\n", errno);
            exit(1);
        }

        tty.c_cc[VMIN]  = block_numChars;
        tty.c_cc[VTIME] = block_timeout;

        if (tcsetattr (fd, TCSANOW, &tty) != 0) {
        	syslog(LOG_INFO, "error %d from tggetattr. Unable to set tty attributes. Exiting...\n", errno);
            exit(1);
        }
}

MYSQL* openSQL(const char *db_username, const char *db_passwd, const char *db_name) 
{

	MYSQL *con = mysql_init(NULL);
  
	if (con == NULL)
	{
    		syslog(LOG_INFO, "Error in openSQL: %s  Exiting...\n", mysql_error(con));
     		exit(1);
	}  

  	if (mysql_real_connect(con, "127.0.0.1", db_username, db_passwd, db_name, 0, NULL, 0) == NULL) 
	{
    		syslog(LOG_INFO, "Error in openSQL: %s  Exiting...\n", mysql_error(con));
    		mysql_close(con);
  		exit(1); 
  	}
  	return  con;
}


int readBarcodes(int commandsFD, int barcodeFD, MYSQL *con)
{
	char barcode[BARCODE_LENGTH + 1];
	char *baseSelect = "SELECT Ing0, Ing1, Ing2, Ing3, Ing4, Ing5 FROM orderTable WHERE orderID=";
	char *baseUpdate = "UPDATE orderTable SET pickedup='true' WHERE orderID=";
	char queryString[200];
	int *ingredients, numRead;

	while (TRUE) {
		// wait for barcode.
		numRead = read (barcodeFD, barcode, sizeof(barcode));
		barcode[numRead] = '\0';
		
		if(numRead < BARCODE_LENGTH)
		{
			syslog(LOG_INFO, "Only read %d chars from barcode. Ignoring input", numRead);
			continue;
		}

		// construct query string
		strcpy(queryString, baseSelect);
		strcat(queryString, barcode);
		syslog(LOG_INFO, "Barcode:%s  Query string: %s", barcode,  queryString);
		// query sql for barcode
		ingredients = getIngredFromSQL(con, queryString);
		if (ingredients == NULL) 
		{
			syslog(LOG_INFO, "No ingreds found. Continuing...");
			// start over if invalid
			continue;
		}

		syslog(LOG_INFO, "Ingredients found. Dispensing...");

		// send commands to CB Board
		if (dispenseDrink(commandsFD, ingredients) != 0)
		{
			// something went wrong. don't touch sql.
			free(ingredients);
			syslog(LOG_INFO, "Error from dispenseDrink, leaving SQL row intact");
			continue;
		}

		// free ingredients, they are no longer needed.
		free(ingredients);

		// something must have gone ok. manipulate sql.
		// create query string
		strcpy(queryString, baseUpdate);
		strcat(queryString, barcode);
		// update sql

		if (mysql_query(con, queryString))
		{      
    			syslog(LOG_INFO, "Unable to update SQL with string: %s", queryString);
    		} else
		{
			syslog(LOG_INFO, "Updated SQL.");
		}

    	// do another barcode
	}
	return 0;
}

struct settings* parseArgs(int argc, char const *argv[])
{
	struct settings* allSettings;
	int temp, opt;

	allSettings = malloc(sizeof(struct settings));
	if (allSettings == NULL)
	{
		syslog(LOG_INFO, "Unable to create settings struct. Exiting...");
		exit(1);
	}

	// strcpy(allSettings->dbName, "orderTable");
	// strcpy(allSettings->dbUsername, "root");
	// strcpy(allSettings->dbPasswd, "password");
	// strcpy(allSettings->cbDevice, "/dev/ttyS0");
	// strcpy(allSettings->barcodeDevice, "/dev/ttyS1");
	// allSettings->cbBaud = 17;
	// allSettings->barcodeBaud = 17;
//	-u <username> -p <password> -d <databaseName> -c <control board tty device name> -b <barcode tty device name> -r <baudRate> -s <baudRate>

	while ((opt = getopt(argc, argv, "u:p:d:c:b:r:s:")) != -1)
	{
	    switch(opt)
	    {
	    	case 'u': // username
    			strcpy(allSettings->dbUsername, optarg);
    			break;

    		case 'p': // password
    			strcpy(allSettings->dbPasswd, optarg);
    			break;

    		case 'd': // dbName
			strcpy(allSettings->dbName, optarg);
    			break;

    		case 'c': // CB tty device
    			strcpy(allSettings->cbDevice, optarg);
    			break;

	   		case 'b': // Barcode tty device
	   		strcpy(allSettings->barcodeDevice, optarg);
    			break;

    		case 'r': // CB Baud
    			allSettings->cbBaud = baudToInt(optarg);
    			break;

    		case 's': // Barcode Baud
    			allSettings->barcodeBaud = baudToInt(optarg);
    			break;
   		case '?':
   			syslog(LOG_INFO, "Invalid startup argument: %c. Exiting...", optopt);
   			exit(1);
 			break;
	    }
	}




	return allSettings;
}

int baudToInt(const char *bRate)
{
	if(!strcmp(bRate, "B38400"))
		return 17;
	else if (!strcmp(bRate, "B19200"))
		return 16;
	else if (!strcmp(bRate, "B9600"))
		return 15;
	else if (!strcmp(bRate, "B4800"))
		return 14;
	else if (!strcmp(bRate, "B2400"))
		return 13;
	else if (!strcmp(bRate, "B1800"))
		return 12;
	else if (!strcmp(bRate, "B1200"))
		return 11;
	else if (!strcmp(bRate, "B600"))
		return 10;
	else if (!strcmp(bRate, "B300"))
		return 7;
	else if (!strcmp(bRate, "B200"))
		return 6;
	else if (!strcmp(bRate, "B150"))
		return 5;
	else if (!strcmp(bRate, "B134"))
		return 4;
	else if (!strcmp(bRate, "B110"))
		return 3;
	else if (!strcmp(bRate, "B75"))
		return 2;
	else if (!strcmp(bRate, "B50"))
		return 1;

	return 0;
// B0	0000000		/* hang up */
// B50	0000001
// B75	0000002
// B110	0000003
// #define  B134	0000004
// #define  B150	0000005
// #define  B200	0000006
// #define  B300	0000007
// #define  B600	0000010
// #define  B1200	0000011
// #define  B1800	0000012
// #define  B2400	0000013
// #define  B4800	0000014
// #define  B9600	0000015
// #define  B19200	0000016
// #define  B38400	0000017
}

/*
 *
 * Input:
 *
 * Output:
 *	- Sucess: 0
 *	- Error: 1
 *
 */
int dispenseDrink(int cb_fd, int *ingredArray) 
{
	int i, response;
	const char *endString = "T";
	char command[50];


	// ask for clear to send
	if(sendCommand_getAck(cb_fd, "D"))
	{
		// uh oh, something went wrong.
		syslog(LOG_INFO, "Comm error with dispenseDrink");
		return 1;
	}

	// loop through and send each ingred, waiting for repsonse between each command
	for (i = 0; i < NUM_INGREDIENTS; i++)
	{
		// convert ingredient to string and store into command
		sprintf(command, "%d", ingredArray[i]);
		// itoa(ingredArray[i], command, 10);
		// append end string
		strcat(command, endString);

		
		// send commmand, wait for response
		if(sendCommand_getAck(cb_fd, command))
		{
			// uh oh, something went wrong.
			syslog(LOG_INFO, "Comm error with dispenseDrink");
			return 1;
		}
	}

	if(sendCommand_getAck(cb_fd, "F"))
	{
		// uh oh, something went wrong.
		syslog(LOG_INFO, "Comm error with dispenseDrink");
		return 1;
	}

	return 0;
}


/*
 *
 * Output: 
 *	- On success: 0
 *	- On failure: 1
 */
int sendCommand_getAck(int fd, const char *command)
{
	char buffer[5];
	int numRead;

	write (fd, command, sizeof(command));
	syslog(LOG_INFO, "sending command to CB: %s", command);
	// wait for response
	numRead = read (fd, buffer, sizeof(buffer));
	buffer[numRead] = '\0';
		
	if(numRead < 1)
	{
		syslog(LOG_INFO, "Error reading ack from fd");
		return 1;
	}

	switch (buffer[0]) 
	{
		case 'f':
			// fall through
		case 'F':
			// fall through
		case 'y':
			// fall through
		case 'Y':
			return 0;
			break;

		case 'n':
			// fall through
		case 'N':
			// fall through
		default:
			return 1;
	}
}

/*
 * Input: 
 *	- a SQL connection
 *  - a query string
 *
 * Output: 
 *  - If one row found, pointer to object from query
 *	  Else: NULL
 *
 *
 *
 * This will query the sql database for a anything 
 *	and will return a pointer to the row if only one entry was 
 *	found and will return NULL otherwise. This function 
 *	will log with syslog if an error is encountered.
 *
 */
int* getIngredFromSQL(MYSQL *sql_con, const char *query)
{
	int num_rows, i;
	MYSQL_ROW row;
	MYSQL_RES *result;
	int *ingred;
	
	if (mysql_query(sql_con, query)) {      
    	syslog(LOG_INFO, "Unable to query SQL with string: %s", query);
    	return NULL;
	}

	result = mysql_store_result(sql_con);
  
  	if (result == NULL) {
  		mysql_free_result(result);
    	syslog(LOG_INFO, "Unable to get result from SQL query: %s", query);
    	return NULL;
 	}

  	num_rows = mysql_num_rows(result);


  	if (num_rows != 1)
  	{
  		syslog(LOG_INFO, "Invalid number of rows returned for query: %s", query);
  		return NULL;
  	}

  	row = mysql_fetch_row(result);
  	mysql_free_result(result);

	ingred = (int*)malloc(sizeof(int) * NUM_INGREDIENTS);
  	int temp;
  	for (i = 0; i < NUM_INGREDIENTS; i++)
  	{
  		temp = atoi(row[i]);
  		ingred[i] = temp;
  	}
  	

  	return ingred;
}