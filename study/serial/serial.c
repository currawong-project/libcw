#include <errno.h>
#include <fcntl.h> 
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <termios.h>
#include <unistd.h>

int set_interface_attribs(int fd, int speed)
{
    struct termios tty;

    if (tcgetattr(fd, &tty) < 0)
    {
        printf("Error from tcgetattr: %s\n", strerror(errno));
        return -1;
    }

    cfsetospeed(&tty, (speed_t)speed);
    cfsetispeed(&tty, (speed_t)speed);

    tty.c_cflag |= (CLOCAL | CREAD);    /* ignore modem controls */
    tty.c_cflag &= ~CSIZE;
    tty.c_cflag |= CS8;         /* 8-bit characters */
    tty.c_cflag &= ~PARENB;     /* no parity bit */
    tty.c_cflag &= ~CSTOPB;     /* only need 1 stop bit */
    tty.c_cflag &= ~CRTSCTS;    /* no hardware flowcontrol */

    /* setup for non-canonical mode */
    tty.c_iflag &= ~(IGNBRK | BRKINT | PARMRK | ISTRIP | INLCR | IGNCR | ICRNL | IXON | IXOFF | IXANY);
    tty.c_lflag &= ~(ECHO | ECHONL | ICANON | ISIG | IEXTEN);
    tty.c_oflag &= ~OPOST;

    /* fetch bytes as they become available */
    tty.c_cc[VMIN] = 0;
    tty.c_cc[VTIME] = 0;

    if (tcsetattr(fd, TCSANOW, &tty) != 0)
    {
        printf("Error from tcsetattr: %s\n", strerror(errno));
        return -1;
    }
    
    return 0;
}

void set_mincount(int fd, int mcount)
{
    struct termios tty;

    if (tcgetattr(fd, &tty) < 0)
    {
        printf("Error tcgetattr: %s\n", strerror(errno));
        return;
    }

    tty.c_cc[VMIN] = mcount ? 1 : 0;
    tty.c_cc[VTIME] = 5;        /* half second timer */

    if (tcsetattr(fd, TCSANOW, &tty) < 0)
        printf("Error tcsetattr: %s\n", strerror(errno));
}

void print_attributes(int fd )
{
  struct termios attr;
  if(tcgetattr(fd,&attr) == -1)
    printf("tcgetattr failed.");

  printf("ibaud:%i",cfgetispeed(&attr));
  printf("obaud:%i",cfgetospeed(&attr));
  
}

int main()
{
    char *portname = "/dev/ttyACM0";
    int fd;
    int wlen;

    fd = open(portname, O_RDWR | O_NOCTTY | O_SYNC);
    if (fd < 0)
    {
        printf("Error opening %s: %s\n", portname, strerror(errno));
        return -1;
    }
    
    /*baudrate 115200, 8 bits, no parity, 1 stop bit */
    set_interface_attribs(fd, B38400);
    //set_mincount(fd, 0);                /* set to pure timed read */

    print_attributes(fd);

    unsigned char buf[80];

    buf[0] = '0';

    printf("Reading\n");

    /* simple noncanonical input */
    do
    {
        int rdlen;

        int displayStringFl = 0;


        /* simple output */
        wlen = write(fd, buf, 1);
        if (wlen != 1)
        {
          printf("Error from write: %d, %d\n", wlen, errno);
        }
    
        tcdrain(fd);    /* delay for output */


        
        //rdlen = read(fd, buf, sizeof(buf) - 1);
        rdlen = read(fd, buf, 1);

        printf(".");
        
        if (rdlen > 0)
        {
          printf("rdlen:%i\n",rdlen);
          
          if(displayStringFl)
          {
            buf[rdlen] = 0;
            printf("Read %d: \"%s\"\n", rdlen, buf);
          }
          else
          {
            unsigned char   *p;
            printf("Read %d:", rdlen);
            for (p = buf; rdlen-- > 0; p++)
                printf(" 0x%x", *p);
            printf("\n");
          }
          
        }
        else
          if (rdlen < 0)
          {
            printf("Error from read: %d: %s\n", rdlen, strerror(errno));
          }

        sleep(1);
        /* repeat read to get full message */
    } while (1);
}
