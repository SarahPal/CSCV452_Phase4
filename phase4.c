
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
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
driver_proc_ptr Disk_QueueT[DISK_UNITS];
driver_proc_ptr Disk_QueueB[DISK_UNITS];

proc_struct ProcTable4[MAXPROC];
proc_ptr Waiting;

static int diskPID[DISK_UNITS];
int diskW_sem[DISK_UNITS];
int diskQ_sem[DISK_UNITS];
int disk_requests[DISK_UNITS];
int num_tracks[DISK_UNITS];
int arm_loc[DISK_UNITS];

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

void disk_req(driver_proc_ptr request, int unit);
void disk_reqII(driver_proc_ptr request, int unit);
void disk_seek(int unit, int first);

static void check_kernel_mode(char* caller_name);
void setUserMode(char* caller_name);


int
start3(char *arg)
{
    char	name[128];
    char    termbuf[10];
    int		i;
    int		clockPID;
    int		pid;
    int		status;


    terminate_disk = 1;
    terminate_clock = 1;
    /*
     * Check kernel mode here.
     */
     check_kernel_mode("start3");

    /* Assignment system call handlers */
    sys_vec[SYS_SLEEP]     = sleep;
    sys_vec[SYS_DISKREAD]  = disk_read;
    sys_vec[SYS_DISKWRITE] = disk_write;
    sys_vec[SYS_DISKSIZE]  = disk_size;

    /* Initialize the phase 4 process table */

    for (int i = 0; i < MAXPROC; i++)
    {
        ProcTable4[i].wake_time = -1;
        ProcTable4[i].sleep_sem = semcreate_real(0);
        ProcTable4[i].term_sem = semcreate_real(0);
        ProcTable4[i].disk_sem = semcreate_real(0);
        ProcTable4[i].wake_up = NULL;
    }


    /* Create clock device driver
     * I am assuming a semaphore here for coordination.  A mailbox can
     * be used instead -- your choice. */
    running = semcreate_real(0);
    clockPID = fork1("ClockDriver", ClockDriver, NULL, USLOSS_MIN_STACK, 2);
    if (clockPID < 0) {
        console("start3(): Can't create ClockDriver\n");
        halt(1);
    }
    /* Wait for the clock driver to start. The idea is that ClockDriver
     * will V the semaphore "running" once it is running. */

    semp_real(running);

    /* Create the disk device drivers here.  You may need to increase
     * the stack size depending on the complexity of your
     * driver, and perhaps do something with the pid returned. */

    for (i = 0; i < DISK_UNITS; i++) {
        sprintf(termbuf, "%d", i);
        sprintf(name, "DiskDriver%d", i);
        diskPID[i] = fork1(name, DiskDriver, termbuf, USLOSS_MIN_STACK, 2);
        if (diskPID[i] < 0) {
           console("start3(): Can't create disk driver %d\n", i);
           halt(1);
        }
    }
    semp_real(running);
    semp_real(running);

    /* Create first user-level process and wait for it to finish.
     * These are lower-case because they are not system calls;
     * system calls cannot be invoked from kernel mode.
     * I'm assuming kernel-mode versions of the system calls
     * with lower-case names. */
    pid = spawn_real("start4", start4, NULL,  8 * USLOSS_MIN_STACK, 3);
    pid = wait_real(&status);

    /* Zap the device drivers */
    terminate_disk = 0;
    for (int i = 0; i < DISK_UNITS; i++)
    {
        semv_real(diskQ_sem[i]);
        semv_real(diskW_sem[i]);
        join(&status);
    }

    terminate_clock = 0;
    zap(clockPID);  /* clock driver */
    join(&status);  /* for the Clock Driver */

    quit(0);
    return 0;
}

void sleep(sysargs *args)
{
    if (DEBUG4 && debugflag4)
        console("        - sleep(): Entering the sleep function\n");
    int seconds = (int)args->arg1;
    int status = sleep_real(seconds);

    if (status == 1)
    {
        args->arg4 = (void *) -1;
    }
    else
    {
        args->arg4 = (void *) 0;
    }

    setUserMode("sleep");

}/* sleep */

int sleep_real(int sec)
{
    if (DEBUG4 && debugflag4)
        console("        - sleep_real(): Entering the sleep_real function\n");

    check_kernel_mode("sleep_real");

    if (sec < 0)
    {
        if (DEBUG4 && debugflag4)
            console("            - sleep_real(): invalid number of seconds. returning...\n");
        return 1;
    }

    int pid = getpid()%MAXPROC;
    int wake_time = sys_clock() + (sec * 1000000);

    ProcTable4[pid].wake_time = wake_time;

    if (Waiting == NULL || Waiting->wake_time > wake_time)
    {
        if (Waiting == NULL)
        {
            Waiting = &(ProcTable4[pid]);
        }
        else
        {
            ProcTable4[pid].wake_up = Waiting;
            Waiting = &(ProcTable4[pid]);
        }
    }
    else        /* Can we actually get here? Actually who cares... */
    {
        proc_ptr curr = Waiting;
        proc_ptr last = NULL;
        while(curr != NULL)
        {
            if (curr->wake_time < wake_time)
            {
                last = curr;
                curr = curr->wake_up;
            }
        }
        last->wake_up = &(ProcTable4[pid]);
        ProcTable4[pid].wake_up = curr;
    }

    semp_real(ProcTable4[pid].sleep_sem);
    return 0;
} /* sleep_real */

void disk_read(sysargs *args)
{
    int unit = (int)args->arg5;
    int track = (int)args->arg3;
    int first = (int)args->arg4;
    int sectors = (int)args->arg2;
    void* buffer = args->arg1;

    long status = disk_read_real(unit, track, first, sectors, buffer);

    if (status == -1)
    {
        if (DEBUG4 && debugflag4)
            console("            - disk_read(): invalid status returned. Returning...\n");
        args->arg4 = (void *) -1;
        return;
    }

    args->arg1 = (int) status;
    args->arg4 = (void *) 0;

    setUserMode("disk_read");

} /* disk_read */

int disk_read_real(int unit, int track, int first, int sectors, void *buffer)
{
   if (DEBUG4 && debugflag4)
        console("        - disk_read_real(): Entering the disk_read_real function...\n");

    if (unit > DISK_UNITS || track >= num_tracks[unit] || first >= DISK_TRACK_SIZE)
    {
        if (DEBUG4 && debugflag4)
            console("        - disk_read_real(): invalid arguments. Returning...\n");
        return -1;
    }

    check_kernel_mode("disk_read_real");

    driver_proc request;
    request.track_start = track;
    request.sector_start = first;
    request.num_sectors = sectors;
    request.disk_buf = buffer;
    request.operation = DISK_READ;
    request.next = NULL;
    request.pid = getpid();

    disk_req(&request, unit);

    if (DEBUG4 && debugflag4)
        console("        - disk_read_real(): returned from disk_req\n");

    for (int i = 0; i < request.num_sectors; i++)
    {
        /* Allows DiskDriver to call waitdevice */
        semv_real(diskW_sem[unit]);

        /* Has current process block */
        semp_real(ProcTable4[getpid() % MAXPROC].disk_sem);
    }
        semp_real(ProcTable4[getpid() % MAXPROC].disk_sem);
    return 0;
} /* disk_read_real */

void disk_write(sysargs *args)
{
    int unit = (int)args->arg5;
    int track = (int)args->arg3;
    int first = (int)args->arg4;
    int sectors = (int)args->arg2;
    void* buffer = args->arg1;

    int status = disk_write_real(unit, track, first, sectors, buffer);
    if (status == -1)
    {
        if (DEBUG4 && debugflag4)
            console("            - disk_read(): invalid status returned. Returning...\n");
        args->arg4 = (void *) -1;
        return;
    }

    args->arg1 = (void *) status;
    args->arg4 = (void *) 0;

    setUserMode("disk_read");
} /* disk_write */


int disk_write_real(int unit, int track, int first, int sectors, void *buffer)
{
    if (DEBUG4 && debugflag4)
        console("        - disk_write_real(): Entering the disk_write_real function...\n");

    if (unit > DISK_UNITS || track >= num_tracks[unit] || first >= DISK_TRACK_SIZE)
    {
        if (DEBUG4 && debugflag4)
            console("        - disk_write_real(): invalid arguments. Returning...\n");
        return -1;
    }

    check_kernel_mode("disk_write_real");

    driver_proc request;
    request.track_start = track;
    request.sector_start = first;
    request.num_sectors = sectors;
    request.disk_buf = buffer;
    request.operation = DISK_WRITE;
    request.next = NULL;
    request.pid = getpid();

    disk_req(&request, unit);

    if (DEBUG4 && debugflag4)
        console("        - disk_write_real(): returned from disk_req\n");


    for (int i = 0; i < request.num_sectors; i++)
    {
        /* Allows DiskDriver to call waitdevice */
        semv_real(diskW_sem[unit]);

        /* Has current process block */
        semp_real(ProcTable4[getpid() % MAXPROC].disk_sem);
    }

    /* Has current process block */
    semp_real(ProcTable4[getpid() % MAXPROC].disk_sem);

    return 0;
} /* disk_write_real */

void disk_size(sysargs *args)
{
    if (DEBUG4 && debugflag4)
        console("        - disk_size(): Entering the disk_size function...\n");

    int unit = (int)args->arg1;
    int sector, track, disk;

    int status = disk_size_real(unit, &sector, &track, &disk);

    if (status == -1)
    {
        if (DEBUG4 && debugflag4)
            console("            - disk_size(): bad status. returning...\n");
        args->arg4 = (void *) -1;
        return;
    }

    args->arg1 = (void *) sector;
    args->arg2 = (void *) track;
    args->arg3 = (void *) disk;
    args->arg4 = (void *) status;

    setUserMode("disk_size");
} /* disk_size */

int disk_size_real(int unit, int *sector, int *track, int *disk)
{
    if (DEBUG4 && debugflag4)
        console("        - disk_size_real(): Entering the disk_size_real function...\n");

    check_kernel_mode("disk_size_real");

    if (unit < 0 || unit > DISK_UNITS)
    {
        if (DEBUG4 && debugflag4)
            console("            - disk_size_real(): Invalid unit number. Returning...\n");
        return -1;
    }

    *sector = DISK_SECTOR_SIZE;
    *track = DISK_TRACK_SIZE;
    *disk = num_tracks[unit];

    return 0;

} /* disk_size_real */

void disk_req(driver_proc_ptr request, int unit)
{
    if (DEBUG4 && debugflag4)
        console("        - disk_req(): Entering the disk_req function...\n");

    /* handles the queue if it's empty */
    if (Disk_QueueT[unit] == NULL)
    {
        if (DEBUG4 && debugflag4)
            console("        - disk_req(): All Queues are empty, adding to DiskQueueT[%d] as head\n", unit);
        Disk_QueueT[unit] = request;
    }

    else
    {
        if (DEBUG4 && debugflag4)
            console("        - disk_req(): Adding to DiskQueueT[%d]\n", unit);

        driver_proc_ptr curr = Disk_QueueT[unit];
        driver_proc_ptr prev = curr;

        /* handles adding to Disk_QueueT */
        if (request->track_start > arm_loc[unit] && request->track_start >= curr->track_start)
        {
            /* orders Disk_QueueT by track_start */

            while(curr->next != NULL && request->track_start >= curr->track_start)
            {
                prev = curr;
                curr = curr->next;
            }
            /* checks final iteration */
            if (request->track_start >= curr->track_start)
                prev = curr;

            /* actually adds the request */
            request->next = prev->next;
            prev->next = request;
        }

        /* handles adding to Disk_QueueB */
        else
        {
            if (DEBUG4 && debugflag4)
                console("        - disk_req(): Adding to DiskQueueB[%d]\n", unit);

            if (Disk_QueueB[unit] == NULL)
            {
                if (DEBUG4 && debugflag4)
                    console("        - disk_req(): DiskQueueB[%d] is empty. Saving to head\n", unit);
                Disk_QueueB[unit] = request;
            }
            else
            {
                driver_proc_ptr curr = Disk_QueueB[unit];
                driver_proc_ptr prev = curr;

                if (DEBUG4 && debugflag4)
                    console("        - disk_req(): DiskQueueB[%d] is not empty\n", unit);

                /* orders Disk_QueueT by track_start */
                if(request->track_start < curr->track_start)
                {
                    Disk_QueueB[unit] = request;
                    request->next = curr;
                }

                else
                {
                    while(curr->next != NULL && request->track_start > curr->track_start)
                    {
                        prev = curr;
                        curr = curr->next;
                    }
                    /* checks final iteration */
                    if (request->track_start >= curr->track_start)
                        prev = curr;

                    /* actually adds the request */
                    request->next = prev->next;
                    prev->next = request;
                }
            }

        }
    }

    if (DEBUG4 && debugflag4)
        console("        - disk_req(): Disk request added to Disk Queue.\n");

    /* Marks that request has entered the queue */
    semv_real(diskQ_sem[unit]);
}


void disk_reqII(driver_proc_ptr request, int unit)
{
    int status;
    if (DEBUG4 && debugflag4)
        console("            - disk_reqII(): Entering disk_reqII...\n");

    if (request == NULL)
    {
        if (DEBUG4 && debugflag4)
            console("            - disk_reqII(): Invalid request. Halting...\n");
        return;
    }

    if (DEBUG4 && debugflag4)
        console("            - disk_reqII(): Calling disk_seek.\n");
    disk_seek(unit, request->track_start);
    if (DEBUG4 && debugflag4)
        console("            - disk_reqII(): disk_seek was successful.\n");

    for (int i = 0; i < request->num_sectors; i++)
    {
        if (DEBUG4 && debugflag4)
            console("            - disk_reqII(): In loop proccessing request %d.\n", i);

        device_request nRequest;

        nRequest.opr = request->operation;
        nRequest.reg1 = (void*) (arm_loc[unit] + i);
        nRequest.reg2 = (void*) request->disk_buf + i * 512;

        device_output(DISK_DEV, unit, &nRequest);
        if (DEBUG4 && debugflag4)
            console("            - disk_reqII(): Before waitdevice call\n");
        waitdevice(DISK_DEV, unit, &status);
        if (DEBUG4 && debugflag4)
            console("            - disk_reqII(): After waitdevice call\n");

        /* Tells process it's good to continue */
        semv_real(ProcTable4[request->pid].disk_sem);

    }
    /* Tells process it's good to continue */

}/* disk_reqII */


void disk_seek(int unit, int first)
{
    int status;

    if (DEBUG4 && debugflag4)
        console("                - disk_seek(): Entering disk_seek...\n");
    if (first >= num_tracks[unit])
    {
        console("                - disk_seek(): Thep entered track was invalid. Halting...\n");
        halt(0);
        return;
    }

    device_request request;
    request.opr = DISK_SEEK;
    request.reg1 = first;

    device_output(DISK_DEV, unit, &request);

    if (DEBUG4 && debugflag4)
        console("                - disk_seek(): Before waitdevice call\n");
    waitdevice(DISK_DEV, unit, &status);
    if (DEBUG4 && debugflag4)
        console("                - disk_seek(): After waitdevice call\n");

    arm_loc[unit] = first;
    if (DEBUG4 && debugflag4)
        console("                - disk_seek(): Current arm location for disk%d: %d\n", unit, arm_loc[unit]);
}

static int ClockDriver(char *arg)
{
    int result;
    int status;

    if (DEBUG4 && debugflag4)
        console("    - ClockDriver(): Entering ClockDriver...\n");

    /* Let the parent know we are running and enable interrupts */
    semv_real(running);
    psr_set(psr_get() | PSR_CURRENT_INT);
    while(!is_zapped())
    {
        result = waitdevice(CLOCK_DEV, 0, &status);
        if (result != DEV_OK)
        {
            if (DEBUG4 && debugflag4)
                console("    - ClockDriver(): waitdevice did not return DEV_OK\n");
                //dump_processes();
            return status;
        }
        /* Compute the current time and wake up any processes
         * whose time has come. */
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

static int DiskDriver(char *arg)
{
    int unit = atoi(arg);
    device_request my_request;
    int result;
    int status;

    diskW_sem[unit] = semcreate_real(0);
    diskQ_sem[unit] = semcreate_real(1);
    disk_requests[unit] = semcreate_real(0);
    Disk_QueueT[unit] = NULL;
    Disk_QueueB[unit] = NULL;
    arm_loc[unit] = 0;

    driver_proc_ptr current_req;

    if (DEBUG4 && debugflag4)
        console("    - DiskDriver()[%d]: Entering DiskDriver...\n", unit);


    /* Prepares the number of tracks for this disk */
    my_request.opr  = DISK_TRACKS;
    my_request.reg1 = &num_tracks[unit];

    result = device_output(DISK_DEV, unit, &my_request);

    if (result != DEV_OK)
    {
        console("    - DiskDriver()[%d]: did not get DEV_OK on DISK_TRACKS call\n", unit);
        console("    - DiskDriver()[%d]: is the file disk%d present???\n", unit, unit);
        halt(1);
    }

    /* Waits for disk input, actually checks tracks */
    if (DEBUG4 && debugflag4)
        console("    - DiskDriver()[%d]: number of tracks before waitdevice: %d\n", unit, num_tracks[unit]);
    waitdevice(DISK_DEV, unit, &status);
    if (DEBUG4 && debugflag4)
        console("    - DiskDriver()[%d]: number of tracks after waitdevice:%d\n", unit, num_tracks[unit]);

    if (result != DEV_OK)
    {
        if (DEBUG4 && debugflag4)
            console("    - DiskDriver()[%d]: waitdevice returned a non-zero value. Returning...\n", unit);
        return 0;
    }

    /* Let the parent know we are running and enable interrupts */
    semv_real(running);

    do
    {
        if (DEBUG4 && debugflag4)
            console("    - DiskDriver()[%d]: start of loop.\n", unit);

        /* Waits for request to enter the queue */
        semp_real(diskQ_sem[unit]);
        if (DEBUG4 && debugflag4)
            console("    - DiskDriver()[%d]: Just passed DiskQ semaphore.\n", unit);

        /* Waits for request to call waitdevice */
        semp_real(diskW_sem[unit]);
        if (DEBUG4 && debugflag4)
            console("    - DiskDriver()[%d]: Just passed DiskW semaphore.\n", unit);

        if (terminate_disk == 0)
        {
            if (DEBUG4 && debugflag4)
                console("    - DiskDriver()[%d]: Exiting DiskDriver.\n", unit);
            break;
        }

        if (Disk_QueueT[unit] == NULL)
        {
            if (DEBUG4 && debugflag4)
                console("    - DiskDriver()[%d]: DiskQueueT[%d] is empty.\n", unit, unit);
            Disk_QueueT[unit] = Disk_QueueB[unit];
            Disk_QueueB[unit] = NULL;
            if (DEBUG4 && debugflag4)
                console("    - DiskDriver()[%d]: DiskQueueT[%d] now equals DiskQueueB[%d]\n", unit, unit, unit);
        }

        driver_proc_ptr request = Disk_QueueT[unit];

        if (request != NULL)
        {
            /* Calls disk_reqII which is where the actual calls happen */
            disk_reqII(request, unit);
            if (DEBUG4 && debugflag4)
                console("    - DiskDriver()[%d]: disk_reqII successful\n", unit);
        }

        if (Disk_QueueT[unit] != NULL)
        {
            if (DEBUG4 && debugflag4)
                console("    - DiskDriver()[%d]: Disk_QueueT[unit] set to next\n", unit, unit);
            Disk_QueueT[unit] = Disk_QueueT[unit]->next;
        }

        if (request != NULL)
        {
            /* Tells requesting process it's good to go */
            semv_real(ProcTable4[request->pid].disk_sem);
        }

   }while(!is_zapped());

   return 0;
}/* DiskDriver */


static void check_kernel_mode(char* caller_name)
{
    union psr_values caller_psr;                                        /* holds the current psr values */
    if (DEBUG4 && debugflag4)
       console("                    - check_kernel_mode(): called for function %s\n", caller_name);

 /* checks if in kernel mode, halts otherwise */
    caller_psr.integer_part = psr_get();                               /* stores current psr values into structure */
    if (caller_psr.bits.cur_mode != 1)
    {
       console("                    - %s(): called while not in kernel mode, by process. Halting... -\n", caller_name);
       halt(1);
   }
}/* check_kernel_mode */


void setUserMode(char* caller_name)
{
    if (DEBUG4 && debugflag4)
        console("                    - setUserMode(): called for function %s\n", caller_name);
    psr_set(psr_get() &~PSR_CURRENT_MODE);
} /* setUserMode */
