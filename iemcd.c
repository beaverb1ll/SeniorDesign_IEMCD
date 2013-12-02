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

#include "hidapi/hidapi.h"


#define TRUE 1
#define FALSE 0

#define BARCODE_LENGTH 34
#define NUM_INGREDIENTS 6
#define MAX_SECONDS_RESERVED 600 // 10 minutes
#define PURGE_BARCODE "95FD69C062F5A6F3501D92A0E946A56789"
#define FULL_VOLUME_LEVEL 68 // in oz
#define PURGE_VOLUME 1 // in oz
#define SEC_TO_OZ_RATIO 1625         // Num OZ/milliSec

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
        int usbTimeout;
        int ttyTimeout;

        // open devices
        hid_device* barcodeUSBDevice;
        MYSQL *con_SQL;
        int fd_CB;
};

struct settings *currentSettings;

int openSerial(const char *ttyName, int speed, int parity, int blockingAmnt);
int set_interface_attribs (int fd, int speed, int parity);
void set_blocking (int fd, int block_numChars, int block_timeout);
MYSQL* openSQL(const char *db_username, const char *db_passwd, const char *db_name);
int getBarcodeUSB(hid_device* handle, char *barcode);
hid_device* openUSB(int vID, int pID);
int convertUSBInput(unsigned char* inputChar);
int readLetterFromUSB(hid_device* handle, int nonblocking);
int doWork(int commandsFD, hid_device *barcodeHandle, MYSQL *con);
int dispenseDrink(int cb_fd, double *ingredArray);
int sendCommand_getAck(int fd, const char *command);
int getSerialAck(int fd);
int getIngredFromSQL(MYSQL *sql_con, const char *query, double *ingred);
struct settings* parseArgs(int argc, char* const* argv);
int baudToInt(const char *bRate);
int daemonize(void);
void sigINT_handler(int signum);
void sigTERM_handler(int signum);
void logInputArgs(struct settings *settings);
void closeConnections(void);
void consumeUSB(hid_device *handle);

int main(int argc, char const *argv[])
{
        daemonize();

        currentSettings = parseArgs(argc, (char* const*)argv);
        syslog(LOG_INFO, "Finished parsing input arguments: ");

        logInputArgs(currentSettings);

        currentSettings->fd_CB = openSerial(currentSettings->cbDevice, currentSettings->cbBaud, 0, 0);    //if no response is recieved within length determined in openSerial(), stop blocking and return.
        syslog(LOG_INFO, "Opened control board serial device %s", currentSettings->cbDevice);

        currentSettings->barcodeUSBDevice = openUSB(currentSettings->barcode_PID, currentSettings->barcode_VID);
        syslog(LOG_INFO, "Opened barcode USB device");

        currentSettings->con_SQL = openSQL(currentSettings->dbUsername, currentSettings->dbPasswd, currentSettings->dbName);
        syslog(LOG_INFO, "Opened SQL database %s", currentSettings->dbName);

        syslog(LOG_INFO, "Entering main loop");
        doWork(currentSettings->fd_CB, currentSettings->barcodeUSBDevice, currentSettings->con_SQL);

        closeConnections();
        return 0;
}

int openSerial(const char *ttyName, int speed, int parity, int blockingAmnt)
{
    int fd;

    fd = open (ttyName, O_RDWR | O_NOCTTY, O_SYNC );
    if (fd < 0)
    {
        syslog(LOG_INFO, "ERROR :: error %d returned while opening serial device %s: %s", errno, ttyName, strerror (errno));
        exit(1);
    }
    set_interface_attribs (fd, speed, parity);
    // set_blocking (fd, blockingAmnt, 600);         // http://linux.die.net/man/3/termios for info
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

    // tty.c_iflag &= ~(IGNBRK | BRKINT | ICRNL | INLCR | PARMRK | INPCK | ISTRIP | IXON);
    // // tty.c_oflag &= ~(OCRNL | ONLCR | ONLRET | ONOCR | ONOEOT| OFILL | OLCUC | OPOST);
    // tty.c_oflag = 0;
    // tty.c_lflag &= ~(ECHO | ECHONL | ICANON | IEXTEN | ISIG);
    // tty.c_cflag &= ~(CSIZE | PARENB);
    // tty.c_cflag |= CS8;

    // tty.c_cc[VMIN]  = 1;
    // tty.c_cc[VTIME] = 0;

    // if(cfsetispeed(&tty, B9600) < 0 || cfsetospeed(&tty, B9600) < 0) {
    //     syslog(LOG_INFO, "ERROR :: Unable to set baud rate. Exiting...\n", errno);
    //     exit(1);
    // }
    // if(tcsetattr(fd, TCSAFLUSH, &tty) < 0) {
    //     syslog(LOG_INFO, "ERROR :: error %d from tcsetattr. Unable to set tty attributes. Exiting...\n", errno);
    //     exit(1);
    // }


/* Set Baud Rate */
 cfsetospeed (&tty, (speed_t)B9600);
 cfsetispeed (&tty, (speed_t)B9600);

// /* Setting other Port Stuff */
 tty.c_lflag    &= ~(ICANON);
 tty.c_cflag     &=  ~PARENB;        // Make 8n1
 tty.c_cflag     &=  ~CSTOPB;
 tty.c_cflag     &=  ~CSIZE;
 tty.c_cflag     |=  CS8;

 tty.c_cflag     &=  ~CRTSCTS;       // no flow control
 tty.c_cc[VMIN]      =   0;                  
 tty.c_cc[VTIME]     =   600;                  // 60 seconds read timeout
 tty.c_cflag     |=  CREAD | CLOCAL;     // turn on READ & ignore ctrl lines

/* Make raw */
 cfmakeraw(&tty);

/* Flush Port, then applies attributes */
 tcflush( fd, TCIFLUSH );

 if ( tcsetattr ( fd, TCSANOW, &tty ) != 0)
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
    //   1) continue reading untill BARCODE_LENGTH is read, then return the barcode to caller
    //   2) if timeout occurs, start over.

    int returnedValue, i = 0;

    // read anything buffered
    consumeUSB(handle);

    // read the rest of the barcode
    while (i < BARCODE_LENGTH )
    {

        returnedValue = readLetterFromUSB(handle, i);

        if (returnedValue == 0)
        {
            // syslog(LOG_INFO, "DEBUG :: Reserved character read, skipping character");
            continue;
        } 
        else if(returnedValue < 0)
        {
            barcode[i] = '\0';
            syslog(LOG_INFO, "DEBUG :: Invalid character read or timeout, skipping barcode: %s read: %d", barcode, i);
            return -1;
        }
        // if its here, got a good character so store the incoming character
        barcode[i] = (char)returnedValue;
        i++;
    }
    barcode[i] = '\0';
    syslog(LOG_INFO, "DEBUG :: Received barcode: %s", barcode);
    consumeUSB(handle);
    return 0;
}

/*
 * Valid character: ascii value
 * Read Timout:     -1   
 * Read Failure:    program exits
 *
 *
 */
int readLetterFromUSB(hid_device* handle, int nonblocking)
{
    unsigned char buf[9];
    int status, returnedChar;

    if (nonblocking > 0)
    {
        status = hid_read_timeout(handle, buf, sizeof(buf), currentSettings->usbTimeout);
        if(status < 1) {
            syslog(LOG_INFO, "DEBUG :: ReadTimeout.");
            return -1;
        }
    } 
    else 
    {
        status = hid_read(handle, buf, sizeof(buf));
        if(status < 1) {
            syslog(LOG_INFO, "ERROR :: Unable to read from USB in getBarcodeUSB. Exiting...");
            exit(1);
        }  
    }
    
    
    // for (i = 0; i < status; i++)
    // {
    //     syslog(LOG_INFO, "DEBUG :: HID READ: %u ", buf[i]);
    // }

    returnedChar = convertUSBInput(buf);
    if (returnedChar < 0)
    {
        // discard all incoming chars for this scan
        // syslog(LOG_INFO, "DEBUG :: Invalid character read %d. consuming incoming chars.", returnedChar);
        consumeUSB(handle);
        return -1;
    }

    return (char)returnedChar;
}

void consumeUSB(hid_device *handle)
{
    int i = 0, status = 1;
    unsigned char buf[9];

    while(status > 0)
    {
        status = hid_read_timeout(handle, buf, sizeof(buf), currentSettings->usbTimeout);
        i++;
    }
    syslog(LOG_INFO, "DEBUG :: Consumed %d characters.", i);
}

/*
 *    Mod Key Pressed: -1
 *    No character   :  0
 *    Character      : ascii value
 */
int convertUSBInput(unsigned char* inputChar)
{
    int modifier = (int)inputChar[0];
    int input = (int)inputChar[2];
    
    // syslog(LOG_INFO, "DEBUG :: Char value to convert: %d", input);
    // syslog(LOG_INFO, "DEBUG :: Mod Keys to convert: %d", modifier);

    if (input > 3 && input < 30) // this is from a-z 
    {
        if (modifier == 0 || modifier == 2 || modifier == 16) // accept lowercase or uppercase by either Left or Right shift keys
        {
            return input + 61; // return only uppercase
        }
    }

    if (input == 0) // this is no character
    {
        return 0;
    }
    if (modifier != 0) // if any modifier is presed here, its not valid
    {
        // syslog(LOG_INFO, "DEBUG :: Mod key is pressed, skipping barcode.");
        return -1;
    }

    if (input == 39)  // this is a zero from the barcode scanner
    {
        return '0';
    }

    if (input > 29 && input < 39) // this is from 1-9 from the barcode scanner
    {
        return input + 19;
    }
    
    // something's wrong if reached here, return invalid char and skip barcode
    // syslog (LOG_INFO, "DEBUG :: No character match. skipping barcode.");
    return -1;
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
    char baseSelect[] = "SELECT Ing0, Ing1, Ing2, Ing3, Ing4, Ing5 FROM orderTable WHERE pickedup='false' AND expired='false' AND orderID=\"";
    char baseUpdate[] = "UPDATE orderTable SET pickedup='true' WHERE orderID=\"";
    char ingredAmountQuery[] = "SELECT SUM(Ing0), SUM(Ing1), SUM(Ing2), SUM(Ing3), SUM(Ing4), SUM(Ing5) FROM orderTable WHERE expired='false' AND pickedUp='false' OR orderID='0'";

    char queryString[200];
    int barcodeValid, i;
    double ingredients[NUM_INGREDIENTS];

    while (TRUE) {
        // wait for barcode.

        // read from usbBarcodeScanner
        // numRead = read (barcodeFD, barcode, sizeof(barcode));
        barcodeValid = getBarcodeUSB(barcodeHandle, barcode);

        if(barcodeValid < 0)
        {
            syslog(LOG_INFO, "DEBUG :: Valid barcode not received. Ignoring input");
            continue;
        }

        // see if it is a purge barcode
        if (strcmp(barcode, PURGE_BARCODE) == 0)
        {
            // PURGE!!!
            int num_purge = 0;

            if (getIngredFromSQL(con, ingredAmountQuery, ingredients))
            {
                syslog(LOG_INFO, "DEBUG :: PURGE VALUES");
                for (i = 0; i < NUM_INGREDIENTS; i++)
                {
                    if (ingredients[i] == FULL_VOLUME_LEVEL)
                    {
                        ingredients[i] = PURGE_VOLUME;
                        num_purge++;

                    } else 
                    {
                        ingredients[i] = 0.0;
                    }
                    syslog(LOG_INFO, "DEBUG :: Ingr%d: %lf", i, ingredients[i]);
                }
                if (num_purge == 0)
                {
                    syslog(LOG_INFO, "DEBUG :: No ingredients to purge. Skipping...");
                    continue;
                }

                //  send dispense command with ingredients
                if (dispenseDrink(commandsFD, ingredients) != 0)
                {
                    // something went wrong. don't touch sql.
                    syslog(LOG_INFO, "DEBUG :: Error from dispenseDrink, leaving SQL row intact");
                    continue;
                }

                // update sql from purging, --> currentlevel - PURGE_VOLUME
                sprintf(queryString, "UPDATE orderTable SET Ing0 = Ing0 - %lf, Ing1 = Ing1 - %lf, Ing2 = Ing2 - %lf, Ing3 = Ing3 - %lf, Ing4 = Ing4 - %lf, Ing5 = Ing5 - %lf WHERE orderID='0'", ingredients[0], ingredients[1], ingredients[2], ingredients[3], ingredients[4], ingredients[5] );

                if (mysql_query(con, queryString))
                {
                    syslog(LOG_INFO, "DEBUG :: Unable to update SQL: %s", queryString);
                } else
                {
                    syslog(LOG_INFO, "DEBUG :: Updated SQL.");
                }

            } else 
            {
                syslog(LOG_INFO, "ERROR: Unable to get bottle levels for purge");
                continue;
            }
            // fetch current levels
            syslog(LOG_INFO, "Done Purging");
            continue;
        }

        // construct query string
        strcpy(queryString, baseSelect);
        strcat(queryString, barcode);
        strcat(queryString, "\"");
        // syslog(LOG_INFO, "DEBUG :: Barcode:%s  Query string: %s", barcode,  queryString);

        // query sql for barcode
        if (!getIngredFromSQL(con, queryString, ingredients))
        {
            syslog(LOG_INFO, "DEBUG :: No ingreds found. Continuing...");
            continue;
        }

        syslog(LOG_INFO, "DEBUG :: Ingredients found. Dispensing...");

        // send commands to CB Board
        if (dispenseDrink(commandsFD, ingredients) != 0)
        {
            // something went wrong. don't touch sql.
            syslog(LOG_INFO, "DEBUG :: Error from dispenseDrink, leaving SQL row intact");
            continue;
        }

        // something must have gone ok. manipulate sql.
        // create query string
        strcpy(queryString, baseUpdate);
        strcat(queryString, barcode);
        strcat(queryString, "\"");

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

    while ((opt = getopt(argc, argv, "u:p:d:c:b:v:s:t:a:")) != -1)
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

            case 't':
                    // USB Read Timeout
                    allSettings->usbTimeout = atoi(optarg);
                    break;
            case 'a':
                    // serial read timeout
                    allSettings->ttyTimeout = atoi(optarg);
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
}
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


/*
 *
 * Input:
 *
 * Output:
 *      - Sucess: 0
 *      - Error: 1
 *
 */
int dispenseDrink(int cb_fd, double *ingredArray)
{
    int i;
    const char *endString = "T";
    char command[50];

    tcflush(cb_fd, TCIFLUSH);

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
        double dispAmount = SEC_TO_OZ_RATIO * ingredArray[i];

        syslog(LOG_INFO, "dispAmount - Double: %lf", dispAmount);
        sprintf(command, "%dT", (int)dispAmount);
        // itoa(ingredArray[i], command, 10);
        // append end string
        // strcat(command, endString);

        syslog(LOG_INFO, "Sent: %s", command);

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


    // wait for response
    if(getSerialAck(cb_fd)) 
    {
        write(cb_fd, "Y", 1);
        syslog(LOG_INFO, "DEBUG :: Dispense Controller failed to dispense");
        return 1;
    }
    write(cb_fd, "Y", 1);

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
    
    write (fd, command, strlen(command));
    syslog(LOG_INFO, "DEBUG :: sending command to CB: %s", command);
    
    return getSerialAck(fd);
}


int getSerialAck(int fd) 
{
    char charRead;
    int numRead;

    // clear receive buffer
    // tcflush(fd, TCIFLUSH);

     // wait for response
    numRead = read(fd, &charRead, 1);

    if (numRead < 0)
    {
            syslog(LOG_INFO, "DEBUG :: Error returned from read");
            return 1;
    }

    if(numRead < 1)
    {
        syslog(LOG_INFO, "DEBUG :: No chars read from file descriptor fd");
        return 1;
    }

    syslog(LOG_INFO, "DEBUG :: ACK received: %c", charRead);
    switch (charRead)
    {
        case 'f':
                // fall through
        case 'F':
                // fall through
        case 'y':
                // fall through
        case 'Y':
                // fall through
        case 'z':
                // fall through
        case 'Z':
            syslog(LOG_INFO, "DEBUG :: Received f/F/y/Y/z/Z for ACK");
            return 0;
            break;

        case 'n':
                // fall through
        case 'N':
                // fall through
            syslog(LOG_INFO, "DEBUG :: Received n/N for ACK");
            return 1;

        default:
            syslog(LOG_INFO, "DEBUG :: Received other ACK");
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
int getIngredFromSQL(MYSQL *sql_con, const char *query, double *ingred)
{
    int num_rows, i;
    MYSQL_ROW row;
    MYSQL_RES *result;

    if (mysql_query(sql_con, query)) {
        syslog(LOG_INFO, "DEBUG :: Unable to query SQL with string: %s", query);
        return FALSE;
    }

    result = mysql_store_result(sql_con);

    if (result == FALSE)
    {
        syslog(LOG_INFO, "DEBUG :: Unable to get result from SQL query: %s", query);
        return FALSE;
    }

    num_rows = mysql_num_rows(result);


    if (num_rows != 1)
    {
        syslog(LOG_INFO, "DEBUG :: Order not found, has expired, or has already been picked up: %s", query);
        return FALSE;
    }

    row = mysql_fetch_row(result);

    // syslog(LOG_INFO, "DEBUG :: Drink has not been picked up");

    // store ingredients from row data
    for (i = 0; i < NUM_INGREDIENTS; i++)
    {
        ingred[i] = atof(row[i]);; 
    }

    mysql_free_result(result);
    return TRUE;
}

int daemonize(void) 
{
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
    syslog (LOG_INFO, "Caught SIGINT. Exiting...");
    closeConnections();
    exit(0);

}

void sigTERM_handler(int signum) 
{
    syslog(LOG_INFO, "Caught SIGTERM. Exiting...");
    closeConnections();
    exit(0);
}

void closeConnections(void)
{
    close(currentSettings->fd_CB);
    mysql_close(currentSettings->con_SQL);
    hid_close(currentSettings->barcodeUSBDevice);
    hid_exit();
}

void logInputArgs(struct settings *settings)
{
    syslog(LOG_INFO, "   DB Name     :: %s", settings->dbName);
    syslog(LOG_INFO, "   DB User     :: %s", settings->dbUsername);
    syslog(LOG_INFO, "   DB Passwod  :: %s", settings->dbPasswd);
    syslog(LOG_INFO, "   CB Device   :: %s", settings->cbDevice);
    syslog(LOG_INFO, "   CB Baud     :: %d", settings->cbBaud);
    syslog(LOG_INFO, "   Bar VID     :: %d", settings->barcode_VID);
    syslog(LOG_INFO, "   Bar PID     :: %d", settings->barcode_PID);
    syslog(LOG_INFO, "   Bar Length  :: %d", settings->barcodeLength);
    syslog(LOG_INFO, "   USB Timeout :: %d", settings->usbTimeout);
    syslog(LOG_INFO, "   TTY Timeout :: %d", settings->ttyTimeout);
}
