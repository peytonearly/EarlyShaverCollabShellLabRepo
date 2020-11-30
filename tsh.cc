 // 
// tsh - A tiny shell program with job control
// 
// Peyton Early   -  109483999
// Nathan Shaver  -  109410328

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

/*                                             
  _  _     ___  __      __ ___   __   __ 
 | || |   / _ \ \ \    / /|   \  \ \ / / 
 | __ |  | (_) | \ \/\/ / | |) |  \ V /  
 |_||_|   \___/   \_/\_/  |___/   _|_|_  
_|"""""|_|"""""|_|"""""|_|"""""|_| """ | 
"`-0-0-'"`-0-0-'"`-0-0-'"`-0-0-'"`-0-0-'               
                                                                    |\.../|            __
                                                                    |     |            ||'."-._.-""-.__.-""-.__.-""-.__.-""-.__.-""-.__.-""-.__.-""-.__.-""--.-"\
                                                                \.__;     ;__./        ||  \ You need to implement the functions eval, builtin_cmd, do_bgfg,     )
                                            |\    /|             \___________/         ||   | waitfg, sigchld_handler, sigstp_handler, sigint_handler           /
                                         ___| \,,/_/              \\ U   U //          ||   | The code below provides the "prototypes" for those functions     (
                                      ---__/ \/    \               \///w\\\/ howdy     ||  /  so that earlier code can refer to them. You need to fill in the   )
                                     __--/     U U \  nyehh         //   \\            |||.'  function bodies below.                                           /
                                     _ -/    (_     \       .--.___/|:===:|\___.--.    |||.-"-.__.-""-.__.-""-.__.-""-.__.-""-.__.-""-.__.-""-.__.-""-.__.-""-/
                                    // /       \_ / ==\      |  |   \ %% /   |  ;      ||
  __-------------------_____--___--/           / \_ U U)     |  ||~| | %| |~||  | /---\||
   /                                           /   \ W\\     |  ;|_| |  | |_|:  |/ ._;`||
  /                                           /        \\    \  ;____|  |____:   ./    ||
 ||                    )                   \_/\         \\    \.. ||||[]||||__\./     [||
 ||                   /              _    (.Y.)|         \    // [|   ||        \     [||
   \\\\\\    | |    /--______      ___\    / \ :          \  //  [:   ||.____.   |\    ||
       \\\\| /   __-  - _/   -U-U-u-    | |   \ \          \//   /|   |      |   ;\    ||
            |   -  -   /                | |     \ )              /|   |      \___/\    ||
            |  |   -  |                 | )     | |              /|   |       | :      ||
             | |    | |                 | |    | |               /|   |       : ---^[[[||
             | |    < |                 | |   |_/                 \___/     *-|__---^[[||
             < |    /__\                <  \                      .| |-*               ||
             /__\                       /___\                    (__))                 ||
*/

void eval(char *cmdline);
int builtin_cmd(char **argv);
void do_bgfg(char **argv);
void waitfg(pid_t pid);

void sigchld_handler(int sig);
void sigtstp_handler(int sig);
void sigint_handler(int sig);

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

  // Parse the command line
  char c;
  while ((c = getopt(argc, argv, "hvp")) != EOF) {
    switch (c) {
    case 'h':          // Print help message
      usage();
      break;
    case 'v':          // Emit additional diagnostic info
      verbose = 1;
      break;
    case 'p':          // Don't print a prompt
      emit_prompt = 0; // Handy for automatic testing
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
  Signal(SIGINT,  sigint_handler);   // Ctrl-c
  Signal(SIGTSTP, sigtstp_handler);  // Ctrl-z
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

  exit(0); // Control never reaches here
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
/////////////////////////////////////////////////////////////////////////////
void eval(char *cmdline) 
{
  // Parse command line
  //
  // The 'argv' vector is filled in by the parseline
  // routine below. It provides the arguments needed
  // for the execve() routine, which you'll need to
  // use below to launch a process.
  //

  char *argv[MAXARGS];
  pid_t pid;

  //
  // The 'bg' variable is TRUE if the job should run
  // in background mode or FALSE if it should run in FG
  //
  
  int bg = parseline(cmdline, argv); // True if bg job, false if fg job
  if (argv[0] == NULL) {             // If passed an empty line
    return;                          // ignore empty lines 
  }
  
  sigset_t mask;
  sigemptyset(&mask);                // Initialize empty signal set
  sigaddset(&mask, SIGCHLD);         // Add SIGCHLD signal to signal set
  struct job_t *job;                 //create a job_t struct
  
  if (!builtin_cmd(argv)) {
    sigprocmask(SIG_BLOCK, &mask, NULL);                   // Block SIGCHLD signals

    if((pid = fork()) == 0){                               // If in child
      setpgid(0, 0);                                       //Reset pig to 0,0
      sigprocmask(SIG_UNBLOCK, &mask, NULL);               // Unblock SIGCHLD signals
      execvp(argv[0], argv);                               // Execute child
      printf("%s: Command not found\n", argv[0]);          // Error statement for touch15/16 if command is not found
      exit(0);                                             // Exit if error occurs within child execution
    }

    addjob(jobs, pid, bg ? BG : FG, cmdline);              // Parent adds job to jobs list

    sigprocmask(SIG_UNBLOCK, &mask, NULL);                 // Unblock SIGCHLD signals
    
    if(!bg) {
      waitfg(pid);                                         // Wait for fg function to end
    }
    else{ 
      job = getjobpid(jobs, pid);                          // Find struct of recently added background job
      printf("[%d] (%d) %s", job->jid, job->pid, cmdline); // Print message
    }
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
//
/////////////////////////////////////////////////////////////////////////////
int builtin_cmd(char **argv) 
{
  string cmd(argv[0]);                   // Reassign our input from char to string

  if (cmd == "quit") {                   // Quits program
    exit(0);
  }
  else if (cmd == "fg" || cmd == "bg") { // Changes FG to BG processes and BG to FG processes
    do_bgfg(argv);
    return 1;
  }
  else if (cmd == "jobs") {              // Prints active jobs
    listjobs(jobs);
    return 1;
  }
  return 0;                              // Not a built in command
}

/////////////////////////////////////////////////////////////////////////////
//
// do_bgfg - Execute the builtin bg and fg commands
//
/////////////////////////////////////////////////////////////////////////////
void do_bgfg(char **argv) 
{
  struct job_t *jobp = NULL;

  //  
  // Ignore command if no argument
  //
  if (argv[1] == NULL) {
    printf("%s command requires PID or %%jobid argument\n", argv[0]);
    return;
  }
    
  //
  // Parse the required PID or %JID arg
  //
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
  
  string cmd(argv[0]);                                                 // Reassign our input from char to string
  if (cmd == "bg") {
    jobp -> state = BG;                                                // Set job state to BG
    kill(-jobp -> pid, SIGCONT);                                       // Have job pause and continue running in background
    printf("[%d] (%d) %s", jobp -> jid, jobp -> pid, jobp -> cmdline); // Print message
  }
  else if (cmd == "fg") {
    jobp -> state = FG;                                                // Set job state to FG
    kill(-jobp -> pid, SIGCONT);                                       // Have job pause and continue running in foreground
    waitfg(jobp -> pid);                                               // Need to wait for fg job to finish because only 1 fg job can run at a time
  }
  return;
}

/////////////////////////////////////////////////////////////////////////////
//
// waitfg - Block until process pid is no longer the foreground process
//
/////////////////////////////////////////////////////////////////////////////
void waitfg(pid_t pid)
{
  struct job_t *job;          // Create a job_t struct to hold pid of jobs
  job = getjobpid(jobs, pid); // Collect pid of job
  while(job -> state == FG){  // Continue waiting until job is no longer in FG
    sleep(.1);                // Function for causing the program to wait
  }
  return;
}


/////////////////////////////////////////////////////////////////////////////
/////////////////////////////////////////////////////////////////////////////
//
//
// Signal handlers
//
//
/////////////////////////////////////////////////////////////////////////////
/////////////////////////////////////////////////////////////////////////////


/////////////////////////////////////////////////////////////////////////////
//
// sigchld_handler - The kernel sends a SIGCHLD to the shell whenever
//     a child job terminates (becomes a zombie), or stops because it
//     received a SIGSTOP or SIGTSTP signal. The handler reaps all
//     available zombie children, but doesn't wait for any other
//     currently running children to terminate.  
//
/////////////////////////////////////////////////////////////////////////////
void sigchld_handler(int sig) 
{
  pid_t pid;
  int status;

  while((pid = waitpid(-1, &status, WUNTRACED | WNOHANG)) > 0){
    struct job_t *job;                                                                      // Create a job_t struct to hold pid of jobs
    job = getjobpid(jobs, pid);
    
    if (WIFSTOPPED(status)) {                                                               // Will return true if child process is currently stopped
      job -> state = ST;                                                                    // Set job state to stopped
      printf("Job [%d] (%d) stopped by signal %d\n", job -> jid, pid, WSTOPSIG(status));    // Print statement for signal handlers
    }
    else if (WIFSIGNALED(status)) {
      printf("Job [%d] (%d) terminated by signal %d\n", job -> jid, pid, WTERMSIG(status)); // Print statement for signal handlers
      deletejob(jobs, pid);                                                                 // Deletes the job pid
    }
    else if (WIFEXITED(status)) {
      deletejob(jobs, pid);                                                                 // Deletes the job pid
    }

  }
  return;
}

/////////////////////////////////////////////////////////////////////////////
//
// sigint_handler - The kernel sends a SIGINT to the shell whenver the
//    user types ctrl-c at the keyboard. Catch it and send it along
//    to the foreground job.  
//
/////////////////////////////////////////////////////////////////////////////
void sigint_handler(int sig) 
{
  pid_t pid = fgpid(jobs); // Collect pid of foreground job
  if(pid <= 0){            // fgpid() returns 0 if no fg job, otherwise returns not 0
    return; 
  }
  kill(-pid, SIGINT);      // Terminate group associated with pid
}

/////////////////////////////////////////////////////////////////////////////
//
// sigtstp_handler - The kernel sends a SIGTSTP to the shell whenever
//     the user types ctrl-z at the keyboard. Catch it and suspend the
//     foreground job by sending it a SIGTSTP.  
//
/////////////////////////////////////////////////////////////////////////////
void sigtstp_handler(int sig) 
{
  pid_t pid = fgpid(jobs); // Collect pid of foreground job
  if(pid <= 0){
    return;                // Stop group associated with pid
  }
  kill(-pid, SIGTSTP);     // Stop group associated with pid
}

/////////////////////////////////////////////////////////////////////////////
/////////////////////////////////////////////////////////////////////////////
//
//
// End Signal Handlers
//
//
/////////////////////////////////////////////////////////////////////////////
/////////////////////////////////////////////////////////////////////////////