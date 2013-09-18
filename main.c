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

int openSerial(const char *ttyName, int speed, int parity, int blockingAmnt);
int set_interface_attribs (int fd, int speed, int parity);
void set_blocking (int fd, int block_numChars, int block_timeout);
MYSQL* openSQL(const char *db_username, const char *db_passwd, const char *db_name);
int readBarcodes(int commandsFD, int barcodeFD, MYSQL *con);
int dispenseDrink(int cb_fd, int *ingredArray);
int sendCommand_getAck(int fd, const char *command);
int* getIngredFromSQL(MYSQL *sql_con, const char *query);
int parseArgs(int argc, char const *argv[]);


int main(int argc, char const *argv[])
{
	int fd_CB, fd_barcode;
	MYSQL *con_SQL;


	parseArgs(argc, argv);

	fd_CB = openSerial("/dev/ttyS0", B38400, 0, 1);

	fd_barcode = openSerial("/dev/ttyS1", B38400, 0, BARCODE_LENGTH);

	con_SQL = openSQL("root", "password", "DBName");

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
        syslog(LOG_INFO, "error %d opening %s: %s", errno, ttyName, strerror (errno));
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
  
	if (con == NULL) {
    	syslog(LOG_INFO, "%s  Exiting...\n", mysql_error(con));
     	exit(1);
	}  

  	if (mysql_real_connect(con, "127.0.0.1", db_username, db_passwd, db_name, 0, NULL, 0) == NULL) {
    	syslog(LOG_INFO, "%s  Exiting...\n", mysql_error(con));
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

		// query sql for barcode
		ingredients = getIngredFromSQL(con, queryString);
		if (ingredients == NULL) 
		{
			// start over if invalid
			continue;
		}


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

		if (mysql_query(con, queryString)) {      
    		sylsog(LOG_INFO, "Unable to update SQL with string: %s", queryString);
    	}

    	// do another barcode
	}
	return 0;
}

int parseArgs(int argc, char const *argv[])
{
	return 0;
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
	char *command[50];


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
		itoa(ingredArray[i], command, 10);
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
	write (fd, command, sizeof(command));
	// wait for response
	numRead = read (fd, buffer, sizeof(buffer));
	buffer[numRead] = '\0';
		
	if(numRead < 1)
	{
		syslog(LOG_INFO, "Error reading ack from fd");
		return 1;
	}

	switch (buffer[1]) 
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
	int num_rows;
	MYSQL_ROW row;
	MYSQL_RES *result;
	int *ingred;
	
	if (mysql_query(sql_con, query)) {      
    	sylsog(LOG_INFO, "Unable to query SQL with string: %s", query);
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
  		return NULL:	
  	}

  	row = mysql_fetch_row(result);
  	mysql_free_result(result);

	ingred = (int*)malloc(sizeof(int) * NUM_INGREDIENTS);
  	
  	for (int i = 0; i < NUM_INGREDIENTS; i++)
  	{
  		ingred[i] = row[i];
  	}
  	

  	return ingred;
}