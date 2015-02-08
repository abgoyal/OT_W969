
#include <linux/init.h>
#include <linux/module.h>
#include <linux/delay.h>
#include <linux/i2c.h>
#include <linux/input.h>
#include <linux/slab.h>
#include <linux/gpio.h>
#include <linux/sched.h>
#include <linux/kthread.h>
#include <linux/bitops.h>
#include <linux/kernel.h>
#include <linux/delay.h>
#include <linux/byteorder/generic.h>
#ifdef CONFIG_HAS_EARLYSUSPEND
#include <linux/earlysuspend.h>
#endif 
#include <linux/interrupt.h>
#include <linux/time.h>
#include <linux/rtpm_prio.h>
#include <mach/mt6573_boot.h>

//#include <pthread.h>
//#include <semaphore.h>
#include <linux/mutex.h>

#include <linux/dma-mapping.h>


#include "tpd_custom_mcs6024.h"
#include "tpd.h"
#include <cust_eint.h>

#ifndef TPD_NO_GPIO 
#include "cust_gpio_usage.h"
#endif


//add wangdongliang DMA
static unsigned char *tpDMABuf_va = NULL;
static u32 tpDMABuf_pa = NULL;
//end wangdongliang DMA

struct mutex clientMutex;  //\u5b9a\u4e49\u5168\u5c40\u4e92\u65a5\u9501


//if the TP has external power with GPIO pin,need define TPD_HAVE_POWER_ON_OFF in tpd_custom_mcs6024.h
#define TPD_HAVE_POWER_ON_OFF

#define MAX_POINT 2

//added wangdongliang

 /*
 Error status codes:
 */
#define CTPM_NOERROR			(0x01 << 0)
#define CTPM_ERR_PARAMETER		(0x01 << 1)
#define CTPM_ERR_PROTOCOL		(0x01 << 2)
#define CTPM_ERR_ECC			(0x01 << 3)
#define CTPM_ERR_MODE			(0x01 << 4)
#define CTPM_ERR_I2C			(0x01 << 5)
#define CTPM_ERR_SCAN			(0x01 << 6)

typedef struct
{
	/*x coordinate*/
	unsigned short	w_tp_x;

	/*y coordinate*/
	unsigned short 	w_tp_y;

	/*point id: start from 1*/
	unsigned char 	bt_tp_id;

	/*0 means press down; 1 means put up*/
	unsigned char 	bt_tp_property;

	/*the strength of the press*/
	unsigned short 	w_tp_strenth;
}ST_TOUCH_POINT, *PST_TOUCH_POINT;

typedef struct
{
	/*the number of touch points*/
	unsigned char 			bt_tp_num;

	/*touch gesture*/
//	E_GESTURE_TYPE		bt_gesture;

	/*point to a list which stored 1 to 5 touch points information*/
	ST_TOUCH_POINT* 	pst_point_info;
}ST_TOUCH_INFO, *PST_TOUCH_INFO;

static ST_TOUCH_POINT touch_point[5];

static ST_TOUCH_INFO ft_ts_data =
{
	.pst_point_info	= touch_point,
};

//ended wangdongliang
struct touch_info {
    int x[MAX_POINT], y[MAX_POINT];
    int p[MAX_POINT];
	int TouchpointFlag;
    int VirtualKeyFlag;
};

static int raw_x[MAX_POINT], raw_y[MAX_POINT];
struct touch_info cinfo, sinfo;



extern struct tpd_device *tpd;
extern int tpd_show_version;
//extern int tpd_debuglog;

static int boot_mode = 0;
static int tpd_flag = 0;
static int tpd_halt=0;
static struct task_struct *thread = NULL;
static DECLARE_WAIT_QUEUE_HEAD(waiter);

#ifdef TPD_HAVE_BUTTON 
static int tpd_keys_local[TPD_KEY_COUNT] = TPD_KEYS;
static int tpd_keys_dim_local[TPD_KEY_COUNT][4] = TPD_KEYS_DIM;
#endif
#if (defined(TPD_WARP_START) && defined(TPD_WARP_END))
static int tpd_wb_start_local[TPD_WARP_CNT] = TPD_WARP_START;
static int tpd_wb_end_local[TPD_WARP_CNT]   = TPD_WARP_END;
#endif
#if (defined(TPD_HAVE_CALIBRATION) && !defined(TPD_CUSTOM_CALIBRATION))
static int tpd_calmat_local[8]     = TPD_CALIBRATION_MATRIX;
static int tpd_def_calmat_local[8] = TPD_CALIBRATION_MATRIX;
#endif

static void tpd_eint_interrupt_handler(void);
static int touch_event_handler(void *unused);
static int tpd_i2c_probe(struct i2c_client *client, const struct i2c_device_id *id);
static int tpd_i2c_detect(struct i2c_client *client, int kind, struct i2c_board_info *info);
static int tpd_i2c_remove(struct i2c_client *client);
extern void mt65xx_eint_unmask(unsigned int line);
extern void mt65xx_eint_mask(unsigned int line);
extern void mt65xx_eint_set_hw_debounce(kal_uint8 eintno, kal_uint32 ms);
extern kal_uint32 mt65xx_eint_set_sens(kal_uint8 eintno, kal_bool sens);
extern void mt65xx_eint_registration(kal_uint8 eintno, kal_bool Dbounce_En,
                                     kal_bool ACT_Polarity, void (EINT_FUNC_PTR)(void),
                                     kal_bool auto_umask);

struct i2c_client *i2c_client = NULL;
static const struct i2c_device_id tpd_i2c_id[] = {{"mtk-tpd",0},{}};
static unsigned short force[] = {0, 0x70, I2C_CLIENT_END,I2C_CLIENT_END};
static const unsigned short * const forces[] = { force, NULL };
static struct i2c_client_address_data addr_data = { .forces = forces,};
struct i2c_driver tpd_i2c_driver = {                       
    .probe = tpd_i2c_probe,                                   
    .remove = tpd_i2c_remove,                           
    .detect = tpd_i2c_detect,                           
    .driver.name = "mtk-tpd", 
    .id_table = tpd_i2c_id,                             
    .address_data = &addr_data,                        
}; 

static void tpd_hw_enable(void)
{
    /* CTP_EN */
	mt_set_gpio_mode(GPIO21, 0);
	mt_set_gpio_dir(GPIO21, GPIO_DIR_OUT);
	mt_set_gpio_out(GPIO21, GPIO_OUT_ONE);
    	mdelay(5);
}

static void tpd_hw_disable(void)
{
    /* CTP_EN */
    	mt_set_gpio_mode(GPIO21, 0);
	mt_set_gpio_dir(GPIO21, GPIO_DIR_OUT);
	mt_set_gpio_out(GPIO21, GPIO_OUT_ZERO);
	mdelay(5);
}

static int tpd_i2c_detect(struct i2c_client *client, int kind, struct i2c_board_info *info) {
    strcpy(info->type, "mtk-tpd");
    return 0;
}
//add wangdongliang
int ft5x02_i2c_rxdata(int txlen, unsigned char *rxdata, int length)
{
	int ret;
	struct i2c_msg msgs[] = {
	{
		.addr   = 0x70,
		.flags = 0,
		.len   = txlen,
		.buf   = rxdata,
	},
	{
		.addr   = 0x70,
		.flags = 1,
		.len   = length,
		.buf   = rxdata,
	},
	};
	mutex_lock(&clientMutex);  //add lock
	i2c_client->addr = i2c_client->addr & I2C_MASK_FLAG;
	ret = i2c_transfer(i2c_client->adapter, msgs, 2);
	mutex_unlock(&clientMutex);  //add unlock
	if (ret < 0) {
		printk("ft5x02_i2c_rxdata failed!\n");
		return -1;
	}
	return 0;
}


int32_t ft5x02_i2c_txdata(unsigned char *txdata, int length)
{
	struct i2c_msg msg[] = {
		{
			.addr = 0x70,
			.flags = 0,
			.len = length,
			.buf = txdata,
		},
	};
	i2c_client->addr = i2c_client->addr & I2C_MASK_FLAG;
	if (i2c_transfer(i2c_client->adapter, msg, 1) < 0) {
		printk("ft5x02_i2c_txdata faild\n");
		return -EIO;
	}

	return 0;
}


int write_reg(unsigned char addr, char v)
{
	char tmp[4], ecc = 0;
	int32_t rc = 0;

	memset(tmp, 0, 2);
	tmp[0] = 0xfc;
	ecc ^= 0xfc;
	tmp[1] = addr;
	ecc ^= addr;
	tmp[2] = v;
	ecc ^= v;
	tmp[3] = ecc;

	rc = ft5x02_i2c_txdata(tmp, 4);
	if (rc < 0){
		printk("ft5x02 write reg failed!\n");
		return rc;
	}
	return 0;
}

int read_reg(unsigned char addr)
{
	char tmp[2];
	int32_t rc = 0;

	memset(tmp, 0, 2);
	tmp[0] = 0xfc;
	tmp[1] = addr +0x40;
	rc = ft5x02_i2c_rxdata(2, tmp, 2);
	if (rc < 0){
		printk("ft5x02_i2c_read failed!\n");
		return rc;
	}
	return tmp[0];
}

//end wangdongliang


static int tpd_i2c_probe(struct i2c_client *client, const struct i2c_device_id *id) {             
	int err = 0,i;
	i2c_client = client;
	int bufff = 0x0;
	printk("enter focal %s \n", __FUNCTION__);
	#ifdef TPD_HAVE_POWER_ON_OFF

//power on
	tpd_hw_enable();


	mdelay(5);
//add for reset pin  ????
    mt_set_gpio_mode(GPIO43, 0);
	mt_set_gpio_dir(GPIO43, GPIO_DIR_OUT);
	mt_set_gpio_out(GPIO43, GPIO_OUT_ONE);



//eint config to gpio
	mt_set_gpio_mode(GPIO_CTP_EINT_PIN, GPIO_CTP_EINT_PIN_M_EINT);
	mt_set_gpio_dir(GPIO_CTP_EINT_PIN, GPIO_DIR_IN);
	mt_set_gpio_pull_enable(GPIO_CTP_EINT_PIN, GPIO_PULL_ENABLE);
	mt_set_gpio_pull_select(GPIO_CTP_EINT_PIN, GPIO_PULL_UP);
	
	#endif

	mdelay(200);	//delay for init, maybe need 200ms
	//read the id num
	bufff = read_reg(0x3d);
	if(bufff < 0)
	{
		printk("find no focal touch panel !!! \n");
		return -1;
	}
	printk(KERN_ERR"focal id is 0x%2x\n", bufff);		//should be 0x79

// enter upgrade mode
	bufff = read_reg(0x3b);
	printk("focaltec's version is %x\n",bufff);


	if(bufff != 0x0c){
		int ret;
		printk("now upgrade.........\n");
		ret = fts_ctpm_fw_upgrade_with_i_file();         //upgrade firmware
		if (ret != ERR_OK){
			printk(KERN_ERR "ft5x02 upgrade failed\n");
			return -1;
		}
		else
			printk("new version is %d\n", read_reg(0x3b));

	}


//updata touch panel firmware end... wangdongliang


	  thread = kthread_run(touch_event_handler, 0, TPD_DEVICE);
    if (IS_ERR(thread)) { 
        err = PTR_ERR(thread);
        TPD_DMESG(TPD_DEVICE " failed to create kernel thread: %d\n", err);
    }    
    

	mt65xx_eint_set_sens(CUST_EINT_TOUCH_PANEL_NUM, CUST_EINT_TOUCH_PANEL_SENSITIVE);
	mt65xx_eint_set_hw_debounce(CUST_EINT_TOUCH_PANEL_NUM, CUST_EINT_TOUCH_PANEL_DEBOUNCE_CN);
	mt65xx_eint_registration(CUST_EINT_TOUCH_PANEL_NUM, CUST_EINT_TOUCH_PANEL_DEBOUNCE_EN, CUST_EINT_TOUCH_PANEL_POLARITY, tpd_eint_interrupt_handler, 1);
	mt65xx_eint_unmask(CUST_EINT_TOUCH_PANEL_NUM);
	
   tpd_load_status = 1;
    return 0;
}

void tpd_eint_interrupt_handler(void) { 

    TPD_DEBUG_PRINT_INT; tpd_flag=1; wake_up_interruptible(&waiter);
} 
static int tpd_i2c_remove(struct i2c_client *client) {return 0;}


// added wangdongliang

unsigned char bt_parser_fts(unsigned char* pbt_buf, unsigned char bt_len, ST_TOUCH_INFO* pst_touch_info)
{
	unsigned short low_byte	= 0;
	unsigned short high_byte	= 0;
	unsigned char point_num 	= 0;
	unsigned char i 			= 0;
	unsigned char ecc 		= 0;
	char j = 0;
	/*check the length of the protocol data*/
	if(bt_len < 26)
	{
		return 0;
	}
	pst_touch_info->bt_tp_num= 0;

	/*check packet head: 0xAAAA.*/
	if(pbt_buf[1]!= 0xaa || pbt_buf[0] != 0xaa)
	{
		return 0;
	}
	/*check data length*/
	if((pbt_buf[2] & 0x3f) != 26)
	{
		return 0;
	}
	/*check points number.*/
	point_num = pbt_buf[3] & 0x0f;
	if(point_num > 5)
	{
		return 0;
	}
	/*remove the touch point information into pst_touch_info.*/
	for(i = 0; i < point_num; i++)
	{

		high_byte = pbt_buf[5+4*i];
		high_byte <<= 8;
		low_byte = pbt_buf[5+4*i+1];
		pst_touch_info->pst_point_info[i].w_tp_x = (high_byte |low_byte) & 0x0fff;

		high_byte = pbt_buf[5+4*i+2];
		high_byte <<= 8;
		low_byte = pbt_buf[5+4*i+3];
		pst_touch_info->pst_point_info[i].w_tp_y = (high_byte |low_byte) & 0x0fff;

		pst_touch_info->bt_tp_num++;
	}

	/*check ecc*/
	ecc = 0;
	for (i=0; i<bt_len-1; i++)
	{
		ecc ^= pbt_buf[i];
	}
	if(ecc != pbt_buf[bt_len-1])
	{
		/*ecc error*/
		return 0;
	}

	return 1;
}

ssize_t mt6573_dma_write_m_byte(uint8_t *cmd, unsigned char*returnData_va, u32 returnData_pa,unsigned char len)
{
    char     readData = 0;
    int     ret=0, loop_i, read_length = 0;
    int    i, total_count = len;



	i2c_client->addr = (i2c_client->addr & I2C_MASK_FLAG) | I2C_DMA_FLAG;
	for(i = 0; i < len; i++)
		returnData_va[i] = cmd[i];
    if (len > 0){
        ret = i2c_master_send(i2c_client, returnData_pa, len);
        if (ret < 0) {
            printk(KERN_ERR"focal write data error!!\n");
            return 0;
        }
    }
    return 1;
}
int i2c_ft5x02_write_dma(struct i2c_client *client, uint8_t *data, uint8_t length)
{

	int retry;

	retry = mt6573_dma_write_m_byte(data, tpDMABuf_va, tpDMABuf_pa, length);

	if(!retry)
	return -1;
	return 0;
}


ssize_t mt6573_dma_read_m_byte(unsigned char cmd, unsigned char*returnData_va, u32 returnData_pa,unsigned char len)
{
    char     readData = 0;
    int     ret=0, read_length = 0;
    int    i, total_count = len;


	if(returnData_va == NULL)
	{
		printk("%s, returnData_va is NULL \n", __FUNCTION__);
		return 0;
	}
	mutex_lock(&clientMutex);  //add lock
    i2c_client->addr = (i2c_client->addr & I2C_MASK_FLAG) | I2C_DMA_FLAG;
    returnData_va[0] = cmd;//use as buffer
    ret = i2c_master_send(i2c_client, returnData_pa, 1);
	mutex_unlock(&clientMutex);  //add unlock

    if (ret < 0) {
        printk(KERN_ERR"focal sends command error!!\n");
        return 0;
    }

    if (len > 0){

	 mutex_lock(&clientMutex);  //add lock
	 i2c_client->addr = (i2c_client->addr & I2C_MASK_FLAG) | I2C_DMA_FLAG;
        ret = i2c_master_recv(i2c_client, returnData_pa, len);
	 mutex_unlock(&clientMutex);  //add unlock

        if (ret < 0) {
            printk(KERN_ERR"focal reads data error!!\n");
            return 0;
        }
    }

    return 1;
}

int touch_gettouchinfo(ST_TOUCH_INFO* pst_touch_info) {
	int ret = 0;
	ret = mt6573_dma_read_m_byte(0xf9, tpDMABuf_va, tpDMABuf_pa, 26);
	if(ret == 0)
	{
		printk("read touch panel data error!!!\n");	
		return -1;
	}
	return bt_parser_fts(tpDMABuf_va, 26, pst_touch_info);
}

static int touch_event_handler(void *unused) {
	struct sched_param param = { .sched_priority = RTPM_PRIO_TPD };
	int m =1;
	int index;
	uint8_t Firmware_version[3] = {0x20,0x00,0x00};
	int ret;
	
	sched_setscheduler(current, SCHED_RR, &param);

	do {
	set_current_state(TASK_INTERRUPTIBLE);
	if (!kthread_should_stop()) {
            TPD_DEBUG_CHECK_NO_RESPONSE;
            do {
				while (tpd_halt) {tpd_flag = 0;sinfo.TouchpointFlag=0; msleep(20);}
               		wait_event_interruptible(waiter,tpd_flag!=0);
					tpd_flag = 0;
            } while(0);

            TPD_DEBUG_SET_TIME;
        }
       set_current_state(TASK_RUNNING);

	//read touch data

	ret = touch_gettouchinfo(&ft_ts_data);
	if(ret < 0)
		continue;


	if (ft_ts_data.bt_tp_num == 0) { //release touch
	
		input_report_abs(tpd->dev, ABS_MT_TOUCH_MAJOR, 0);
		input_report_key(tpd->dev, KEY_MENU, 0);
		input_report_key(tpd->dev, KEY_SEARCH, 0);
		input_report_key(tpd->dev, KEY_BACK, 0);
	}
	if (ft_ts_data.bt_tp_num >= 1) {

			if(touch_point[0].w_tp_y<480){
			input_report_abs(tpd->dev, ABS_MT_TOUCH_MAJOR,255);
			input_report_abs(tpd->dev, ABS_MT_POSITION_X,touch_point[0].w_tp_x);
			input_report_abs(tpd->dev, ABS_MT_POSITION_Y,touch_point[0].w_tp_y);
			input_mt_sync(tpd->dev);
			}else if(touch_point[0].w_tp_y>500){

					if(touch_point[0].w_tp_x>9 && touch_point[0].w_tp_x<81)
					input_report_key(tpd->dev, KEY_MENU, 1);
					else if(touch_point[0].w_tp_x>124&&touch_point[0].w_tp_x<196)
					input_report_key(tpd->dev, KEY_SEARCH, 1);
					else if(touch_point[0].w_tp_x>239&&touch_point[0].w_tp_x<311)
					input_report_key(tpd->dev, KEY_BACK, 1);
			}
	}

	if (ft_ts_data.bt_tp_num >= 2) {

			if(touch_point[1].w_tp_y<480){
			input_report_abs(tpd->dev, ABS_MT_TOUCH_MAJOR,255);
			input_report_abs(tpd->dev, ABS_MT_POSITION_X,touch_point[1].w_tp_x);
			input_report_abs(tpd->dev, ABS_MT_POSITION_Y,touch_point[1].w_tp_y);
			input_mt_sync(tpd->dev);
			}
			else if(touch_point[1].w_tp_y>500){
				if(touch_point[1].w_tp_x>9&&touch_point[1].w_tp_x<81){
					input_report_key(tpd->dev, KEY_MENU, 1);
				}else if(touch_point[1].w_tp_x>124&&touch_point[1].w_tp_x<196){
					input_report_key(tpd->dev, KEY_SEARCH, 1);
				}else if(touch_point[1].w_tp_x>239&&touch_point[1].w_tp_x<311){
					input_report_key(tpd->dev, KEY_BACK, 1);
				}

			}

	}

	input_sync(tpd->dev);

    } while (!kthread_should_stop());
    return 0;
}


int tpd_local_init(void)
{

    boot_mode = get_boot_mode();
    // Software reset mode will be treated as normal boot
    if(boot_mode==3) boot_mode = NORMAL_BOOT;

//add wangdongliang DMA
    tpDMABuf_va = (unsigned char *)dma_alloc_coherent(NULL, 4096, &tpDMABuf_pa, GFP_KERNEL);
    if(!tpDMABuf_va){
		printk(KERN_ERR"xxxx Allocate DMA I2C Buffer failed!xxxx\n");
		return -1;
    }
//end wangdongliang DMA
	mutex_init(&clientMutex);


     if(i2c_add_driver(&tpd_i2c_driver)!=0) {
      printk(KERN_ERR"unable to add focal i2c driver.\n");
      return -1;
    }
#ifdef TPD_HAVE_BUTTON
    tpd_button_setting(TPD_KEY_COUNT, tpd_keys_local, tpd_keys_dim_local);// initialize tpd button data
#endif   

#if (defined(TPD_WARP_START) && defined(TPD_WARP_END))
    TPD_DO_WARP = 1;
    memcpy(tpd_wb_start, tpd_wb_start_local, TPD_WARP_CNT*4);
    memcpy(tpd_wb_end, tpd_wb_start_local, TPD_WARP_CNT*4);
#endif

#if (defined(TPD_HAVE_CALIBRATION) && !defined(TPD_CUSTOM_CALIBRATION))
    memcpy(tpd_calmat, tpd_calmat_local, 8*4);
    memcpy(tpd_def_calmat, tpd_def_calmat_local, 8*4);
#endif
		TPD_DMESG("end %s, %d\n", __FUNCTION__, __LINE__);
		tpd_type_cap = 1;
    return 0;
}

/* Function to manage low power suspend */
void tpd_suspend(struct early_suspend *h)
{
	int ret;
	printk(" %s\n", __FUNCTION__);
	tpd_halt = 1;
	mt65xx_eint_mask(CUST_EINT_TOUCH_PANEL_NUM);

	ret=write_reg(0x3a,0x03);//set tp power state :Hibernate mode
	if(ret<0)
		printk(KERN_ERR " set tp power state failed\n");

}

/* Function to manage power-on resume */
void tpd_resume(struct early_suspend *h)
{
	printk(" %s\n", __FUNCTION__);

//prefer RST pin gpio43 (maybe error when use WK pin gpio21)
	mt_set_gpio_mode(GPIO43, 0);
	mt_set_gpio_dir(GPIO43, GPIO_DIR_OUT);

	mt_set_gpio_out(GPIO43, GPIO_OUT_ONE);
	mt_set_gpio_out(GPIO43, GPIO_OUT_ZERO);
	mdelay(5);
	mt_set_gpio_out(GPIO43, GPIO_OUT_ONE);

	mt65xx_eint_unmask(CUST_EINT_TOUCH_PANEL_NUM);
	mdelay(120);			//120ms is needed

	tpd_halt = 0;

}

static struct tpd_driver_t tpd_device_driver = {
	.tpd_device_name = "ft5x02",
	.tpd_local_init = tpd_local_init,
	.suspend = tpd_suspend,
	.resume = tpd_resume,
#ifdef TPD_HAVE_BUTTON
	.tpd_have_button = 1,
#else
	.tpd_have_button = 0,
#endif	
};
/* called when loaded into kernel */
static int __init tpd_driver_init(void) {
    printk(KERN_ERR"focaltec 5X02 touch panel driver init xxxx\n");


		if(tpd_driver_add(&tpd_device_driver) < 0)
			TPD_DMESG("add generic driver failed\n");
    return 0;
}

/* should never be called */
static void __exit tpd_driver_exit(void) {
    TPD_DMESG("MediaTek mcs6024 touch panel driver exit\n");
    //input_unregister_device(tpd->dev);
    tpd_driver_remove(&tpd_device_driver);
}

module_init(tpd_driver_init);
module_exit(tpd_driver_exit);

