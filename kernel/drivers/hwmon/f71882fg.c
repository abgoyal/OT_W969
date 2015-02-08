

#include <linux/module.h>
#include <linux/init.h>
#include <linux/slab.h>
#include <linux/jiffies.h>
#include <linux/platform_device.h>
#include <linux/hwmon.h>
#include <linux/hwmon-sysfs.h>
#include <linux/err.h>
#include <linux/mutex.h>
#include <linux/io.h>
#include <linux/acpi.h>

#define DRVNAME "f71882fg"

#define SIO_F71858FG_LD_HWM	0x02	/* Hardware monitor logical device */
#define SIO_F71882FG_LD_HWM	0x04	/* Hardware monitor logical device */
#define SIO_UNLOCK_KEY		0x87	/* Key to enable Super-I/O */
#define SIO_LOCK_KEY		0xAA	/* Key to diasble Super-I/O */

#define SIO_REG_LDSEL		0x07	/* Logical device select */
#define SIO_REG_DEVID		0x20	/* Device ID (2 bytes) */
#define SIO_REG_DEVREV		0x22	/* Device revision */
#define SIO_REG_MANID		0x23	/* Fintek ID (2 bytes) */
#define SIO_REG_ENABLE		0x30	/* Logical device enable */
#define SIO_REG_ADDR		0x60	/* Logical device address (2 bytes) */

#define SIO_FINTEK_ID		0x1934	/* Manufacturers ID */
#define SIO_F71858_ID		0x0507  /* Chipset ID */
#define SIO_F71862_ID		0x0601	/* Chipset ID */
#define SIO_F71882_ID		0x0541	/* Chipset ID */
#define SIO_F71889_ID		0x0723	/* Chipset ID */
#define SIO_F8000_ID		0x0581	/* Chipset ID */

#define REGION_LENGTH		8
#define ADDR_REG_OFFSET		5
#define DATA_REG_OFFSET		6

#define F71882FG_REG_PECI		0x0A

#define F71882FG_REG_IN_STATUS		0x12 /* f71882fg only */
#define F71882FG_REG_IN_BEEP		0x13 /* f71882fg only */
#define F71882FG_REG_IN(nr)		(0x20  + (nr))
#define F71882FG_REG_IN1_HIGH		0x32 /* f71882fg only */

#define F71882FG_REG_FAN(nr)		(0xA0 + (16 * (nr)))
#define F71882FG_REG_FAN_TARGET(nr)	(0xA2 + (16 * (nr)))
#define F71882FG_REG_FAN_FULL_SPEED(nr)	(0xA4 + (16 * (nr)))
#define F71882FG_REG_FAN_STATUS		0x92
#define F71882FG_REG_FAN_BEEP		0x93

#define F71882FG_REG_TEMP(nr)		(0x70 + 2 * (nr))
#define F71882FG_REG_TEMP_OVT(nr)	(0x80 + 2 * (nr))
#define F71882FG_REG_TEMP_HIGH(nr)	(0x81 + 2 * (nr))
#define F71882FG_REG_TEMP_STATUS	0x62
#define F71882FG_REG_TEMP_BEEP		0x63
#define F71882FG_REG_TEMP_CONFIG	0x69
#define F71882FG_REG_TEMP_HYST(nr)	(0x6C + (nr))
#define F71882FG_REG_TEMP_TYPE		0x6B
#define F71882FG_REG_TEMP_DIODE_OPEN	0x6F

#define F71882FG_REG_PWM(nr)		(0xA3 + (16 * (nr)))
#define F71882FG_REG_PWM_TYPE		0x94
#define F71882FG_REG_PWM_ENABLE		0x96

#define F71882FG_REG_FAN_HYST(nr)	(0x98 + (nr))

#define F71882FG_REG_POINT_PWM(pwm, point)	(0xAA + (point) + (16 * (pwm)))
#define F71882FG_REG_POINT_TEMP(pwm, point)	(0xA6 + (point) + (16 * (pwm)))
#define F71882FG_REG_POINT_MAPPING(nr)		(0xAF + 16 * (nr))

#define	F71882FG_REG_START		0x01

#define FAN_MIN_DETECT			366 /* Lowest detectable fanspeed */

static unsigned short force_id;
module_param(force_id, ushort, 0);
MODULE_PARM_DESC(force_id, "Override the detected device ID");

enum chips { f71858fg, f71862fg, f71882fg, f71889fg, f8000 };

static const char *f71882fg_names[] = {
	"f71858fg",
	"f71862fg",
	"f71882fg",
	"f71889fg",
	"f8000",
};

static struct platform_device *f71882fg_pdev;

/* Super-I/O Function prototypes */
static inline int superio_inb(int base, int reg);
static inline int superio_inw(int base, int reg);
static inline void superio_enter(int base);
static inline void superio_select(int base, int ld);
static inline void superio_exit(int base);

struct f71882fg_sio_data {
	enum chips type;
};

struct f71882fg_data {
	unsigned short addr;
	enum chips type;
	struct device *hwmon_dev;

	struct mutex update_lock;
	int temp_start;			/* temp numbering start (0 or 1) */
	char valid;			/* !=0 if following fields are valid */
	unsigned long last_updated;	/* In jiffies */
	unsigned long last_limits;	/* In jiffies */

	/* Register Values */
	u8	in[9];
	u8	in1_max;
	u8	in_status;
	u8	in_beep;
	u16	fan[4];
	u16	fan_target[4];
	u16	fan_full_speed[4];
	u8	fan_status;
	u8	fan_beep;
	/* Note: all models have only 3 temperature channels, but on some
	   they are addressed as 0-2 and on others as 1-3, so for coding
	   convenience we reserve space for 4 channels */
	u16	temp[4];
	u8	temp_ovt[4];
	u8	temp_high[4];
	u8	temp_hyst[2]; /* 2 hysts stored per reg */
	u8	temp_type[4];
	u8	temp_status;
	u8	temp_beep;
	u8	temp_diode_open;
	u8	temp_config;
	u8	pwm[4];
	u8	pwm_enable;
	u8	pwm_auto_point_hyst[2];
	u8	pwm_auto_point_mapping[4];
	u8	pwm_auto_point_pwm[4][5];
	s8	pwm_auto_point_temp[4][4];
};

/* Sysfs in */
static ssize_t show_in(struct device *dev, struct device_attribute *devattr,
	char *buf);
static ssize_t show_in_max(struct device *dev, struct device_attribute
	*devattr, char *buf);
static ssize_t store_in_max(struct device *dev, struct device_attribute
	*devattr, const char *buf, size_t count);
static ssize_t show_in_beep(struct device *dev, struct device_attribute
	*devattr, char *buf);
static ssize_t store_in_beep(struct device *dev, struct device_attribute
	*devattr, const char *buf, size_t count);
static ssize_t show_in_alarm(struct device *dev, struct device_attribute
	*devattr, char *buf);
/* Sysfs Fan */
static ssize_t show_fan(struct device *dev, struct device_attribute *devattr,
	char *buf);
static ssize_t show_fan_full_speed(struct device *dev,
	struct device_attribute *devattr, char *buf);
static ssize_t store_fan_full_speed(struct device *dev,
	struct device_attribute *devattr, const char *buf, size_t count);
static ssize_t show_fan_beep(struct device *dev, struct device_attribute
	*devattr, char *buf);
static ssize_t store_fan_beep(struct device *dev, struct device_attribute
	*devattr, const char *buf, size_t count);
static ssize_t show_fan_alarm(struct device *dev, struct device_attribute
	*devattr, char *buf);
/* Sysfs Temp */
static ssize_t show_temp(struct device *dev, struct device_attribute
	*devattr, char *buf);
static ssize_t show_temp_max(struct device *dev, struct device_attribute
	*devattr, char *buf);
static ssize_t store_temp_max(struct device *dev, struct device_attribute
	*devattr, const char *buf, size_t count);
static ssize_t show_temp_max_hyst(struct device *dev, struct device_attribute
	*devattr, char *buf);
static ssize_t store_temp_max_hyst(struct device *dev, struct device_attribute
	*devattr, const char *buf, size_t count);
static ssize_t show_temp_crit(struct device *dev, struct device_attribute
	*devattr, char *buf);
static ssize_t store_temp_crit(struct device *dev, struct device_attribute
	*devattr, const char *buf, size_t count);
static ssize_t show_temp_crit_hyst(struct device *dev, struct device_attribute
	*devattr, char *buf);
static ssize_t show_temp_type(struct device *dev, struct device_attribute
	*devattr, char *buf);
static ssize_t show_temp_beep(struct device *dev, struct device_attribute
	*devattr, char *buf);
static ssize_t store_temp_beep(struct device *dev, struct device_attribute
	*devattr, const char *buf, size_t count);
static ssize_t show_temp_alarm(struct device *dev, struct device_attribute
	*devattr, char *buf);
static ssize_t show_temp_fault(struct device *dev, struct device_attribute
	*devattr, char *buf);
/* PWM and Auto point control */
static ssize_t show_pwm(struct device *dev, struct device_attribute *devattr,
	char *buf);
static ssize_t store_pwm(struct device *dev, struct device_attribute *devattr,
	const char *buf, size_t count);
static ssize_t show_pwm_enable(struct device *dev,
	struct device_attribute *devattr, char *buf);
static ssize_t store_pwm_enable(struct device *dev,
	struct device_attribute	*devattr, const char *buf, size_t count);
static ssize_t show_pwm_interpolate(struct device *dev,
	struct device_attribute *devattr, char *buf);
static ssize_t store_pwm_interpolate(struct device *dev,
	struct device_attribute *devattr, const char *buf, size_t count);
static ssize_t show_pwm_auto_point_channel(struct device *dev,
	struct device_attribute *devattr, char *buf);
static ssize_t store_pwm_auto_point_channel(struct device *dev,
	struct device_attribute *devattr, const char *buf, size_t count);
static ssize_t show_pwm_auto_point_temp_hyst(struct device *dev,
	struct device_attribute *devattr, char *buf);
static ssize_t store_pwm_auto_point_temp_hyst(struct device *dev,
	struct device_attribute *devattr, const char *buf, size_t count);
static ssize_t show_pwm_auto_point_pwm(struct device *dev,
	struct device_attribute *devattr, char *buf);
static ssize_t store_pwm_auto_point_pwm(struct device *dev,
	struct device_attribute *devattr, const char *buf, size_t count);
static ssize_t show_pwm_auto_point_temp(struct device *dev,
	struct device_attribute *devattr, char *buf);
static ssize_t store_pwm_auto_point_temp(struct device *dev,
	struct device_attribute *devattr, const char *buf, size_t count);
/* Sysfs misc */
static ssize_t show_name(struct device *dev, struct device_attribute *devattr,
	char *buf);

static int __devinit f71882fg_probe(struct platform_device * pdev);
static int f71882fg_remove(struct platform_device *pdev);

static struct platform_driver f71882fg_driver = {
	.driver = {
		.owner	= THIS_MODULE,
		.name	= DRVNAME,
	},
	.probe		= f71882fg_probe,
	.remove		= f71882fg_remove,
};

static DEVICE_ATTR(name, S_IRUGO, show_name, NULL);

static struct sensor_device_attribute_2 f71858fg_in_temp_attr[] = {
	SENSOR_ATTR_2(in0_input, S_IRUGO, show_in, NULL, 0, 0),
	SENSOR_ATTR_2(in1_input, S_IRUGO, show_in, NULL, 0, 1),
	SENSOR_ATTR_2(in2_input, S_IRUGO, show_in, NULL, 0, 2),
	SENSOR_ATTR_2(temp1_input, S_IRUGO, show_temp, NULL, 0, 0),
	SENSOR_ATTR_2(temp1_max, S_IRUGO|S_IWUSR, show_temp_max,
		store_temp_max, 0, 0),
	SENSOR_ATTR_2(temp1_max_hyst, S_IRUGO|S_IWUSR, show_temp_max_hyst,
		store_temp_max_hyst, 0, 0),
	SENSOR_ATTR_2(temp1_max_alarm, S_IRUGO, show_temp_alarm, NULL, 0, 0),
	SENSOR_ATTR_2(temp1_crit, S_IRUGO|S_IWUSR, show_temp_crit,
		store_temp_crit, 0, 0),
	SENSOR_ATTR_2(temp1_crit_hyst, S_IRUGO, show_temp_crit_hyst, NULL,
		0, 0),
	SENSOR_ATTR_2(temp1_crit_alarm, S_IRUGO, show_temp_alarm, NULL, 0, 4),
	SENSOR_ATTR_2(temp1_fault, S_IRUGO, show_temp_fault, NULL, 0, 0),
	SENSOR_ATTR_2(temp2_input, S_IRUGO, show_temp, NULL, 0, 1),
	SENSOR_ATTR_2(temp2_max, S_IRUGO|S_IWUSR, show_temp_max,
		store_temp_max, 0, 1),
	SENSOR_ATTR_2(temp2_max_hyst, S_IRUGO|S_IWUSR, show_temp_max_hyst,
		store_temp_max_hyst, 0, 1),
	SENSOR_ATTR_2(temp2_max_alarm, S_IRUGO, show_temp_alarm, NULL, 0, 1),
	SENSOR_ATTR_2(temp2_crit, S_IRUGO|S_IWUSR, show_temp_crit,
		store_temp_crit, 0, 1),
	SENSOR_ATTR_2(temp2_crit_hyst, S_IRUGO, show_temp_crit_hyst, NULL,
		0, 1),
	SENSOR_ATTR_2(temp2_crit_alarm, S_IRUGO, show_temp_alarm, NULL, 0, 5),
	SENSOR_ATTR_2(temp2_type, S_IRUGO, show_temp_type, NULL, 0, 1),
	SENSOR_ATTR_2(temp2_fault, S_IRUGO, show_temp_fault, NULL, 0, 1),
	SENSOR_ATTR_2(temp3_input, S_IRUGO, show_temp, NULL, 0, 2),
	SENSOR_ATTR_2(temp3_max, S_IRUGO|S_IWUSR, show_temp_max,
		store_temp_max, 0, 2),
	SENSOR_ATTR_2(temp3_max_hyst, S_IRUGO|S_IWUSR, show_temp_max_hyst,
		store_temp_max_hyst, 0, 2),
	SENSOR_ATTR_2(temp3_max_alarm, S_IRUGO, show_temp_alarm, NULL, 0, 2),
	SENSOR_ATTR_2(temp3_crit, S_IRUGO|S_IWUSR, show_temp_crit,
		store_temp_crit, 0, 2),
	SENSOR_ATTR_2(temp3_crit_hyst, S_IRUGO, show_temp_crit_hyst, NULL,
		0, 2),
	SENSOR_ATTR_2(temp3_crit_alarm, S_IRUGO, show_temp_alarm, NULL, 0, 6),
	SENSOR_ATTR_2(temp3_fault, S_IRUGO, show_temp_fault, NULL, 0, 2),
};

/* Temp and in attr common to the f71862fg, f71882fg and f71889fg */
static struct sensor_device_attribute_2 fxxxx_in_temp_attr[] = {
	SENSOR_ATTR_2(in0_input, S_IRUGO, show_in, NULL, 0, 0),
	SENSOR_ATTR_2(in1_input, S_IRUGO, show_in, NULL, 0, 1),
	SENSOR_ATTR_2(in2_input, S_IRUGO, show_in, NULL, 0, 2),
	SENSOR_ATTR_2(in3_input, S_IRUGO, show_in, NULL, 0, 3),
	SENSOR_ATTR_2(in4_input, S_IRUGO, show_in, NULL, 0, 4),
	SENSOR_ATTR_2(in5_input, S_IRUGO, show_in, NULL, 0, 5),
	SENSOR_ATTR_2(in6_input, S_IRUGO, show_in, NULL, 0, 6),
	SENSOR_ATTR_2(in7_input, S_IRUGO, show_in, NULL, 0, 7),
	SENSOR_ATTR_2(in8_input, S_IRUGO, show_in, NULL, 0, 8),
	SENSOR_ATTR_2(temp1_input, S_IRUGO, show_temp, NULL, 0, 1),
	SENSOR_ATTR_2(temp1_max, S_IRUGO|S_IWUSR, show_temp_max,
		store_temp_max, 0, 1),
	SENSOR_ATTR_2(temp1_max_hyst, S_IRUGO|S_IWUSR, show_temp_max_hyst,
		store_temp_max_hyst, 0, 1),
	/* Should really be temp1_max_alarm, but older versions did not handle
	   the max and crit alarms separately and lm_sensors v2 depends on the
	   presence of temp#_alarm files. The same goes for temp2/3 _alarm. */
	SENSOR_ATTR_2(temp1_alarm, S_IRUGO, show_temp_alarm, NULL, 0, 1),
	SENSOR_ATTR_2(temp1_max_beep, S_IRUGO|S_IWUSR, show_temp_beep,
		store_temp_beep, 0, 1),
	SENSOR_ATTR_2(temp1_crit, S_IRUGO|S_IWUSR, show_temp_crit,
		store_temp_crit, 0, 1),
	SENSOR_ATTR_2(temp1_crit_hyst, S_IRUGO, show_temp_crit_hyst, NULL,
		0, 1),
	SENSOR_ATTR_2(temp1_crit_alarm, S_IRUGO, show_temp_alarm, NULL, 0, 5),
	SENSOR_ATTR_2(temp1_crit_beep, S_IRUGO|S_IWUSR, show_temp_beep,
		store_temp_beep, 0, 5),
	SENSOR_ATTR_2(temp1_type, S_IRUGO, show_temp_type, NULL, 0, 1),
	SENSOR_ATTR_2(temp1_fault, S_IRUGO, show_temp_fault, NULL, 0, 1),
	SENSOR_ATTR_2(temp2_input, S_IRUGO, show_temp, NULL, 0, 2),
	SENSOR_ATTR_2(temp2_max, S_IRUGO|S_IWUSR, show_temp_max,
		store_temp_max, 0, 2),
	SENSOR_ATTR_2(temp2_max_hyst, S_IRUGO|S_IWUSR, show_temp_max_hyst,
		store_temp_max_hyst, 0, 2),
	/* Should be temp2_max_alarm, see temp1_alarm note */
	SENSOR_ATTR_2(temp2_alarm, S_IRUGO, show_temp_alarm, NULL, 0, 2),
	SENSOR_ATTR_2(temp2_max_beep, S_IRUGO|S_IWUSR, show_temp_beep,
		store_temp_beep, 0, 2),
	SENSOR_ATTR_2(temp2_crit, S_IRUGO|S_IWUSR, show_temp_crit,
		store_temp_crit, 0, 2),
	SENSOR_ATTR_2(temp2_crit_hyst, S_IRUGO, show_temp_crit_hyst, NULL,
		0, 2),
	SENSOR_ATTR_2(temp2_crit_alarm, S_IRUGO, show_temp_alarm, NULL, 0, 6),
	SENSOR_ATTR_2(temp2_crit_beep, S_IRUGO|S_IWUSR, show_temp_beep,
		store_temp_beep, 0, 6),
	SENSOR_ATTR_2(temp2_type, S_IRUGO, show_temp_type, NULL, 0, 2),
	SENSOR_ATTR_2(temp2_fault, S_IRUGO, show_temp_fault, NULL, 0, 2),
	SENSOR_ATTR_2(temp3_input, S_IRUGO, show_temp, NULL, 0, 3),
	SENSOR_ATTR_2(temp3_max, S_IRUGO|S_IWUSR, show_temp_max,
		store_temp_max, 0, 3),
	SENSOR_ATTR_2(temp3_max_hyst, S_IRUGO|S_IWUSR, show_temp_max_hyst,
		store_temp_max_hyst, 0, 3),
	/* Should be temp3_max_alarm, see temp1_alarm note */
	SENSOR_ATTR_2(temp3_alarm, S_IRUGO, show_temp_alarm, NULL, 0, 3),
	SENSOR_ATTR_2(temp3_max_beep, S_IRUGO|S_IWUSR, show_temp_beep,
		store_temp_beep, 0, 3),
	SENSOR_ATTR_2(temp3_crit, S_IRUGO|S_IWUSR, show_temp_crit,
		store_temp_crit, 0, 3),
	SENSOR_ATTR_2(temp3_crit_hyst, S_IRUGO, show_temp_crit_hyst, NULL,
		0, 3),
	SENSOR_ATTR_2(temp3_crit_alarm, S_IRUGO, show_temp_alarm, NULL, 0, 7),
	SENSOR_ATTR_2(temp3_crit_beep, S_IRUGO|S_IWUSR, show_temp_beep,
		store_temp_beep, 0, 7),
	SENSOR_ATTR_2(temp3_type, S_IRUGO, show_temp_type, NULL, 0, 3),
	SENSOR_ATTR_2(temp3_fault, S_IRUGO, show_temp_fault, NULL, 0, 3),
};

/* For models with in1 alarm capability */
static struct sensor_device_attribute_2 fxxxx_in1_alarm_attr[] = {
	SENSOR_ATTR_2(in1_max, S_IRUGO|S_IWUSR, show_in_max, store_in_max,
		0, 1),
	SENSOR_ATTR_2(in1_beep, S_IRUGO|S_IWUSR, show_in_beep, store_in_beep,
		0, 1),
	SENSOR_ATTR_2(in1_alarm, S_IRUGO, show_in_alarm, NULL, 0, 1),
};

static struct sensor_device_attribute_2 f8000_in_temp_attr[] = {
	SENSOR_ATTR_2(in0_input, S_IRUGO, show_in, NULL, 0, 0),
	SENSOR_ATTR_2(in1_input, S_IRUGO, show_in, NULL, 0, 1),
	SENSOR_ATTR_2(in2_input, S_IRUGO, show_in, NULL, 0, 2),
	SENSOR_ATTR_2(temp1_input, S_IRUGO, show_temp, NULL, 0, 0),
	SENSOR_ATTR_2(temp1_max, S_IRUGO|S_IWUSR, show_temp_crit,
		store_temp_crit, 0, 0),
	SENSOR_ATTR_2(temp1_max_hyst, S_IRUGO|S_IWUSR, show_temp_max,
		store_temp_max, 0, 0),
	SENSOR_ATTR_2(temp1_alarm, S_IRUGO, show_temp_alarm, NULL, 0, 4),
	SENSOR_ATTR_2(temp1_fault, S_IRUGO, show_temp_fault, NULL, 0, 0),
	SENSOR_ATTR_2(temp2_input, S_IRUGO, show_temp, NULL, 0, 1),
	SENSOR_ATTR_2(temp2_max, S_IRUGO|S_IWUSR, show_temp_crit,
		store_temp_crit, 0, 1),
	SENSOR_ATTR_2(temp2_max_hyst, S_IRUGO|S_IWUSR, show_temp_max,
		store_temp_max, 0, 1),
	SENSOR_ATTR_2(temp2_alarm, S_IRUGO, show_temp_alarm, NULL, 0, 5),
	SENSOR_ATTR_2(temp2_type, S_IRUGO, show_temp_type, NULL, 0, 1),
	SENSOR_ATTR_2(temp2_fault, S_IRUGO, show_temp_fault, NULL, 0, 1),
	SENSOR_ATTR_2(temp3_input, S_IRUGO, show_temp, NULL, 0, 2),
	SENSOR_ATTR_2(temp3_max, S_IRUGO|S_IWUSR, show_temp_crit,
		store_temp_crit, 0, 2),
	SENSOR_ATTR_2(temp3_max_hyst, S_IRUGO|S_IWUSR, show_temp_max,
		store_temp_max, 0, 2),
	SENSOR_ATTR_2(temp3_alarm, S_IRUGO, show_temp_alarm, NULL, 0, 6),
	SENSOR_ATTR_2(temp3_fault, S_IRUGO, show_temp_fault, NULL, 0, 2),
};

/* Fan / PWM attr common to all models */
static struct sensor_device_attribute_2 fxxxx_fan_attr[4][6] = { {
	SENSOR_ATTR_2(fan1_input, S_IRUGO, show_fan, NULL, 0, 0),
	SENSOR_ATTR_2(fan1_full_speed, S_IRUGO|S_IWUSR,
		      show_fan_full_speed,
		      store_fan_full_speed, 0, 0),
	SENSOR_ATTR_2(fan1_alarm, S_IRUGO, show_fan_alarm, NULL, 0, 0),
	SENSOR_ATTR_2(pwm1, S_IRUGO|S_IWUSR, show_pwm, store_pwm, 0, 0),
	SENSOR_ATTR_2(pwm1_enable, S_IRUGO|S_IWUSR, show_pwm_enable,
		      store_pwm_enable, 0, 0),
	SENSOR_ATTR_2(pwm1_interpolate, S_IRUGO|S_IWUSR,
		      show_pwm_interpolate, store_pwm_interpolate, 0, 0),
}, {
	SENSOR_ATTR_2(fan2_input, S_IRUGO, show_fan, NULL, 0, 1),
	SENSOR_ATTR_2(fan2_full_speed, S_IRUGO|S_IWUSR,
		      show_fan_full_speed,
		      store_fan_full_speed, 0, 1),
	SENSOR_ATTR_2(fan2_alarm, S_IRUGO, show_fan_alarm, NULL, 0, 1),
	SENSOR_ATTR_2(pwm2, S_IRUGO|S_IWUSR, show_pwm, store_pwm, 0, 1),
	SENSOR_ATTR_2(pwm2_enable, S_IRUGO|S_IWUSR, show_pwm_enable,
		      store_pwm_enable, 0, 1),
	SENSOR_ATTR_2(pwm2_interpolate, S_IRUGO|S_IWUSR,
		      show_pwm_interpolate, store_pwm_interpolate, 0, 1),
}, {
	SENSOR_ATTR_2(fan3_input, S_IRUGO, show_fan, NULL, 0, 2),
	SENSOR_ATTR_2(fan3_full_speed, S_IRUGO|S_IWUSR,
		      show_fan_full_speed,
		      store_fan_full_speed, 0, 2),
	SENSOR_ATTR_2(fan3_alarm, S_IRUGO, show_fan_alarm, NULL, 0, 2),
	SENSOR_ATTR_2(pwm3, S_IRUGO|S_IWUSR, show_pwm, store_pwm, 0, 2),
	SENSOR_ATTR_2(pwm3_enable, S_IRUGO|S_IWUSR, show_pwm_enable,
		      store_pwm_enable, 0, 2),
	SENSOR_ATTR_2(pwm3_interpolate, S_IRUGO|S_IWUSR,
		      show_pwm_interpolate, store_pwm_interpolate, 0, 2),
}, {
	SENSOR_ATTR_2(fan4_input, S_IRUGO, show_fan, NULL, 0, 3),
	SENSOR_ATTR_2(fan4_full_speed, S_IRUGO|S_IWUSR,
		      show_fan_full_speed,
		      store_fan_full_speed, 0, 3),
	SENSOR_ATTR_2(fan4_alarm, S_IRUGO, show_fan_alarm, NULL, 0, 3),
	SENSOR_ATTR_2(pwm4, S_IRUGO|S_IWUSR, show_pwm, store_pwm, 0, 3),
	SENSOR_ATTR_2(pwm4_enable, S_IRUGO|S_IWUSR, show_pwm_enable,
		      store_pwm_enable, 0, 3),
	SENSOR_ATTR_2(pwm4_interpolate, S_IRUGO|S_IWUSR,
		      show_pwm_interpolate, store_pwm_interpolate, 0, 3),
} };

/* Attr for models which can beep on Fan alarm */
static struct sensor_device_attribute_2 fxxxx_fan_beep_attr[] = {
	SENSOR_ATTR_2(fan1_beep, S_IRUGO|S_IWUSR, show_fan_beep,
		store_fan_beep, 0, 0),
	SENSOR_ATTR_2(fan2_beep, S_IRUGO|S_IWUSR, show_fan_beep,
		store_fan_beep, 0, 1),
	SENSOR_ATTR_2(fan3_beep, S_IRUGO|S_IWUSR, show_fan_beep,
		store_fan_beep, 0, 2),
	SENSOR_ATTR_2(fan4_beep, S_IRUGO|S_IWUSR, show_fan_beep,
		store_fan_beep, 0, 3),
};

static struct sensor_device_attribute_2 f71862fg_auto_pwm_attr[] = {
	SENSOR_ATTR_2(pwm1_auto_channels_temp, S_IRUGO|S_IWUSR,
		      show_pwm_auto_point_channel,
		      store_pwm_auto_point_channel, 0, 0),
	SENSOR_ATTR_2(pwm1_auto_point1_pwm, S_IRUGO|S_IWUSR,
		      show_pwm_auto_point_pwm, store_pwm_auto_point_pwm,
		      1, 0),
	SENSOR_ATTR_2(pwm1_auto_point2_pwm, S_IRUGO|S_IWUSR,
		      show_pwm_auto_point_pwm, store_pwm_auto_point_pwm,
		      4, 0),
	SENSOR_ATTR_2(pwm1_auto_point1_temp, S_IRUGO|S_IWUSR,
		      show_pwm_auto_point_temp, store_pwm_auto_point_temp,
		      0, 0),
	SENSOR_ATTR_2(pwm1_auto_point2_temp, S_IRUGO|S_IWUSR,
		      show_pwm_auto_point_temp, store_pwm_auto_point_temp,
		      3, 0),
	SENSOR_ATTR_2(pwm1_auto_point1_temp_hyst, S_IRUGO|S_IWUSR,
		      show_pwm_auto_point_temp_hyst,
		      store_pwm_auto_point_temp_hyst,
		      0, 0),
	SENSOR_ATTR_2(pwm1_auto_point2_temp_hyst, S_IRUGO,
		      show_pwm_auto_point_temp_hyst, NULL, 3, 0),

	SENSOR_ATTR_2(pwm2_auto_channels_temp, S_IRUGO|S_IWUSR,
		      show_pwm_auto_point_channel,
		      store_pwm_auto_point_channel, 0, 1),
	SENSOR_ATTR_2(pwm2_auto_point1_pwm, S_IRUGO|S_IWUSR,
		      show_pwm_auto_point_pwm, store_pwm_auto_point_pwm,
		      1, 1),
	SENSOR_ATTR_2(pwm2_auto_point2_pwm, S_IRUGO|S_IWUSR,
		      show_pwm_auto_point_pwm, store_pwm_auto_point_pwm,
		      4, 1),
	SENSOR_ATTR_2(pwm2_auto_point1_temp, S_IRUGO|S_IWUSR,
		      show_pwm_auto_point_temp, store_pwm_auto_point_temp,
		      0, 1),
	SENSOR_ATTR_2(pwm2_auto_point2_temp, S_IRUGO|S_IWUSR,
		      show_pwm_auto_point_temp, store_pwm_auto_point_temp,
		      3, 1),
	SENSOR_ATTR_2(pwm2_auto_point1_temp_hyst, S_IRUGO|S_IWUSR,
		      show_pwm_auto_point_temp_hyst,
		      store_pwm_auto_point_temp_hyst,
		      0, 1),
	SENSOR_ATTR_2(pwm2_auto_point2_temp_hyst, S_IRUGO,
		      show_pwm_auto_point_temp_hyst, NULL, 3, 1),

	SENSOR_ATTR_2(pwm3_auto_channels_temp, S_IRUGO|S_IWUSR,
		      show_pwm_auto_point_channel,
		      store_pwm_auto_point_channel, 0, 2),
	SENSOR_ATTR_2(pwm3_auto_point1_pwm, S_IRUGO|S_IWUSR,
		      show_pwm_auto_point_pwm, store_pwm_auto_point_pwm,
		      1, 2),
	SENSOR_ATTR_2(pwm3_auto_point2_pwm, S_IRUGO|S_IWUSR,
		      show_pwm_auto_point_pwm, store_pwm_auto_point_pwm,
		      4, 2),
	SENSOR_ATTR_2(pwm3_auto_point1_temp, S_IRUGO|S_IWUSR,
		      show_pwm_auto_point_temp, store_pwm_auto_point_temp,
		      0, 2),
	SENSOR_ATTR_2(pwm3_auto_point2_temp, S_IRUGO|S_IWUSR,
		      show_pwm_auto_point_temp, store_pwm_auto_point_temp,
		      3, 2),
	SENSOR_ATTR_2(pwm3_auto_point1_temp_hyst, S_IRUGO|S_IWUSR,
		      show_pwm_auto_point_temp_hyst,
		      store_pwm_auto_point_temp_hyst,
		      0, 2),
	SENSOR_ATTR_2(pwm3_auto_point2_temp_hyst, S_IRUGO,
		      show_pwm_auto_point_temp_hyst, NULL, 3, 2),
};

/* PWM attr common to the f71858fg, f71882fg and f71889fg */
static struct sensor_device_attribute_2 fxxxx_auto_pwm_attr[4][14] = { {
	SENSOR_ATTR_2(pwm1_auto_channels_temp, S_IRUGO|S_IWUSR,
		      show_pwm_auto_point_channel,
		      store_pwm_auto_point_channel, 0, 0),
	SENSOR_ATTR_2(pwm1_auto_point1_pwm, S_IRUGO|S_IWUSR,
		      show_pwm_auto_point_pwm, store_pwm_auto_point_pwm,
		      0, 0),
	SENSOR_ATTR_2(pwm1_auto_point2_pwm, S_IRUGO|S_IWUSR,
		      show_pwm_auto_point_pwm, store_pwm_auto_point_pwm,
		      1, 0),
	SENSOR_ATTR_2(pwm1_auto_point3_pwm, S_IRUGO|S_IWUSR,
		      show_pwm_auto_point_pwm, store_pwm_auto_point_pwm,
		      2, 0),
	SENSOR_ATTR_2(pwm1_auto_point4_pwm, S_IRUGO|S_IWUSR,
		      show_pwm_auto_point_pwm, store_pwm_auto_point_pwm,
		      3, 0),
	SENSOR_ATTR_2(pwm1_auto_point5_pwm, S_IRUGO|S_IWUSR,
		      show_pwm_auto_point_pwm, store_pwm_auto_point_pwm,
		      4, 0),
	SENSOR_ATTR_2(pwm1_auto_point1_temp, S_IRUGO|S_IWUSR,
		      show_pwm_auto_point_temp, store_pwm_auto_point_temp,
		      0, 0),
	SENSOR_ATTR_2(pwm1_auto_point2_temp, S_IRUGO|S_IWUSR,
		      show_pwm_auto_point_temp, store_pwm_auto_point_temp,
		      1, 0),
	SENSOR_ATTR_2(pwm1_auto_point3_temp, S_IRUGO|S_IWUSR,
		      show_pwm_auto_point_temp, store_pwm_auto_point_temp,
		      2, 0),
	SENSOR_ATTR_2(pwm1_auto_point4_temp, S_IRUGO|S_IWUSR,
		      show_pwm_auto_point_temp, store_pwm_auto_point_temp,
		      3, 0),
	SENSOR_ATTR_2(pwm1_auto_point1_temp_hyst, S_IRUGO|S_IWUSR,
		      show_pwm_auto_point_temp_hyst,
		      store_pwm_auto_point_temp_hyst,
		      0, 0),
	SENSOR_ATTR_2(pwm1_auto_point2_temp_hyst, S_IRUGO,
		      show_pwm_auto_point_temp_hyst, NULL, 1, 0),
	SENSOR_ATTR_2(pwm1_auto_point3_temp_hyst, S_IRUGO,
		      show_pwm_auto_point_temp_hyst, NULL, 2, 0),
	SENSOR_ATTR_2(pwm1_auto_point4_temp_hyst, S_IRUGO,
		      show_pwm_auto_point_temp_hyst, NULL, 3, 0),
}, {
	SENSOR_ATTR_2(pwm2_auto_channels_temp, S_IRUGO|S_IWUSR,
		      show_pwm_auto_point_channel,
		      store_pwm_auto_point_channel, 0, 1),
	SENSOR_ATTR_2(pwm2_auto_point1_pwm, S_IRUGO|S_IWUSR,
		      show_pwm_auto_point_pwm, store_pwm_auto_point_pwm,
		      0, 1),
	SENSOR_ATTR_2(pwm2_auto_point2_pwm, S_IRUGO|S_IWUSR,
		      show_pwm_auto_point_pwm, store_pwm_auto_point_pwm,
		      1, 1),
	SENSOR_ATTR_2(pwm2_auto_point3_pwm, S_IRUGO|S_IWUSR,
		      show_pwm_auto_point_pwm, store_pwm_auto_point_pwm,
		      2, 1),
	SENSOR_ATTR_2(pwm2_auto_point4_pwm, S_IRUGO|S_IWUSR,
		      show_pwm_auto_point_pwm, store_pwm_auto_point_pwm,
		      3, 1),
	SENSOR_ATTR_2(pwm2_auto_point5_pwm, S_IRUGO|S_IWUSR,
		      show_pwm_auto_point_pwm, store_pwm_auto_point_pwm,
		      4, 1),
	SENSOR_ATTR_2(pwm2_auto_point1_temp, S_IRUGO|S_IWUSR,
		      show_pwm_auto_point_temp, store_pwm_auto_point_temp,
		      0, 1),
	SENSOR_ATTR_2(pwm2_auto_point2_temp, S_IRUGO|S_IWUSR,
		      show_pwm_auto_point_temp, store_pwm_auto_point_temp,
		      1, 1),
	SENSOR_ATTR_2(pwm2_auto_point3_temp, S_IRUGO|S_IWUSR,
		      show_pwm_auto_point_temp, store_pwm_auto_point_temp,
		      2, 1),
	SENSOR_ATTR_2(pwm2_auto_point4_temp, S_IRUGO|S_IWUSR,
		      show_pwm_auto_point_temp, store_pwm_auto_point_temp,
		      3, 1),
	SENSOR_ATTR_2(pwm2_auto_point1_temp_hyst, S_IRUGO|S_IWUSR,
		      show_pwm_auto_point_temp_hyst,
		      store_pwm_auto_point_temp_hyst,
		      0, 1),
	SENSOR_ATTR_2(pwm2_auto_point2_temp_hyst, S_IRUGO,
		      show_pwm_auto_point_temp_hyst, NULL, 1, 1),
	SENSOR_ATTR_2(pwm2_auto_point3_temp_hyst, S_IRUGO,
		      show_pwm_auto_point_temp_hyst, NULL, 2, 1),
	SENSOR_ATTR_2(pwm2_auto_point4_temp_hyst, S_IRUGO,
		      show_pwm_auto_point_temp_hyst, NULL, 3, 1),
}, {
	SENSOR_ATTR_2(pwm3_auto_channels_temp, S_IRUGO|S_IWUSR,
		      show_pwm_auto_point_channel,
		      store_pwm_auto_point_channel, 0, 2),
	SENSOR_ATTR_2(pwm3_auto_point1_pwm, S_IRUGO|S_IWUSR,
		      show_pwm_auto_point_pwm, store_pwm_auto_point_pwm,
		      0, 2),
	SENSOR_ATTR_2(pwm3_auto_point2_pwm, S_IRUGO|S_IWUSR,
		      show_pwm_auto_point_pwm, store_pwm_auto_point_pwm,
		      1, 2),
	SENSOR_ATTR_2(pwm3_auto_point3_pwm, S_IRUGO|S_IWUSR,
		      show_pwm_auto_point_pwm, store_pwm_auto_point_pwm,
		      2, 2),
	SENSOR_ATTR_2(pwm3_auto_point4_pwm, S_IRUGO|S_IWUSR,
		      show_pwm_auto_point_pwm, store_pwm_auto_point_pwm,
		      3, 2),
	SENSOR_ATTR_2(pwm3_auto_point5_pwm, S_IRUGO|S_IWUSR,
		      show_pwm_auto_point_pwm, store_pwm_auto_point_pwm,
		      4, 2),
	SENSOR_ATTR_2(pwm3_auto_point1_temp, S_IRUGO|S_IWUSR,
		      show_pwm_auto_point_temp, store_pwm_auto_point_temp,
		      0, 2),
	SENSOR_ATTR_2(pwm3_auto_point2_temp, S_IRUGO|S_IWUSR,
		      show_pwm_auto_point_temp, store_pwm_auto_point_temp,
		      1, 2),
	SENSOR_ATTR_2(pwm3_auto_point3_temp, S_IRUGO|S_IWUSR,
		      show_pwm_auto_point_temp, store_pwm_auto_point_temp,
		      2, 2),
	SENSOR_ATTR_2(pwm3_auto_point4_temp, S_IRUGO|S_IWUSR,
		      show_pwm_auto_point_temp, store_pwm_auto_point_temp,
		      3, 2),
	SENSOR_ATTR_2(pwm3_auto_point1_temp_hyst, S_IRUGO|S_IWUSR,
		      show_pwm_auto_point_temp_hyst,
		      store_pwm_auto_point_temp_hyst,
		      0, 2),
	SENSOR_ATTR_2(pwm3_auto_point2_temp_hyst, S_IRUGO,
		      show_pwm_auto_point_temp_hyst, NULL, 1, 2),
	SENSOR_ATTR_2(pwm3_auto_point3_temp_hyst, S_IRUGO,
		      show_pwm_auto_point_temp_hyst, NULL, 2, 2),
	SENSOR_ATTR_2(pwm3_auto_point4_temp_hyst, S_IRUGO,
		      show_pwm_auto_point_temp_hyst, NULL, 3, 2),
}, {
	SENSOR_ATTR_2(pwm4_auto_channels_temp, S_IRUGO|S_IWUSR,
		      show_pwm_auto_point_channel,
		      store_pwm_auto_point_channel, 0, 3),
	SENSOR_ATTR_2(pwm4_auto_point1_pwm, S_IRUGO|S_IWUSR,
		      show_pwm_auto_point_pwm, store_pwm_auto_point_pwm,
		      0, 3),
	SENSOR_ATTR_2(pwm4_auto_point2_pwm, S_IRUGO|S_IWUSR,
		      show_pwm_auto_point_pwm, store_pwm_auto_point_pwm,
		      1, 3),
	SENSOR_ATTR_2(pwm4_auto_point3_pwm, S_IRUGO|S_IWUSR,
		      show_pwm_auto_point_pwm, store_pwm_auto_point_pwm,
		      2, 3),
	SENSOR_ATTR_2(pwm4_auto_point4_pwm, S_IRUGO|S_IWUSR,
		      show_pwm_auto_point_pwm, store_pwm_auto_point_pwm,
		      3, 3),
	SENSOR_ATTR_2(pwm4_auto_point5_pwm, S_IRUGO|S_IWUSR,
		      show_pwm_auto_point_pwm, store_pwm_auto_point_pwm,
		      4, 3),
	SENSOR_ATTR_2(pwm4_auto_point1_temp, S_IRUGO|S_IWUSR,
		      show_pwm_auto_point_temp, store_pwm_auto_point_temp,
		      0, 3),
	SENSOR_ATTR_2(pwm4_auto_point2_temp, S_IRUGO|S_IWUSR,
		      show_pwm_auto_point_temp, store_pwm_auto_point_temp,
		      1, 3),
	SENSOR_ATTR_2(pwm4_auto_point3_temp, S_IRUGO|S_IWUSR,
		      show_pwm_auto_point_temp, store_pwm_auto_point_temp,
		      2, 3),
	SENSOR_ATTR_2(pwm4_auto_point4_temp, S_IRUGO|S_IWUSR,
		      show_pwm_auto_point_temp, store_pwm_auto_point_temp,
		      3, 3),
	SENSOR_ATTR_2(pwm4_auto_point1_temp_hyst, S_IRUGO|S_IWUSR,
		      show_pwm_auto_point_temp_hyst,
		      store_pwm_auto_point_temp_hyst,
		      0, 3),
	SENSOR_ATTR_2(pwm4_auto_point2_temp_hyst, S_IRUGO,
		      show_pwm_auto_point_temp_hyst, NULL, 1, 3),
	SENSOR_ATTR_2(pwm4_auto_point3_temp_hyst, S_IRUGO,
		      show_pwm_auto_point_temp_hyst, NULL, 2, 3),
	SENSOR_ATTR_2(pwm4_auto_point4_temp_hyst, S_IRUGO,
		      show_pwm_auto_point_temp_hyst, NULL, 3, 3),
} };

/* Fan attr specific to the f8000 (4th fan input can only measure speed) */
static struct sensor_device_attribute_2 f8000_fan_attr[] = {
	SENSOR_ATTR_2(fan4_input, S_IRUGO, show_fan, NULL, 0, 3),
};

static struct sensor_device_attribute_2 f8000_auto_pwm_attr[] = {
	SENSOR_ATTR_2(pwm1_auto_channels_temp, S_IRUGO|S_IWUSR,
		      show_pwm_auto_point_channel,
		      store_pwm_auto_point_channel, 0, 0),
	SENSOR_ATTR_2(temp1_auto_point1_pwm, S_IRUGO|S_IWUSR,
		      show_pwm_auto_point_pwm, store_pwm_auto_point_pwm,
		      0, 2),
	SENSOR_ATTR_2(temp1_auto_point2_pwm, S_IRUGO|S_IWUSR,
		      show_pwm_auto_point_pwm, store_pwm_auto_point_pwm,
		      1, 2),
	SENSOR_ATTR_2(temp1_auto_point3_pwm, S_IRUGO|S_IWUSR,
		      show_pwm_auto_point_pwm, store_pwm_auto_point_pwm,
		      2, 2),
	SENSOR_ATTR_2(temp1_auto_point4_pwm, S_IRUGO|S_IWUSR,
		      show_pwm_auto_point_pwm, store_pwm_auto_point_pwm,
		      3, 2),
	SENSOR_ATTR_2(temp1_auto_point5_pwm, S_IRUGO|S_IWUSR,
		      show_pwm_auto_point_pwm, store_pwm_auto_point_pwm,
		      4, 2),
	SENSOR_ATTR_2(temp1_auto_point1_temp, S_IRUGO|S_IWUSR,
		      show_pwm_auto_point_temp, store_pwm_auto_point_temp,
		      0, 2),
	SENSOR_ATTR_2(temp1_auto_point2_temp, S_IRUGO|S_IWUSR,
		      show_pwm_auto_point_temp, store_pwm_auto_point_temp,
		      1, 2),
	SENSOR_ATTR_2(temp1_auto_point3_temp, S_IRUGO|S_IWUSR,
		      show_pwm_auto_point_temp, store_pwm_auto_point_temp,
		      2, 2),
	SENSOR_ATTR_2(temp1_auto_point4_temp, S_IRUGO|S_IWUSR,
		      show_pwm_auto_point_temp, store_pwm_auto_point_temp,
		      3, 2),
	SENSOR_ATTR_2(temp1_auto_point1_temp_hyst, S_IRUGO|S_IWUSR,
		      show_pwm_auto_point_temp_hyst,
		      store_pwm_auto_point_temp_hyst,
		      0, 2),
	SENSOR_ATTR_2(temp1_auto_point2_temp_hyst, S_IRUGO,
		      show_pwm_auto_point_temp_hyst, NULL, 1, 2),
	SENSOR_ATTR_2(temp1_auto_point3_temp_hyst, S_IRUGO,
		      show_pwm_auto_point_temp_hyst, NULL, 2, 2),
	SENSOR_ATTR_2(temp1_auto_point4_temp_hyst, S_IRUGO,
		      show_pwm_auto_point_temp_hyst, NULL, 3, 2),

	SENSOR_ATTR_2(pwm2_auto_channels_temp, S_IRUGO|S_IWUSR,
		      show_pwm_auto_point_channel,
		      store_pwm_auto_point_channel, 0, 1),
	SENSOR_ATTR_2(temp2_auto_point1_pwm, S_IRUGO|S_IWUSR,
		      show_pwm_auto_point_pwm, store_pwm_auto_point_pwm,
		      0, 0),
	SENSOR_ATTR_2(temp2_auto_point2_pwm, S_IRUGO|S_IWUSR,
		      show_pwm_auto_point_pwm, store_pwm_auto_point_pwm,
		      1, 0),
	SENSOR_ATTR_2(temp2_auto_point3_pwm, S_IRUGO|S_IWUSR,
		      show_pwm_auto_point_pwm, store_pwm_auto_point_pwm,
		      2, 0),
	SENSOR_ATTR_2(temp2_auto_point4_pwm, S_IRUGO|S_IWUSR,
		      show_pwm_auto_point_pwm, store_pwm_auto_point_pwm,
		      3, 0),
	SENSOR_ATTR_2(temp2_auto_point5_pwm, S_IRUGO|S_IWUSR,
		      show_pwm_auto_point_pwm, store_pwm_auto_point_pwm,
		      4, 0),
	SENSOR_ATTR_2(temp2_auto_point1_temp, S_IRUGO|S_IWUSR,
		      show_pwm_auto_point_temp, store_pwm_auto_point_temp,
		      0, 0),
	SENSOR_ATTR_2(temp2_auto_point2_temp, S_IRUGO|S_IWUSR,
		      show_pwm_auto_point_temp, store_pwm_auto_point_temp,
		      1, 0),
	SENSOR_ATTR_2(temp2_auto_point3_temp, S_IRUGO|S_IWUSR,
		      show_pwm_auto_point_temp, store_pwm_auto_point_temp,
		      2, 0),
	SENSOR_ATTR_2(temp2_auto_point4_temp, S_IRUGO|S_IWUSR,
		      show_pwm_auto_point_temp, store_pwm_auto_point_temp,
		      3, 0),
	SENSOR_ATTR_2(temp2_auto_point1_temp_hyst, S_IRUGO|S_IWUSR,
		      show_pwm_auto_point_temp_hyst,
		      store_pwm_auto_point_temp_hyst,
		      0, 0),
	SENSOR_ATTR_2(temp2_auto_point2_temp_hyst, S_IRUGO,
		      show_pwm_auto_point_temp_hyst, NULL, 1, 0),
	SENSOR_ATTR_2(temp2_auto_point3_temp_hyst, S_IRUGO,
		      show_pwm_auto_point_temp_hyst, NULL, 2, 0),
	SENSOR_ATTR_2(temp2_auto_point4_temp_hyst, S_IRUGO,
		      show_pwm_auto_point_temp_hyst, NULL, 3, 0),

	SENSOR_ATTR_2(pwm3_auto_channels_temp, S_IRUGO|S_IWUSR,
		      show_pwm_auto_point_channel,
		      store_pwm_auto_point_channel, 0, 2),
	SENSOR_ATTR_2(temp3_auto_point1_pwm, S_IRUGO|S_IWUSR,
		      show_pwm_auto_point_pwm, store_pwm_auto_point_pwm,
		      0, 1),
	SENSOR_ATTR_2(temp3_auto_point2_pwm, S_IRUGO|S_IWUSR,
		      show_pwm_auto_point_pwm, store_pwm_auto_point_pwm,
		      1, 1),
	SENSOR_ATTR_2(temp3_auto_point3_pwm, S_IRUGO|S_IWUSR,
		      show_pwm_auto_point_pwm, store_pwm_auto_point_pwm,
		      2, 1),
	SENSOR_ATTR_2(temp3_auto_point4_pwm, S_IRUGO|S_IWUSR,
		      show_pwm_auto_point_pwm, store_pwm_auto_point_pwm,
		      3, 1),
	SENSOR_ATTR_2(temp3_auto_point5_pwm, S_IRUGO|S_IWUSR,
		      show_pwm_auto_point_pwm, store_pwm_auto_point_pwm,
		      4, 1),
	SENSOR_ATTR_2(temp3_auto_point1_temp, S_IRUGO|S_IWUSR,
		      show_pwm_auto_point_temp, store_pwm_auto_point_temp,
		      0, 1),
	SENSOR_ATTR_2(temp3_auto_point2_temp, S_IRUGO|S_IWUSR,
		      show_pwm_auto_point_temp, store_pwm_auto_point_temp,
		      1, 1),
	SENSOR_ATTR_2(temp3_auto_point3_temp, S_IRUGO|S_IWUSR,
		      show_pwm_auto_point_temp, store_pwm_auto_point_temp,
		      2, 1),
	SENSOR_ATTR_2(temp3_auto_point4_temp, S_IRUGO|S_IWUSR,
		      show_pwm_auto_point_temp, store_pwm_auto_point_temp,
		      3, 1),
	SENSOR_ATTR_2(temp3_auto_point1_temp_hyst, S_IRUGO|S_IWUSR,
		      show_pwm_auto_point_temp_hyst,
		      store_pwm_auto_point_temp_hyst,
		      0, 1),
	SENSOR_ATTR_2(temp3_auto_point2_temp_hyst, S_IRUGO,
		      show_pwm_auto_point_temp_hyst, NULL, 1, 1),
	SENSOR_ATTR_2(temp3_auto_point3_temp_hyst, S_IRUGO,
		      show_pwm_auto_point_temp_hyst, NULL, 2, 1),
	SENSOR_ATTR_2(temp3_auto_point4_temp_hyst, S_IRUGO,
		      show_pwm_auto_point_temp_hyst, NULL, 3, 1),
};

/* Super I/O functions */
static inline int superio_inb(int base, int reg)
{
	outb(reg, base);
	return inb(base + 1);
}

static int superio_inw(int base, int reg)
{
	int val;
	val  = superio_inb(base, reg) << 8;
	val |= superio_inb(base, reg + 1);
	return val;
}

static inline void superio_enter(int base)
{
	/* according to the datasheet the key must be send twice! */
	outb(SIO_UNLOCK_KEY, base);
	outb(SIO_UNLOCK_KEY, base);
}

static inline void superio_select(int base, int ld)
{
	outb(SIO_REG_LDSEL, base);
	outb(ld, base + 1);
}

static inline void superio_exit(int base)
{
	outb(SIO_LOCK_KEY, base);
}

static inline int fan_from_reg(u16 reg)
{
	return reg ? (1500000 / reg) : 0;
}

static inline u16 fan_to_reg(int fan)
{
	return fan ? (1500000 / fan) : 0;
}

static u8 f71882fg_read8(struct f71882fg_data *data, u8 reg)
{
	u8 val;

	outb(reg, data->addr + ADDR_REG_OFFSET);
	val = inb(data->addr + DATA_REG_OFFSET);

	return val;
}

static u16 f71882fg_read16(struct f71882fg_data *data, u8 reg)
{
	u16 val;

	val  = f71882fg_read8(data, reg) << 8;
	val |= f71882fg_read8(data, reg + 1);

	return val;
}

static void f71882fg_write8(struct f71882fg_data *data, u8 reg, u8 val)
{
	outb(reg, data->addr + ADDR_REG_OFFSET);
	outb(val, data->addr + DATA_REG_OFFSET);
}

static void f71882fg_write16(struct f71882fg_data *data, u8 reg, u16 val)
{
	f71882fg_write8(data, reg,     val >> 8);
	f71882fg_write8(data, reg + 1, val & 0xff);
}

static u16 f71882fg_read_temp(struct f71882fg_data *data, int nr)
{
	if (data->type == f71858fg)
		return f71882fg_read16(data, F71882FG_REG_TEMP(nr));
	else
		return f71882fg_read8(data, F71882FG_REG_TEMP(nr));
}

static struct f71882fg_data *f71882fg_update_device(struct device *dev)
{
	struct f71882fg_data *data = dev_get_drvdata(dev);
	int nr, reg = 0, reg2;
	int nr_fans = (data->type == f71882fg) ? 4 : 3;
	int nr_ins = (data->type == f71858fg || data->type == f8000) ? 3 : 9;

	mutex_lock(&data->update_lock);

	/* Update once every 60 seconds */
	if (time_after(jiffies, data->last_limits + 60 * HZ) ||
			!data->valid) {
		if (data->type == f71882fg || data->type == f71889fg) {
			data->in1_max =
				f71882fg_read8(data, F71882FG_REG_IN1_HIGH);
			data->in_beep =
				f71882fg_read8(data, F71882FG_REG_IN_BEEP);
		}

		/* Get High & boundary temps*/
		for (nr = data->temp_start; nr < 3 + data->temp_start; nr++) {
			data->temp_ovt[nr] = f71882fg_read8(data,
						F71882FG_REG_TEMP_OVT(nr));
			data->temp_high[nr] = f71882fg_read8(data,
						F71882FG_REG_TEMP_HIGH(nr));
		}

		if (data->type != f8000) {
			data->temp_hyst[0] = f71882fg_read8(data,
						F71882FG_REG_TEMP_HYST(0));
			data->temp_hyst[1] = f71882fg_read8(data,
						F71882FG_REG_TEMP_HYST(1));
		}

		if (data->type == f71862fg || data->type == f71882fg ||
		    data->type == f71889fg) {
			data->fan_beep = f71882fg_read8(data,
						F71882FG_REG_FAN_BEEP);
			data->temp_beep = f71882fg_read8(data,
						F71882FG_REG_TEMP_BEEP);
			/* Have to hardcode type, because temp1 is special */
			reg  = f71882fg_read8(data, F71882FG_REG_TEMP_TYPE);
			data->temp_type[2] = (reg & 0x04) ? 2 : 4;
			data->temp_type[3] = (reg & 0x08) ? 2 : 4;
		}
		/* Determine temp index 1 sensor type */
		if (data->type == f71889fg) {
			reg2 = f71882fg_read8(data, F71882FG_REG_START);
			switch ((reg2 & 0x60) >> 5) {
			case 0x00: /* BJT / Thermistor */
				data->temp_type[1] = (reg & 0x02) ? 2 : 4;
				break;
			case 0x01: /* AMDSI */
				data->temp_type[1] = 5;
				break;
			case 0x02: /* PECI */
			case 0x03: /* Ibex Peak ?? Report as PECI for now */
				data->temp_type[1] = 6;
				break;
			}
		} else {
			reg2 = f71882fg_read8(data, F71882FG_REG_PECI);
			if ((reg2 & 0x03) == 0x01)
				data->temp_type[1] = 6; /* PECI */
			else if ((reg2 & 0x03) == 0x02)
				data->temp_type[1] = 5; /* AMDSI */
			else if (data->type == f71862fg ||
				 data->type == f71882fg)
				data->temp_type[1] = (reg & 0x02) ? 2 : 4;
			else /* f71858fg and f8000 only support BJT */
				data->temp_type[1] = 2;
		}

		data->pwm_enable = f71882fg_read8(data,
						  F71882FG_REG_PWM_ENABLE);
		data->pwm_auto_point_hyst[0] =
			f71882fg_read8(data, F71882FG_REG_FAN_HYST(0));
		data->pwm_auto_point_hyst[1] =
			f71882fg_read8(data, F71882FG_REG_FAN_HYST(1));

		for (nr = 0; nr < nr_fans; nr++) {
			data->pwm_auto_point_mapping[nr] =
			    f71882fg_read8(data,
					   F71882FG_REG_POINT_MAPPING(nr));

			if (data->type != f71862fg) {
				int point;
				for (point = 0; point < 5; point++) {
					data->pwm_auto_point_pwm[nr][point] =
						f71882fg_read8(data,
							F71882FG_REG_POINT_PWM
							(nr, point));
				}
				for (point = 0; point < 4; point++) {
					data->pwm_auto_point_temp[nr][point] =
						f71882fg_read8(data,
							F71882FG_REG_POINT_TEMP
							(nr, point));
				}
			} else {
				data->pwm_auto_point_pwm[nr][1] =
					f71882fg_read8(data,
						F71882FG_REG_POINT_PWM
						(nr, 1));
				data->pwm_auto_point_pwm[nr][4] =
					f71882fg_read8(data,
						F71882FG_REG_POINT_PWM
						(nr, 4));
				data->pwm_auto_point_temp[nr][0] =
					f71882fg_read8(data,
						F71882FG_REG_POINT_TEMP
						(nr, 0));
				data->pwm_auto_point_temp[nr][3] =
					f71882fg_read8(data,
						F71882FG_REG_POINT_TEMP
						(nr, 3));
			}
		}
		data->last_limits = jiffies;
	}

	/* Update every second */
	if (time_after(jiffies, data->last_updated + HZ) || !data->valid) {
		data->temp_status = f71882fg_read8(data,
						F71882FG_REG_TEMP_STATUS);
		data->temp_diode_open = f71882fg_read8(data,
						F71882FG_REG_TEMP_DIODE_OPEN);
		for (nr = data->temp_start; nr < 3 + data->temp_start; nr++)
			data->temp[nr] = f71882fg_read_temp(data, nr);

		data->fan_status = f71882fg_read8(data,
						F71882FG_REG_FAN_STATUS);
		for (nr = 0; nr < nr_fans; nr++) {
			data->fan[nr] = f71882fg_read16(data,
						F71882FG_REG_FAN(nr));
			data->fan_target[nr] =
			    f71882fg_read16(data, F71882FG_REG_FAN_TARGET(nr));
			data->fan_full_speed[nr] =
			    f71882fg_read16(data,
					    F71882FG_REG_FAN_FULL_SPEED(nr));
			data->pwm[nr] =
			    f71882fg_read8(data, F71882FG_REG_PWM(nr));
		}

		/* The f8000 can monitor 1 more fan, but has no pwm for it */
		if (data->type == f8000)
			data->fan[3] = f71882fg_read16(data,
						F71882FG_REG_FAN(3));
		if (data->type == f71882fg || data->type == f71889fg)
			data->in_status = f71882fg_read8(data,
						F71882FG_REG_IN_STATUS);
		for (nr = 0; nr < nr_ins; nr++)
			data->in[nr] = f71882fg_read8(data,
						F71882FG_REG_IN(nr));

		data->last_updated = jiffies;
		data->valid = 1;
	}

	mutex_unlock(&data->update_lock);

	return data;
}

/* Sysfs Interface */
static ssize_t show_fan(struct device *dev, struct device_attribute *devattr,
	char *buf)
{
	struct f71882fg_data *data = f71882fg_update_device(dev);
	int nr = to_sensor_dev_attr_2(devattr)->index;
	int speed = fan_from_reg(data->fan[nr]);

	if (speed == FAN_MIN_DETECT)
		speed = 0;

	return sprintf(buf, "%d\n", speed);
}

static ssize_t show_fan_full_speed(struct device *dev,
				   struct device_attribute *devattr, char *buf)
{
	struct f71882fg_data *data = f71882fg_update_device(dev);
	int nr = to_sensor_dev_attr_2(devattr)->index;
	int speed = fan_from_reg(data->fan_full_speed[nr]);
	return sprintf(buf, "%d\n", speed);
}

static ssize_t store_fan_full_speed(struct device *dev,
				    struct device_attribute *devattr,
				    const char *buf, size_t count)
{
	struct f71882fg_data *data = dev_get_drvdata(dev);
	int err, nr = to_sensor_dev_attr_2(devattr)->index;
	long val;

	err = strict_strtol(buf, 10, &val);
	if (err)
		return err;

	val = SENSORS_LIMIT(val, 23, 1500000);
	val = fan_to_reg(val);

	mutex_lock(&data->update_lock);
	f71882fg_write16(data, F71882FG_REG_FAN_FULL_SPEED(nr), val);
	data->fan_full_speed[nr] = val;
	mutex_unlock(&data->update_lock);

	return count;
}

static ssize_t show_fan_beep(struct device *dev, struct device_attribute
	*devattr, char *buf)
{
	struct f71882fg_data *data = f71882fg_update_device(dev);
	int nr = to_sensor_dev_attr_2(devattr)->index;

	if (data->fan_beep & (1 << nr))
		return sprintf(buf, "1\n");
	else
		return sprintf(buf, "0\n");
}

static ssize_t store_fan_beep(struct device *dev, struct device_attribute
	*devattr, const char *buf, size_t count)
{
	struct f71882fg_data *data = dev_get_drvdata(dev);
	int err, nr = to_sensor_dev_attr_2(devattr)->index;
	unsigned long val;

	err = strict_strtoul(buf, 10, &val);
	if (err)
		return err;

	mutex_lock(&data->update_lock);
	data->fan_beep = f71882fg_read8(data, F71882FG_REG_FAN_BEEP);
	if (val)
		data->fan_beep |= 1 << nr;
	else
		data->fan_beep &= ~(1 << nr);

	f71882fg_write8(data, F71882FG_REG_FAN_BEEP, data->fan_beep);
	mutex_unlock(&data->update_lock);

	return count;
}

static ssize_t show_fan_alarm(struct device *dev, struct device_attribute
	*devattr, char *buf)
{
	struct f71882fg_data *data = f71882fg_update_device(dev);
	int nr = to_sensor_dev_attr_2(devattr)->index;

	if (data->fan_status & (1 << nr))
		return sprintf(buf, "1\n");
	else
		return sprintf(buf, "0\n");
}

static ssize_t show_in(struct device *dev, struct device_attribute *devattr,
	char *buf)
{
	struct f71882fg_data *data = f71882fg_update_device(dev);
	int nr = to_sensor_dev_attr_2(devattr)->index;

	return sprintf(buf, "%d\n", data->in[nr] * 8);
}

static ssize_t show_in_max(struct device *dev, struct device_attribute
	*devattr, char *buf)
{
	struct f71882fg_data *data = f71882fg_update_device(dev);

	return sprintf(buf, "%d\n", data->in1_max * 8);
}

static ssize_t store_in_max(struct device *dev, struct device_attribute
	*devattr, const char *buf, size_t count)
{
	struct f71882fg_data *data = dev_get_drvdata(dev);
	int err;
	long val;

	err = strict_strtol(buf, 10, &val);
	if (err)
		return err;

	val /= 8;
	val = SENSORS_LIMIT(val, 0, 255);

	mutex_lock(&data->update_lock);
	f71882fg_write8(data, F71882FG_REG_IN1_HIGH, val);
	data->in1_max = val;
	mutex_unlock(&data->update_lock);

	return count;
}

static ssize_t show_in_beep(struct device *dev, struct device_attribute
	*devattr, char *buf)
{
	struct f71882fg_data *data = f71882fg_update_device(dev);
	int nr = to_sensor_dev_attr_2(devattr)->index;

	if (data->in_beep & (1 << nr))
		return sprintf(buf, "1\n");
	else
		return sprintf(buf, "0\n");
}

static ssize_t store_in_beep(struct device *dev, struct device_attribute
	*devattr, const char *buf, size_t count)
{
	struct f71882fg_data *data = dev_get_drvdata(dev);
	int err, nr = to_sensor_dev_attr_2(devattr)->index;
	unsigned long val;

	err = strict_strtoul(buf, 10, &val);
	if (err)
		return err;

	mutex_lock(&data->update_lock);
	data->in_beep = f71882fg_read8(data, F71882FG_REG_IN_BEEP);
	if (val)
		data->in_beep |= 1 << nr;
	else
		data->in_beep &= ~(1 << nr);

	f71882fg_write8(data, F71882FG_REG_IN_BEEP, data->in_beep);
	mutex_unlock(&data->update_lock);

	return count;
}

static ssize_t show_in_alarm(struct device *dev, struct device_attribute
	*devattr, char *buf)
{
	struct f71882fg_data *data = f71882fg_update_device(dev);
	int nr = to_sensor_dev_attr_2(devattr)->index;

	if (data->in_status & (1 << nr))
		return sprintf(buf, "1\n");
	else
		return sprintf(buf, "0\n");
}

static ssize_t show_temp(struct device *dev, struct device_attribute *devattr,
	char *buf)
{
	struct f71882fg_data *data = f71882fg_update_device(dev);
	int nr = to_sensor_dev_attr_2(devattr)->index;
	int sign, temp;

	if (data->type == f71858fg) {
		/* TEMP_TABLE_SEL 1 or 3 ? */
		if (data->temp_config & 1) {
			sign = data->temp[nr] & 0x0001;
			temp = (data->temp[nr] >> 5) & 0x7ff;
		} else {
			sign = data->temp[nr] & 0x8000;
			temp = (data->temp[nr] >> 5) & 0x3ff;
		}
		temp *= 125;
		if (sign)
			temp -= 128000;
	} else
		temp = data->temp[nr] * 1000;

	return sprintf(buf, "%d\n", temp);
}

static ssize_t show_temp_max(struct device *dev, struct device_attribute
	*devattr, char *buf)
{
	struct f71882fg_data *data = f71882fg_update_device(dev);
	int nr = to_sensor_dev_attr_2(devattr)->index;

	return sprintf(buf, "%d\n", data->temp_high[nr] * 1000);
}

static ssize_t store_temp_max(struct device *dev, struct device_attribute
	*devattr, const char *buf, size_t count)
{
	struct f71882fg_data *data = dev_get_drvdata(dev);
	int err, nr = to_sensor_dev_attr_2(devattr)->index;
	long val;

	err = strict_strtol(buf, 10, &val);
	if (err)
		return err;

	val /= 1000;
	val = SENSORS_LIMIT(val, 0, 255);

	mutex_lock(&data->update_lock);
	f71882fg_write8(data, F71882FG_REG_TEMP_HIGH(nr), val);
	data->temp_high[nr] = val;
	mutex_unlock(&data->update_lock);

	return count;
}

static ssize_t show_temp_max_hyst(struct device *dev, struct device_attribute
	*devattr, char *buf)
{
	struct f71882fg_data *data = f71882fg_update_device(dev);
	int nr = to_sensor_dev_attr_2(devattr)->index;
	int temp_max_hyst;

	mutex_lock(&data->update_lock);
	if (nr & 1)
		temp_max_hyst = data->temp_hyst[nr / 2] >> 4;
	else
		temp_max_hyst = data->temp_hyst[nr / 2] & 0x0f;
	temp_max_hyst = (data->temp_high[nr] - temp_max_hyst) * 1000;
	mutex_unlock(&data->update_lock);

	return sprintf(buf, "%d\n", temp_max_hyst);
}

static ssize_t store_temp_max_hyst(struct device *dev, struct device_attribute
	*devattr, const char *buf, size_t count)
{
	struct f71882fg_data *data = dev_get_drvdata(dev);
	int err, nr = to_sensor_dev_attr_2(devattr)->index;
	ssize_t ret = count;
	u8 reg;
	long val;

	err = strict_strtol(buf, 10, &val);
	if (err)
		return err;

	val /= 1000;

	mutex_lock(&data->update_lock);

	/* convert abs to relative and check */
	data->temp_high[nr] = f71882fg_read8(data, F71882FG_REG_TEMP_HIGH(nr));
	val = SENSORS_LIMIT(val, data->temp_high[nr] - 15,
			    data->temp_high[nr]);
	val = data->temp_high[nr] - val;

	/* convert value to register contents */
	reg = f71882fg_read8(data, F71882FG_REG_TEMP_HYST(nr / 2));
	if (nr & 1)
		reg = (reg & 0x0f) | (val << 4);
	else
		reg = (reg & 0xf0) | val;
	f71882fg_write8(data, F71882FG_REG_TEMP_HYST(nr / 2), reg);
	data->temp_hyst[nr / 2] = reg;

	mutex_unlock(&data->update_lock);
	return ret;
}

static ssize_t show_temp_crit(struct device *dev, struct device_attribute
	*devattr, char *buf)
{
	struct f71882fg_data *data = f71882fg_update_device(dev);
	int nr = to_sensor_dev_attr_2(devattr)->index;

	return sprintf(buf, "%d\n", data->temp_ovt[nr] * 1000);
}

static ssize_t store_temp_crit(struct device *dev, struct device_attribute
	*devattr, const char *buf, size_t count)
{
	struct f71882fg_data *data = dev_get_drvdata(dev);
	int err, nr = to_sensor_dev_attr_2(devattr)->index;
	long val;

	err = strict_strtol(buf, 10, &val);
	if (err)
		return err;

	val /= 1000;
	val = SENSORS_LIMIT(val, 0, 255);

	mutex_lock(&data->update_lock);
	f71882fg_write8(data, F71882FG_REG_TEMP_OVT(nr), val);
	data->temp_ovt[nr] = val;
	mutex_unlock(&data->update_lock);

	return count;
}

static ssize_t show_temp_crit_hyst(struct device *dev, struct device_attribute
	*devattr, char *buf)
{
	struct f71882fg_data *data = f71882fg_update_device(dev);
	int nr = to_sensor_dev_attr_2(devattr)->index;
	int temp_crit_hyst;

	mutex_lock(&data->update_lock);
	if (nr & 1)
		temp_crit_hyst = data->temp_hyst[nr / 2] >> 4;
	else
		temp_crit_hyst = data->temp_hyst[nr / 2] & 0x0f;
	temp_crit_hyst = (data->temp_ovt[nr] - temp_crit_hyst) * 1000;
	mutex_unlock(&data->update_lock);

	return sprintf(buf, "%d\n", temp_crit_hyst);
}

static ssize_t show_temp_type(struct device *dev, struct device_attribute
	*devattr, char *buf)
{
	struct f71882fg_data *data = f71882fg_update_device(dev);
	int nr = to_sensor_dev_attr_2(devattr)->index;

	return sprintf(buf, "%d\n", data->temp_type[nr]);
}

static ssize_t show_temp_beep(struct device *dev, struct device_attribute
	*devattr, char *buf)
{
	struct f71882fg_data *data = f71882fg_update_device(dev);
	int nr = to_sensor_dev_attr_2(devattr)->index;

	if (data->temp_beep & (1 << nr))
		return sprintf(buf, "1\n");
	else
		return sprintf(buf, "0\n");
}

static ssize_t store_temp_beep(struct device *dev, struct device_attribute
	*devattr, const char *buf, size_t count)
{
	struct f71882fg_data *data = dev_get_drvdata(dev);
	int err, nr = to_sensor_dev_attr_2(devattr)->index;
	unsigned long val;

	err = strict_strtoul(buf, 10, &val);
	if (err)
		return err;

	mutex_lock(&data->update_lock);
	data->temp_beep = f71882fg_read8(data, F71882FG_REG_TEMP_BEEP);
	if (val)
		data->temp_beep |= 1 << nr;
	else
		data->temp_beep &= ~(1 << nr);

	f71882fg_write8(data, F71882FG_REG_TEMP_BEEP, data->temp_beep);
	mutex_unlock(&data->update_lock);

	return count;
}

static ssize_t show_temp_alarm(struct device *dev, struct device_attribute
	*devattr, char *buf)
{
	struct f71882fg_data *data = f71882fg_update_device(dev);
	int nr = to_sensor_dev_attr_2(devattr)->index;

	if (data->temp_status & (1 << nr))
		return sprintf(buf, "1\n");
	else
		return sprintf(buf, "0\n");
}

static ssize_t show_temp_fault(struct device *dev, struct device_attribute
	*devattr, char *buf)
{
	struct f71882fg_data *data = f71882fg_update_device(dev);
	int nr = to_sensor_dev_attr_2(devattr)->index;

	if (data->temp_diode_open & (1 << nr))
		return sprintf(buf, "1\n");
	else
		return sprintf(buf, "0\n");
}

static ssize_t show_pwm(struct device *dev,
			struct device_attribute *devattr, char *buf)
{
	struct f71882fg_data *data = f71882fg_update_device(dev);
	int val, nr = to_sensor_dev_attr_2(devattr)->index;
	mutex_lock(&data->update_lock);
	if (data->pwm_enable & (1 << (2 * nr)))
		/* PWM mode */
		val = data->pwm[nr];
	else {
		/* RPM mode */
		val = 255 * fan_from_reg(data->fan_target[nr])
			/ fan_from_reg(data->fan_full_speed[nr]);
	}
	mutex_unlock(&data->update_lock);
	return sprintf(buf, "%d\n", val);
}

static ssize_t store_pwm(struct device *dev,
			 struct device_attribute *devattr, const char *buf,
			 size_t count)
{
	struct f71882fg_data *data = dev_get_drvdata(dev);
	int err, nr = to_sensor_dev_attr_2(devattr)->index;
	long val;

	err = strict_strtol(buf, 10, &val);
	if (err)
		return err;

	val = SENSORS_LIMIT(val, 0, 255);

	mutex_lock(&data->update_lock);
	data->pwm_enable = f71882fg_read8(data, F71882FG_REG_PWM_ENABLE);
	if ((data->type == f8000 && ((data->pwm_enable >> 2 * nr) & 3) != 2) ||
	    (data->type != f8000 && !((data->pwm_enable >> 2 * nr) & 2))) {
		count = -EROFS;
		goto leave;
	}
	if (data->pwm_enable & (1 << (2 * nr))) {
		/* PWM mode */
		f71882fg_write8(data, F71882FG_REG_PWM(nr), val);
		data->pwm[nr] = val;
	} else {
		/* RPM mode */
		int target, full_speed;
		full_speed = f71882fg_read16(data,
					     F71882FG_REG_FAN_FULL_SPEED(nr));
		target = fan_to_reg(val * fan_from_reg(full_speed) / 255);
		f71882fg_write16(data, F71882FG_REG_FAN_TARGET(nr), target);
		data->fan_target[nr] = target;
		data->fan_full_speed[nr] = full_speed;
	}
leave:
	mutex_unlock(&data->update_lock);

	return count;
}

static ssize_t show_pwm_enable(struct device *dev,
			       struct device_attribute *devattr, char *buf)
{
	int result = 0;
	struct f71882fg_data *data = f71882fg_update_device(dev);
	int nr = to_sensor_dev_attr_2(devattr)->index;

	switch ((data->pwm_enable >> 2 * nr) & 3) {
	case 0:
	case 1:
		result = 2; /* Normal auto mode */
		break;
	case 2:
		result = 1; /* Manual mode */
		break;
	case 3:
		if (data->type == f8000)
			result = 3; /* Thermostat mode */
		else
			result = 1; /* Manual mode */
		break;
	}

	return sprintf(buf, "%d\n", result);
}

static ssize_t store_pwm_enable(struct device *dev, struct device_attribute
				*devattr, const char *buf, size_t count)
{
	struct f71882fg_data *data = dev_get_drvdata(dev);
	int err, nr = to_sensor_dev_attr_2(devattr)->index;
	long val;

	err = strict_strtol(buf, 10, &val);
	if (err)
		return err;

	/* Special case for F8000 pwm channel 3 which only does auto mode */
	if (data->type == f8000 && nr == 2 && val != 2)
		return -EINVAL;

	mutex_lock(&data->update_lock);
	data->pwm_enable = f71882fg_read8(data, F71882FG_REG_PWM_ENABLE);
	/* Special case for F8000 auto PWM mode / Thermostat mode */
	if (data->type == f8000 && ((data->pwm_enable >> 2 * nr) & 1)) {
		switch (val) {
		case 2:
			data->pwm_enable &= ~(2 << (2 * nr));
			break;		/* Normal auto mode */
		case 3:
			data->pwm_enable |= 2 << (2 * nr);
			break;		/* Thermostat mode */
		default:
			count = -EINVAL;
			goto leave;
		}
	} else {
		switch (val) {
		case 1:
			/* The f71858fg does not support manual RPM mode */
			if (data->type == f71858fg &&
			    ((data->pwm_enable >> (2 * nr)) & 1)) {
				count = -EINVAL;
				goto leave;
			}
			data->pwm_enable |= 2 << (2 * nr);
			break;		/* Manual */
		case 2:
			data->pwm_enable &= ~(2 << (2 * nr));
			break;		/* Normal auto mode */
		default:
			count = -EINVAL;
			goto leave;
		}
	}
	f71882fg_write8(data, F71882FG_REG_PWM_ENABLE, data->pwm_enable);
leave:
	mutex_unlock(&data->update_lock);

	return count;
}

static ssize_t show_pwm_auto_point_pwm(struct device *dev,
				       struct device_attribute *devattr,
				       char *buf)
{
	int result;
	struct f71882fg_data *data = f71882fg_update_device(dev);
	int pwm = to_sensor_dev_attr_2(devattr)->index;
	int point = to_sensor_dev_attr_2(devattr)->nr;

	mutex_lock(&data->update_lock);
	if (data->pwm_enable & (1 << (2 * pwm))) {
		/* PWM mode */
		result = data->pwm_auto_point_pwm[pwm][point];
	} else {
		/* RPM mode */
		result = 32 * 255 / (32 + data->pwm_auto_point_pwm[pwm][point]);
	}
	mutex_unlock(&data->update_lock);

	return sprintf(buf, "%d\n", result);
}

static ssize_t store_pwm_auto_point_pwm(struct device *dev,
					struct device_attribute *devattr,
					const char *buf, size_t count)
{
	struct f71882fg_data *data = dev_get_drvdata(dev);
	int err, pwm = to_sensor_dev_attr_2(devattr)->index;
	int point = to_sensor_dev_attr_2(devattr)->nr;
	long val;

	err = strict_strtol(buf, 10, &val);
	if (err)
		return err;

	val = SENSORS_LIMIT(val, 0, 255);

	mutex_lock(&data->update_lock);
	data->pwm_enable = f71882fg_read8(data, F71882FG_REG_PWM_ENABLE);
	if (data->pwm_enable & (1 << (2 * pwm))) {
		/* PWM mode */
	} else {
		/* RPM mode */
		if (val < 29)	/* Prevent negative numbers */
			val = 255;
		else
			val = (255 - val) * 32 / val;
	}
	f71882fg_write8(data, F71882FG_REG_POINT_PWM(pwm, point), val);
	data->pwm_auto_point_pwm[pwm][point] = val;
	mutex_unlock(&data->update_lock);

	return count;
}

static ssize_t show_pwm_auto_point_temp_hyst(struct device *dev,
					     struct device_attribute *devattr,
					     char *buf)
{
	int result = 0;
	struct f71882fg_data *data = f71882fg_update_device(dev);
	int nr = to_sensor_dev_attr_2(devattr)->index;
	int point = to_sensor_dev_attr_2(devattr)->nr;

	mutex_lock(&data->update_lock);
	if (nr & 1)
		result = data->pwm_auto_point_hyst[nr / 2] >> 4;
	else
		result = data->pwm_auto_point_hyst[nr / 2] & 0x0f;
	result = 1000 * (data->pwm_auto_point_temp[nr][point] - result);
	mutex_unlock(&data->update_lock);

	return sprintf(buf, "%d\n", result);
}

static ssize_t store_pwm_auto_point_temp_hyst(struct device *dev,
					      struct device_attribute *devattr,
					      const char *buf, size_t count)
{
	struct f71882fg_data *data = dev_get_drvdata(dev);
	int err, nr = to_sensor_dev_attr_2(devattr)->index;
	int point = to_sensor_dev_attr_2(devattr)->nr;
	u8 reg;
	long val;

	err = strict_strtol(buf, 10, &val);
	if (err)
		return err;

	val /= 1000;

	mutex_lock(&data->update_lock);
	data->pwm_auto_point_temp[nr][point] =
		f71882fg_read8(data, F71882FG_REG_POINT_TEMP(nr, point));
	val = SENSORS_LIMIT(val, data->pwm_auto_point_temp[nr][point] - 15,
				data->pwm_auto_point_temp[nr][point]);
	val = data->pwm_auto_point_temp[nr][point] - val;

	reg = f71882fg_read8(data, F71882FG_REG_FAN_HYST(nr / 2));
	if (nr & 1)
		reg = (reg & 0x0f) | (val << 4);
	else
		reg = (reg & 0xf0) | val;

	f71882fg_write8(data, F71882FG_REG_FAN_HYST(nr / 2), reg);
	data->pwm_auto_point_hyst[nr / 2] = reg;
	mutex_unlock(&data->update_lock);

	return count;
}

static ssize_t show_pwm_interpolate(struct device *dev,
				    struct device_attribute *devattr, char *buf)
{
	int result;
	struct f71882fg_data *data = f71882fg_update_device(dev);
	int nr = to_sensor_dev_attr_2(devattr)->index;

	result = (data->pwm_auto_point_mapping[nr] >> 4) & 1;

	return sprintf(buf, "%d\n", result);
}

static ssize_t store_pwm_interpolate(struct device *dev,
				     struct device_attribute *devattr,
				     const char *buf, size_t count)
{
	struct f71882fg_data *data = dev_get_drvdata(dev);
	int err, nr = to_sensor_dev_attr_2(devattr)->index;
	unsigned long val;

	err = strict_strtoul(buf, 10, &val);
	if (err)
		return err;

	mutex_lock(&data->update_lock);
	data->pwm_auto_point_mapping[nr] =
		f71882fg_read8(data, F71882FG_REG_POINT_MAPPING(nr));
	if (val)
		val = data->pwm_auto_point_mapping[nr] | (1 << 4);
	else
		val = data->pwm_auto_point_mapping[nr] & (~(1 << 4));
	f71882fg_write8(data, F71882FG_REG_POINT_MAPPING(nr), val);
	data->pwm_auto_point_mapping[nr] = val;
	mutex_unlock(&data->update_lock);

	return count;
}

static ssize_t show_pwm_auto_point_channel(struct device *dev,
					   struct device_attribute *devattr,
					   char *buf)
{
	int result;
	struct f71882fg_data *data = f71882fg_update_device(dev);
	int nr = to_sensor_dev_attr_2(devattr)->index;

	result = 1 << ((data->pwm_auto_point_mapping[nr] & 3) -
		       data->temp_start);

	return sprintf(buf, "%d\n", result);
}

static ssize_t store_pwm_auto_point_channel(struct device *dev,
					    struct device_attribute *devattr,
					    const char *buf, size_t count)
{
	struct f71882fg_data *data = dev_get_drvdata(dev);
	int err, nr = to_sensor_dev_attr_2(devattr)->index;
	long val;

	err = strict_strtol(buf, 10, &val);
	if (err)
		return err;

	switch (val) {
	case 1:
		val = 0;
		break;
	case 2:
		val = 1;
		break;
	case 4:
		val = 2;
		break;
	default:
		return -EINVAL;
	}
	val += data->temp_start;
	mutex_lock(&data->update_lock);
	data->pwm_auto_point_mapping[nr] =
		f71882fg_read8(data, F71882FG_REG_POINT_MAPPING(nr));
	val = (data->pwm_auto_point_mapping[nr] & 0xfc) | val;
	f71882fg_write8(data, F71882FG_REG_POINT_MAPPING(nr), val);
	data->pwm_auto_point_mapping[nr] = val;
	mutex_unlock(&data->update_lock);

	return count;
}

static ssize_t show_pwm_auto_point_temp(struct device *dev,
					struct device_attribute *devattr,
					char *buf)
{
	int result;
	struct f71882fg_data *data = f71882fg_update_device(dev);
	int pwm = to_sensor_dev_attr_2(devattr)->index;
	int point = to_sensor_dev_attr_2(devattr)->nr;

	result = data->pwm_auto_point_temp[pwm][point];
	return sprintf(buf, "%d\n", 1000 * result);
}

static ssize_t store_pwm_auto_point_temp(struct device *dev,
					 struct device_attribute *devattr,
					 const char *buf, size_t count)
{
	struct f71882fg_data *data = dev_get_drvdata(dev);
	int err, pwm = to_sensor_dev_attr_2(devattr)->index;
	int point = to_sensor_dev_attr_2(devattr)->nr;
	long val;

	err = strict_strtol(buf, 10, &val);
	if (err)
		return err;

	val /= 1000;

	if (data->type == f71889fg)
		val = SENSORS_LIMIT(val, -128, 127);
	else
		val = SENSORS_LIMIT(val, 0, 127);

	mutex_lock(&data->update_lock);
	f71882fg_write8(data, F71882FG_REG_POINT_TEMP(pwm, point), val);
	data->pwm_auto_point_temp[pwm][point] = val;
	mutex_unlock(&data->update_lock);

	return count;
}

static ssize_t show_name(struct device *dev, struct device_attribute *devattr,
	char *buf)
{
	struct f71882fg_data *data = dev_get_drvdata(dev);
	return sprintf(buf, "%s\n", f71882fg_names[data->type]);
}

static int __devinit f71882fg_create_sysfs_files(struct platform_device *pdev,
	struct sensor_device_attribute_2 *attr, int count)
{
	int err, i;

	for (i = 0; i < count; i++) {
		err = device_create_file(&pdev->dev, &attr[i].dev_attr);
		if (err)
			return err;
	}
	return 0;
}

static void f71882fg_remove_sysfs_files(struct platform_device *pdev,
	struct sensor_device_attribute_2 *attr, int count)
{
	int i;

	for (i = 0; i < count; i++)
		device_remove_file(&pdev->dev, &attr[i].dev_attr);
}

static int __devinit f71882fg_probe(struct platform_device *pdev)
{
	struct f71882fg_data *data;
	struct f71882fg_sio_data *sio_data = pdev->dev.platform_data;
	int err, i, nr_fans = (sio_data->type == f71882fg) ? 4 : 3;
	u8 start_reg;

	data = kzalloc(sizeof(struct f71882fg_data), GFP_KERNEL);
	if (!data)
		return -ENOMEM;

	data->addr = platform_get_resource(pdev, IORESOURCE_IO, 0)->start;
	data->type = sio_data->type;
	data->temp_start =
	    (data->type == f71858fg || data->type == f8000) ? 0 : 1;
	mutex_init(&data->update_lock);
	platform_set_drvdata(pdev, data);

	start_reg = f71882fg_read8(data, F71882FG_REG_START);
	if (start_reg & 0x04) {
		dev_warn(&pdev->dev, "Hardware monitor is powered down\n");
		err = -ENODEV;
		goto exit_free;
	}
	if (!(start_reg & 0x03)) {
		dev_warn(&pdev->dev, "Hardware monitoring not activated\n");
		err = -ENODEV;
		goto exit_free;
	}

	/* Register sysfs interface files */
	err = device_create_file(&pdev->dev, &dev_attr_name);
	if (err)
		goto exit_unregister_sysfs;

	if (start_reg & 0x01) {
		switch (data->type) {
		case f71858fg:
			data->temp_config =
				f71882fg_read8(data, F71882FG_REG_TEMP_CONFIG);
			if (data->temp_config & 0x10)
				/* The f71858fg temperature alarms behave as
				   the f8000 alarms in this mode */
				err = f71882fg_create_sysfs_files(pdev,
					f8000_in_temp_attr,
					ARRAY_SIZE(f8000_in_temp_attr));
			else
				err = f71882fg_create_sysfs_files(pdev,
					f71858fg_in_temp_attr,
					ARRAY_SIZE(f71858fg_in_temp_attr));
			break;
		case f71882fg:
		case f71889fg:
			err = f71882fg_create_sysfs_files(pdev,
					fxxxx_in1_alarm_attr,
					ARRAY_SIZE(fxxxx_in1_alarm_attr));
			if (err)
				goto exit_unregister_sysfs;
			/* fall through! */
		case f71862fg:
			err = f71882fg_create_sysfs_files(pdev,
					fxxxx_in_temp_attr,
					ARRAY_SIZE(fxxxx_in_temp_attr));
			break;
		case f8000:
			err = f71882fg_create_sysfs_files(pdev,
					f8000_in_temp_attr,
					ARRAY_SIZE(f8000_in_temp_attr));
			break;
		}
		if (err)
			goto exit_unregister_sysfs;
	}

	if (start_reg & 0x02) {
		data->pwm_enable =
			f71882fg_read8(data, F71882FG_REG_PWM_ENABLE);

		/* Sanity check the pwm settings */
		switch (data->type) {
		case f71858fg:
			err = 0;
			for (i = 0; i < nr_fans; i++)
				if (((data->pwm_enable >> (i * 2)) & 3) == 3)
					err = 1;
			break;
		case f71862fg:
			err = (data->pwm_enable & 0x15) != 0x15;
			break;
		case f71882fg:
		case f71889fg:
			err = 0;
			break;
		case f8000:
			err = data->pwm_enable & 0x20;
			break;
		}
		if (err) {
			dev_err(&pdev->dev,
				"Invalid (reserved) pwm settings: 0x%02x\n",
				(unsigned int)data->pwm_enable);
			err = -ENODEV;
			goto exit_unregister_sysfs;
		}

		err = f71882fg_create_sysfs_files(pdev, &fxxxx_fan_attr[0][0],
				ARRAY_SIZE(fxxxx_fan_attr[0]) * nr_fans);
		if (err)
			goto exit_unregister_sysfs;

		if (data->type == f71862fg || data->type == f71882fg ||
		    data->type == f71889fg) {
			err = f71882fg_create_sysfs_files(pdev,
					fxxxx_fan_beep_attr, nr_fans);
			if (err)
				goto exit_unregister_sysfs;
		}

		switch (data->type) {
		case f71862fg:
			err = f71882fg_create_sysfs_files(pdev,
					f71862fg_auto_pwm_attr,
					ARRAY_SIZE(f71862fg_auto_pwm_attr));
			break;
		case f8000:
			err = f71882fg_create_sysfs_files(pdev,
					f8000_fan_attr,
					ARRAY_SIZE(f8000_fan_attr));
			if (err)
				goto exit_unregister_sysfs;
			err = f71882fg_create_sysfs_files(pdev,
					f8000_auto_pwm_attr,
					ARRAY_SIZE(f8000_auto_pwm_attr));
			break;
		case f71889fg:
			for (i = 0; i < nr_fans; i++) {
				data->pwm_auto_point_mapping[i] =
					f71882fg_read8(data,
						F71882FG_REG_POINT_MAPPING(i));
				if (data->pwm_auto_point_mapping[i] & 0x80)
					break;
			}
			if (i != nr_fans) {
				dev_warn(&pdev->dev,
					 "Auto pwm controlled by raw digital "
					 "data, disabling pwm auto_point "
					 "sysfs attributes\n");
				break;
			}
			/* fall through */
		default: /* f71858fg / f71882fg */
			err = f71882fg_create_sysfs_files(pdev,
				&fxxxx_auto_pwm_attr[0][0],
				ARRAY_SIZE(fxxxx_auto_pwm_attr[0]) * nr_fans);
		}
		if (err)
			goto exit_unregister_sysfs;

		for (i = 0; i < nr_fans; i++)
			dev_info(&pdev->dev, "Fan: %d is in %s mode\n", i + 1,
				 (data->pwm_enable & (1 << 2 * i)) ?
				 "duty-cycle" : "RPM");
	}

	data->hwmon_dev = hwmon_device_register(&pdev->dev);
	if (IS_ERR(data->hwmon_dev)) {
		err = PTR_ERR(data->hwmon_dev);
		data->hwmon_dev = NULL;
		goto exit_unregister_sysfs;
	}

	return 0;

exit_unregister_sysfs:
	f71882fg_remove(pdev); /* Will unregister the sysfs files for us */
	return err; /* f71882fg_remove() also frees our data */
exit_free:
	kfree(data);
	return err;
}

static int f71882fg_remove(struct platform_device *pdev)
{
	struct f71882fg_data *data = platform_get_drvdata(pdev);
	int nr_fans = (data->type == f71882fg) ? 4 : 3;
	u8 start_reg = f71882fg_read8(data, F71882FG_REG_START);

	platform_set_drvdata(pdev, NULL);
	if (data->hwmon_dev)
		hwmon_device_unregister(data->hwmon_dev);

	device_remove_file(&pdev->dev, &dev_attr_name);

	if (start_reg & 0x01) {
		switch (data->type) {
		case f71858fg:
			if (data->temp_config & 0x10)
				f71882fg_remove_sysfs_files(pdev,
					f8000_in_temp_attr,
					ARRAY_SIZE(f8000_in_temp_attr));
			else
				f71882fg_remove_sysfs_files(pdev,
					f71858fg_in_temp_attr,
					ARRAY_SIZE(f71858fg_in_temp_attr));
			break;
		case f71882fg:
		case f71889fg:
			f71882fg_remove_sysfs_files(pdev,
					fxxxx_in1_alarm_attr,
					ARRAY_SIZE(fxxxx_in1_alarm_attr));
			/* fall through! */
		case f71862fg:
			f71882fg_remove_sysfs_files(pdev,
					fxxxx_in_temp_attr,
					ARRAY_SIZE(fxxxx_in_temp_attr));
			break;
		case f8000:
			f71882fg_remove_sysfs_files(pdev,
					f8000_in_temp_attr,
					ARRAY_SIZE(f8000_in_temp_attr));
			break;
		}
	}

	if (start_reg & 0x02) {
		f71882fg_remove_sysfs_files(pdev, &fxxxx_fan_attr[0][0],
				ARRAY_SIZE(fxxxx_fan_attr[0]) * nr_fans);

		if (data->type == f71862fg || data->type == f71882fg ||
		    data->type == f71889fg)
			f71882fg_remove_sysfs_files(pdev,
					fxxxx_fan_beep_attr, nr_fans);

		switch (data->type) {
		case f71862fg:
			f71882fg_remove_sysfs_files(pdev,
					f71862fg_auto_pwm_attr,
					ARRAY_SIZE(f71862fg_auto_pwm_attr));
			break;
		case f8000:
			f71882fg_remove_sysfs_files(pdev,
					f8000_fan_attr,
					ARRAY_SIZE(f8000_fan_attr));
			f71882fg_remove_sysfs_files(pdev,
					f8000_auto_pwm_attr,
					ARRAY_SIZE(f8000_auto_pwm_attr));
			break;
		default: /* f71858fg / f71882fg / f71889fg */
			f71882fg_remove_sysfs_files(pdev,
				&fxxxx_auto_pwm_attr[0][0],
				ARRAY_SIZE(fxxxx_auto_pwm_attr[0]) * nr_fans);
		}
	}

	kfree(data);

	return 0;
}

static int __init f71882fg_find(int sioaddr, unsigned short *address,
	struct f71882fg_sio_data *sio_data)
{
	int err = -ENODEV;
	u16 devid;

	/* Don't step on other drivers' I/O space by accident */
	if (!request_region(sioaddr, 2, DRVNAME)) {
		printk(KERN_ERR DRVNAME ": I/O address 0x%04x already in use\n",
				(int)sioaddr);
		return -EBUSY;
	}

	superio_enter(sioaddr);

	devid = superio_inw(sioaddr, SIO_REG_MANID);
	if (devid != SIO_FINTEK_ID) {
		pr_debug(DRVNAME ": Not a Fintek device\n");
		goto exit;
	}

	devid = force_id ? force_id : superio_inw(sioaddr, SIO_REG_DEVID);
	switch (devid) {
	case SIO_F71858_ID:
		sio_data->type = f71858fg;
		break;
	case SIO_F71862_ID:
		sio_data->type = f71862fg;
		break;
	case SIO_F71882_ID:
		sio_data->type = f71882fg;
		break;
	case SIO_F71889_ID:
		sio_data->type = f71889fg;
		break;
	case SIO_F8000_ID:
		sio_data->type = f8000;
		break;
	default:
		printk(KERN_INFO DRVNAME ": Unsupported Fintek device: %04x\n",
		       (unsigned int)devid);
		goto exit;
	}

	if (sio_data->type == f71858fg)
		superio_select(sioaddr, SIO_F71858FG_LD_HWM);
	else
		superio_select(sioaddr, SIO_F71882FG_LD_HWM);

	if (!(superio_inb(sioaddr, SIO_REG_ENABLE) & 0x01)) {
		printk(KERN_WARNING DRVNAME ": Device not activated\n");
		goto exit;
	}

	*address = superio_inw(sioaddr, SIO_REG_ADDR);
	if (*address == 0) {
		printk(KERN_WARNING DRVNAME ": Base address not set\n");
		goto exit;
	}
	*address &= ~(REGION_LENGTH - 1);	/* Ignore 3 LSB */

	err = 0;
	printk(KERN_INFO DRVNAME ": Found %s chip at %#x, revision %d\n",
		f71882fg_names[sio_data->type],	(unsigned int)*address,
		(int)superio_inb(sioaddr, SIO_REG_DEVREV));
exit:
	superio_exit(sioaddr);
	release_region(sioaddr, 2);
	return err;
}

static int __init f71882fg_device_add(unsigned short address,
	const struct f71882fg_sio_data *sio_data)
{
	struct resource res = {
		.start	= address,
		.end	= address + REGION_LENGTH - 1,
		.flags	= IORESOURCE_IO,
	};
	int err;

	f71882fg_pdev = platform_device_alloc(DRVNAME, address);
	if (!f71882fg_pdev)
		return -ENOMEM;

	res.name = f71882fg_pdev->name;
	err = acpi_check_resource_conflict(&res);
	if (err)
		goto exit_device_put;

	err = platform_device_add_resources(f71882fg_pdev, &res, 1);
	if (err) {
		printk(KERN_ERR DRVNAME ": Device resource addition failed\n");
		goto exit_device_put;
	}

	err = platform_device_add_data(f71882fg_pdev, sio_data,
				       sizeof(struct f71882fg_sio_data));
	if (err) {
		printk(KERN_ERR DRVNAME ": Platform data allocation failed\n");
		goto exit_device_put;
	}

	err = platform_device_add(f71882fg_pdev);
	if (err) {
		printk(KERN_ERR DRVNAME ": Device addition failed\n");
		goto exit_device_put;
	}

	return 0;

exit_device_put:
	platform_device_put(f71882fg_pdev);

	return err;
}

static int __init f71882fg_init(void)
{
	int err = -ENODEV;
	unsigned short address;
	struct f71882fg_sio_data sio_data;

	memset(&sio_data, 0, sizeof(sio_data));

	if (f71882fg_find(0x2e, &address, &sio_data) &&
	    f71882fg_find(0x4e, &address, &sio_data))
		goto exit;

	err = platform_driver_register(&f71882fg_driver);
	if (err)
		goto exit;

	err = f71882fg_device_add(address, &sio_data);
	if (err)
		goto exit_driver;

	return 0;

exit_driver:
	platform_driver_unregister(&f71882fg_driver);
exit:
	return err;
}

static void __exit f71882fg_exit(void)
{
	platform_device_unregister(f71882fg_pdev);
	platform_driver_unregister(&f71882fg_driver);
}

MODULE_DESCRIPTION("F71882FG Hardware Monitoring Driver");
MODULE_AUTHOR("Hans Edgington, Hans de Goede (hdegoede@redhat.com)");
MODULE_LICENSE("GPL");

module_init(f71882fg_init);
module_exit(f71882fg_exit);