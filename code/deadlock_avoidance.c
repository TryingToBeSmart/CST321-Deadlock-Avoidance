#include <stdlib.h>
#include <unistd.h>
#include <stdio.h>
#include <errno.h>
#include <semaphore.h>
#include <pthread.h>
#include <sys/mman.h>
#include <signal.h>

sem_t* semaphore;
pid_t pid;
pid_t otherPid;
sigset_t sigSet;

void endProcess_signalHandler(int sig)
{
    printf("Caught signal: %d\n", sig);
    printf("Exit child process\n");
    sem_post(semaphore);
    exit(0);
}

void isAlive_signalHandler(int sig)
{
    printf("childProcess: I am alive\n");
}

// simulate a timer of 10 sec by going to sleep, then check if semaphore is locked, 
// indicating a hung child process
void *checkHungChild(void *argStatus)
{
    int *status = argStatus;
    printf("Checking for hung child process...\n");
    sleep(10);
    if(sem_trywait(semaphore) != 0)
    {
        printf("Child process appears to be hung...\n");
        *status = 1;
    }
    else
    {
        printf("Child process appears to be ok...\n");
        *status = 0;
    }
    return NULL;
}

// Simulate a stuck process by obtaining a lock on a semaphore then sitting for 60 sec
void childProcess()
{
    // Set up a signal for the parentProcess to check on the childProcess
    signal(SIGUSR1, endProcess_signalHandler);
    signal(SIGUSR2, isAlive_signalHandler);

    int value;
    sem_getvalue(semaphore, &value);
    printf("Child process semaphore count: %d\n", value);
    printf("Child process is grabbing semaphore\n");

    // grab the lock and wait
    sem_wait(semaphore);
    sem_getvalue(semaphore, &value);
    printf("Child process semaphore count: %d\n", value);
    printf("Now going to sleep...\n");

    printf("child process using the semaphore for 60 seconds\n");
    /* Critical Region */
    for(int i = 0; i < 60; ++i)
    {
        // printf("child process using the semaphore\n");
        sleep(1);
    }

    /* End Critical Region */
    sem_post(semaphore);

    printf("Exit child process\n");
    exit(0);
}

// Sleep for 10 seconds then attampt to get semaphore.
// If it can't get the semaphore, then kill the child process 
// and see if it can get the semaphore after that
void parentProcess()
{
    printf("Parent process starting\n");
    sleep(2);
    if(getpgid(otherPid) >= 0) printf("parentProcess: child process is running...\n");

    int value;
    sem_getvalue(semaphore, &value);
    printf("parentProcess: semaphore count: %d\n", value);

    // Try to get semaphore, if blocked, then set timer
    if(sem_trywait(semaphore) != 0)
    {
        // blocked so set timer
        pthread_t tid1;
        int status = 0;
        printf("parentProcess can't get semaphore\n");
        printf("Child process is running too long\n");

        // Create a thread and call the checkHungChild function and pass the status address to it
        // this creates a timer in the checkHungChild function for 10 seconds
        if(pthread_create(&tid1, NULL, checkHungChild, &status))
        {
            printf("ERROR creating timer thread\n");
            exit(1);
        }
        if(pthread_join(tid1, NULL))
        {
            printf("ERROR joining timer thread\n");
            exit(1);
        }

        printf("parentProcess: sending signal to see if child process is alive\nchildProcess:");
        kill(otherPid, SIGUSR2);
        sleep(1);
        
        // kill child process if status == 1
        if(status == 1)
        {
            printf("child process with ID: %d is taking too long\nKill? (y/n)", otherPid);
            char input;
            scanf("%c", &input);
            if(input == 'y' || input == 'Y')
            {    
                kill(otherPid, SIGUSR1);
                printf("Killed child process\n");

                printf("Proving child process is killed; should see no response from SIGUSR2 signal\n");
                sleep(5);
                kill(otherPid, SIGUSR2);
                sleep(1);
                printf("Done proving child process is killed\n");
            }

            // Try to get semaphore again
            sem_getvalue(semaphore, &value);
            printf("Parent process: semaphore count: %d\n", value);
            if(sem_trywait(semaphore) != 0)
            {
                if(value == 0) sem_post(semaphore);
                printf("Got the semaphore, clean up\n");
                sem_getvalue(semaphore, &value);
                printf("Parent process: semaphore count; %d\n", value);
            }
            else{
                printf("Parent process: got semaphore\n");
            }
        }
    }
    printf("Exit parent process\n");
    exit(0);
}

int main()
{
    // semaphore to be accessed by multiple processes
    // semaphore = sem_open("Semaphore", O_CREAT, 00644, 1); 
    
    // semaphore created in shared memory with mmap
    semaphore = (sem_t*)mmap(0,sizeof(sem_t), PROT_READ|PROT_WRITE,MAP_SHARED|MAP_ANONYMOUS, -1, 0);

    // Use sem_init to create a shared (non-zero 2nd arg) semaphore with initial count of 1 (3rd arg)
    if(sem_init(semaphore, 1, 1) != 0)
    {
        printf("Failed creating semaphore\n");
        return -1;
    }

    // Use fork() to make 2 processes
    pid = fork();
    
    // fork failed
    if (pid <= -1)
    {
        printf("Fork failed\n");
        return -1;
    }

    // Call child process function
    if (pid == 0)
    {
        printf("Starting Child process with ID: %d...\n", getpid());
        otherPid = getppid();
        childProcess();
    }

    // Call parent process function
    else
    {
        printf("Starting Parent process with ID: %d...\n", getpid());
        otherPid = pid;
        parentProcess();
    }

    // Clean up
    sem_destroy(semaphore);

    return 0;
}