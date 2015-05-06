/* This is a stepper engine chardev driver with constant step time interval.
Copyright (C) 2015 Jan Cybulski

This program is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 2 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program.  If not, see <http://www.gnu.org/licenses/>.
*/
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/ioport.h>
#include <linux/gpio.h>
#include <linux/fs.h>
#include <linux/device.h>
#include <linux/cdev.h>
#include <asm/uaccess.h>
#include <linux/hrtimer.h>

#define MOD_AUTHOR "Jan Cybulski"
#define MOD_DESC "Stepper engine chardev driver with constant step time interval"
#define A1_PIN 4
#define A2_PIN 17
#define B1_PIN 22
#define B2_PIN 23

#define ENGINE_DRIVER_NAME "JCY_STEPPER_ENGINE_DRIVER"
#define DEVICE_NAME "stepper"




static struct gpio_chip *gpiochip;
static int engine_state=0;
static int engine_state_final=0;
static int device_opened=0;
static int temp_first_read;
struct cdev cdev;
struct class *class;
dev_t deviceMajMin;
static int steptime=20;//in milliseconds between two steps

struct hrtimer step_timer;

static int is_right_chip(struct gpio_chip *chip, void *data)
{
	printk (KERN_INFO ENGINE_DRIVER_NAME ": is_right_chip %s %d\n", chip->label, strcmp(data, chip->label));

	if (strcmp(data, chip->label) == 0)
		return 1;
	return 0;
}

static int device_open(struct inode *inode, struct file *file)
{
	if (device_opened)
		return -EBUSY;

	device_opened++;
	temp_first_read = 1;
	try_module_get(THIS_MODULE);

	return 0;
}

static int device_release(struct inode *inode, struct file *file)
{
	device_opened--;
	module_put(THIS_MODULE);
	return 0;
}

static ssize_t device_read(struct file *filp,	/* see include/linux/fs.h   */
				char *buffer,	/* buffer to fill with data */
				size_t length,	/* length of the buffer     */
				loff_t * offset)
{
	char innerBuf[10];
	int bytes_read = sprintf(innerBuf,"%d",engine_state);
	if(temp_first_read) {
		temp_first_read =0;
		if (copy_to_user(buffer, innerBuf, bytes_read)) {
			return -EFAULT;
		}
		return bytes_read;
	}
	else {
		return 0;
	}

}

static void run_timer(int period)
{
	ktime_t ktime;
	ktime = ktime_set(0, period*1000000);//0 seconds, period miliseconds
	hrtimer_start(&step_timer, ktime, HRTIMER_MODE_REL);
}

enum hrtimer_restart hrtimer_routine(struct hrtimer *timer)
{

	if (engine_state_final > engine_state)
		engine_state++;
	else if (engine_state_final < engine_state)
		engine_state--;
	switch (engine_state % 4) {
		case 0:
			gpiochip->set(gpiochip, A1_PIN, 1);
			gpiochip->set(gpiochip, A2_PIN, 1);
			gpiochip->set(gpiochip, B1_PIN, 1);
			gpiochip->set(gpiochip, B2_PIN, 0);
			break;
		case 1:
			gpiochip->set(gpiochip, A1_PIN, 1);
			gpiochip->set(gpiochip, A2_PIN, 0);
			gpiochip->set(gpiochip, B1_PIN, 1);
			gpiochip->set(gpiochip, B2_PIN, 1);
			break;
		case 2:
			gpiochip->set(gpiochip, A1_PIN, 1);
			gpiochip->set(gpiochip, A2_PIN, 1);
			gpiochip->set(gpiochip, B1_PIN, 0);
			gpiochip->set(gpiochip, B2_PIN, 1);
			break;
		case 3:
			gpiochip->set(gpiochip, A1_PIN, 0);
			gpiochip->set(gpiochip, A2_PIN, 1);
			gpiochip->set(gpiochip, B1_PIN, 1);
			gpiochip->set(gpiochip, B2_PIN, 1);
			break;
	}

	run_timer(steptime);
	return HRTIMER_NORESTART;
}


static ssize_t
device_write(struct file *filp, const char *buff, size_t len, loff_t *off)
{
	char innerBuf[10];

	if (len > 9)
		len = 9;

	if (copy_from_user(innerBuf, buff, len) ) {return -EFAULT;}
	sscanf(innerBuf,"%d",&engine_state_final);
	printk(KERN_INFO ENGINE_DRIVER_NAME ": engine state %d\n", engine_state_final);
	return len;
}


struct file_operations fops = {
	.read = device_read,
	.write = device_write,
	.open = device_open,
	.release = device_release
};

__init int engine_init(void){
	int ret;

	printk (KERN_INFO ENGINE_DRIVER_NAME ": init\n") ;

	gpiochip = gpiochip_find("bcm2708_gpio", is_right_chip);
	if (!gpiochip)
		return -ENODEV;

	if (gpio_request(A1_PIN, ENGINE_DRIVER_NAME "A1_PIN")) {
		printk(KERN_ALERT ENGINE_DRIVER_NAME
				": cant claim gpio pin %d\n", A1_PIN);
		ret = -ENODEV;
		goto exitA1;
	}
	if (gpio_request(A2_PIN, ENGINE_DRIVER_NAME "A2_PIN")) {
		printk(KERN_ALERT ENGINE_DRIVER_NAME
				": cant claim gpio pin %d\n", A2_PIN);
		ret = -ENODEV;
		goto exitA2;
	}
	if (gpio_request(B1_PIN, ENGINE_DRIVER_NAME "B1_PIN")) {
		printk(KERN_ALERT ENGINE_DRIVER_NAME
				": cant claim gpio pin %d\n", B1_PIN);
		ret = -ENODEV;
		goto exitB1;
	}
	if (gpio_request(B2_PIN, ENGINE_DRIVER_NAME "B2_PIN")) {
		printk(KERN_ALERT ENGINE_DRIVER_NAME
				": cant claim gpio pin %d\n", B2_PIN);
		ret = -ENODEV;
		goto exitB2;
	}
	printk(KERN_INFO ENGINE_DRIVER_NAME ": got all pins.\n");

	gpiochip->direction_output(gpiochip, A1_PIN, 1);
	gpiochip->set(gpiochip, A1_PIN, 1);
	gpiochip->direction_output(gpiochip, A2_PIN, 1);
	gpiochip->set(gpiochip, A2_PIN, 1);
	gpiochip->direction_output(gpiochip, B1_PIN, 1);
	gpiochip->set(gpiochip, B1_PIN, 1);
	gpiochip->direction_output(gpiochip, B1_PIN, 1);
	gpiochip->set(gpiochip, B2_PIN, 1);

	alloc_chrdev_region(&deviceMajMin, 0, 1, DEVICE_NAME);
	printk (KERN_INFO ENGINE_DRIVER_NAME ": major: %d\n", MAJOR(deviceMajMin));

	class = class_create(THIS_MODULE, DEVICE_NAME);
	cdev_init(&cdev, &fops);
	cdev.owner = THIS_MODULE;
	cdev_add(&cdev, deviceMajMin, 1);
	device_create(class, NULL, deviceMajMin, NULL, DEVICE_NAME);

	hrtimer_init(&step_timer, CLOCK_MONOTONIC, HRTIMER_MODE_REL);
	step_timer.function = &hrtimer_routine;
	run_timer(steptime);

	return 0;
exitB2:
	gpio_free(B1_PIN);
exitB1:
	gpio_free(A2_PIN);
exitA2:
	gpio_free(A1_PIN);
exitA1:
	return ret;

}
__exit void engine_exit(void) {
	printk ( KERN_INFO ENGINE_DRIVER_NAME ": exit\n" ) ;
	hrtimer_cancel(&step_timer);
	gpiochip->direction_output(gpiochip, A1_PIN, 0);
	gpiochip->direction_output(gpiochip, A2_PIN, 0);
	gpiochip->direction_output(gpiochip, B1_PIN, 0);
	gpiochip->direction_output(gpiochip, B2_PIN, 0);


	gpio_free(A1_PIN);
	gpio_free(A2_PIN);
	gpio_free(B1_PIN);
	gpio_free(B2_PIN);

	device_destroy(class, deviceMajMin);
	cdev_del(&cdev);
	class_destroy(class);

}


module_init ( engine_init ) ;
module_exit ( engine_exit ) ;
MODULE_LICENSE("GPL v2" ) ;
MODULE_AUTHOR( MOD_AUTHOR ) ;
MODULE_DESCRIPTION( MOD_DESC ) ;
