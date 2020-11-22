// 
// tsh - A tiny shell program with job control
// 
// <Put your name and login ID here>
// Peyton Early
// Nathan Shaver

using namespace std;

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <ctype.h>
#include <signal.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <errno.h>
#include <string>

#include "globals.h"
#include "jobs.h"
#include "helper-routines.h"

//
// Needed global variable definitions
//

static char prompt[] = "tsh> ";
int verbose = 0;

//
// You need to implement the functions eval, builtin_cmd, do_bgfg,
// waitfg, sigchld_handler, sigstp_handler, sigint_handler
//
// The code below provides the "prototypes" for those functions
// so that earlier code can refer to them. You need to fill in the
// function bodies below.
//

void eval(char *cmdline);
int builtin_cmd(char **argv);
void do_bgfg(char **argv);
void waitfg(pid_t pid);

void sigchld_handler(int sig);
void sigtstp_handler(int sig);
void sigint_handler(int sig);

//
// Provided functions
//
// void clearjob(struct job_t *job) - clear entries in a job struct
// void initjobs(struct job_t *jobs) - initialize the job listt
// int maxjid(struct job_t *jobs) - returns largest allocated job ID
// int addjob(struct job_t *jobs, pid_t pid, int state, char *cmdline) - add a job to the job list
// int deletejob(struct job_t *jobs, pid_t pid) - delete a job whose PID=pid from the job list
// pid_t fgpid(struct job_t *jobs) - return PID of current forground job, 0 if no such job
// struct job_t *getjobpid(struct job_t *jobs, pid_t pid) - find a job (by PID) on the job list
// struct job_t *getjobjid(struct job_t *jobs, int jid) - find a job (by JID) on the job list
// int pid2jid(pid_t pid) - map process ID to job ID
// void listjobs(struct job_t *jobs) - print the job list
// void usage(void) - print a help message
// void unix_error(const char *msg) - unix-style error routine
// void app_error(const char *msg) - application-style error routine
// handler_t *Signal(int signum, handler_t *handler) - wrapper for sigaction function
// void sigquit_handler(int sig) - Gracefully terminate child shell
// int parseline(const char *cmdline, char **argv) - Parse command line and build argv array


//
// main - The shell's main routine 
//
int main(int argc, char *argv[]) 
{
  int emit_prompt = 1; // emit prompt (default)

  //
  // Redirect stderr to stdout (so that driver will get all output
  // on the pipe connected to stdout)
  //
  dup2(1, 2);

  /* Parse the command line */
  char c;
  while ((c = getopt(argc, argv, "hvp")) != EOF) {
    switch (c) {
    case 'h':             // print help message
      usage();
      break;
    case 'v':             // emit additional diagnostic info
      verbose = 1;
      break;
    case 'p':             // don't print a prompt
      emit_prompt = 0;  // handy for automatic testing
      break;
    default:
      usage();
    }
  }

  //
  // Install the signal handlers
  //

  //
  // These are the ones you will need to implement
  //
  Signal(SIGINT,  sigint_handler);   // ctrl-c
  Signal(SIGTSTP, sigtstp_handler);  // ctrl-z
  Signal(SIGCHLD, sigchld_handler);  // Terminated or stopped child

  //
  // This one provides a clean way to kill the shell
  //
  Signal(SIGQUIT, sigquit_handler); 

  //
  // Initialize the job list
  //
  initjobs(jobs);

  //
  // Execute the shell's read/eval loop
  //
  for(;;) {
    //
    // Read command line
    //
    if (emit_prompt) {
      printf("%s", prompt);
      fflush(stdout);
    }

    char cmdline[MAXLINE];

    if ((fgets(cmdline, MAXLINE, stdin) == NULL) && ferror(stdin)) {
      app_error("fgets error");
    }
    //
    // End of file? (did user type ctrl-d?)
    //
    if (feof(stdin)) {
      fflush(stdout);
      exit(0);
    }

    //
    // Evaluate command line
    //
    eval(cmdline);
    fflush(stdout);
    fflush(stdout);
  } 

  exit(0); //control never reaches here
}
  
/////////////////////////////////////////////////////////////////////////////
//
// eval - Evaluate the command line that the user has just typed in
// 
// If the user has requested a built-in command (quit, jobs, bg or fg)
// then execute it immediately. Otherwise, fork a child process and
// run the job in the context of the child. If the job is running in
// the foreground, wait for it to terminate and then return.  Note:
// each child process must have a unique process group ID so that our
// background children don't receive SIGINT (SIGTSTP) from the kernel
// when we type ctrl-c (ctrl-z) at the keyboard.
//
void eval(char *cmdline) 
{
  /* Parse command line */
  //
  // The 'argv' vector is filled in by the parseline
  // routine below. It provides the arguments needed
  // for the execve() routine, which you'll need to
  // use below to launch a process.
  //
  char *argv[MAXARGS];

  //
  // The 'bg' variable is TRUE if the job should run
  // in background mode or FALSE if it should run in FG
  //
  int bg = parseline(cmdline, argv); // True if bg job, false if fg job
  if (argv[0] == NULL) { // If passed an empty line
    return;   /* ignore empty lines */
  }

  if (!builtin_cmd(argv)) {
    return;
  }

  return;
}

/////////////////////////////////////////////////////////////////////////////
//
// builtin_cmd - If the user has typed a built-in command then execute
// it immediately. The command name would be in argv[0] and
// is a C string. We've cast this to a C++ string type to simplify
// string comparisons; however, the do_bgfg routine will need 
// to use the argv array as well to look for a job number.

int builtin_cmd(char **argv) 
{
  string cmd(argv[0]);

  if (cmd == "quit") {
     exit(0); // Exit shell
  }
  else if (cmd == "fg" || cmd == "bg") {
    // do_bgfg
    do_bgfg(argv); // Restart job in bg or fg
    return 1;
    // waitfg
  }
  else if (cmd == "jobs") {
    listjobs(jobs); // List running and stopped background jobs
    return 1;
  }
  else {
    return 0; // Not a built in command
  }
}

/////////////////////////////////////////////////////////////////////////////
//
// do_bgfg - Execute the builtin bg and fg commands
//
void do_bgfg(char **argv) 
{
  struct job_t *jobp=NULL;
    
  /* Ignore command if no argument */
  if (argv[1] == NULL) {
    printf("%s command requires PID or %%jobid argument\n", argv[0]);
    return;
  }
    
  /* Parse the required PID or %JID arg */
  if (isdigit(argv[1][0])) {
    pid_t pid = atoi(argv[1]);
    if (!(jobp = getjobpid(jobs, pid))) {
      printf("(%d): No such process\n", pid);
      return;
    }
  }
  else if (argv[1][0] == '%') {
    int jid = atoi(&argv[1][1]);
    if (!(jobp = getjobjid(jobs, jid))) {
      printf("%s: No such job\n", argv[1]);
      return;
    }
  }	    
  else {
    printf("%s: argument must be a PID or %%jobid\n", argv[0]);
    return;
  }

  //
  // You need to complete rest. At this point,
  // the variable 'jobp' is the job pointer
  // for the job ID specified as an argument.
  //
  // Your actions will depend on the specified command
  // so we've converted argv[0] to a string (cmd) for
  // your benefit.
  //
  string cmd(argv[0]); // Convert char to string

  if (cmd == "bg") {
    jobp->state = BG; // Set job state to BG
    if (getjobpid(jobs, jobp->pid)) { // Search if job already exists
      kill(-jobp->pid, SIGCONT); // Have job pause and continue running in background
      printf("[%d] (%d) %s", jobp->jid, jobp->pid, jobp->cmdline);
    }
    else {
      addjob(jobs, jobp->pid, jobp->state, jobp->cmdline);
    }
  }
  else if (cmd == "fg") {
    jobp->state = FG; // Set job state to FG
    if (getjobpid(jobs, jobp->pid)) {
      kill(-jobp->pid, SIGCONT); // Have job pause and continue running in foreground
    }
    else {
      addjob(jobs, jobp->pid, jobp->state, jobp->cmdline);
    }
    waitfg(jobp->pid); // Need to wait for fg job to finish because only 1 fg job can run at a time
  }

  return;
}

/////////////////////////////////////////////////////////////////////////////
//
// waitfg - Block until process pid is no longer the foreground process
//
void waitfg(pid_t pid)
{
  while (pid == fgpid(jobs)) { // While job pid is still the foreground pid, then wait
    sleep(1);
  }
  return;
}

/////////////////////////////////////////////////////////////////////////////
//
// Signal handlers
//


/////////////////////////////////////////////////////////////////////////////
//
// sigchld_handler - The kernel sends a SIGCHLD to the shell whenever
//     a child job terminates (becomes a zombie), or stops because it
//     received a SIGSTOP or SIGTSTP signal. The handler reaps all
//     available zombie children, but doesn't wait for any other
//     currently running children to terminate.  
//
void sigchld_handler(int sig) 
{
  return;
}

/////////////////////////////////////////////////////////////////////////////
//
// sigint_handler - The kernel sends a SIGINT to the shell whenver the
//    user types ctrl-c at the keyboard.  Catch it and send it along
//    to the foreground job.  
//
void sigint_handler(int sig) 
{
  return;
}

/////////////////////////////////////////////////////////////////////////////
//
// sigtstp_handler - The kernel sends a SIGTSTP to the shell whenever
//     the user types ctrl-z at the keyboard. Catch it and suspend the
//     foreground job by sending it a SIGTSTP.  
//
void sigtstp_handler(int sig) 
{
  return;
}

/*********************
 * End signal handlers
 *********************/




