#define DEBUG4 1

typedef struct driver_proc driver_proc;
typedef struct driver_proc * driver_proc_ptr;
typedef struct proc_struct proc_struct;
typedef struct proc_struct * proc_ptr;

struct driver_proc {
   driver_proc_ptr next_ptr;

   int   wake_time;    /* for sleep syscall */
   int   been_zapped;


   /* Used for disk requests */
   int   operation;    /* DISK_READ, DISK_WRITE, DISK_SEEK, DISK_TRACKS */
   int   track_start;
   int   sector_start;
   int   num_sectors;
   void *disk_buf;

   //more fields to add

};

struct proc_struct {
   short          pid;               /* process id */
   short          ppid;
   char           name[MAXNAME];
   char           startArg[MAXARG];
   int            priority;
   int (* start_func) (char *);
   int            stack_size;
   int            spawnBox;
   int            num_children;
   proc_ptr       next;
   proc_ptr       childProcPtr;
   proc_ptr       nextSiblingPtr;
   /* other fields as needed... */
};

struct psr_bits {
    unsigned int cur_mode:1;
    unsigned int cur_int_enable:1;
    unsigned int prev_mode:1;
    unsigned int prev_int_enable:1;
    unsigned int unused:28;
};

union psr_values {
   struct psr_bits bits;
   unsigned int integer_part;
};

