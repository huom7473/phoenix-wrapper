#include <stdio.h>
#include <poll.h>
#include <unistd.h>
#include <stdlib.h>
#include <fcntl.h>
#include <sys/types.h>
#include <string.h>
#include <errno.h>
#include <termios.h>
#include <time.h>
#include <getopt.h>
#include <signal.h>
#include <math.h>

#define KEYBOARD 0
#define SCRIPT 1
#define READ 0
#define WRITE 1
#define BUFSIZE 2048
#define PAUSE_DELAY 500000 //delay before pausing miner after seeing initial devfee message, in us
#define CLOCK CLOCK_MONOTONIC

int input[2], output[2]; //input/output pipes to forked process (miner)
int logfd, current_byte = 0, i = 0, clock_started = 0; //global variables for auxilliary functions (event_loop)
int pause_flag = 0, dev_running = 0; //whether to pause miner when dev is running, and whether devfee is running currently
double total_fee_time = 0; //total time spent mining for dev, in seconds

char big_buf[BUFSIZE]; //stores output from forked process until it's printed to terminal

struct termios term_orig, term_copy; //terminal settings, mostly for entering character-at-a-time mode. restore original before exiting.

/* to monitor pause/total time -
   start = time at first successful DAG generation
   current = time struct used for getting current time
   devstart = time since devfee has started (or since last call to update_time)
              valid only if dev_running is set to true (1). */
struct timespec ts_start, ts_current, ts_devstart; //to monitor pause/total time

void event_loop(struct pollfd*);
void cleanup(int);
void update_time(void);
int generate_statistics_message(char*, size_t, int);


int main(int argc, char **argv) {
  char* script;

  static struct option long_options[] = {
    {"help", no_argument, 0, 'h'},
    {"pause", no_argument, 0, 'p'},
    {0, 0, 0, 0}
  };

  int c;
  while ((c = getopt_long(argc, argv, "p", long_options, 0)) != -1) {
    switch (c) {
    case 'p':
      pause_flag = 1;
      break;
    case 'h':
    default:
      fprintf(stderr, "usage: %s [-p, --pause] /path/to/miner\n", argv[0]);
      if (c != 'h') exit(1);
      exit(0);
    }
  }
  
  if (argc - optind != 1) { //check if there's exactly one argument at the end
    fprintf(stderr, "%s: must specify path to miner executable/script. --help for usage message\n", argv[0]);
    exit(1);
  }
  
  script = argv[optind];

  tcgetattr(STDIN_FILENO, &term_orig);
  term_copy = term_orig; //store attributes for later

  term_copy.c_iflag = ISTRIP;
  term_copy.c_oflag = 0;
  term_copy.c_lflag = 0;
  tcsetattr(STDIN_FILENO, TCSANOW, &term_copy);

  if (pipe(input) == -1 || pipe(output) == -1) {
    fprintf(stderr, "error opening pipes\n");
    tcsetattr(STDIN_FILENO, TCSANOW, &term_orig);
    exit(1);
  }

  pid_t pid = fork();

  if(pid == 0) {
    close(input[WRITE]);
    dup2(input[READ], STDIN_FILENO);

    close(output[READ]);
    dup2(output[WRITE], STDOUT_FILENO);
    dup2(output[WRITE], STDERR_FILENO);

    if (execl(script, script, NULL) == -1)
      fprintf(stderr, "error executing %s: %s\n", script, strerror(errno));
  }
  else {
    close(input[READ]);
    close(output[WRITE]);

    signal(SIGINT, cleanup);
    signal(SIGTERM, cleanup);

    logfd = creat("dev_stop.log", 0666);
    struct pollfd pfds[2]; //poll for stuff from script output, and read from stdin

    pfds[KEYBOARD].events = pfds[SCRIPT].events = POLLIN;

    pfds[KEYBOARD].fd = STDIN_FILENO;
    pfds[SCRIPT].fd = output[READ];

    while (1) {
      event_loop(pfds);
    }
  }
}

void event_loop(struct pollfd *pfds) { //assume 2 poll fds
  int eventc, bytes_read;
  char temp_buf[BUFSIZE], statistics_buf[256];
  eventc = poll(pfds, 2, -1); //infinite timeout
  if (eventc > 0) {
    if (pfds[SCRIPT].revents & POLLIN) {
      bytes_read = read(output[READ], big_buf + current_byte, BUFSIZE - current_byte);
      current_byte += bytes_read;
      while (i < current_byte) { //process all the commands in the buffer
        if (big_buf[i] == '\n') {
          memcpy(temp_buf, big_buf, i + 1); //copy whole line, including \n
          temp_buf[i] = '\0'; //terminate line with null character to make it a string
          current_byte -= i + 1;
          memmove(big_buf, big_buf + i + 1, current_byte);
          fprintf(stdout,"%s\r\n", temp_buf); //terminal in non-canonical mode needs both newline and carriage return
          /*also important that the carriage return is first, else the newline causes a flush and then the carriage return is buffered
          until the next line*/
	  
          if(strstr(temp_buf, "DevFee: Connecting")) { //check for devfee start
            clock_gettime(CLOCK, &ts_devstart); //set devfee start time
	    dev_running = 1;
	    if (pause_flag) {
	      usleep(PAUSE_DELAY);
	      write(input[WRITE], "p", 1); //pause on stop message, after waiting for specified delay
	      /*it seems that pausing immediately doesn't actually stop mining
		might want to use a separate thread for this to avoid main thread sleeping (too lazy for now)*/
	    }
          }
	  else if (strstr(temp_buf, "DevFee: Disconnected")) {
	    if (pause_flag)
	      write(input[WRITE], "p", 1); //unpause immediately on stop message

	    generate_statistics_message(statistics_buf, 256, 0);
	    dprintf(logfd, "%s\n", statistics_buf);
	    dev_running = 0; //set dev_running to false after message is generated, else update_time won't work properly
	  }
	  else if (!clock_started && strstr(temp_buf, "DAG generated")) { //start clock on first GPU's successful DAG generation
	    clock_gettime(CLOCK, &ts_start);
	    clock_started = 1;
	  }
	  
          dprintf(logfd, "%s\n", temp_buf);
          i = 0;
        }
        else
          ++i;
      }
    }
    else if (pfds[SCRIPT].revents & (POLLHUP | POLLERR)) {
      tcsetattr(STDIN_FILENO, TCSANOW, &term_orig);
      fprintf(stderr, "child process hung up - exiting\n");
      exit(0);
    }
    else if (pfds[KEYBOARD].revents & POLLIN) {
      bytes_read = read(STDIN_FILENO, temp_buf, BUFSIZE);
      if (bytes_read == 1 && temp_buf[0] == 0x03) {
        cleanup(SIGINT);
      }
      else if (bytes_read == 1 && temp_buf[0] == '`') {
	generate_statistics_message(temp_buf, BUFSIZE, 1); //can reuse temp_buf since we won't be doing any more checks this loop
        fprintf(stderr, "%s\r\n", temp_buf);
      }
      else
	write(input[WRITE], temp_buf, bytes_read);
    }
  }
}

void cleanup(int signum __attribute__((unused))) {
  tcsetattr(STDIN_FILENO, TCSANOW, &term_orig);
  fprintf(stderr, "\033[1;31mreceived ctrl-c/other stop signal - exiting (hopefully taking the miner down as well)\033[0m\n");
  
  int statsfd;
  char buf[256]; //just to hold statistics message
  if ((statsfd = open("stats.log", O_CREAT | O_WRONLY | O_APPEND, 0666)) == -1)
    fprintf(stderr, "could not open stats.log file: %s\n", strerror(errno));
  else {
    generate_statistics_message(buf, 256, 0);
    dprintf(statsfd, "%s\n", buf);
    fprintf(stderr, "devfee stats logged to stats.log\n");
  }
  
  exit(0);
}

/* If devfee is currently running, get the current time and update the total devfee time, also setting the
   new devfee start period to the current time to avoid counting the same time frame multiple times.
   Does nothing is devfee isn't running. */
void update_time(void) {
  if (!dev_running)
    return;
  
  clock_gettime(CLOCK, &ts_current);
  total_fee_time += (ts_current.tv_sec - ts_devstart.tv_sec) + (ts_current.tv_nsec - ts_devstart.tv_nsec)/1e9;
  ts_devstart = ts_current; //set start point to current
}

int format_time(double sec, char *buf, size_t bufsize) {
  int h, m;
  double s;
  sec = round(sec*100.0)/100.0;
  h = sec/3600;
  m = (sec-3600*h)/60;
  s = sec-3600*h-60*m;

  return snprintf(buf, bufsize, "%.2d:%.2d:%05.2f", h, m, s);
}

int generate_statistics_message(char *buf, size_t bufsize, int terminal) {
  update_time(); //get latest devfee runtime
  clock_gettime(CLOCK, &ts_current);
  double total_elapsed = clock_started ? (ts_current.tv_sec - ts_start.tv_sec) + (ts_current.tv_nsec - ts_start.tv_nsec)/1e9 : 0;
  
  char total_elapsed_s[16], total_fee_time_s[16];
  format_time(total_elapsed, total_elapsed_s, 16);
  format_time(total_fee_time, total_fee_time_s, 16);
	      
  if (terminal)
    return snprintf(buf, bufsize, "\033[1;31mTotal time elapsed: %s | Devfee time: %s | Actual Devfee: %.2f%%\033[0m",
	   total_elapsed_s, total_fee_time_s, 100*(total_fee_time/total_elapsed));

  return snprintf(buf, bufsize, "Total time elapsed: %s | Devfee time: %s | Actual Devfee: %.2f%%",
	   total_elapsed_s, total_fee_time_s, 100*(total_fee_time/total_elapsed));
}
