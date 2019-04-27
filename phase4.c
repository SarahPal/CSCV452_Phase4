
#include <stdlib.h>
#include <stdio.h>
#include <strings.h>
#include <usloss.h>
#include <phase1.h>
#include <phase2.h>
#include <phase3.h>
#include <phase4.h>
#include <usyscall.h>
#include <provided_prototypes.h>
#include "driver.h"

/* --------------------------------GLOBALS------------------------------------*/

#define debugflag4 0

static int running; /*semaphore to synchronize drivers and start3*/
int terminate_disk;
int terminate_clock;

static struct driver_proc Driver_Table[MAXPROC];
static int diskpids[DISK_UNITS];
proc_struct ProcTable4[MAXPROC];
proc_ptr Waiting;
int disk_sem[DISK_UNITS];
int diskQ_sem[DISK_UNITS];
int num_tracks[DISK_UNITS];

/* -------------------------------PROTOTYPES----------------------------------*/

static int	ClockDriver(char *);
static int	DiskDriver(char *);


void sleep(sysargs *args);
int sleep_real(int sec);
void disk_read(sysargs *args);
int disk_read_real(int unit, int track, int first, int sectors, void *buffer);
void disk_write(sysargs *args);
int disk_write_real(int unit, int track, int first, int sectors, void *buffer);
void disk_size(sysargs *args);
int disk_size_real(int unit, int *sector, int *track, int *disk);
void term_read(sysargs *args);
int term_read_real(int unit, int size, char *buffer);
void term_write(sysargs *args);
int term_write_real(int unit, int size, char *text);

static void check_kernel_mode(char *caller_name);
void setUserMode();


int
start3(char *arg)
{
    char	name[128];
    char    termbuf[10];
    int		i;
    int		clockPID;
    int		pid;
    int		status;


    int terminate_disk = 1;
    int terminate_clock = 1;
    /*
     * Check kernel mode here.

     */
     check_kernel_mode("start3");

    /* Assignment system call handlers */
    sys_vec[SYS_SLEEP]     = sleep;
    sys_vec[SYS_DISKREAD]  = disk_read;
    sys_vec[SYS_DISKWRITE] = disk_write;
    sys_vec[SYS_DISKSIZE]  = disk_size;
    sys_vec[SYS_TERMREAD]  = term_read;
    sys_vec[SYS_TERMWRITE] = term_write;
    //more for this phase's system call handlings


    /* Initialize the phase 4 process table */

    for(int i = 0; i < MAXPROC; i++)
    {
        ProcTable4[i].wake_time = -1;
        ProcTable4[i].sleep_sem = semcreate_real(0);
        ProcTable4[i].term_sem = semcreate_real(0);
        ProcTable4[i].disk_sem = semcreate_real(0);
        ProcTable4[i].wake_up = NULL;
    }


    /*
     * Create clock device driver
     * I am assuming a semaphore here for coordination.  A mailbox can
     * be used instead -- your choice.
     */
    running = semcreate_real(0);
    clockPID = fork1("Clock driver", ClockDriver, NULL, USLOSS_MIN_STACK, 2);
    if (clockPID < 0) {
        console("start3(): Can't create clock driver\n");
        halt(1);
    }
    /*
     * Wait for the clock driver to start. The idea is that ClockDriver
     * will V the semaphore "running" once it is running.
     */

    semp_real(running);

    /*
     * Create the disk device drivers here.  You may need to increase
     * the stack size depending on the complexity of your
     * driver, and perhaps do something with the pid returned.
     */
    char buf[10];
    for (i = 0; i < DISK_UNITS; i++) {
        sprintf(buf, "%d", i);
        sprintf(name, "DiskDriver%d", i);
        diskpids[i] = fork1(name, DiskDriver, buf, USLOSS_MIN_STACK, 2);
        if (diskpids[i] < 0) {
           console("start3(): Can't create disk driver %d\n", i);
           halt(1);
        }
    }
    semp_real(running);
    semp_real(running);

    /*
     * Create first user-level process and wait for it to finish.
     * These are lower-case because they are not system calls;
     * system calls cannot be invoked from kernel mode.
     * I'm assuming kernel-mode versions of the system calls
     * with lower-case names.
     */
    pid = spawn_real("start4", start4, NULL,  8 * USLOSS_MIN_STACK, 3);
    pid = wait_real(&status);

    terminate_clock = 0;
    /*
     * Zap the device drivers
     */
    zap(clockPID);  // clock driver
    join(&status); /* for the Clock Driver */

    terminate_disk = 0;
    for(int i = 0; i < DISK_UNITS; i++)
    {
        semv_real(disk_sem[i]);
        zap(diskpids[i]);
        join(&status);
    }

    quit(0);
}

void sleep(sysargs *args)
{
    if(DEBUG4 && debugflag4)
        console("    - sleep(): Entering the sleep function\n");
    int seconds = (int)args->arg1;
    int status = sleep_real(seconds);

    if(status == 1)
        args->arg4 = (void *) -1;
    else
        args->arg4 = (void *) 0;

    setUserMode();

}/* sleep */

int sleep_real(int sec)
{
    if(DEBUG4 && debugflag4)
        console("    - sleep_real(): Entering the sleep_real function\n");

    check_kernel_mode("sleep_real");

    if(sec < 0)
    {
        if(DEBUG4 && debugflag4)
            console("        - sleep_real(): invalid number of seconds. returning...\n");
        return 1;
    }

    int pid = getpid()%MAXPROC;
    int wake_time = sys_clock() + (sec * 1000000);

    ProcTable4[pid].wake_time = wake_time;

    if(Waiting == NULL || Waiting->wake_up > wake_time)
    {
        if(Waiting == NULL)
        {
            Waiting = &(ProcTable4[pid]);
        }
        else
        {
            ProcTable4[pid].wake_up = Waiting;
            Waiting = &(ProcTable4[pid]);
        }
    }
    else
    {
        proc_ptr curr = Waiting;
        proc_ptr last = NULL;

        while(curr != NULL && curr->wake_time < wake_time)
        {
            last = curr;
            curr = curr->wake_up;
        }

        last->wake_up = &(ProcTable4[pid]);
        ProcTable4[pid].wake_up = curr;
    }

    semp_real(ProcTable4[pid].sleep_sem);
    return 0;
} /* sleep_real */

void disk_read(sysargs *args)
{

} /* disk_read */

int disk_read_real(int unit, int track, int first, int sectors, void *buffer)
{
    return 1;

} /* disk_read_real */

void disk_write(sysargs *args)
{

} /* disk_write */

int disk_write_real(int unit, int track, int first, int sectors, void *buffer)
{
    return 1;

} /* disk_write_real */

void disk_size(sysargs *args)
{
    if(DEBUG4 && debugflag4)
        console("    - disk_size(): Entering the disk_size function...\n");

    int unit = args->arg1;
    int sector, track, disk;

    int status = disk_size_real(unit, &sector, &track, &disk);

    if(status == -1)
    {
        if(DEBUG4 && debugflag4)
            console("        - disk_size(): bad status. returning...\n");
        args->arg4 = (void *) -1;
        return;
    }

    args->arg1 = (void *) sector;
    args->arg2 = (void *) track;
    args->arg3 = (void *) disk;
    args->arg4 = (void *) status;

    setUserMode();
} /* disk_size */

int disk_size_real(int unit, int *sector, int *track, int *disk)
{
    if(DEBUG4 && debugflag4)
        console("    - disk_size_real(): Entering the disk_size_real function...\n");

    check_kernel_mode("disk_size_real");

    if(unit < 0 || unit > DISK_UNITS)
    {
        if(DEBUG4 && debugflag4)
            console("       - disk_size_real(): Invalid unit number. Returning...\n");
        return -1;
    }

    *sector = DISK_SECTOR_SIZE;
    *track = DISK_TRACK_SIZE;
    *disk = num_tracks[unit];

    return 0;

} /* disk_size_real */

void term_read(sysargs *args)
{

} /* term_read */

int term_read_real(int unit, int size, char *buffer)
{
    return 1;
} /* term_read_real */

void term_write(sysargs *args)
{

} /* term_write */

int term_write_real(int unit, int size, char *buffer)
{
    return 1;

} /* term_write_real */

static int
ClockDriver(char *arg)
{
    int result;
    int status;

    /*
     * Let the parent know we are running and enable interrupts.
     */
    semv_real(running);
    psr_set(psr_get() | PSR_CURRENT_INT);
    while(!is_zapped()) {
        result = waitdevice(CLOCK_DEV, 0, &status);
        if (result != 0) {
            return 0;
        }
        /*
         * Compute the current time and wake up any processes
         * whose time has come.
         */
         while(Waiting != NULL && Waiting->wake_time < sys_clock())
         {
             semv_real(Waiting->sleep_sem);
              proc_ptr temp = Waiting->wake_up;
              Waiting->wake_up = NULL;
              Waiting->wake_time = -1;
              Waiting = temp;

         }
    }
    return 0;
}

static int
DiskDriver(char *arg)
{
   int unit = atoi(arg);
   device_request my_request;
   int result;
   int status;

   disk_sem[unit] = semcreate_real(0);
   diskQ_sem[unit] = semcreate_real(1);

   for(int i = 0; i < DISK_INT; i++)
   {
       num_tracks[i] = 0;
   }

   driver_proc_ptr current_req;

   if (DEBUG4 && debugflag4)
      console("    - DiskDriver(%d): started\n", unit);


   /* Get the number of tracks for this disk */
   my_request.opr  = DISK_TRACKS;
   my_request.reg1 = &num_tracks[unit];

   result = device_output(DISK_DEV, unit, &my_request);
   if(DEBUG4 && debugflag4)
        console("        - DiskDriver(): number of tracks in unit %d: %d\n", unit, num_tracks);

   if (result != DEV_OK) {
      console("        - DiskDriver %d: did not get DEV_OK on DISK_TRACKS call\n", unit);
      console("        - DiskDriver %d: is the file disk%d present???\n", unit, unit);
      halt(1);
   }

   waitdevice(DISK_DEV, unit, &status);
   if (DEBUG4 && debugflag4)
      console("        - DiskDriver(%d): tracks = %d\n", unit, num_tracks[unit]);

   if(result != 0)
   {
       if(DEBUG4 && debugflag4)
          console("        - DiskDriver(%d): waitdevice returned a non-zero value. Returning...\n");
       return 0;
   }

   semv_real(running);

   while(!is_zapped())
   {
       semp_real(disk_sem[unit]);

       if(terminate_disk == 0)
       {
           break;
       }
   }
   return 0;
}

/*----------------------------------------------------------------*
 * Name        : check_kernel_mode                                *
 * Purpose     : Checks the current kernel mode.                  *
 * Parameters  : name of calling function                         *
 * Returns     : nothing                                          *
 * Side Effects: halts process if in user mode                    *
 *----------------------------------------------------------------*/
static void check_kernel_mode(char *caller_name)
{
    union psr_values caller_psr;                                        /* holds the current psr values */
    if (DEBUG4 && debugflag4)
       console("    - check_kernel_mode(): called for function %s\n", caller_name);

 /* checks if in kernel mode, halts otherwise */
    caller_psr.integer_part = psr_get();                               /* stores current psr values into structure */
    if (caller_psr.bits.cur_mode != 1)
    {
       console("        - %s(): called while not in kernel mode, by process. Halting... -\n", caller_name);
       halt(1);
   }
}/* check_kernel_mode */

void setUserMode()
{
    if(DEBUG4 && debugflag4)
        console("    - setUserMode(): inside setUserMode\n");
    psr_set(psr_get() &~PSR_CURRENT_MODE);

    if(DEBUG4 && debugflag4)
        console("        - setUserMode(): user mode set successfully\n");
} /* setUserMode */
