#include <time.h>
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

#include "hidapi.h"


#define TRUE 1
#define FALSE 0

#define BARCODE_LENGTH 40
#define NUM_INGREDIENTS 6
#define MAX_SECONDS_RESERVED 600 // 10 minutes

#define VENDOR_ID 0x05e0  
#define PRODUCT_ID 0x1200 

struct settings {
        char dbName[100];
        char dbUsername[100];
        char dbPasswd[100];
        char cbDevice[100];
        int barcode_PID;
        int barcode_VID;
        int cbBaud;
        int barcodeLength;
};

int openSerial(const char *ttyName, int speed, int parity, int blockingAmnt);
int set_interface_attribs (int fd, int speed, int parity);
void set_blocking (int fd, int block_numChars, int block_timeout);
MYSQL* openSQL(const char *db_username, const char *db_passwd, const char *db_name);
int getBarcodeUSB(hid_device* handle, char *barcode);
hid_device* openUSB(int vID, int pID);
char convertUSB(unsigned char inputChar);
int doWork(int commandsFD, hid_device *barcodeHandle, MYSQL *con);
int dispenseDrink(int cb_fd, int *ingredArray);
int sendCommand_getAck(int fd, const char *command);
int* getIngredFromSQL(MYSQL *sql_con, const char *query);
struct settings* parseArgs(int argc, char* const* argv);
int baudToInt(const char *bRate);
int daemonize(void);
void sigINT_handler(int signum);
void sigTERM_handler(int signum);
void logInputArgs(struct settings *settings);

int main(int argc, char const *argv[])
{
        int fd_CB;
        hid_device* barcodeUSBDevice;
        MYSQL *con_SQL;
        struct settings *currentSettings;

        daemonize();

        currentSettings = parseArgs(argc, (char* const*)argv);
        syslog(LOG_INFO, "Finished parsing input arguments: ");

        logInputArgs(currentSettings);

        fd_CB = openSerial(currentSettings->cbDevice, currentSettings->cbBaud, 0, 0);    //if no response is recieved within length determined in openSerial(), stop blocking and return.
        syslog(LOG_INFO, "Opened control board serial device %s", currentSettings->cbDevice);

        barcodeUSBDevice = openUSB(currentSettings->barcode_PID, currentSettings->barcode_VID);
        syslog(LOG_INFO, "Opened barcode USB device");

        con_SQL = openSQL(currentSettings->dbUsername, currentSettings->dbPasswd, currentSettings->dbName);
        syslog(LOG_INFO, "Opened SQL database %s", currentSettings->dbName);

        syslog(LOG_INFO, "Entering main loop");
        doWork(fd_CB, barcodeUSBDevice, con_SQL);

        close(fd_CB);
        mysql_close(con_SQL);
        return 0;
}

int openSerial(const char *ttyName, int speed, int parity, int blockingAmnt)
{
    int fd;

    fd = open (ttyName, O_RDWR | O_NOCTTY | O_SYNC);
    if (fd < 0)
    {
        syslog(LOG_INFO, "ERROR :: error %d returned while opening serial device %s: %s", errno, ttyName, strerror (errno));
        exit(1);
    }
    set_interface_attribs (fd, speed, parity);
    set_blocking (fd, blockingAmnt, 1);         // block for BARCODE_LENGTH chars or .1 sec
    return fd;
}

int set_interface_attribs (int fd, int speed, int parity)
{
        struct termios tty;
        memset (&tty, 0, sizeof tty);
        if (tcgetattr (fd, &tty) != 0)
        {
            syslog(LOG_INFO, "ERROR :: error %d from tcsetattr. Unable to set tty attributes. Exiting...\n", errno);
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
            syslog(LOG_INFO, "ERROR :: error %d from tcsetattr. Unable to set tty attributes. Exiting...\n", errno);
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
            syslog(LOG_INFO, "ERROR :: error %d from tggetattr. Unable to set tty attributes. Exiting...\n", errno);
            exit(1);
        }

        tty.c_cc[VMIN]  = block_numChars;
        tty.c_cc[VTIME] = block_timeout;

        if (tcsetattr (fd, TCSANOW, &tty) != 0) {
                syslog(LOG_INFO, "ERROR :: error %d from tggetattr. Unable to set tty attributes. Exiting...\n", errno);
            exit(1);
        }
}

MYSQL* openSQL(const char *db_username, const char *db_passwd, const char *db_name)
{

        MYSQL *con = mysql_init(NULL);

        if (con == NULL)
        {
                syslog(LOG_INFO, "ERROR :: Error in openSQL: %s  Exiting...\n", mysql_error(con));
                exit(1);
        }

        if (mysql_real_connect(con, "127.0.0.1", db_username, db_passwd, db_name, 0, NULL, 0) == NULL)
        {
                syslog(LOG_INFO, "ERROR :: Error in openSQL: %s  Exiting...\n", mysql_error(con));
                mysql_close(con);
                exit(1);
        }
        return  con;
}

int getBarcodeUSB(hid_device* handle, char *barcode) 
{

    // read from usb device with a blocking configuration untill the first is recievd. 
    // after first is recieved, either:
    //       1) continue reading untill BARCODE_LENGTH is read, then return the barcode to caller
    //   2) if timeout occurs, start over.


    int status, i = 0;
    char tempChar;
    unsigned char buf[9];

    // read without timeout
    status = hid_read(handle, buf, sizeof(buf));
    if(status < 1) {
        syslog(LOG_INFO, "ERROR :: Unable to read from USB in getBarcodeUSB. Exiting...");
        exit(1);
    }

    int ii;
    for (ii = 0; ii < status; ii++)
    {
        syslog(LOG_INFO, "DEBUG :: HID READ: %u ", buf[ii]);
    }

    tempChar = convertUSBInput(buf);
    if (tempChar == 0)
    {
        barcode[i] = '\0';
        /// uh oh, invalid char, close and try again.
        syslog(LOG_INFO, "DEBUG :: Bad character read, skipping barcode scan");
        syslog(LOG_INFO, "DEBUG :: Bad Char Read: %c", buf[2]);
        syslog(LOG_INFO, "DEBUG :: Skipped Barcode: %s", barcode);
        return 1;
    }

    // if its here, its valid so store the incoming character
    barcode[0] = tempChar;
    i = 1;

    // read the rest of the barcode
    while (i < BARCODE_LENGTH)
    {
        status = hid_read_timeout(handle, buf, sizeof(buf), 500);
        if (status < 1) // error happened when reading
        {
            barcode[i] = '\0';
            syslog(LOG_INFO, "DEBUG :: Timeout reached when reading barcode. Skipping...");
            syslog(LOG_INFO, "DEBUG :: Num chars read: %d", i);
            syslog(LOG_INFO, "DEBUG :: Barcode: %s", barcode);
            return 1;
        }

        for (ii = 0; ii < status; ii++)
        {
            syslog(LOG_INFO, "DEBUG :: HID READ: %u ", buf[ii]);
        }

        tempChar = convertUSBInput(buf);
        if (tempChar == 0)
        {
            /// uh oh, invalid char, close and try again.
            syslog(LOG_INFO, "DEBUG :: Reserved character read, skipping character");
            continue;
        } else if(tempChar == 1)
        {
        	syslog(LOG_INFO, "DEBUG :: Invalid character read, skipping barcode");
        	return 1;
        }

        // if its here, got a good character so store the incoming character
        barcode[i] = tempChar;
        i++;
    }
    barcode[i] = '\0';
    return 0;
}

char convertUSBInput(unsigned char* inputChar)
{
    unsigned int modifier = (unsigned int)inputChar[0];
	unsigned int input = (unsigned int)inputChar[2];
	
	syslog(LOG_INFO, "DEBUG :: Char value to convert: %d", input);
    syslog(LOG_INFO, "DEBUG :: Mod Keys to convert: %d", modifier);
	
    if (modifier != 0)
    {
        syslog(LOG_INFO, "DEBUG :: Mod key is pressed, skipping barcode.");
        return 1;
    }

	if (input == 0)
	{
			return 0;
	}
	if (input == 39)  // this is a zero from the barcode scanner
	{
		return '0';
	}

	if (input > 29 && input < 39) // this is from 1-9 from the barcode scanner
	{
		return input + 19;
	}
	if (input > 3 && input < 30) // this is from a-z 
	{
    	return input + 93;
	}

    // something's wrong if reached here, return invalid char and skip barcode
    syslog (LOG_INFO, "DEBUG :: No character match. skipping barcode.");
	return 1;
}

hid_device* openUSB(int vID, int pID)
{
        hid_device *handle;

        if(hid_init()) 
        {
                syslog(LOG_INFO, "ERROR :: Unable to initialize USB HID. Exiting...");
                exit(1);
        }

        handle = hid_open(0x05e0, 0x1200, NULL);
    if (!handle) {
        printf("ERROR :: Unable to open USB device %d:%d. Exiting...", vID, pID);
        exit(1);
    }

    // set as blocking
    hid_set_nonblocking(handle, 0);
    return handle;
}


int doWork(int commandsFD, hid_device *barcodeHandle, MYSQL *con)
{
        char barcode[BARCODE_LENGTH + 1];
        char baseSelect[] = "SELECT Ing0, Ing1, Ing2, Ing3, Ing4, Ing5, pickedUp, expired FROM orderTable WHERE orderID=";
        char baseUpdate[] = "UPDATE orderTable SET pickedup='true' WHERE orderID=";
        char queryString[200];
        int *ingredients, barcodeValid;

        while (TRUE) {
                // wait for barcode.

                // read from usbBarcodeScanner
                // numRead = read (barcodeFD, barcode, sizeof(barcode));
                barcodeValid = getBarcodeUSB(barcodeHandle, barcode);

                if(barcodeValid == 1)
                {
                        syslog(LOG_INFO, "DEBUG :: Valid barcode not received. Ignoring input");
                        continue;
                }

                // construct query string
                strcpy(queryString, baseSelect);
                strcat(queryString, barcode);
                syslog(LOG_INFO, "DEBUG :: Barcode:%s  Query string: %s", barcode,  queryString);

                // query sql for barcode
                ingredients = getIngredFromSQL(con, queryString);
                if (ingredients == NULL)
                {
                        syslog(LOG_INFO, "DEBUG :: No ingreds found. Continuing...");
                        continue;
                }

                syslog(LOG_INFO, "DEBUG :: Ingredients found. Dispensing...");

                // send commands to CB Board
                if (dispenseDrink(commandsFD, ingredients) != 0)
                {
                        // something went wrong. don't touch sql.
                        free(ingredients);
                        syslog(LOG_INFO, "DEBUG :: Error from dispenseDrink, leaving SQL row intact");
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
                        syslog(LOG_INFO, "DEBUG :: Unable to update SQL with string: %s", queryString);
				} else
                {
                        syslog(LOG_INFO, "DEBUG :: Updated SQL.");
                }
        // do another barcode
        }
        return 0;
}

struct settings* parseArgs(int argc, char* const* argv)
{
        struct settings* allSettings;
        int opt;

        allSettings = (struct settings*)calloc(1, sizeof(struct settings));
        if (allSettings == NULL)
        {
                syslog(LOG_INFO, "ERROR :: Unable to create settings struct. Exiting...");
                exit(1);
        }

        while ((opt = getopt(argc, argv, "u:p:d:c:b:v:s:")) != -1)
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

                case 'c': // CB tty device name
                        strcpy(allSettings->cbDevice, optarg);
                        break;

                        case 'b': // CB buadrate
                                allSettings->cbBaud = baudToInt(optarg);
                        break;

                case 'v': // USB Vendor ID
                        allSettings->barcode_VID = atoi(optarg);
                        break;

                case 's': // USB Product ID
                        allSettings->barcode_PID = atoi(optarg);
                        break;
    
                case '?':
                        syslog(LOG_INFO, "ERROR :: Invalid startup argument: %c :: Exiting...", optopt);
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
// B0   0000000         /* hang up */
// B50  0000001
// B75  0000002
// B110 0000003
// B134 0000004
// B150 0000005
// B200 0000006
// B300 0000007
// B600 0000010
// B1200        0000011
// B1800        0000012
// B2400        0000013
// B4800        0000014
// B9600        0000015
// B19200       0000016
// B38400       0000017
}

/*
 *
 * Input:
 *
 * Output:
 *      - Sucess: 0
 *      - Error: 1
 *
 */
int dispenseDrink(int cb_fd, int *ingredArray)
{
        int i;
        const char *endString = "T";
        char command[50];


        // ask for clear to send
        if(sendCommand_getAck(cb_fd, "D"))
        {
                // uh oh, something went wrong.
                syslog(LOG_INFO, "DEBUG :: Comm error with dispenseDrink");
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
                        syslog(LOG_INFO, "ERROR :: Comm error with dispenseDrink");
                        return 1;
                }
        }

        if(sendCommand_getAck(cb_fd, "F"))
        {
                // uh oh, something went wrong.
                syslog(LOG_INFO, "DEBUG :: Comm error with dispenseDrink");
                return 1;
        }

        return 0;
}


/*
 *
 * Output:
 *      - On success: 0
 *      - On failure: 1
 */
int sendCommand_getAck(int fd, const char *command)
{
        char buffer[5];
        int numRead;

        write (fd, command, sizeof(command));
        syslog(LOG_INFO, "DEBUG :: sending command to CB: %s", command);
        // clear receive buffer
        tcflush(fd, TCIFLUSH);

        // wait for response
        numRead = read (fd, buffer, sizeof(buffer));
        buffer[numRead] = '\0';

        if(numRead < 1)
        {
                syslog(LOG_INFO, "DEBUG :: Error reading ack from fd");
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
 *      - a SQL connection
 *  - a query string
 *
 * Output:
 *  - If one row found, pointer to object from query
 *        Else: NULL
 *
 *
 *
 * This will query the sql database for a anything
 *      and will return a pointer to the row if only one entry was
 *      found and will return NULL otherwise. This function
 *      will log with syslog if an error is encountered.
 *
 */
int* getIngredFromSQL(MYSQL *sql_con, const char *query)
{
        int num_rows, i;
        MYSQL_ROW row;
        MYSQL_RES *result;
        int *ingred;

        if (mysql_query(sql_con, query)) {
        syslog(LOG_INFO, "DEBUG :: Unable to query SQL with string: %s", query);
        return NULL;
        }

        result = mysql_store_result(sql_con);

        if (result == NULL)
        {
        syslog(LOG_INFO, "DEBUG :: Unable to get result from SQL query: %s", query);
        return NULL;
        }

        num_rows = mysql_num_rows(result);


        if (num_rows != 1)
        {
                syslog(LOG_INFO, "DEBUG :: Invalid number of rows returned for query: %s", query);
                return NULL;
        }

        row = mysql_fetch_row(result);


        // ========= DEBUG =====================
        int numFields = mysql_num_fields(result);
        int ii;
        for( ii=0; ii < numFields; ii++)
        {
        syslog(LOG_INFO, "DEBUG :: %d : %s",ii, row[ii] ? row[ii] : "NULL");  // Not NULL then print
    }
        // ======== END DEBUG ==================

        // verify time hasn't expired
        if (!strcmp("true", row[NUM_INGREDIENTS+1]))
        {
                syslog(LOG_INFO, "DEBUG :: Drink order expired. skipping...");
                return NULL;
        }
        syslog(LOG_INFO, "DEBUG :: Drink order not expired. Continuing... ");


        // verify drink has not been picked up yet.
        if(!strcmp("true", row[NUM_INGREDIENTS]))
        {
                syslog(LOG_INFO, "DEBUG :: Drink already picked up");
                return NULL;
        }
        syslog(LOG_INFO, "DEBUG :: Drink has not been picked up");

        ingred = (int*)malloc(sizeof(int) * NUM_INGREDIENTS);
        // store ingredients from row data
        for (i = 0; i < NUM_INGREDIENTS; i++)
        {
                ingred[i] = atoi(row[i]);;
        }

    mysql_free_result(result);
        return ingred;
}

int daemonize(void) {
        /* Our process ID and Session ID */
    pid_t pid, sid;
    
    /* Fork off the parent process */
    pid = fork();
    if (pid < 0) {
        exit(EXIT_FAILURE);
    }
    /* If we got a good PID, then
       we can exit the parent process. */
    if (pid > 0) {
        exit(EXIT_SUCCESS);
    }

    /* Change the file mode mask */
    umask(0);
            
    /* Open any logs here */ 
     openlog("iemcd", LOG_PID|LOG_CONS, LOG_USER);
     syslog(LOG_INFO, "Daemon Started.\n");
            
    /* Create a new SID for the child process */
    sid = setsid();
    if (sid < 0) {
        /* Log the failure */
        syslog(LOG_INFO, "ERROR :: Unable to create new SID. Exiting.");
        exit(EXIT_FAILURE);
    }
    syslog(LOG_INFO, "finished forking");
    
    /* Change the current working directory */
    if ((chdir("/")) < 0) {
        /* Log the failure */
        syslog(LOG_INFO, "ERROR :: Unable to change working directory. Exiting");
        exit(EXIT_FAILURE);
    }
    
    /* Close out the standard file descriptors */
    close(STDIN_FILENO);
    close(STDOUT_FILENO);
    close(STDERR_FILENO);
    
    /* Daemon-specific initialization goes here */
    
    /* Register Signal Handlers*/
    signal(SIGTERM, sigTERM_handler);
    signal(SIGINT, sigINT_handler);
        return 0;
 }
 
void sigINT_handler(int signum) 
{
    syslog (LOG_INFO, "Caught SIGINT. Exiting.");
    exit(0);

}

void sigTERM_handler(int signum) 
{
    syslog(LOG_INFO, "Caught SIGTERM. Exiting.");  
    exit(0);
}

void logInputArgs(struct settings *settings)
{
        syslog(LOG_INFO, "   DB Name    :: %s", settings->dbName);
        syslog(LOG_INFO, "   DB User    :: %s", settings->dbUsername);
        syslog(LOG_INFO, "   DB Passwod :: %s", settings->dbPasswd);
        syslog(LOG_INFO, "   CB Device  :: %s", settings->cbDevice);
        syslog(LOG_INFO, "   CB Baud    :: %d", settings->cbBaud);
        syslog(LOG_INFO, "   Bar VID    :: %d", settings->barcode_VID);
        syslog(LOG_INFO, "   Bar PID    :: %d", settings->barcode_PID);
        syslog(LOG_INFO, "   Bar Length :: %d", settings->barcodeLength);
}
