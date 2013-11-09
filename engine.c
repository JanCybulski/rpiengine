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
#define MOD_DESC "H-bridge connected DC Engine chardev driver based on soft PWM"
#define PWR_PIN 4
#define DIR1_PIN 17
#define DIR2_PIN 18

#define ENGINE_DRIVER_NAME "JCY_ENGINE_DRIVER"
#define DEVICE_NAME "engine"




static struct gpio_chip *gpiochip;
static int engine_state=0;
static int device_opened=0;
static int temp_first_read;
struct cdev cdev;
struct class *class;
dev_t deviceMajMin;

//static struct timer_list PWM_timer;
struct hrtimer PWM_timer;

static int is_right_chip(struct gpio_chip *chip, void *data)
{
	printk ( KERN_INFO ENGINE_DRIVER_NAME ": is_right_chip %s %d\n", chip->label, strcmp(data, chip->label));

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
	ktime = ktime_set( 0, period*1000000 );
	hrtimer_start( &PWM_timer, ktime, HRTIMER_MODE_REL );

}

enum hrtimer_restart hrtimer_routine(struct hrtimer *timer)
{

	static int state = 0;
	static const int engine_period = 100;
	int engine_state_mod = 0;
	//if (state == 1)
		engine_state_mod = engine_state >= 0 ? engine_state : -engine_state;
	if(engine_state == 0) {
		printk(KERN_INFO ENGINE_DRIVER_NAME ": \ttimer, full stop\n");
		gpiochip->set(gpiochip, PWR_PIN, 1);
		run_timer(engine_period);
	} else if (engine_state_mod == 100) {
		printk(KERN_INFO ENGINE_DRIVER_NAME ": \ttimer, full on\n");
		gpiochip->set(gpiochip, PWR_PIN, 0);
		run_timer(engine_period);
	} else {
		if (state == 0) {
		printk(KERN_INFO ENGINE_DRIVER_NAME ": \ttimer, on\n");
			run_timer(engine_state_mod);
			gpiochip->set(gpiochip, PWR_PIN, 0);
			state = 1;
		} else {
printk(KERN_INFO ENGINE_DRIVER_NAME ": \ttimer, off\n");
			run_timer(engine_period - engine_state_mod);
			gpiochip->set(gpiochip, PWR_PIN, 1);
			state = 0;
		}
	}
	return HRTIMER_NORESTART;
}

static void init_PWM_timer(void)
{

  hrtimer_init( &PWM_timer, CLOCK_MONOTONIC, HRTIMER_MODE_REL );
  
  PWM_timer.function = &hrtimer_routine;

  run_timer(100);


}

static void fini_PWM_timer(void)
{

	hrtimer_cancel( &PWM_timer );
}




static ssize_t
device_write(struct file *filp, const char *buff, size_t len, loff_t * off)
{
	char innerBuf[10];

	if (len > 9)
		len = 9;

	if (copy_from_user(innerBuf, buff, len) ) {return -EFAULT;}
	sscanf(innerBuf,"%d",&engine_state);
	if (engine_state > 100)
		engine_state = 100;
	if (engine_state < -100)
		engine_state = -100;
	printk(KERN_INFO ENGINE_DRIVER_NAME ": engine state %d\n", engine_state);
	
	if (engine_state > 0) {
		//gpiochip->set(gpiochip, PWR_PIN, 0);
		gpiochip->set(gpiochip, DIR1_PIN, 1);
		gpiochip->set(gpiochip, DIR2_PIN, 0);
	} else if (engine_state < 0) {
		//gpiochip->set(gpiochip, PWR_PIN, 0);
		gpiochip->set(gpiochip, DIR1_PIN, 0);
		gpiochip->set(gpiochip, DIR2_PIN, 1);
	} else if (engine_state == 0) {
		gpiochip->set(gpiochip, PWR_PIN, 1);
		gpiochip->set(gpiochip, DIR1_PIN, 1);
		gpiochip->set(gpiochip, DIR2_PIN, 0);
	}
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



	printk ( KERN_INFO ENGINE_DRIVER_NAME ": init\n" ) ;


	gpiochip = gpiochip_find("bcm2708_gpio", is_right_chip);
	if (!gpiochip)
		return -ENODEV;

/*request for power pin*/
	if (gpio_request(PWR_PIN, ENGINE_DRIVER_NAME "PWR_PIN")) {
		printk(KERN_ALERT ENGINE_DRIVER_NAME
				": cant claim gpio pin %d\n", PWR_PIN);
		ret = -ENODEV;
		goto exitP;
	}
/*request for direction pin #1*/
	if (gpio_request(DIR1_PIN, ENGINE_DRIVER_NAME "DIR1_PIN")) {
		printk(KERN_ALERT ENGINE_DRIVER_NAME
				": cant claim gpio pin %d\n", DIR1_PIN);
		ret = -ENODEV;
		goto exitD1;
	}
/*request for direction pin #2*/
	if (gpio_request(DIR2_PIN, ENGINE_DRIVER_NAME "DIR2_PIN")) {
		printk(KERN_ALERT ENGINE_DRIVER_NAME
				": cant claim gpio pin %d\n", DIR1_PIN);
		ret = -ENODEV;
		goto exitD2;
	}
	printk(KERN_INFO ENGINE_DRIVER_NAME ": got all pins.\n");

	gpiochip->direction_output(gpiochip, DIR1_PIN, 1);
	gpiochip->set(gpiochip, DIR1_PIN, 1);
	gpiochip->direction_output(gpiochip, DIR2_PIN, 1);
	gpiochip->set(gpiochip, DIR2_PIN, 0);
	gpiochip->direction_output(gpiochip, PWR_PIN, 1);
	gpiochip->set(gpiochip, PWR_PIN, 1);

	alloc_chrdev_region(&deviceMajMin, 0, 1, DEVICE_NAME);
	printk (KERN_INFO ENGINE_DRIVER_NAME ": major: %d\n", MAJOR(deviceMajMin));

	class = class_create(THIS_MODULE, DEVICE_NAME);
	cdev_init(&cdev, &fops);
	cdev.owner = THIS_MODULE;
	cdev_add(&cdev, deviceMajMin, 1);
	device_create(class, NULL, deviceMajMin, NULL, DEVICE_NAME);

	init_PWM_timer();


	return 0;
exitD2:
	gpio_free(DIR1_PIN);
exitD1:
	gpio_free(PWR_PIN);
exitP:
	return ret;

}
__exit void engine_exit(void) {
	printk ( KERN_INFO ENGINE_DRIVER_NAME ": exit\n" ) ;
	fini_PWM_timer();
	gpiochip->set(gpiochip, PWR_PIN, 1);
	gpiochip->set(gpiochip, DIR1_PIN, 1);
	gpiochip->set(gpiochip, DIR2_PIN, 0);
	gpiochip->direction_output(gpiochip, PWR_PIN, 0);
	gpiochip->direction_output(gpiochip, DIR1_PIN, 0);
	gpiochip->direction_output(gpiochip, DIR2_PIN, 0);


	gpio_free(PWR_PIN);
	gpio_free(DIR1_PIN);
	gpio_free(DIR2_PIN);

	device_destroy(class, deviceMajMin);
	cdev_del(&cdev);
	class_destroy(class);

}


module_init ( engine_init ) ;
module_exit ( engine_exit ) ;
MODULE_LICENSE("GPL v2" ) ;
MODULE_AUTHOR( MOD_AUTHOR ) ;
MODULE_DESCRIPTION( MOD_DESC ) ;
