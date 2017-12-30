#include <stdio.h>
#include <strings.h>
#include <unistd.h>
#include <errno.h>
#include <termios.h>
#include <fcntl.h>
#include <time.h>
#include <stdlib.h>

#define STATUS_SIZE 16

#define RS232_SPEED 115200

static int newsfd;
static struct termios oldtio;
static char commDevice[]="/dev/cuaa0";
 
static int InitComm(int initBaudRate) {
  struct termios newtio;

  /* open the device to be non-blocking (read will return immediately) */
  newsfd = open(commDevice, O_RDWR | O_NOCTTY | O_NONBLOCK);
  if (newsfd <0) {perror(commDevice); exit(-1); }
  
  /* allow the process to receive SIGIO */
  fcntl(newsfd, F_SETOWN, getpid());
  /* Make the file descriptor asynchronous (the manual page says only
     O_APPEND and O_NONBLOCK, will work with F_SETFL...) */
  fcntl(newsfd, F_SETFL, O_ASYNC);
  
  tcgetattr(newsfd,&oldtio); /* save current port settings */
  /* set new port settings for canonical input processing */
  bzero(&newtio,sizeof(newtio));
  newtio.c_cflag = CS8 | CLOCAL | CREAD ; 
  newtio.c_iflag = IGNPAR;
  newtio.c_oflag = 0;
  newtio.c_lflag = 0;
  newtio.c_cc[VMIN] = 0;
  newtio.c_cc[VTIME] = 0;
  newtio.c_ispeed = initBaudRate;
  newtio.c_ospeed = initBaudRate;
  tcflush(newsfd, TCIFLUSH);
  if (tcsetattr(newsfd,TCSANOW,&newtio)==-1) {
    printf("Serial port initialization failed, errno: %d\n",errno);
    return -1;
  }

  return 0;
}

static int SendChar(char d) {
  if (write(newsfd, &d, 1)<0)
    return -1;
  else
    return d;
}


static int ReceiveChar(void) {
    unsigned char d;
  if (read(newsfd, &d, 1) == 1) {
    return d;
  } else {
    return -1;
  }
}

static int ReceiveData(char *data, int len) { 
  int i = 0;
  int startTime = time(NULL);

  while(i<len && time(NULL) - startTime < 2) { 
    if(read(newsfd,data+i,1)==1)
      i++;       
  }
  return i;
}

#if 0
static int CloseComm(void)  { 
  /* restore old port settings */
  tcsetattr(newsfd,TCSANOW,&oldtio);
  /* close serial port */
  close(newsfd); 
  return 0; 
}
#endif



#include <sys/types.h>
#include <sys/uio.h>
#include <unistd.h>
#include <termios.h>
#include <signal.h>

#define TIMEOUT 0 /* no timeout */
struct termios org, new;
 
void rememberterminal()
{
#ifdef _HPUX_SOURCE
  ioctl(0, TCGETATTR, &org);   /* nykyiset arvot talteen */
#else
  tcgetattr(0, &org);
#endif
}
 
void initterminal(int timeout)
{
#ifdef _HPUX_SOURCE
  ioctl(0, TCGETATTR, &new); 
#else
  tcgetattr(0, &new);
#endif
  new.c_lflag &= ~ECHO;
  new.c_lflag &= ~ICANON; /* ei-kanoninen input-moodi */
 
  /* seuraavat arvot maarittelevat luettavien merkkien minimimaaran ja */
  /* niiden lukemiseen maksimissaan kaytettavan ajan kymmenesosasekunteina */
  /* tarkempaa selvitysta loytyy komennolla 'man termio' */
  new.c_cc[VMIN] = 1;
  new.c_cc[VTIME] = timeout; /* TIMEOUT*0.1s */

#ifdef VDSUSP /*not for LINUX*/
  new.c_cc[VDSUSP] = 0;
#endif

#ifdef _HPUX_SOURCE
  ioctl(0, TCSETATTR, &new); 
#else
  tcsetattr(0, TCSANOW, &new);
#endif
}
 
void backterminal()
{
#ifdef _HPUX_SOURCE
  ioctl(0, TCSETATTR, &org);  
#else
  tcsetattr(0, TCSANOW, &org);
#endif
}

void SigCont()
{
    initterminal(TIMEOUT);
    signal(SIGCONT, SigCont);
    return;
}




int main(int argc, char **argv) {
  time_t lastOpT;
  int i, c;
  int blockSize, blocks, usedBlocks;
  int totErr = 0;
  int mode = 0;
  FILE *sendFile = 0;

  if (InitComm(RS232_SPEED))
    exit(-1);

  if (argc > 1) {
      sendFile = fopen(argv[1], "rb");
  }

  printf("Interactive mode\n");

  fcntl(fcntl(STDIN_FILENO,  F_DUPFD, 0), F_SETFL, O_NONBLOCK);

  rememberterminal();
  signal(SIGCONT, SigCont);
  initterminal(TIMEOUT);

  while(1) {
      unsigned char d;
      int c;

      if (sendFile) {
	  char line[80];
	  if (fgets(line, 80-1, sendFile)) {
	      if (line[0] != '#') {
		  c = strtol(line, NULL, 0);
#if 1
		  printf("-> '%d' %02x\n", c, c);
#endif
		  SendChar(c);
	      }
	  } else {
	      fclose(sendFile);
	      sendFile = NULL;
	  }
      } else
      if ((c = getchar()) >= 0) {
	  /*	printf("ch=0x%04x\n", c);*/
	  d = c;// & 127;
	  SendChar(d);
#if 1
	  printf("-> '%d' %02x\n", d, d);
#endif
      } else if ((c = ReceiveChar()) >= 0) {
	  if (argc > 1) {
	      printf("%c %02x ", isprint(c) ? c : '.', c);
	  } else {
	      if (isprint(c) || c == '\r' || c == '\n') {
		  putchar((c == '\r') ? '\n' : c);
	      } else {
#if 0
		  if (c & 128) {
		      printf("\033[5m%c\033[0m", ' ' + (c & 31));
		  } else {
		      printf("\033[7m%c\033[0m", ' ' + (c & 31));
		  }
#else
		  printf("\033[5m\\x%02x\033[0m", c);
#endif
	      }
	  }
	  fflush(stdout);
      } else {
	  usleep(20000);
      }
  }
    backterminal();
  return 0;
}
