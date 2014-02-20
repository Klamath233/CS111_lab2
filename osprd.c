#include <linux/version.h>
#include <linux/autoconf.h>
#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/init.h>

#include <linux/sched.h>
#include <linux/kernel.h>  /* printk() */
#include <linux/errno.h>   /* error codes */
#include <linux/types.h>   /* size_t */
#include <linux/vmalloc.h>
#include <linux/genhd.h>
#include <linux/blkdev.h>
#include <linux/wait.h>
#include <linux/file.h>

#include "spinlock.h"
#include "osprd.h"

// Code by Xi Han starts:
#define ARRAY_INIT_SIZE 2
// Code by Xi Han ends.
/* The size of an OSPRD sector. */
#define SECTOR_SIZE	512

/* This flag is added to an OSPRD file's f_flags to indicate that the file
 * is locked. */
#define F_OSPRD_LOCKED	0x80000

/* eprintk() prints messages to the console.
 * (If working on a real Linux machine, change KERN_NOTICE to KERN_ALERT or
 * KERN_EMERG so that you are sure to see the messages.  By default, the
 * kernel does not print all messages to the console.  Levels like KERN_ALERT
 * and KERN_EMERG will make sure that you will see messages.) */
#define eprintk(format, ...) printk(KERN_NOTICE format, ## __VA_ARGS__)

MODULE_LICENSE("Dual BSD/GPL");
MODULE_DESCRIPTION("CS 111 RAM Disk");
// EXERCISE: Pass your names into the kernel as the module's authors.
MODULE_AUTHOR("Shubham Gandhi & Xi Han");

#define OSPRD_MAJOR	222

/* This module parameter controls how big the disk will be.
 * You can specify module parameters when you load the module,
 * as an argument to insmod: "insmod osprd.ko nsectors=4096" */
static int nsectors = 32;
module_param(nsectors, int, 0);


/* The internal representation of our device. */
typedef struct osprd_info {
	uint8_t *data;                  // The data array. Its size is
	                                // (nsectors * SECTOR_SIZE) bytes.

	osp_spinlock_t mutex;           // Mutex for synchronizing access to
					// this block device

	unsigned ticket_head;		// Currently running ticket for
					// the device lock

	unsigned ticket_tail;		// Next available ticket for
					// the device lock

	wait_queue_head_t blockq;       // Wait queue for tasks blocked on
					// the device lock

	/* HINT: You may want to add additional fields to help
	         in detecting deadlock. */

	// Code by Xi Han starts:
	int read_lock;
	int write_lock;
	unsigned *dead_tickets;
	size_t dead_tickets_length;
	size_t dead_tickets_capacity;
	// Code by Xi Han ends.
	
	// Code by SG starts
	int deadlock_write;
	int deadlock_write;
	// Code by SG ends

	// The following elements are used internally; you don't need
	// to understand them.
	struct request_queue *queue;    // The device request queue.
	spinlock_t qlock;		// Used internally for mutual
	                                //   exclusion in the 'queue'.
	struct gendisk *gd;             // The generic disk.
} osprd_info_t;

#define NOSPRD 4
static osprd_info_t osprds[NOSPRD];


// Declare useful helper functions

// Code by Xi Han starts:
// If current ticket tail of d is dead, return 1.
// Otherwise, return 0.
// d is guaranteed not to be modified.
static int is_ticket_tail_dead(osprd_info_t *d);
// Code by Xi Han ends.

/*
 * file2osprd(filp)
 *   Given an open file, check whether that file corresponds to an OSP ramdisk.
 *   If so, return a pointer to the ramdisk's osprd_info_t.
 *   If not, return NULL.
 */
static osprd_info_t *file2osprd(struct file *filp);

/*
 * for_each_open_file(task, callback, user_data)
 *   Given a task, call the function 'callback' once for each of 'task's open
 *   files.  'callback' is called as 'callback(filp, user_data)'; 'filp' is
 *   the open file, and 'user_data' is copied from for_each_open_file's third
 *   argument.
 */
static void for_each_open_file(struct task_struct *task,
			       void (*callback)(struct file *filp,
						osprd_info_t *user_data),
			       osprd_info_t *user_data);

// Code by SG starts

// Declare helper funciton that will check locks
// This function will be passed into for_each_file_open to check each file
void check_locks(struct file * filp, osprd_info_t *d)
{
	// Check to see if current process already is already in request queue
	// Or if its already holding the wrong lock on the device, then return deadlock.
	
	// Only check device we are requesting lock for
	if(file2osprd(filp) == d)
	{
		// If we have lock on device, check to see if its allowed
		if(filp->f_flags & F_OSPRD_LOCKED)
		{	
			// Limitation: Does not check blocked requests?
			// Will cause deadlock on write
			if(filp->f_mode & FMODE_WRITE )
				d->deadlock_read = 1;
			// and/or will cause deadlock on read
			d->deadlock_write = 1;
		}
	}
}


//if the device is already in use, and we are about to block to wait for it, make sure processes
//owning the locks dont include us!
bool deadlock (osprd_info_t *d, int filp_writable)
{
	osp_spin_lock(&d->mutex);
	
	// Reset deadlock flagging variables
	d->deadlock_write = 0;
	d->deadlock_read = 0;
	
	// Check locks on each of the current process' open files
	for_each_open_file(current,check_locks,d);
	
	//if filp_writable is true, we are requesting a write block
	if( d->deadlock_read || (d->deadlock_write && filp_writable) )
	{
		osp_spin_unlock(&d->mutex);
		return true;
	}
	
	// No deadlocks, return false
	osp_spin_unlock(&d->mutex);  
	return false;
}

// Code by SG ends

/*
 * osprd_process_request(d, req)
 *   Called when the user reads or writes a sector.
 *   Should perform the read or write, as appropriate.
 */
static void osprd_process_request(osprd_info_t *d, struct request *req)
{
	// Code by Xi Han starts:
	int direction;
	size_t buffer_size;
	uint8_t *dev_offset;
	uint8_t *req_offset;
	if (!blk_fs_request(req)) {
		end_request(req, 0);
		return;
	}
	// Code by Xi Han ends.
	// EXERCISE: Perform the read or write request by copying data between
	// our data array and the request's buffer.
	// Hint: The 'struct request' argument tells you what kind of request
	// this is, and which sectors are being read or written.
	// Read about 'struct request' in <linux/blkdev.h>.
	// Consider the 'req->sector', 'req->current_nr_sectors', and
	// 'req->buffer' members, and the rq_data_dir() function.

	// Your code here.

	// Code by Xi Han starts:
	printk("TRYING TO ACQUIRE LOCK ...\n");
	osp_spin_lock(&d->mutex);
	printk("ACQUIRE LOCK\n");
	direction = (int)rq_data_dir(req);

	// 0 stands for a read from the device.
	// Nonzero stands for a write to the device.
	req_offset = (uint8_t *)req->buffer;
	dev_offset = d->data + req->sector * SECTOR_SIZE;
	buffer_size = req->current_nr_sectors * SECTOR_SIZE;

	// Error checking code.
	if (req->sector < 0 || 
	    req->sector + req->current_nr_sectors > nsectors) {
		end_request(req, 0);
		osp_spin_unlock(&d->mutex);
		return;
	}

	// Reading/Writing.
	if (direction == WRITE) { // Writing.
		memcpy(dev_offset, req_offset, buffer_size);
	} else { // Reading.
		memcpy(req_offset, dev_offset, buffer_size);
	}
	osp_spin_unlock(&d->mutex);
	printk("RELEASE LOCK");
	// Code by Xi Han ends.
	// eprintk("Should process request...\n");

	end_request(req, 1);
}


// This function is called when a /dev/osprdX file is opened.
// You aren't likely to need to change this.
static int osprd_open(struct inode *inode, struct file *filp)
{
	// Always set the O_SYNC flag. That way, we will get writes immediately
	// instead of waiting for them to get through write-back caches.
	filp->f_flags |= O_SYNC;
	return 0;
}


// This function is called when a /dev/osprdX file is finally closed.
// (If the file descriptor was dup2ed, this function is called only when the
// last copy is closed.)
static int osprd_close_last(struct inode *inode, struct file *filp)
{
	if (filp) {
		osprd_info_t *d = file2osprd(filp);
		int filp_writable = filp->f_mode & FMODE_WRITE;

		// EXERCISE: If the user closes a ramdisk file that holds
		// a lock, release the lock.  Also wake up blocked processes
		// as appropriate.

		// Your code here.
		
		// Code by Xi Han starts:
		printk("In osprd_close_last: \n");
		osp_spin_lock(&d->mutex);
		printk("ACQUIRE MUTEX\n");
		if (filp->f_flags & F_OSPRD_LOCKED) {
			if (filp_writable) {
				d->write_lock--;
			} else {
				d->read_lock--;
			}
			wake_up_all(&d->blockq);
			filp->f_flags &= !F_OSPRD_LOCKED;
		}
		osp_spin_unlock(&d->mutex);
		printk("RELEASE MUTEX\n");
		// Code by Xi Han ends.

		// This line avoids compiler warnings; you may remove it.
		(void) filp_writable, (void) d;

	}

	return 0;
}


/*
 * osprd_lock
 */

/*
 * osprd_ioctl(inode, filp, cmd, arg)
 *   Called to perform an ioctl on the named file.
 */
int osprd_ioctl(struct inode *inode, struct file *filp,
		unsigned int cmd, unsigned long arg)
{
	osprd_info_t *d = file2osprd(filp);	// device info
	int r = 0;			// return value: initially 0

	// is file open for writing?
	int filp_writable = (filp->f_mode & FMODE_WRITE) != 0;

	// This line avoids compiler warnings; you may remove it.
	(void) filp_writable, (void) d;
	printk("In osprd_ioctl\n");
	// Set 'r' to the ioctl's return value: 0 on success, negative on error

	if (cmd == OSPRDIOCACQUIRE) {

		// EXERCISE: Lock the ramdisk.
		//
		// If *filp is open for writing (filp_writable), then attempt
		// to write-lock the ramdisk; otherwise attempt to read-lock
		// the ramdisk.
		//
		
		// Code by SG starts
		// Check deadlocks before processing requests
		if(deadlock(d,filp_writable))
		{
			// Do not process R/W request if deadlock
			eprintk("Avoiding deadlock\n");
			return -EDEADLK;
		}
		// Code by SG ends
		
		// Code by Xi Han starts:
		printk("In OSPRDIOCACQUIRE:\n");
		unsigned local_ticket;
		int wei_retval;
		osp_spin_lock(&d->mutex);
		printk("ACQUIRE MUTEX\n");
		local_ticket = d->ticket_head++;
		printk("head: %u local: %u tail: %u\n", d->ticket_head, local_ticket,
		d->ticket_tail);
		osp_spin_unlock(&d->mutex);
		printk("RELEASE MUTEX\n");
		printk("write locks: %d\n", d->write_lock);
		printk("read locks: %d\n", d->read_lock);
		printk("%d\n", d->write_lock == 0 && (!filp_writable || d->read_lock ==
		0) && d->ticket_tail == local_ticket);
		wei_retval = wait_event_interruptible(d->blockq,
							d->write_lock == 0 &&
							(!filp_writable || d->read_lock == 0) &&
							d->ticket_tail == local_ticket);
		printk("wait returns: %d\n", wei_retval);
		if (wei_retval == -ERESTARTSYS) {
			osp_spin_lock(&d->mutex);
			printk("PROCESSING SIGNAL ... ADDING TICKET TO DEAD LIST\n");
			
			// If the process is killed. The ticket it holds expires.
			// We mark this by adding it to an array.
			d->dead_tickets[d->dead_tickets_length++] = local_ticket;
			if (d->dead_tickets_length == d->dead_tickets_capacity) {
				d->dead_tickets_capacity *= 2;
				unsigned *temp_dead_tickets = kzalloc(sizeof(unsigned) *
				d->dead_tickets_capacity, GFP_ATOMIC);
				memcpy(temp_dead_tickets, d->dead_tickets, sizeof(unsigned) *
				d->dead_tickets_length);
				kfree(d->dead_tickets);
				d->dead_tickets = temp_dead_tickets;
			}
			while (is_ticket_tail_dead(d)) {
				d->ticket_tail++;
			}
			osp_spin_unlock(&d->mutex);
			return -ERESTARTSYS;
		}

		osp_spin_lock(&d->mutex);
		printk("ACQUIRE MUTEX\n");
		if (filp_writable) 
		{
			// Code by SG starts
			// Check deadlocks before processing request
			if(deadlock(d,filp_writable))
			{
				// Do not process R/W request if deadlock
				eprintk("Avoiding deadlock\n");
				return -EDEADLK;
			}
			d->deadlock_write = 0;
			// Code by SG ends
			
			d->write_lock++;
		} 
		else 
		{
			// Code by SG starts
			// Check deadlocks before processing requests
			if(deadlock(d,filp_writable))
			{
				// Do not process R/W request if deadlock
				eprintk("Avoiding deadlock\n");
				return -EDEADLK;
			}
			d->deadlock_read = 0;
			// Code by SG ends
			
			d->read_lock++;
		}
		filp->f_flags |= F_OSPRD_LOCKED;
		do {
			printk("INCREMENTING TICKET TAIL ...\n");
			d->ticket_tail++;
		} while (is_ticket_tail_dead(d));
		osp_spin_unlock(&d->mutex);
		printk("RELEASE MUTEX\n");
		// Code by Xi Han ends.

        // This lock request must block using 'd->blockq' until:
		// 1) no other process holds a write lock;
		// 2) either the request is for a read lock, or no other process
		//    holds a read lock; and
		// 3) lock requests should be serviced in order, so no process
		//    that blocked earlier is still blocked waiting for the
		//    lock.
		//
		// If a process acquires a lock, mark this fact by setting
		// 'filp->f_flags |= F_OSPRD_LOCKED'.  You also need to
		// keep track of how many read and write locks are held:
		// change the 'osprd_info_t' structure to do this.
		//
		// Also wake up processes waiting on 'd->blockq' as needed.
		//
		// If the lock request would cause a deadlock, return -EDEADLK.
		// If the lock request blocks and is awoken by a signal, then
		// return -ERESTARTSYS.
		// Otherwise, if we can grant the lock request, return 0.

		// 'd->ticket_head' and 'd->ticket_tail' should help you
		// service lock requests in order.  These implement a ticket
		// order: 'ticket_tail' is the next ticket, and 'ticket_head'
		// is the ticket currently being served.  You should set a local
		// variable to 'd->ticket_head' and increment 'd->ticket_head'.
		// Then, block at least until 'd->ticket_tail == local_ticket'.
		// (Some of these operations are in a critical section and must
		// be protected by a spinlock; which ones?)

		// Your code here (instead of the next two lines).

	} else if (cmd == OSPRDIOCTRYACQUIRE) {

		// EXERCISE: ATTEMPT to lock the ramdisk.
		//
		// This is just like OSPRDIOCACQUIRE, except it should never
		// block.  If OSPRDIOCACQUIRE would block or return deadlock,
		// OSPRDIOCTRYACQUIRE should return -EBUSY.
		// Otherwise, if we can grant the lock request, return 0.

		// Your code here (instead of the next two lines).

		// Code by Xi Han starts:
		printk("In OSPRDIOCTRYACQUIRE:\n");
		osp_spin_lock(&d->mutex);
		printk("ACQUIRE MUTEX\n");
		if (d->write_lock == 0 &&
			(!filp_writable || d->read_lock == 0) &&
			d->ticket_tail == d->ticket_head) {

			if (filp_writable) {
				d->write_lock++;
			} else {
				d->read_lock++;
			}

			filp->f_flags |= F_OSPRD_LOCKED;
		} else {
			r = -EBUSY;
		}
		osp_spin_unlock(&d->mutex);
		printk("RELEASE MUTEX\n");
		// Code by Xi Han ends.

	} else if (cmd == OSPRDIOCRELEASE) {

		// EXERCISE: Unlock the ramdisk.
		//
		// If the file hasn't locked the ramdisk, return -EINVAL.
		// Otherwise, clear the lock from filp->f_flags, wake up
		// the wait queue, perform any additional accounting steps
		// you need, and return 0.

		// Your code here (instead of the next line).
		printk("In OSPRDIOCRELEASE:\n");
		osp_spin_lock(&d->mutex);
		printk("ACQUIRE MUTEX\n");
		if (!filp->f_flags & F_OSPRD_LOCKED) {
			r = -EINVAL;
		} else {
			if (filp_writable) {
				d->write_lock--;
			} else {
				d->read_lock--;
			}
			wake_up_all(&d->blockq);
			filp->f_flags &= !F_OSPRD_LOCKED;
		}
		osp_spin_unlock(&d->mutex);
		printk("RELEASE MUTEX\n");
	} else
		r = -ENOTTY; /* unknown command */
	return r;
}


// Initialize internal fields for an osprd_info_t.

static void osprd_setup(osprd_info_t *d)
{
	/* Initialize the wait queue. */
	init_waitqueue_head(&d->blockq);
	osp_spin_lock_init(&d->mutex);
	d->ticket_head = d->ticket_tail = 0;
	/* Add code here if you add fields to osprd_info_t. */
	// Code by Xi Han starts:
	// Initialization of additonal fields.
	d->read_lock = 0;
	d->write_lock = 0;
	d->dead_tickets = kzalloc(sizeof(unsigned) * ARRAY_INIT_SIZE, GFP_ATOMIC);
	d->dead_tickets_length = 0;
	d->dead_tickets_capacity = ARRAY_INIT_SIZE;
	// Code by Xi Han ends.
}

// Code by Xi Han starts:
// Simple linear search.
static int is_ticket_tail_dead(osprd_info_t *d) {
	size_t i;
	for (i = 0; i < d->dead_tickets_length; i++) {
		if (d->ticket_tail == d->dead_tickets[i]) {
			return 1;
		}
	}
	return 0;
}
// Code by Xi Han ends.
/*****************************************************************************/
/*         THERE IS NO NEED TO UNDERSTAND ANY CODE BELOW THIS LINE!          */
/*                                                                           */
/*****************************************************************************/

// Process a list of requests for a osprd_info_t.
// Calls osprd_process_request for each element of the queue.

static void osprd_process_request_queue(request_queue_t *q)
{
	osprd_info_t *d = (osprd_info_t *) q->queuedata;
	struct request *req;

	while ((req = elv_next_request(q)) != NULL)
		osprd_process_request(d, req);
}


// Some particularly horrible stuff to get around some Linux issues:
// the Linux block device interface doesn't let a block device find out
// which file has been closed.  We need this information.

static struct file_operations osprd_blk_fops;
static int (*blkdev_release)(struct inode *, struct file *);

static int _osprd_release(struct inode *inode, struct file *filp)
{
	if (file2osprd(filp))
		osprd_close_last(inode, filp);
	return (*blkdev_release)(inode, filp);
}

static int _osprd_open(struct inode *inode, struct file *filp)
{
	if (!osprd_blk_fops.open) {
		memcpy(&osprd_blk_fops, filp->f_op, sizeof(osprd_blk_fops));
		blkdev_release = osprd_blk_fops.release;
		osprd_blk_fops.release = _osprd_release;
	}
	filp->f_op = &osprd_blk_fops;
	return osprd_open(inode, filp);
}


// The device operations structure.

static struct block_device_operations osprd_ops = {
	.owner = THIS_MODULE,
	.open = _osprd_open,
	// .release = osprd_release, // we must call our own release
	.ioctl = osprd_ioctl
};


// Given an open file, check whether that file corresponds to an OSP ramdisk.
// If so, return a pointer to the ramdisk's osprd_info_t.
// If not, return NULL.

static osprd_info_t *file2osprd(struct file *filp)
{
	if (filp) {
		struct inode *ino = filp->f_dentry->d_inode;
		if (ino->i_bdev
		    && ino->i_bdev->bd_disk
		    && ino->i_bdev->bd_disk->major == OSPRD_MAJOR
		    && ino->i_bdev->bd_disk->fops == &osprd_ops)
			return (osprd_info_t *) ino->i_bdev->bd_disk->private_data;
	}
	return NULL;
}


// Call the function 'callback' with data 'user_data' for each of 'task's
// open files.

static void for_each_open_file(struct task_struct *task,
		  void (*callback)(struct file *filp, osprd_info_t *user_data),
		  osprd_info_t *user_data)
{
	int fd;
	task_lock(task);
	spin_lock(&task->files->file_lock);
	{
#if LINUX_VERSION_CODE <= KERNEL_VERSION(2, 6, 13)
		struct files_struct *f = task->files;
#else
		struct fdtable *f = task->files->fdt;
#endif
		for (fd = 0; fd < f->max_fds; fd++)
			if (f->fd[fd])
				(*callback)(f->fd[fd], user_data);
	}
	spin_unlock(&task->files->file_lock);
	task_unlock(task);
}


// Destroy a osprd_info_t.

static void cleanup_device(osprd_info_t *d)
{
	wake_up_all(&d->blockq);
	if (d->gd) {
		del_gendisk(d->gd);
		put_disk(d->gd);
	}
	if (d->queue)
		blk_cleanup_queue(d->queue);
	if (d->data)
		vfree(d->data);
}


// Initialize a osprd_info_t.

static int setup_device(osprd_info_t *d, int which)
{
	memset(d, 0, sizeof(osprd_info_t));

	/* Get memory to store the actual block data. */
	if (!(d->data = vmalloc(nsectors * SECTOR_SIZE)))
		return -1;
	memset(d->data, 0, nsectors * SECTOR_SIZE);

	/* Set up the I/O queue. */
	spin_lock_init(&d->qlock);
	if (!(d->queue = blk_init_queue(osprd_process_request_queue, &d->qlock)))
		return -1;
	blk_queue_hardsect_size(d->queue, SECTOR_SIZE);
	d->queue->queuedata = d;

	/* The gendisk structure. */
	if (!(d->gd = alloc_disk(1)))
		return -1;
	d->gd->major = OSPRD_MAJOR;
	d->gd->first_minor = which;
	d->gd->fops = &osprd_ops;
	d->gd->queue = d->queue;
	d->gd->private_data = d;
	snprintf(d->gd->disk_name, 32, "osprd%c", which + 'a');
	set_capacity(d->gd, nsectors);
	add_disk(d->gd);

	/* Call the setup function. */
	osprd_setup(d);

	return 0;
}

static void osprd_exit(void);


// The kernel calls this function when the module is loaded.
// It initializes the 4 osprd block devices.

static int __init osprd_init(void)
{
	int i, r;

	// shut up the compiler
	(void) for_each_open_file;
#ifndef osp_spin_lock
	(void) osp_spin_lock;
	(void) osp_spin_unlock;
#endif

	/* Register the block device name. */
	if (register_blkdev(OSPRD_MAJOR, "osprd") < 0) {
		printk(KERN_WARNING "osprd: unable to get major number\n");
		return -EBUSY;
	}

	/* Initialize the device structures. */
	for (i = r = 0; i < NOSPRD; i++)
		if (setup_device(&osprds[i], i) < 0)
			r = -EINVAL;

	if (r < 0) {
		printk(KERN_EMERG "osprd: can't set up device structures\n");
		osprd_exit();
		return -EBUSY;
	} else
		return 0;
}


// The kernel calls this function to unload the osprd module.
// It destroys the osprd devices.

static void osprd_exit(void)
{
	int i;
	for (i = 0; i < NOSPRD; i++)
		cleanup_device(&osprds[i]);
	unregister_blkdev(OSPRD_MAJOR, "osprd");
}


// Tell Linux to call those functions at init and exit time.
module_init(osprd_init);
module_exit(osprd_exit);
