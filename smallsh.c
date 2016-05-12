/********************************************************************************************************************************************************
*** Program Filename: smallsh.c
*** Author: Juan C. Solis
*** Class: CS344-400
*** Date: 7/28/2015
*** Due date: 8/3/2015
*** Description:
***              This is a small shell that will run command line instructions and return the results similar to other shells,
***              but without many of their fancier features. This shell will allow for the redirection of standard input and
***              standard output and it will support both foreground and background processes. It will support three built in commands:
***              exit, cd, and status. It will also support comments, which are lines beginning with the # character. It can also run
***              existing linux commands
***
*** Implementation notes: All error statements will be redirected to stdout so that they can be seen during the testingscript.
***
*** Credits:
***          http://stephen-brennan.com/2015/01/16/write-a-shell-in-c/ - I followed this guide and was able to replace or improve ineffcient code
***
* ********************************************************************************************************************************************************/

#include <stdio.h>
#include <stdlib.h>    // getenv 
#include <string.h>    // strcmp(), strtok()
#include <sys/stat.h>  
#include <sys/types.h> // getpid()
#include <sys/wait.h>  // waitpid()
#include <unistd.h>    // chdir(), fork(), exec()
#include <fcntl.h>     // file control
#include <signal.h>    

/*************************************
GLOBALS AND CONSTANTS VARIABLES
*************************************/
#define MAX_LENGTH 2048 
#define MAX_ARGS 512 // switched to getline method which will allocate buffer
#define TOKEN_BUFSIZE 64
#define TOKEN_DELIM " \t\r\n\a"

//Singly linked list implementation for keeping a list of background processes to kill once shell is exiting (To prevent orphans)
typedef struct bg_child
{
     int bg_pid; //the process id of the child
     struct bg_child  * next;
}bg_child;


/*************************************
FUNCTION PROTOTYPES
*************************************/
pid_t getpid(void);  //Used to get the process id of the program
char *read_line();   //Will get user_input
char **parse_line(char *user_input, int *num_args); //will parse through the line and tokenize the command and arguments into an array
int smallsh_execute(char **args, int num_args, int *exit_status, int *signal_flag, int *terminating_signal, bg_child *head); //checks user input for built in commands, if not passes the command and arguments to smallsh_launch()
int smallsh_launch(char **args, int num_args, int background_process, int *signal_flag, int *terminating_signal, bg_child *head); //will fork() the program and checks for redirection and pass any commands to exec()
int smallsh_redirect_check(int input_flag, int output_flag); //checks for redirection requests made by user
void smallsh_bg_status_check(); //waits for completed child processes and prints the exit status or terminating signal
void smallsh_bg_list_insert(bg_child *pointer, int bg_pid); //inserts and allocates memory for new background process id into the linked list


int main()
{
     //init linkedlist of bg_processes
     bg_child *head, *current;
     head = (bg_child *)malloc(sizeof(bg_child));
     current = head;
     current->bg_pid = -1;
     current->next = NULL;

     char *user_input;        //variable for holding user input
     char **args;             //array for all arguments entered by user
     int num_args;            //the number of arguments including the command
     int smallsh_status;      //variable to determine when to exit the smallsh loop
     int exit_status = 0;     //variable to track exit status of last ran foreground command
     int signal_flag = 0;     //flag for if a foreground process was terminated (false == 0, true == 1)
     int terminating_signal;  //holds the terminating signal number if signal flag is set


     /*********************************************************************
     Signal handler to ignore SIGINT - Credit: Lecture 13 "Signals"
     **********************************************************************/

     struct sigaction act;
     act.sa_flags = 0;
     act.sa_handler = SIG_IGN;
     //sigfillset(&(act.sa_mask));
     sigaction(SIGINT, &act, NULL);

     /********************
     MAIN SMALLSH LOOP
     *********************/
     do
     {
          //check for completed background processes
          smallsh_bg_status_check();

          //print prompt
          printf(": ");
          fflush(stdout); //flush prompt according to assignment directions

          //get user input
          user_input = read_line();

          //process user input
          args = parse_line(user_input, &num_args);

          //find out what user has entered and execute any commands
          smallsh_status = smallsh_execute(args, num_args, &exit_status, &signal_flag, &terminating_signal, head);

          free(user_input);
          free(args);

     } while (smallsh_status); //smallsh_execute will always return 1 except if user has entered command "exit"


     //kill any running background processes to prevent orphans
     current = head;
     while (current != NULL)
     {
          if (current->bg_pid != -1) //head has bg_pid of -1
          {

               kill(current->bg_pid, SIGKILL);
          }

          current = current->next;
     }

     //free the linked list - Credit: http://stackoverflow.com/questions/7025328/linkedlist-how-to-free-the-memory-allocated-using-malloc
     while ((current = head) != NULL) // set currrent to head, stop if list empty.
     {
          head = head->next;
          free(current);              // delete saved pointer.
     }


     return 0;
}


/**********************************************************************
** Function: char *read_line()
** Description: This function will get user input and allocate buffer through getline()
** Credit: http://stephen-brennan.com/2015/01/16/write-a-shell-in-c/
**
** Parameters: none
**********************************************************************/
char *read_line()
{
     char *line = NULL;       //"the manpage for getline() specifies that the first argument should be freeable, so line should be initialized to NULL"-From the article
     ssize_t buffer_size = 0; // getline will be able to dynamically allocate a buffer size
     getline(&line, &buffer_size, stdin);
     return line;
}

/**********************************************************************
** Function: char **parse_line(char *user_input, int *num_args)
** Description: This function will tokenize the user_input line
**              and store the command and arguments into an array.
**              It will also set the variable for number of arguments.
** Credit: http://stephen-brennan.com/2015/01/16/write-a-shell-in-c/
**
** Parameters: the user_input line and pointer to num_args (reference)
**********************************************************************/
char **parse_line(char *user_input, int *num_args)
{
     int buffer_size = TOKEN_BUFSIZE;
     int position = 0;
     char *token;
     char **tokens = malloc(buffer_size * sizeof(char*));

     token = strtok(user_input, TOKEN_DELIM);

     while (token != NULL)
     {
          tokens[position] = token;

          position++;

          if (position >= buffer_size) //need to reallocate more memory 
          {
               buffer_size += TOKEN_BUFSIZE;
               tokens = realloc(tokens, buffer_size * sizeof(char*));
          }

          token = strtok(NULL, TOKEN_DELIM);
     }

     tokens[position] = NULL; //add NULL to end of array for when passed to exec()

     *num_args = (position); //set the number of arguments including the command

     return tokens;
}

/*********************************************************************************************************************
** Function: smallsh_execute(char **args, int num_args, int *exit_status, int *signal_flag, int *terminating_signal)
** Description: Compares the args array to any built in commands, if not it will pass the command to exec()
**
** Parameters: the args array, number of elements, pointer to exit status, signal_flag, and terminating_signal
***********************************************************************************************************************/
int smallsh_execute(char **args, int num_args, int *exit_status, int *signal_flag, int *terminating_signal, bg_child *head)
{

     int background_proccess = 0; //variable for checking if a command is to be executed as a background process. (0 == false, 1 == true)

     //Compare arguments with built in functions

     if ((args[0] == NULL) || (strcmp(args[0], "#") == 0)) //if command is empty or a comment
     {
          return 1; //do nothing and reprint prompt
     }
     /* Built in command: "cd" should support both relative and absolute path */
     else if (strcmp(args[0], "cd") == 0)
     {
          //change to directory specified in the HOME environment variable
          if (args[1] == NULL)
          {
               //credit: http://www.cplusplus.com/reference/cstdlib/getenv/
               char* home_path;
               home_path = getenv("HOME");

               if (home_path != NULL)
               {
                    chdir(home_path);
               }
          }
          //change to specified directory, if not print error
          else
          {
               if (chdir(args[1]) != 0)
               {
                    printf("smallsh: Could not find the directory \"%s\"\n", args[1]);
                    *exit_status = 1;
               }
          }

          return 1; //reprint prompt

     }
     /* Built in command: "status" */
     else if (strcmp(args[0], "status") == 0)
     {
          if (*signal_flag == 0) //no signals were set
          {
               printf("Exit value: %d\n", *exit_status);
          }
          else
          {
               printf("Terminated by signal %d\n", *terminating_signal);
          }
          return 1; //reprint prompt
     }
     /* Built in command: "exit" */
     else if (strcmp(args[0], "exit") == 0)
     {
          return 0; //End the smallsh shell loop
     }

     /* Check if the command will be run in the background*/
     if (strcmp(args[num_args - 1], "&") == 0) // The "&" argument must always be last argument, but the last element is array is NULL
     {
          background_proccess = 1; //set to true
          args[num_args - 1] = NULL; //remove the"&" argument from the array
     }

     /* Not a built in command */
     //Pass the arguments to be exec()
     *exit_status = smallsh_launch(args, num_args, background_proccess, signal_flag, terminating_signal, head);

     return 1; //reprint prompt
}

/*****************************************************************************************************************************
** Function: smallsh_launch(char **args, int num_args, int background_process, int *signal_flag, int *terminating_signal)
** Description: This function will fork() the program and check for any redirection and pass non-built in commands to exec()
**
** Parameters: Pointers to terminating_signal, signal_flag to set them depending on execution of foreground command
******************************************************************************************************************************/
int smallsh_launch(char **args, int num_args, int background_process, int *signal_flag, int *terminating_signal, bg_child *head)
{
     pid_t pid, wpid;
     int status;

     pid = fork();

     /********************************/
     /*     This is the Child        */
     /********************************/
     if (pid == 0)
     {

          //restore sigation to default for all foreground process - Credit: Professor Brewster from class discusion boards
          struct sigaction act;
          act.sa_handler = SIG_DFL;
          act.sa_flags = 0;
          sigaction(SIGINT, &act, 0);

          //check for input/output redirection and set filenames
          int fd; //file descriptor
          int input_flag = -1;
          int output_flag = -1;

          //Counter for preventing segmentation fault when background command (Due to num_arrays being one less)
          int counter = num_args;
          if (background_process == 1)
          {
               counter = (num_args - 1);
          }

          //set redirect flag positions if any - Credit: Inspired by Ian Dalrymple from class discussion boards
          int i;
          for (i = 0; i < counter; i++)
          {
               if (strcmp(args[i], "<") == 0)
               {
                    input_flag = i;
               }
               else if (strcmp(args[i], ">") == 0)
               {
                    output_flag = i;
               }
          }

          /* Check for input/ouput redirection */
          if ((input_flag > -1) || (output_flag > -1))
          {

               //Credit: Lecture 12- "Pipes and Redirection"

               if (smallsh_redirect_check(input_flag, output_flag) == 0)//perform input redirection
               {
                    fd = open(args[input_flag + 1], O_RDONLY, 0644); //Open file that is to be read from -Credit: http://linux.die.net/man/3/open

                    if (fd == -1)
                    {
                         printf("smallsh: cannot open %s for input\n", args[input_flag + 1]);
                         exit(1); //exit the child process with exit stats 1
                    }
                    else
                    {
                         //redirect stdin to the specified file
                         if (dup2(fd, 0) == -1)
                         {
                              printf("smallsh: Could not redirect stdin for input file\n");
                              exit(1); //exit the child process
                         }

                         args[input_flag] = NULL;

                         close(fd);

                    }
               }
               else //perform ouput redirection
               {
                    fd = open(args[output_flag + 1], O_WRONLY | O_CREAT | O_TRUNC, 0644); //opens file to write to. Will create if not exists or truncate to 0 if it does

                    if (fd == -1)
                    {
                         printf("smallsh: cannot not open %s for output\n", args[output_flag + 1]);
                         exit(1); //exit the child process
                    }
                    else
                    {
                         //redirect stdout to the specified file
                         if (dup2(fd, 1) == -1)
                         {
                              printf("smallsh: Could not redirect stdout for output file\n");
                              exit(1); //exit the child process
                         }

                         args[output_flag] = NULL;

                         close(fd);
                    }
               }

          }
          else if ((background_process == 1) && (input_flag < 0)) //user did not specify input redirect
          {
               fd = open("/dev/null", O_RDONLY, 0644);  //redirect stdin to "/dev/null" 

               if (fd == -1)
               {
                    printf("smallsh: Could not open \"/dev/null\"\n");
                    exit(1); //exit the child process
               }
               else
               {
                    //redirect stdin to the specified file
                    if (dup2(fd, 0) == -1)
                    {
                         printf("smallsh: Could not redirect stdin to \"/dev/null\"\n");
                         exit(1); //exit the child process
                    }

                    close(fd);

               }
          }

          if (execvp(args[0], args) == -1)
          {
               printf("smallsh: no such file or directory\n"); //command not found
               exit(1); //exit the child process
          }

     }
     /********************************/
     /* There was an error forking   */
     /********************************/
     else if (pid < 0)
     {
          printf("smallsh: Error forking!\n");
          return 1;
     }
     /********************************/
     /*     This is the Parent       */
     /********************************/
     else
     {
          do
          {
               if (background_process == 1) //if set to true
               {
                    smallsh_bg_list_insert(head, pid); //add background process id to linked list

                    printf("Background pid %d has begun.\n", pid);
                    fflush(stdout);
               }
               else //it is a foreground command
               {
                    wpid = waitpid(pid, &status, WUNTRACED); //wait unitl it is completed
               }

          } while (!WIFEXITED(status) && !WIFSIGNALED(status));

          //Status and termination signals for foreground processes
          if (WIFEXITED(status))
          {
               *signal_flag = 0; //set to false
               return (WEXITSTATUS(status)); //return exit_status

          }
          else if (WIFSIGNALED(status))
          {
               *signal_flag = 1; //set to true
               *terminating_signal = WTERMSIG(status);
               return 1; //return exit status 1
          }
     }
}

/**********************************************************************
** Function: smallsh_redirect_check(int input_flag, int output_flag)
** Description: checks the "<" or ">" location for which one is first
**              and returns 0 if input redirection is to be done or
**              will return 1 for output redirection
**
** Parameters: values for input_flag andint output_flag
**********************************************************************/
int smallsh_redirect_check(int input_flag, int output_flag)
{
     if ((input_flag > -1) && (output_flag > -1)) //both flags have been set
     {
          if (input_flag < output_flag)
          {
               return 0; //perform input redirection first
          }
          else
          {
               return 1; //perform output redirection first
          }
     }
     else if ((input_flag > -1) && (output_flag == -1)) //only input has been set
     {
          return 0; //perform input redirection
     }
     else //only output has been set
     {
          return 1; //perform output redirection
     }
}


/*******************************************************************************************************
** Function: smallsh_bg_status_check()
** Description: Waits for completed child processes and retrieves their status or terminating signals
** Parameters: none
********************************************************************************************************/
void smallsh_bg_status_check()
{
     int pid;
     int bg_status; //status variable for background processes

     //loop to check for any finished background process
     do
     {
          pid = waitpid(-1, &bg_status, WNOHANG);

          if (pid > 0) //something has died
          {
               printf("Background pid %d is done: ", pid);

               if (WIFEXITED(bg_status)) //normal exit
               {
                    printf("Exited value: %d\n", WEXITSTATUS(bg_status));
               }
               else if (WIFSIGNALED(bg_status) != 0) //it got terminated
               {
                    printf("terminated by signal %d\n", WTERMSIG(bg_status));
               }
          }
     } while (pid > 0);
}

/*******************************************************************************************************
** Function: smallsh_bg_list_insert(bg_child *pointer, int bg_pid)
** Description: inserts and allocates memory for new background process id into the linked list
** Parameters: pointer to the head of the linked list and the value to add for the new node
********************************************************************************************************/
void smallsh_bg_list_insert(bg_child *pointer, int bg_pid)
{
     /* Iterate through the list till we encounter the last node.*/
     while (pointer->next != NULL)
     {
          pointer = pointer->next;
     }
     /* Allocate memory for the new bg_child node and put its pid in it.*/
     pointer->next = (bg_child *)malloc(sizeof(bg_child));
     pointer = pointer->next;
     pointer->bg_pid = bg_pid;
     pointer->next = NULL;
}