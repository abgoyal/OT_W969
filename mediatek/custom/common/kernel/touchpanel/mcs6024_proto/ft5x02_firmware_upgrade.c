
/********************************************************************************
 *                                                                               *
 *                              Focaltech Systems (R)                            *
 *                                                                               *
 *                               All Rights Reserved                             *
 *                                                                                *
 *  THIS WORK CONTAINS TRADE SECRET AND PROPRIETARY INFORMATION WHICH IS          *
 *  THE PROPERTY OF MENTOR GRAPHICS CORPORATION OR ITS LICENSORS AND IS           *
 *  SUBJECT TO LICENSE TERMS.                                                   *
 *                                                                               *
 *******************************************************************************/
 
 /*******************************************************************************
 *
 * Filename:
 * ---------
 *   : I2C_PP_Std.c 
 *
 * Project:
 * --------
 *  ctpm
 *
 * Description:
 * ------------
 * upgrade the CTPM firmware by Host side.
 *   
 *   
 * Author: 
 * -------
 * Wang xinming  
 *
 * 2010-06-07
 *
 * Last changed:
 * ------------- 
 * Author:  
 *
 * Modtime:   
 *
 * Revision: 0.1
*******************************************************************************/

#include <linux/interrupt.h>
#include <linux/i2c.h>
#include <linux/sched.h>
#include <linux/kthread.h>
#include <linux/wait.h>
#include <linux/time.h>
#include <linux/earlysuspend.h>
#include <linux/delay.h>
#include <linux/kernel.h>
#include <linux/module.h>

#include <linux/earlysuspend.h>
#include <linux/hrtimer.h>

#include <linux/io.h>
#include <linux/platform_device.h>

#include <mach/mt6573_boot.h>
#include <linux/gpio.h>
#include "tpd_custom_mcs6024.h"
#include "tpd.h"
#include <cust_eint.h>


typedef unsigned char	FTS_BYTE;
typedef unsigned short  FTS_WORD;
typedef unsigned int    FTS_DWRD;
typedef signed int      FTS_BOOL;

extern void T0_Waitms (unsigned short ms);

#define FTS_NULL 	0x0
#define FTS_TRUE 	0x01
#define FTS_FALSE 	0x0

#define PROTOCOL_LEN 26

#define POINTER_CHECK(p)	if((p)==FTS_NULL){return FTS_FALSE;}

/*Note: please modify this MACRO if the slave address changed*/
#define I2C_CTPM_ADDRESS 	0x70

//#include "cust_gpio_usage.h"
#include "tpd_custom_mcs6024.h"



/*the follow three funcions should be implemented by user*/
/**************************************************************************************/
/*
[function]: 
    callback: read data from ctpm by i2c interface,implemented by special user;
[parameters]:
    bt_ctpm_addr[in]    :the address of the ctpm;
    pbt_buf[out]        :data buffer;
    dw_lenth[in]        :the length of the data buffer;
[return]:
    FTS_TRUE     :success;
    FTS_FALSE    :fail;
*/





void delay_ms(FTS_WORD  w_ms)
{
    unsigned int i;
    unsigned j;
    for (j = 0; j<w_ms; j++)
     {
        for (i = 0; i < 1000; i++)
        {
            udelay(1);
        }
     }
    
   
    //platform related, please implement this function
    //T0_Waitms(w_ms);
}
//#endif 
/*
[function]: 
    callback: write data to ctpm by i2c interface,implemented by special user;
[parameters]:
    bt_ctpm_addr[in]    :the address of the ctpm;
    pbt_buf[in]        :data buffer;
    dw_lenth[in]        :the length of the data buffer;
[return]:
    FTS_TRUE     :success;
    FTS_FALSE    :fail;
*/

FTS_BOOL i2c_write_interface(FTS_BYTE bt_ctpm_addr, FTS_BYTE* pbt_buf, FTS_DWRD dw_lenth)
{
	int ret;   
	//struct i2c_client *i2c_client;
	i2c_client->addr = bt_ctpm_addr& I2C_MASK_FLAG;
    ret = i2c_master_send(i2c_client, pbt_buf, dw_lenth);
	if(ret <=  0)
{
	printk("write_interface error!\n");
	return 0;
}    
	return 1;
}
FTS_BOOL i2c_read_interface(FTS_BYTE bt_ctpm_addr, FTS_BYTE* pbt_buf, FTS_DWRD dw_lenth)
{
	int ret;   
	//struct i2c_client *i2c_client;
	i2c_client->addr = bt_ctpm_addr& I2C_MASK_FLAG;
    ret = i2c_master_recv(i2c_client, pbt_buf, dw_lenth);
	if(ret <=  0)
{
	printk(" read_interface error!\n");
	return 0;
}    
	return 1;
}

/***************************************************************************************/

/*
[function]: 
    write a value to register.
[parameters]:
    e_reg_name[in]    :register name;
    pbt_buf[in]        :the returned register value;
[return]:
    FTS_TRUE    :success;
    FTS_FALSE    :io fail;
*/
FTS_BOOL fts_register_write(FTS_BYTE e_reg_name, FTS_BYTE bt_value)
{

      unsigned char tmp[4], ecc = 0;
	int32_t rc = 0;

	memset(tmp, 0, 2);
	tmp[0] = 0xfc;
	ecc ^= 0xfc;
	tmp[1] = e_reg_name;
	ecc ^= e_reg_name;
	tmp[2] = bt_value;
	ecc ^= bt_value;
	tmp[3] = ecc;

	rc = i2c_write_interface(I2C_CTPM_ADDRESS, tmp, 4);
	if (rc != 1){
		printk("i2c_write_interface failed!\n");
		return rc;
	}
	return 0;

}


unsigned char CTPM_FW[]=
{
#include "ft5x02_firmware_upgrade.h"
};


//#if CFG_SUPPORT_FLASH_UPGRADE 
/*
[function]: 
    send a command to ctpm.
[parameters]:
    btcmd[in]        :command code;
    btPara1[in]    :parameter 1;    
    btPara2[in]    :parameter 2;    
    btPara3[in]    :parameter 3;    
    num[in]        :the valid input parameter numbers, if only command code needed and no parameters followed,then the num is 1;    
[return]:
    FTS_TRUE    :success;
    FTS_FALSE    :io fail;
*/
FTS_BOOL cmd_write(FTS_BYTE btcmd,FTS_BYTE btPara1,FTS_BYTE btPara2,FTS_BYTE btPara3,FTS_BYTE num)
{
    FTS_BYTE write_cmd[4] = {0};

    write_cmd[0] = btcmd;
    write_cmd[1] = btPara1;
    write_cmd[2] = btPara2;
    write_cmd[3] = btPara3;
    return i2c_write_interface(I2C_CTPM_ADDRESS, write_cmd, num);
 //return ft5x02_i2c_txdata(I2C_CTPM_ADDRESS, write_cmd, num);
}

/*
[function]: 
    write data to ctpm , the destination address is 0.
[parameters]:
    pbt_buf[in]    :point to data buffer;
    bt_len[in]        :the data numbers;    
[return]:
    FTS_TRUE    :success;
    FTS_FALSE    :io fail;
*/
FTS_BOOL byte_write(FTS_BYTE* pbt_buf, FTS_DWRD dw_len)
{
    POINTER_CHECK(pbt_buf);
    
    return i2c_write_interface(I2C_CTPM_ADDRESS, pbt_buf, dw_len);
}

/*
[function]: 
    read out data from ctpm,the destination address is 0.
[parameters]:
    pbt_buf[out]    :point to data buffer;
    bt_len[in]        :the data numbers;    
[return]:
    FTS_TRUE    :success;
    FTS_FALSE    :io fail;
*/

FTS_BOOL byte_read(FTS_BYTE* pbt_buf, FTS_BYTE bt_len)
{
    POINTER_CHECK(pbt_buf);
    
    return i2c_read_interface(I2C_CTPM_ADDRESS, pbt_buf, bt_len);
}


/*
[function]: 
    burn the FW to ctpm.
[parameters]:(ref. SPEC)
    pbt_buf[in]    :point to Head+FW ;
    dw_lenth[in]:the length of the FW + 6(the Head length);    
    bt_ecc[in]    :the ECC of the FW
[return]:
    ERR_OK        :no error;
    ERR_MODE    :fail to switch to UPDATE mode;
    ERR_READID    :read id fail;
    ERR_ERASE    :erase chip fail;
    ERR_STATUS    :status error;
    ERR_ECC        :ecc error.
*/

void fts_ctpm_auto_clb(void)
{
	unsigned char uc_temp;
    	unsigned char i ;

	printk("[FTS] start auto CLB.\n");
    	delay_ms(200);
	fts_register_write(0x3c, 4);	//enter factory mode
	delay_ms(100);
	//fts_register_write(0x01, 4);	//enter calibration mode
	delay_ms(300);

	for (i=0;i<5;i++)
	{
		uc_temp = read_reg(0x3c);
		if (uc_temp == 0x01)
		{
			break;	//return to work mode, calibration finish
		}
		delay_ms(200);
		printk("[FTS] waiting calibration %d, reg 0x3c is %d\n", i, uc_temp);
	}
	printk("[FTS] calibration OK.\n");

	delay_ms(200);
	fts_register_write(0x3c, 3);	//enter factory mode
	delay_ms(100);
	fts_register_write(0x00, 0x00);
	delay_ms(300);
	fts_register_write(0x01, 0x01);
	delay_ms(300);
	
	printk("[FTS] store CLB result OK.\n");


	//reset to make TP work normal
	printk("step 9:reset pin RST\n");
	mt_set_gpio_mode(GPIO43, 0);
	mt_set_gpio_dir(GPIO43, GPIO_DIR_OUT);

	mt_set_gpio_out(GPIO43, GPIO_OUT_ONE);
	mt_set_gpio_out(GPIO43, GPIO_OUT_ZERO);
	mdelay(5);
	mt_set_gpio_out(GPIO43, GPIO_OUT_ONE);
	mdelay(120);

	
}

#define    FTS_PACKET_LENGTH        128

E_UPGRADE_ERR_TYPE fts_ctpm_fw_upgrade_with_i_file(void)
{

	FTS_BYTE*     pbt_buf = FTS_NULL;
	E_UPGRADE_ERR_TYPE  ret;
	unsigned char uc_temp;



	//=========FW upgrade========================*/
	pbt_buf = CTPM_FW;
	/*call the upgrade function*/
	ret =  fts_ctpm_fw_upgrade(pbt_buf,sizeof(CTPM_FW));

	fts_ctpm_auto_clb();

//	delay_ms(1000);
//	uc_temp = read_reg(0x3b);
//	printk("new version is = %x\n", uc_temp);	

	return ret;
}




E_UPGRADE_ERR_TYPE  fts_ctpm_fw_upgrade(FTS_BYTE* pbt_buf, FTS_DWRD dw_lenth)
{

    FTS_BYTE reg_val[2] = {0};
    FTS_DWRD i = 0;


    FTS_DWRD packet_number;
    FTS_DWRD j;
    FTS_DWRD temp;
    FTS_DWRD lenght;
    FTS_BYTE packet_buf[FTS_PACKET_LENGTH + 6];
    FTS_BYTE  auc_i2c_write_buf[10];
    FTS_BYTE bt_ecc;
	int ret;

      msleep(200); //make sure CTP bootup normally

    /*********Step 1:Reset  CTPM ***************************/
    /*write 0xaa to register 0xfc*/
      ret = read_reg(0x3c);
	
      fts_register_write(0x3c,0xaa);
//     write_reg(0x3c,0xaa);
      delay_ms(50);
     /*write 0x55 to register 0xfc*/
	
	fts_register_write(0x3c,0x55);
//	write_reg(0x3c,0x55);	  
      	delay_ms(30);
 	printk("step 1: reset\n");
 
       auc_i2c_write_buf[0] = 0x55;
//       ret =  i2c_write_interface(I2C_CTPM_ADDRESS, auc_i2c_write_buf, 1);
//           delay_ms(1);
       auc_i2c_write_buf[1] = 0xaa;
	i = 0;
	do{
	ret =  i2c_write_interface(I2C_CTPM_ADDRESS, auc_i2c_write_buf, 2);
	i++;
	}while(i<5&&ret>=0);
	
     printk("Step 2: Enter update mode. \n");
   
       delay_ms(5);


      i = 0;
    /*********Step 3:check READ-ID**************************/
    /*send the opration head*/
    reg_val[0] = 0xbb; reg_val[1] = 0xcc;

    do{
        if(i > 3)
       {        printk("step3 :error!\n");
            return ERR_READID; 
        }
       cmd_write(0x90,0x00,0x00,0x00,4);
	 delay_ms(1);
       byte_read(reg_val,2);

        i++;
        printk("Step 3: CTPM ID,ID1 = 0x %x,ID2 = 0x %x\n", reg_val[0], reg_val[1]);
    }while(reg_val[0] != 0x79 || reg_val[1] != 0x03);

     /*********Step 4:erase app*******************************/
    cmd_write(0x61,0x00,0x00,0x00,1);
    delay_ms(1500);
    printk("Step 4: erase. \n");



    /*********Step 5:write firmware(FW) to ctpm flash************/
    bt_ecc = 0;
    printk("Step 5: start upgrade. \n");
     
    dw_lenth = dw_lenth - 8;
    printk("length 0x%x. \n", dw_lenth);
    packet_number = (dw_lenth) / FTS_PACKET_LENGTH;
    packet_buf[0] = 0xbf;
    packet_buf[1] = 0x00;
    for (j=0;j<packet_number;j++)
    {
        temp = j * FTS_PACKET_LENGTH;
        packet_buf[2] = (FTS_BYTE)(temp>>8);
        packet_buf[3] = (FTS_BYTE)temp;
        lenght = FTS_PACKET_LENGTH;
        packet_buf[4] = (FTS_BYTE)(lenght>>8);
        packet_buf[5] = (FTS_BYTE)lenght;

        for (i=0;i<FTS_PACKET_LENGTH;i++)
        {
            packet_buf[6+i] = pbt_buf[j*FTS_PACKET_LENGTH + i]; 
            bt_ecc ^= packet_buf[6+i];
        }


	i2c_ft5x02_write_dma(i2c_client, packet_buf, FTS_PACKET_LENGTH + 6);        
//byte_write(packet_buf,FTS_PACKET_LENGTH + 6);
        delay_ms(FTS_PACKET_LENGTH/6 + 1);
        if ((j * FTS_PACKET_LENGTH % 1024) == 0)
        {
              printk("upgrade the 0x%x th byte.\n", ((unsigned int)j) * FTS_PACKET_LENGTH);
        }
    }

    if ((dw_lenth) % FTS_PACKET_LENGTH > 0)
    {
        temp = packet_number * FTS_PACKET_LENGTH;
        packet_buf[2] = (FTS_BYTE)(temp>>8);
        packet_buf[3] = (FTS_BYTE)temp;

        temp = (dw_lenth) % FTS_PACKET_LENGTH;
        packet_buf[4] = (FTS_BYTE)(temp>>8);
        packet_buf[5] = (FTS_BYTE)temp;

        for (i=0;i<temp;i++)
        {
            packet_buf[6+i] = pbt_buf[ packet_number*FTS_PACKET_LENGTH + i]; 
            bt_ecc ^= packet_buf[6+i];
        }
	i2c_ft5x02_write_dma(i2c_client, packet_buf, temp + 6);        
//        byte_write(packet_buf,temp+6);    
        delay_ms(20);
    }

     //send the last six packet
     for (i = 0; i<6; i++)
     {
         temp = 0x6ffa + i;
         packet_buf[2] = (FTS_BYTE)(temp>>8);
         packet_buf[3] = (FTS_BYTE)temp;
         temp =1;
         packet_buf[4] = (FTS_BYTE)(temp>>8);
         packet_buf[5] = (FTS_BYTE)temp;
         packet_buf[6] = pbt_buf[ dw_lenth + i]; 
         bt_ecc ^= packet_buf[6];
//		i2c_ft5x02_write_dma(i2c_client, packet_buf, temp + 6);  
         byte_write(packet_buf,7);  
         delay_ms(20);
     }

    	/*********Step 6: read out checksum**********************/  
      cmd_write(0xcc,0x00,0x00,0x00,1);
	byte_read(reg_val,1);
      printk("Step 6:  ecc read 0x%x, new firmware 0x%x. \n", reg_val[0], bt_ecc);

    if(reg_val[0] != bt_ecc)
    {
	printk("step 6 :ecc error!\n");
        return ERR_ECC;
    }

    	/*********Step 7: reset the new FW***********************/
	cmd_write(0x07,0x00,0x00,0x00,1);
	delay_ms(300);

	 return ERR_OK;
}




#ifdef __cplusplus
}
#endif

