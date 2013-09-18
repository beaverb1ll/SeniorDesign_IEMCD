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

#include <stdio.h>

#define TRUE 1
#define FALSE 0

#define BARCODE_LENGTH 50



int main(int argc, char const *argv[])
{
	int fd_CB, fd_barcode;
	MYSQL *con_SQL;


	parseArgs(int argc, char const *argv[]);

	fd_CB = openSerial("/dev/ttyS0", B38400, 0);

	fd_barcode = openBarcodeComm();

	con_SQL = openSQL("DBName");

	readBarcodes();

	mysql_close(con_SQL);
	return 0;
}

int openSerial(const char *ttyName, int speed, int parity ) 
{
    int fd; 

    fd = open (ttyName, O_RDWR | O_NOCTTY | O_SYNC);
    if (fd < 0)
    {
        syslog(LOG_INFO, "error %d opening %s: %s", errno, ttyName, strerror (errno));
        exit(1);
    }
    set_interface_attribs (fd, speed, parity);
    set_blocking (fd, BARCODE_LENGTH, 1); 	// block for BARCODE_LENGTH chars or .1 sec
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

MYSQL* openSQL(const char *db_Name) {

	MYSQL *con = mysql_init(NULL);
  
	if (con == NULL) {
    	fprintf(stderr, "%s\n", mysql_error(con));
     	exit(1);
	}  

  	if (mysql_real_connect(con, "localhost", "user12", "34klq*", db_Name, 0, NULL, 0) == NULL) {
    	finish_with_error(con);
  	}
  	return  con;
}

int readBarcodes() {
	
	while (TRUE) {


	}
	return 0;
}