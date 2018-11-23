/* my_module.c: Example char device module.
 *
 */
/* Kernel Programming */
#define MODULE
#define LINUX
#define __KERNEL__

#include <linux/kernel.h>  	
#include <linux/module.h>
#include <linux/fs.h>       		
#include <asm/uaccess.h>
#include <linux/errno.h>
#include <linux/slab.h>
#include <linux/sched.h>

#include "my_module.h"

#define MY_DEVICE "w19_device"
#define INIT_BUFF_SIZE 4096

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Anonymous");

struct file_operations my_fops = {
    .open = my_open,
    .release = my_release,
    .read = my_read,
    .write = my_write,
    .ioctl = my_ioctl
};

typedef struct deviceBuffer {
	char *mem_buffer;
	int minor;
	int r_seek;
	int w_seek;
	int write_flag;
	int read_flag;
	int open_flag;
	struct deviceBuffer *next; } devBuff;

// Aid function: a^b
int power(int a, int b) {
	int ret = 1;
	int i = 0;
	for (; i < b; i++) {
		ret = ret * a;
	}
	return ret;
}

/* globals */
int my_major = 0; /* will hold the major # of my device driver */
devBuff* top_buff;

int init_module(void)
{
    my_major = register_chrdev(my_major, MY_DEVICE, &my_fops);

    if (my_major < 0)
    {
	printk(KERN_WARNING "can't get dynamic major\n");
	return my_major;
    }

	top_buff = NULL;
    return 0;
}

void cleanup_module(void)
{
	devBuff* curr_device;
	devBuff* next_device = NULL;
	printk("%s: %s called\n", MY_DEVICE, __FUNCTION__);

	// clean-up all resources
	for (curr_device = top_buff; curr_device != NULL;) {
		next_device = curr_device->next;
		kfree(curr_device->mem_buffer);
		printk("%s: %s: freed buffer memory of minor device %d\n", __FUNCTION__, MY_DEVICE, curr_device->minor);
		kfree(curr_device);
		curr_device = next_device;
	}
	top_buff = NULL;

	int ret = unregister_chrdev(my_major, MY_DEVICE);
	if (ret < 0) {
		printk("%s: Error in unregister_chrdev: %d\n",MY_DEVICE, ret);
	}

    return;
}

int my_open(struct inode *inode, struct file *filp)
{
	int minor;
	devBuff *curr_device;
	devBuff *prev = NULL;

	printk("%s: %s called\n", MY_DEVICE, __FUNCTION__);
	MOD_INC_USE_COUNT;
	// Find minor of device in the list of devices
	minor = MINOR(inode->i_rdev);
	for (curr_device = top_buff; curr_device != NULL; curr_device = curr_device->next) {
		if (minor == curr_device->minor) {
			printk("%s: %s: device with minor %d was found\n", MY_DEVICE, __FUNCTION__, minor);
			break;
		}
		prev = curr_device;
	}
	// first time we open the device
	if (!curr_device) {
		printk("%s: %s: creating device with minor %d\n", MY_DEVICE, __FUNCTION__, minor);
		curr_device = kmalloc(sizeof(devBuff), GFP_KERNEL);
		if (!curr_device) {
			printk(KERN_WARNING "%s: %s: kmalloc failed", MY_DEVICE, __FUNCTION__);
			MOD_DEC_USE_COUNT;
			return -ENOMEM;
		}
		curr_device->mem_buffer = kmalloc(INIT_BUFF_SIZE, GFP_KERNEL);
		if (!curr_device->mem_buffer) {
			printk(KERN_WARNING "%s: %s: kmalloc failed for device buffer", MY_DEVICE, __FUNCTION__);
			kfree(curr_device);
			MOD_DEC_USE_COUNT;
			return -ENOMEM;
		}
		curr_device->w_seek = 0;
		curr_device->r_seek = 0;
		curr_device->write_flag = 0;
		curr_device->read_flag = 0;
		curr_device->minor = minor;
		curr_device->next = NULL;
		// update pointers of buffer in the list
		if (!top_buff) {
			top_buff = curr_device;
		}
		else if (prev) {
			prev->next = curr_device;
		}
	}

	curr_device->open_flag = 1;
	if (filp->f_mode & FMODE_READ) {
		curr_device->read_flag = 1;
	}

	if (filp->f_mode & FMODE_WRITE) {
		curr_device->write_flag = 1;
	}

	filp->private_data = curr_device;
    return 0;
}

int my_release(struct inode *inode, struct file *filp)
{
	devBuff* curr_device = filp->private_data;

	printk("%s: %s called\n", MY_DEVICE, __FUNCTION__);

	if (!curr_device->open_flag) {
		printk(KERN_WARNING "%s: %s no sense in closing a closed device\n", MY_DEVICE, __FUNCTION__);
		return -EBADF;
	}

	if (filp->f_mode & FMODE_READ) {
		curr_device->read_flag = 0;
	}

	if (filp->f_mode & FMODE_WRITE) {
		curr_device->write_flag = 0;
	}

	curr_device->open_flag = 0;

	printk("%s: %s done\n", MY_DEVICE, __FUNCTION__);
	MOD_DEC_USE_COUNT;
	return 0;
}

ssize_t my_read(struct file *filp, char *buf, size_t count, loff_t *f_pos)
{
	devBuff* curr_device = filp->private_data;
	int size_not_read;
	int ret_val;
	printk("%s: %s called\n", MY_DEVICE, __FUNCTION__);

	if (!curr_device->read_flag || !curr_device->read_flag) {
		printk(KERN_WARNING "%s: %s device is not open for read\n", MY_DEVICE, __FUNCTION__);
		return -EBADF;
	}

	// check the case where the written data is smaller than requested count
	if (curr_device->r_seek + count > curr_device->w_seek) {
		printk("%s: %s: tried to read more bytes than possible\n", MY_DEVICE, __FUNCTION__);
		count = curr_device->w_seek - curr_device->r_seek;
	}
	size_not_read = copy_to_user(buf, curr_device->mem_buffer + curr_device->r_seek, count);
	
	if (size_not_read !=0) {
		printk("%s: %s: not all bytes were copied\n", MY_DEVICE, __FUNCTION__);
		return -ENOMEM;
	}
	ret_val = count - size_not_read;
	curr_device->r_seek += ret_val;
	return ret_val; 
}

ssize_t my_write(struct file *filp, const char *buf, size_t count, loff_t *f_pos)
{
	devBuff *curr_device = filp->private_data;
	int size_not_write;
	int ret_val;
	struct task_struct* p = current;
	int curr_pid = p->pid;
	int temp = curr_pid;
	int pid_size = 0;

	printk("%s: %s called\n", MY_DEVICE, __FUNCTION__);

	if (!curr_device->open_flag || !curr_device->write_flag) {
		printk(KERN_WARNING "%s: %s device is not open for write\n", MY_DEVICE, __FUNCTION__);
		return -EBADF;
	}

	// Can't write empty string
	if (!count) {
		printk(KERN_WARNING "%s: %s can not write string of length 0\n", MY_DEVICE, __FUNCTION__);
		return -EINVAL;
	}

	// check how many digits in PID
	while (temp >= 1) {
		pid_size++;
		temp = temp / 10;
	}

	if (curr_device->w_seek + count > INIT_BUFF_SIZE) {
		printk(KERN_WARNING "%s: %s: tried to write more bytes than buffer size\n", MY_DEVICE, __FUNCTION__);

		// do reallocaiton
		char* temp_mem_buff = curr_device->mem_buffer;
		char* new_mem_buff = kmalloc((curr_device->w_seek) + count + pid_size + 6, GFP_KERNEL);
		printk(KERN_WARNING "%s: %s: before updating new buff, the curr device buff: %s\n", MY_DEVICE, __FUNCTION__, curr_device->mem_buffer);
		printk(KERN_WARNING "%s: %s: the buffer that we want to write: %s\n", MY_DEVICE, __FUNCTION__, buf);
		int i = 0;
		for (; i < curr_device->w_seek; i++) {
			new_mem_buff[i] = curr_device->mem_buffer[i];
		}
		
		kfree(temp_mem_buff);
		curr_device->mem_buffer = new_mem_buff;
		printk(KERN_WARNING "%s: %s: after updating new buff, the curr device buff: %s\n", MY_DEVICE, __FUNCTION__, curr_device->mem_buffer);
		if (!curr_device->mem_buffer) {
			printk(KERN_WARNING "%s: %s: malloc failed for device buffer", MY_DEVICE, __FUNCTION__);
			return -ENOMEM;
		}
	}
	curr_device->mem_buffer[curr_device->w_seek] = '[';
	temp = pid_size;
	int i = 1;
	for (; i <= pid_size; i++) {
		int temp2 = curr_pid / (power(10, temp - 1));
		curr_device->mem_buffer[curr_device->w_seek + i] = '0' + temp2;
		curr_pid = curr_pid - temp2 * power(10, temp - 1);
		temp--;
	}
	curr_device->mem_buffer[curr_device->w_seek + pid_size + 1] = ']';
	curr_device->mem_buffer[curr_device->w_seek + pid_size + 2] = ' ';

	size_not_write = copy_from_user(curr_device->mem_buffer + curr_device->w_seek + pid_size + 3, buf, count);
	if (size_not_write!=0) {
		printk("%s: %s: not all bytes were copied\n", MY_DEVICE, __FUNCTION__);
		return -ENOMEM;
	}

	ret_val = count - size_not_write;
	curr_device->mem_buffer[curr_device->w_seek + pid_size + 3 + ret_val] = '\n';
	curr_device->w_seek += ret_val + pid_size + 4;
	return ret_val; 
}

int my_ioctl(struct inode *inode, struct file *filp, unsigned int cmd, unsigned long arg)
{
	devBuff* curr_device = filp->private_data;
	printk("%s: %s called\n", MY_DEVICE, __FUNCTION__);

	switch (cmd)
	{
	case MY_RESET:
		printk("%s: %s: minor device %d called RESET\n", MY_DEVICE, __FUNCTION__, curr_device->minor);
		curr_device->w_seek = 0;
		curr_device->r_seek = 0;
		break;
	case MY_RESTART:
		printk("%s: %s: minor device %d called RESTART\n", MY_DEVICE, __FUNCTION__, curr_device->minor);
		curr_device->r_seek = 0;
		break;
	default:
		printk(KERN_WARNING "%s: %s: there is no ioctl %u\n",MY_DEVICE, __FUNCTION__, cmd);
		return -ENOTTY;
	}
	return 0;
}
