
#ifndef TOUCHPANEL_H__
#define TOUCHPANEL_H__

extern struct i2c_client *i2c_client;
//extern unsigned char *tpDMABuf_va = NULL;
//extern u32 tpDMABuf_pa = NULL;

typedef enum
{
	ERR_OK,
	ERR_MODE,
	ERR_READID,
	ERR_ERASE,
	ERR_STATUS,
	ERR_ECC
}E_UPGRADE_ERR_TYPE;


/* Pre-defined definition */
#define TPD_TYPE_CAPACITIVE
#define TPD_TYPE_RESISTIVE
#define TPD_POWER_SOURCE         MT6573_POWER_VGP2
#define TPD_I2C_NUMBER           0
#define TPD_WAKEUP_TRIAL         60
#define TPD_WAKEUP_DELAY         100

#define TPD_DELAY                (2*HZ/100)
//#define TPD_RES_X                480
//#define TPD_RES_Y                800
#define TPD_CALIBRATION_MATRIX  {962,0,0,0,1600,0,0,0};

#define TPD_HAVE_BUTTON
#define TPD_BUTTON_HEIGHT	480
#define TPD_KEY_COUNT           3
#define TPD_KEYS                { KEY_MENU, KEY_SEARCH,KEY_BACK}
#define TPD_KEYS_DIM            {{53,505,106,50},{159,505,106,50},{265,505,106,50}}

//#define TPD_HAVE_CALIBRATION
//#define TPD_HAVE_BUTTON
#define TPD_HAVE_TREMBLE_ELIMINATION
extern int ft5x02_i2c_rxdata(int txlen, unsigned char *rxdata, int length);
extern int read_reg(unsigned char addr);
extern int write_reg(unsigned char addr, char v);
extern int i2c_ft5x02_write_dma(struct i2c_client *client, uint8_t *data, uint8_t length);



extern E_UPGRADE_ERR_TYPE fts_ctpm_fw_upgrade_with_i_file(void);
extern E_UPGRADE_ERR_TYPE  fts_ctpm_fw_upgrade(unsigned char* pbt_buf, unsigned int dw_lenth);


#endif /* TOUCHPANEL_H__ */
