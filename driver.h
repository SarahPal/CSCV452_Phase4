#define DEBUG4 1

#define CHECKMODE {						                    \
	if (psr_get() & PSR_CURRENT_MODE) { 				    \
	    console("Trying to invoke syscall from kernel\n");	\
	    halt(1);						                    \
	}							                            \
}


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
   void* disk_buf;

   driver_proc_ptr next;

   //more fields to add
   int   pid;

};

struct proc_struct {
   int      wake_time;
   int      sleep_sem;
   int      term_sem;
   int      disk_sem;
   proc_ptr  wake_up;
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
