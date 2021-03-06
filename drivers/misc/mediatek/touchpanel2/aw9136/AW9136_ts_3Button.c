/**************************************************************************
*  AW9136_ts_3button.c
* 
*  Create Date :
* 
*  Modify Date : 
*
*  Create by   : AWINIC Technology CO., LTD
*
*  Version     : 1.0 , 2014/06/27
*                2.0 , 2014/06/27
*                2.1 , 2014/07/01
*                2.2 , 2014/07/02
* 
**************************************************************************/
//////////////////////////////////////////////////////////////
//  
//  APPLICATION DEFINE :
//
//                   Mobile -    MENU      HOME      BACK
//                   AW9136 -     S3        S2        S1
//
//////////////////////////////////////////////////////////////

#include <linux/i2c.h>
#include <linux/input.h>
#include <linux/gpio.h>
#include <linux/earlysuspend.h>
#include <linux/interrupt.h>
#include <linux/delay.h>
#include <linux/irq.h>
#include <linux/firmware.h>
#include <linux/platform_device.h>

#include <linux/slab.h>
#include <linux/fs.h>
#include <linux/proc_fs.h>
#include <asm/uaccess.h>
#include <asm/io.h>
#include <linux/init.h>
#include <linux/pci.h>
#include <linux/dma-mapping.h>
#include <linux/gameport.h>
#include <linux/moduleparam.h>
#include <linux/mutex.h>

#include <mach/mt_gpio.h>
#include "AW9136_reg.h"
#include "AW9136_para.h"
#include "cust_gpio_usage.h"
#include <cust_eint.h>
#include <mach/mt_gpt.h>
#include <linux/wakelock.h>

#define TS_DEBUG_MSG 			0

//////////////////////////////////////
// IO PIN DEFINE
//////////////////////////////////////
#define AW9136_PDN          (GPIO5|0x80000000)
#define AW9136_EINT_NUM     4
#define AW9136_EINT_PIN     (GPIO4|0x80000000)

#define AW9136_ts_NAME	   	       	"AW9136_ts"
#define AW9136_ts_I2C_ADDR		     0x2C
#define AW9136_ts_I2C_BUS		     1

#define AW9136 
#define AW9136_EINT_SUPPORT   // use interrupt mode


#define ABS(x,y)         ((x>y)?(x-y):(y-x))

////////////////////////////////////////////////////////////////////
#ifdef AW_AUTO_CALI
#define CALI_NUM	2
#define CALI_RAW_MIN	1000
#define CALI_RAW_MAX	3000	

unsigned char cali_flag = 0;
unsigned char cali_num = 0;
unsigned char cali_cnt = 0;
unsigned char cali_used = 0;
unsigned char old_cali_dir[6];	//	0: no cali		1: ofr pos cali		2: ofr neg cali

unsigned int old_ofr_cfg[6];

long Ini_sum[6];
#endif
////////////////////////////////////////////////////////////////////
static int debug_level=0;

#if 1
#define TS_DBG printk
#endif



static int AW9136_create_sysfs(struct i2c_client *client);

struct aw9136_i2c_setup_data {
	unsigned i2c_bus;  //the same number as i2c->adap.nr in adapter probe function
	unsigned short i2c_address;
	int irq;
	char type[I2C_NAME_SIZE];
};

struct AW9136_ts_data {
	struct input_dev	*input_dev;
#ifdef AW9136_EINT_SUPPORT
	struct work_struct 	eint_work;
#else
	struct work_struct 	pen_event_work;
	struct workqueue_struct *ts_workqueue;
	struct timer_list touch_timer;
#endif
	struct early_suspend	early_suspend;
};

struct AW9136_ts_data *AW9136_ts;
struct wake_lock touchkey_wakelock;

//////////////////////////////////////////////////////
//
// Touch process variable
//
//////////////////////////////////////////////////////

static unsigned char suspend_flag = 0 ; //0: normal; 1: sleep

int WorkMode             = 1 ; //1: sleep, 2: normal

char	AW9136_UCF_FILENAME[50] = {0,};
unsigned int arraylen=59;

unsigned int romcode[59] = 
{
0x9f0a,0x800,0x900,0x3600,0x3,0x3700,0x190b,0x180a,0x1c00,0x1800,0x1008,0x520,0x1800,0x1004,0x513,0x1800,
0x1010,0x52d,0x3,0xa4ff,0xa37f,0xa27f,0x3cff,0x3cff,0x3cff,0xc4ff,0xc37f,0xc27f,0x38ff,0x387f,0xbf00,0x3,
0xa3ff,0xa47f,0xa27f,0x3cff,0x3cff,0x3cff,0xc3ff,0xc47f,0xc27f,0x38ff,0x387f,0xbf00,0x3,0xa2ff,0xa37f,0xa47f,
0x3cff,0x3cff,0x3cff,0xc2ff,0xc37f,0xc47f,0x38ff,0x387f,0xbf00,0x3,0x3402,
};

//////////////////////////////////////////////////////

static struct i2c_client *this_client;
static struct aw9136_i2c_setup_data AW9136_ts_setup={AW9136_ts_I2C_BUS, AW9136_ts_I2C_ADDR, 0, AW9136_ts_NAME};


#if 0
extern void mt65xx_eint_unmask(unsigned int line);
extern void mt65xx_eint_mask(unsigned int line);
extern void mt65xx_eint_set_polarity(unsigned int eint_num, unsigned int pol);
extern void mt65xx_eint_set_hw_debounce(unsigned int eint_num, unsigned int ms);
extern unsigned int mt65xx_eint_set_sens(unsigned int eint_num, unsigned int sens);
extern void mt65xx_eint_registration(unsigned int eint_num, unsigned int is_deb_en, unsigned int pol, void (EINT_FUNC_PTR)(void), unsigned int is_auto_umask);
#endif
#if 1//def MT6582
extern void mt_eint_mask(unsigned int eint_num);
extern void mt_eint_unmask(unsigned int eint_num);
extern void mt_eint_set_hw_debounce(unsigned int eint_num, unsigned int ms);
extern void mt_eint_set_polarity(unsigned int eint_num, unsigned int pol);
extern unsigned int mt_eint_set_sens(unsigned int eint_num, unsigned int sens);
extern void mt_eint_registration(unsigned int eint_num, unsigned int flow, void (EINT_FUNC_PTR)(void), unsigned int is_auto_umask);
extern void mt_eint_print_status(void);
#endif

//////////////////////////////////////////////////////
//
// PDN power control
//
//////////////////////////////////////////////////////
static void AW9136_ts_pwron(void)
{
    mt_set_gpio_mode(AW9136_PDN,GPIO_MODE_00);  
    mt_set_gpio_dir(AW9136_PDN,GPIO_DIR_OUT);
    mt_set_gpio_out(AW9136_PDN,GPIO_OUT_ONE);
    msleep(20);
}

static void AW9136_ts_pwroff(void)
{
	    mt_set_gpio_out(AW9136_PDN,GPIO_OUT_ZERO);  
}

static void AW9136_ts_config_pins(void)
{
	AW9136_ts_pwron();   //jed
	msleep(10); //wait for stable
}


//////////////////////////////////////////////////////
//
// i2c write and read
//
//////////////////////////////////////////////////////

unsigned int I2C_write_reg(unsigned char addr, unsigned int reg_data)
{

	   int ret,i;
	u8 wdbuf[512] = {0};

	wdbuf[0] = addr;

       wdbuf[2] = (unsigned char)(reg_data & 0x00ff);
	wdbuf[1] = (unsigned char)((reg_data & 0xff00)>>8);

	struct i2c_msg msgs[] = {
		{
			.addr	= this_client->addr,
			.flags	= 0,
			.len	= 3,
			.buf	= wdbuf,
		},
	};

	ret = i2c_transfer(this_client->adapter, msgs, 1);
	if (ret < 0)
		pr_err("msg %s i2c read error: %d\n", __func__, ret);

    return ret;

}


unsigned int I2C_read_reg(unsigned char addr)
{

	int ret,i;
	u8 rdbuf[512] = {0};
	unsigned int getdata;

	rdbuf[0] = addr;
	struct i2c_msg msgs[] = {
		{
			.addr	= this_client->addr,
			.flags	= 0,
			.len	= 1,
			.buf	= rdbuf,
		},
		{
			.addr	= this_client->addr,
			.flags	= I2C_M_RD,
			.len	= 2,
			.buf	= rdbuf,
		},
	};

	ret = i2c_transfer(this_client->adapter, msgs, 2);
	if (ret < 0)
		pr_err("msg %s i2c read error: %d\n", __func__, ret);

	getdata=rdbuf[0] & 0x00ff;
	getdata<<= 8;
	getdata |=rdbuf[1];

    return getdata;
	
}


//////////////////////////////////////////////////////////////////////
// AW9136 initial register @ mobile active
//////////////////////////////////////////////////////////////////////
void AW_NormalMode(void)
{
	unsigned int i;
	I2C_write_reg(GCR,0x0000);  // disable chip

        ///////////////////////////////////////
        // LED config
	I2C_write_reg(LER1,N_LER1);			//LED enable
	I2C_write_reg(LER2,N_LER2);			//LED enable

	//I2C_write_reg(CTRS1,0x0000);	//LED control RAM or I2C
	//I2C_write_reg(CTRS2,0x0000);	//LED control RAM or I2C
	I2C_write_reg(IMAX1,N_IMAX1);		//LED MAX light setting
	I2C_write_reg(IMAX2,N_IMAX2);		//LED MAX light setting
	I2C_write_reg(IMAX3,N_IMAX3);		//LED MAX light setting
	I2C_write_reg(IMAX4,N_IMAX4);		//LED MAX light setting
	I2C_write_reg(IMAX5,N_IMAX5);		//LED MAX light setting

	I2C_write_reg(LCR,N_LCR);				//LED effect control
	I2C_write_reg(IDLECR,N_IDLECR);		//IDLE time setting
        ///////////////////////////////////////
        // cap-touch config
	I2C_write_reg(SLPR,N_SLPR);   // touch key enable
	I2C_write_reg(SCFG1,N_SCFG1);  // scan time setting
	I2C_write_reg(SCFG2,N_SCFG2);		// bit0~3 is sense seting
	
	I2C_write_reg(OFR2,N_OFR2);   // offset
	I2C_write_reg(OFR3,N_OFR3);		 // offset

	I2C_write_reg(THR2, N_THR2);		//S1 press thred setting
	I2C_write_reg(THR3, N_THR3);		//S2 press thred setting
	I2C_write_reg(THR4, N_THR4);		//S3 press thred setting
	
	I2C_write_reg(SETCNT,N_SETCNT);  // debounce
	I2C_write_reg(BLCTH,N_BLCTH);   // base trace rate 
	
	I2C_write_reg(AKSR,N_AKSR);    // AKS 

#ifndef AW_AUTO_CALI
	I2C_write_reg(INTER,N_INTER);	 	// signel click interrupt 
#else
	I2C_write_reg(INTER,0x0080);	 	// signel click interrupt 
#endif

	I2C_write_reg(GDTR,N_GDTR);    // gesture time setting
	I2C_write_reg(GDCFGR,N_GDCFGR);	 //gesture  key select
	I2C_write_reg(TAPR1,N_TAPR1);		//double click 1
	I2C_write_reg(TAPR2,N_TAPR2);		//double click 2
	I2C_write_reg(TDTR,N_TDTR);			//double click time
#ifndef AW_AUTO_CALI
	I2C_write_reg(GIER,N_GIER);			//gesture and double click enable
#else
	I2C_write_reg(GIER,0x0000);			//gesture and double click disable
#endif	

        ///////////////////////////////////////
	I2C_write_reg(GCR,0x0003);    // LED enable and touch scan enable

        ///////////////////////////////////////
        // LED RAM program
/*
	I2C_write_reg(INTVEC,0x5);	
	I2C_write_reg(TIER,0x1c);  
	I2C_write_reg(PMR,0x0000);
	I2C_write_reg(RMR,0x0000);
	I2C_write_reg(WADDR,0x0000);

	for(i=0;i<59;i++)
	{
		I2C_write_reg(WDATA,N_romcode[i]);
	}

	I2C_write_reg(SADDR,0x0000);
	I2C_write_reg(PMR,0x0001);
	I2C_write_reg(RMR,0x0002);
*/
	WorkMode = 2;
	printk("AW9136 enter Normal mode\n");

}


//////////////////////////////////////////////////////////////////////
// AW9136 initial register @ mobile sleep
//////////////////////////////////////////////////////////////////////
void AW_SleepMode(void)
{
	unsigned int i;

	I2C_write_reg(GCR,0x0000);   // disable chip

        ///////////////////////////////////////
        // LED config
	I2C_write_reg(LER1,S_LER1);		//LED enable
	I2C_write_reg(LER2,S_LER2);		//LED enable
	//I2C_write_reg(CTRS1,0x0000);
	//I2C_write_reg(CTRS2,0x0000);

	I2C_write_reg(LCR,S_LCR);			//LED effect control
	I2C_write_reg(IDLECR,S_IDLECR);		//IDLE time setting

	I2C_write_reg(IMAX1,S_IMAX1);		//LED MAX light setting
	I2C_write_reg(IMAX2,S_IMAX2);		//LED MAX light setting
	I2C_write_reg(IMAX3,S_IMAX3);		//LED MAX light setting
	I2C_write_reg(IMAX4,S_IMAX4);		//LED MAX light setting
	I2C_write_reg(IMAX5,S_IMAX5);		//LED MAX light setting

        ///////////////////////////////////////
        // cap-touch config
	I2C_write_reg(SLPR,S_SLPR);   // touch key enable

	I2C_write_reg(SCFG1,S_SCFG1);  // scan time setting
	I2C_write_reg(SCFG2,S_SCFG2);		// bit0~3 is sense seting

	I2C_write_reg(OFR2,S_OFR2);   // offset
	I2C_write_reg(OFR3,S_OFR3);   // offset

	I2C_write_reg(THR2, S_THR2);		//S1 press thred setting
	I2C_write_reg(THR3, S_THR3);		//S2 press thred setting
	I2C_write_reg(THR4, S_THR4);		//S3 press thred setting
	
	I2C_write_reg(SETCNT,S_SETCNT);		// debounce
	I2C_write_reg(IDLECR, S_IDLECR);
	I2C_write_reg(BLCTH,S_BLCTH);		//base speed setting

	I2C_write_reg(AKSR,S_AKSR);			//AKS

#ifndef AW_AUTO_CALI
	I2C_write_reg(INTER,S_INTER);	 	// signel click interrupt 
#else
	I2C_write_reg(INTER,0x0080);	 	// signel click interrupt 
#endif


	I2C_write_reg(GDCFGR,S_GDCFGR); //gesture  key select
	I2C_write_reg(TAPR1,S_TAPR1);		//double click 1
	I2C_write_reg(TAPR2,S_TAPR2);		//double click 2

	I2C_write_reg(TDTR,S_TDTR);	//double click time
#ifndef AW_AUTO_CALI
	I2C_write_reg(GIER,S_GIER);			//gesture and double click enable
#else
	I2C_write_reg(GIER,0x0000);			//gesture and double click disable
#endif	

        ///////////////////////////////////////
	I2C_write_reg(GCR, 0x0003);   // enable chip sensor function
	
  ///////////////////////////////////////
  // LED RAM program
/*
	I2C_write_reg(INTVEC,0x5);	
	I2C_write_reg(TIER,0x1c);  
	I2C_write_reg(PMR,0x0000);
	I2C_write_reg(RMR,0x0000);
	I2C_write_reg(WADDR,0x0000);

	for(i=0;i<59;i++)
	{
		I2C_write_reg(WDATA,S_romcode[i]);
	}

	I2C_write_reg(SADDR,0x0000);
	I2C_write_reg(PMR,0x0001);
	I2C_write_reg(RMR,0x0002);
*/
	WorkMode = 1;
	printk("AW9136 enter Sleep mode\n");
}


void AW9136_LED_ON()
{
	I2C_write_reg(IMAX1,0x3300);
	I2C_write_reg(IMAX2,0x0003);
	I2C_write_reg(LER1,0x001c);
	I2C_write_reg(CTRS1,0x00fc);
	I2C_write_reg(CMDR,0xa2ff);  //LED1 ON
	I2C_write_reg(CMDR,0xa3ff);  //LED2 ON
	I2C_write_reg(CMDR,0xa4ff);  //LED3 ON
	I2C_write_reg(GCR,0x0003);
}

void AW9136_LED_OFF()
{
	I2C_write_reg(IMAX1,0x3300);
	I2C_write_reg(IMAX2,0x0003);
	I2C_write_reg(LER1,0x001c);
	I2C_write_reg(CTRS1,0x00fc);
	I2C_write_reg(CMDR,0xa200);  //LED1 OFF
	I2C_write_reg(CMDR,0xa300);  //LED2 OFF
	I2C_write_reg(CMDR,0xa400);  //LED3 OFF
	I2C_write_reg(GCR,0x0003);
}

////////////////////////////////////////////////////////////////////////
// AW91xx Auto Calibration
////////////////////////////////////////////////////////////////////////
#ifdef AW_AUTO_CALI
unsigned char AW91xx_Auto_Cali()
{
	unsigned char i;
	unsigned char cali_dir[6];

	unsigned int buf[6];
	unsigned int ofr_cfg[6];
	unsigned int sen_num;
	
	if(cali_num == 0)
	{
		ofr_cfg[0] = I2C_read_reg(0x13);
		ofr_cfg[1] = I2C_read_reg(0x14);
		ofr_cfg[2] = I2C_read_reg(0x15);
	}
	else
	{
		for(i=0; i<3; i++)
		{
			ofr_cfg[i] = old_ofr_cfg[i];
		}	
	}
	
	I2C_write_reg(0x1e,0x3);
	for(i=0; i<6; i++)
	{
		buf[i] = I2C_read_reg(0x36+i);
	}
	sen_num = I2C_read_reg(0x02);		// SLPR
	
	for(i=0; i<6; i++) 
		Ini_sum[i] = (cali_cnt==0)? (0) : (Ini_sum[i] + buf[i]);

	if(cali_cnt==4)
	{
		for(i=0; i<6; i++)
		{
			if((sen_num & (1<<i)) == 0)	// sensor used
			{
				if((Ini_sum[i]>>2) < CALI_RAW_MIN)
				{
					if((i%2) && ((ofr_cfg[i>>1]&0xFF00)==0x1000))	// 0x10** -> 0x00**
					{
						ofr_cfg[i>>1] = ofr_cfg[i>>1] & 0x00FF;
						cali_dir[i] = 2;
					}
					else if((i%2) && ((ofr_cfg[i>>1]&0xFF00)==0x0000))	// 0x00**	no calibration
					{
						cali_dir[i] = 0;
					}
					else if (((i%2)==0) && ((ofr_cfg[i>>1]&0x00FF)==0x0010))	// 0x**10 -> 0x**00
					{
						ofr_cfg[i>>1] = ofr_cfg[i>>1] & 0xFF00;
						cali_dir[i] = 2;				
					}
					else if (((i%2)==0) && ((ofr_cfg[i>>1]&0x00FF)==0x0000))	// 0x**00 no calibration
					{
						cali_dir[i] = 0;				
					}
					else 
					{
						ofr_cfg[i>>1] = ofr_cfg[i>>1] - ((i%2)? (1<<8):1);
						cali_dir[i] = 2;				
					}
				}
				else if((Ini_sum[i]>>2) > CALI_RAW_MAX)
				{
					if((i%2) && ((ofr_cfg[i>>1]&0xFF00)==0x1F00))	// 0x1F** no calibration
					{
						cali_dir[i] = 0;
					}
					else if (((i%2)==0) && ((ofr_cfg[i>>1]&0x00FF)==0x001F))	// 0x**1F no calibration
					{
						cali_dir[i] = 0;				
					}
					else
					{
						ofr_cfg[i>>1] = ofr_cfg[i>>1] + ((i%2)? (1<<8):1);				
						cali_dir[i] = 1;				
					}
				}
				else
				{
					cali_dir[i] = 0;
				}
				
				if(cali_num > 0)
				{
					if(cali_dir[i] != old_cali_dir[i])
					{
						cali_dir[i] = 0;
						ofr_cfg[i>>1] = old_ofr_cfg[i>>1];
					}
				}
			}
		}
		
		cali_flag = 0;
		for(i=0; i<6; i++)
		{
			if((sen_num & (1<<i)) == 0)	// sensor used
			{
				if(cali_dir[i] != 0)
				{
					cali_flag = 1;
				}			
			}
		}
		if((cali_flag==0) && (cali_num==0))
		{
			cali_used = 0;
		}
		else
		{
			cali_used = 1;
		}
		
		if(cali_flag == 0)
		{
			cali_num = 0;
			cali_cnt = 0;
			return 0;
		}
		
		I2C_write_reg(GCR, 0x0000);
		for(i=0; i<3; i++)
		{
			I2C_write_reg(OFR1+i, ofr_cfg[i]);
		}
		I2C_write_reg(GCR, 0x0003);
		
		if(cali_num == (CALI_NUM -1))	// no calibration
		{
			cali_flag = 0;
			cali_num = 0;
			cali_cnt = 0;
			
			return 0;
		}
		
		for(i=0; i<6; i++)
		{
			old_cali_dir[i] = cali_dir[i];
		}
		
		for(i=0; i<3; i++)
		{
			old_ofr_cfg[i] = ofr_cfg[i];
		}
		
		cali_num ++;
	}

	if(cali_cnt < 4)
	{
		cali_cnt ++;	
	}
	else
	{
		cali_cnt = 0;
	}
	
	return 1;
}
#endif

/////////////////////////////////////////////
// report left slip 
/////////////////////////////////////////////
void AW_left_slip(void)
{
	TS_DBG("AW9136 left slip \n");
	input_report_key(AW9136_ts->input_dev, KEY_F1, 1);
	input_sync(AW9136_ts->input_dev);
	input_report_key(AW9136_ts->input_dev, KEY_F1, 0);
	input_sync(AW9136_ts->input_dev);
}


/////////////////////////////////////////////
// report right slip 
/////////////////////////////////////////////
void AW_right_slip(void)
{
	TS_DBG("AW9136 right slip \n");
	input_report_key(AW9136_ts->input_dev, KEY_F2, 1);
	input_sync(AW9136_ts->input_dev);
	input_report_key(AW9136_ts->input_dev, KEY_F2, 0);
	input_sync(AW9136_ts->input_dev);
}


/////////////////////////////////////////////
// report MENU-BTN double click 
/////////////////////////////////////////////
void AW_left_double(void)
{
	TS_DBG("AW9136 Left double click \n");
	input_report_key(AW9136_ts->input_dev, KEY_PREVIOUSSONG, 1);
	input_sync(AW9136_ts->input_dev);
	input_report_key(AW9136_ts->input_dev, KEY_PREVIOUSSONG, 0);
	input_sync(AW9136_ts->input_dev);
}

/////////////////////////////////////////////
// report HOME-BTN double click 
/////////////////////////////////////////////
void AW_center_double(void)
{
	TS_DBG("AW9136 Center double click \n");
	input_report_key(AW9136_ts->input_dev, KEY_F3, 1);
	input_sync(AW9136_ts->input_dev);
	input_report_key(AW9136_ts->input_dev, KEY_F3, 0);
	input_sync(AW9136_ts->input_dev);
}
/////////////////////////////////////////////
// report BACK-BTN double click 
/////////////////////////////////////////////
void AW_right_double(void)
{
	TS_DBG("AW9136 Right_double click \n");
	input_report_key(AW9136_ts->input_dev, KEY_NEXTSONG, 1);
	input_sync(AW9136_ts->input_dev);
	input_report_key(AW9136_ts->input_dev, KEY_NEXTSONG, 0);
	input_sync(AW9136_ts->input_dev);
}

/////////////////////////////////////////////
// report MENU-BTN single click 
/////////////////////////////////////////////
void AW_left_press(void)
{
	input_report_key(AW9136_ts->input_dev, KEY_MENU, 1);
	input_sync(AW9136_ts->input_dev);
	TS_DBG("AW9136 left press \n");

}

/////////////////////////////////////////////
// report HOME-BTN single click 
/////////////////////////////////////////////
void AW_center_press(void)
{
	TS_DBG("AW9136 center press \n");
	input_report_key(AW9136_ts->input_dev, KEY_HOMEPAGE, 1);
	input_sync(AW9136_ts->input_dev);


}
/////////////////////////////////////////////
// report BACK-BTN single click 
/////////////////////////////////////////////
void AW_right_press(void)
{
	input_report_key(AW9136_ts->input_dev, KEY_BACK, 1);
	input_sync(AW9136_ts->input_dev);
	TS_DBG("AW9136 right press \n");
}

/////////////////////////////////////////////
// report MENU-BTN single click 
/////////////////////////////////////////////
void AW_left_release(void)
{

	input_report_key(AW9136_ts->input_dev, KEY_MENU, 0);
	input_sync(AW9136_ts->input_dev);
	TS_DBG("AW9136 left release\n");

}

/////////////////////////////////////////////
// report HOME-BTN single click 
/////////////////////////////////////////////
void AW_center_release(void)
{
	
	input_report_key(AW9136_ts->input_dev, KEY_HOMEPAGE, 0);
	input_sync(AW9136_ts->input_dev);
	TS_DBG("AW9136 center release \n");

}
/////////////////////////////////////////////
// report BACK-BTN single click 
/////////////////////////////////////////////
void AW_right_release(void)
{

	input_report_key(AW9136_ts->input_dev, KEY_BACK, 0);
	input_sync(AW9136_ts->input_dev);
	TS_DBG("AW9136 right release \n");
}


////////////////////////////////////////////////////
//
// Function : Cap-touch main program @ mobile sleep 
//            wake up after double-click/right_slip/left_slip
//
////////////////////////////////////////////////////
void AW_SleepMode_Proc(void)
{
	unsigned int buff1;
#ifdef AW_AUTO_CALI
	if(cali_flag)
	{
		AW91xx_Auto_Cali();
		if(cali_flag == 0)
		{	
			if(cali_used)
			{
				I2C_write_reg(GCR,0x0000);	 // disable chip
			}
			I2C_write_reg(INTER,S_INTER);			
			I2C_write_reg(GIER,S_GIER);
			if(cali_used)
			{
				I2C_write_reg(GCR,0x0003);	 // enable chip
			}
		}
		return ;
	}
#endif
	
	if(debug_level == 0)
	{
		buff1=I2C_read_reg(0x2e);			//read gesture interupt status
		if(buff1 == 0x10)
		{
				AW_center_double();
		}
		else if(buff1 == 0x01)
		{
				AW_right_slip();
		}
		else if (buff1 == 0x02)
		{
				AW_left_slip();					
		}
	
	}
}

////////////////////////////////////////////////////
//
// Function : Cap-touch main pragram @ mobile normal state
//            press/release
//
////////////////////////////////////////////////////
void AW_NormalMode_Proc(void)
{
	unsigned int buff1,buff2;
#ifdef AW_AUTO_CALI	
	if(cali_flag)
	{
		AW91xx_Auto_Cali();
		if(cali_flag == 0)
		{	
			if(cali_used)
			{
				I2C_write_reg(GCR,0x0000);	 // disable chip
			}
			I2C_write_reg(INTER,N_INTER);			
			I2C_write_reg(GIER,N_GIER);
			if(cali_used)
			{
				I2C_write_reg(GCR,0x0003);	 // enable chip
			}
		}
		return ;
	}
#endif
	if(debug_level == 0)
	{
		buff1=I2C_read_reg(0x31);
		buff2=I2C_read_reg(0x32);		//read key interupt status
		printk("buff1:0x%x,buff2:0x%x\n",buff1,buff2);
		if(buff2 & 0x10)								//S3 click
		{
				if(buff1 == 0x00)
				{
						AW_left_release();
				}
				else if(buff1 == 0x10)
				{
						AW_left_press();
				}

		}
		else if(buff2 & 0x08)					//S2 click
		{
				if(buff1 == 0x00)
				{
						AW_center_release();
				}
				else if (buff1 == 0x08)
				{
						AW_center_press();
				}


		}
		else if(buff2 & 0x04)					//S1 click
		{
				if(buff1 == 0x00)
				{
					   AW_right_release();
				}
				else if(buff1 == 0x04)
				{
					 AW_right_press();
				}
		}
	}

}

#ifdef AW9136_EINT_SUPPORT

static int AW9136_ts_clear_intr(struct i2c_client *client) 
{
	int res;

	res = I2C_read_reg(0x32);
	if(res <= 0)
	{
		goto EXIT_ERR;
	}
	else
	{
		res = 0;
	}

	return res;

EXIT_ERR:
	TS_DBG("AW9136_ts_clear_intr fail\n");
	return 1;
}

////////////////////////////////////////////////////
//
// Function : Interrupt sub-program
//            work in AW_SleepMode_Proc() or 
//            AW_NormalMode_Proc()
//
////////////////////////////////////////////////////
static void AW9136_ts_eint_work(struct work_struct *work)
{
	//printk("AW9136 Eint work \n");
	//if(debug_level == 0)
	{
		switch(WorkMode)
		{
			case 1:
			AW_SleepMode_Proc();
			break;
			
			case 2:
			AW_NormalMode_Proc();
			break;
			
			default:
			break;
		}
	
	}
	AW9136_ts_clear_intr(this_client);
	mt_eint_unmask(AW9136_EINT_NUM); 
}


void AW9136_ts_eint_func(void)
{	
	if(AW9136_ts == NULL)
	{
		return;
	}

	schedule_work(&AW9136_ts->eint_work);

}

int AW9136_ts_setup_eint(void)
{

    mt_set_gpio_mode(AW9136_EINT_PIN, GPIO_MODE_00);//eint mode
    mt_set_gpio_dir(AW9136_EINT_PIN, GPIO_DIR_IN);
    mt_set_gpio_pull_enable(AW9136_EINT_PIN, 1);
    mt_set_gpio_pull_select(AW9136_EINT_PIN, GPIO_PULL_UP);

    /*mt65xx_eint_set_sens(AW9136_EINT_NUM, 1);//level
    mt65xx_eint_set_polarity(AW9136_EINT_NUM, CUST_EINT_POLARITY_LOW);
    mt65xx_eint_set_hw_debounce(AW9136_EINT_NUM, 0);
    mt65xx_eint_registration(AW9136_EINT_NUM, 0, CUST_EINT_POLARITY_LOW, AW9136_ts_eint_func, 0);
    mt65xx_eint_unmask(AW9136_EINT_NUM);*/
  
   
    mt_eint_set_hw_debounce(AW9136_EINT_NUM, 0);
    mt_eint_registration(AW9136_EINT_NUM, CUST_EINTF_TRIGGER_LOW, AW9136_ts_eint_func, 0);
    mt_eint_unmask(AW9136_EINT_NUM);

    return 0;
}
#else

static void AW9136_ts_work(struct work_struct *work)
{
	switch(WorkMode)
	{
		case 1:
		AW_SleepMode_Proc();
		break;
		
		case 2:
		AW_NormalMode_Proc();
		break;
		
		default:
		break;
	}
	
}

void AW9136_tpd_polling(unsigned long unuse)
{
    struct AW9136_ts_data *data = i2c_get_clientdata(this_client);

    if (!work_pending(&data->pen_event_work)) 
	{
    	queue_work(data->ts_workqueue, &data->pen_event_work);
    }
	data->touch_timer.expires = jiffies + HZ/FRAME_RATE;
	add_timer(&data->touch_timer);
}
#endif

////////////////////////////////////////////////////
//
// Function : AW9136 initial @ mobile goto sleep mode
//            enter SleepMode
//
////////////////////////////////////////////////////
static void AW9136_ts_suspend(struct early_suspend *handler)
{
   
    
	struct AW9136_ts_data *data = i2c_get_clientdata(this_client);

		if(WorkMode != 1)
		{
			AW_SleepMode();
			suspend_flag = 1;
#ifdef AW_AUTO_CALI
		cali_flag = 1;
		cali_num = 0;
		cali_cnt = 0;
#endif			
		}
	   	printk("==AW9136_ts_suspend=\n");
#ifndef AW9136_EINT_SUPPORT
		del_timer(&data->touch_timer);
#endif

}

////////////////////////////////////////////////////
//
// Function : AW9136 initial @ mobile wake up
//            enter NormalMode 
//
////////////////////////////////////////////////////
static void AW9136_ts_resume(struct early_suspend *handler)
{	
	struct AW9136_ts_data *data = i2c_get_clientdata(this_client);

	if(WorkMode != 2)
	{
		AW_NormalMode();
		suspend_flag = 0;
#ifdef AW_AUTO_CALI
		cali_flag = 1;
		cali_num = 0;
		cali_cnt = 0;
#endif				
	}
	printk("AW9136 WAKE UP!!!");
#ifndef AW9136_EINT_SUPPORT
	data->touch_timer.expires = jiffies + HZ*5;
	add_timer(&data->touch_timer);
#endif
}

////////////////////////////////////////////////////
//
// Function : AW9136 initial @ mobile power on
//            enter NormalMode directly
//
////////////////////////////////////////////////////
static int AW9136_ts_probe(struct i2c_client *client, const struct i2c_device_id *id)
{

	struct input_dev *input_dev;
	int err = 0;
	unsigned int reg_value,reg_value1; 
	
	printk("==AW9136_ts_probe=\n");
	if (!i2c_check_functionality(client->adapter, I2C_FUNC_I2C)) {
		err = -ENODEV;
		goto exit_check_functionality_failed;
	}

	printk("==kzalloc=");
	AW9136_ts = kzalloc(sizeof(*AW9136_ts), GFP_KERNEL);
	if (!AW9136_ts)	{
		err = -ENOMEM;
		goto exit_alloc_data_failed;
	}

	AW9136_ts_config_pins();

	client->addr = 0x2C;								// chip  I2C address
	client->timing= 400;
	this_client = client;
	i2c_set_clientdata(client, AW9136_ts);
	//sc8810_i2c_set_clk(2,500000);

	printk("AW9136 I2C addr=%x\n", client->addr);
	
	reg_value = I2C_read_reg(0x00);				//read chip ID
    printk("AW9136 chip ID = 0x%4x\n", reg_value);

	if(reg_value != 0xb223)
	{
		err = -ENODEV;
		goto exit_create_singlethread;
	}


	memcpy(AW9136_UCF_FILENAME,"/data/AW9136ucf",15);
	
#ifdef AW9136_EINT_SUPPORT
		TS_DBG("AW9136 INIT_WORK\n");
		INIT_WORK(&AW9136_ts->eint_work, AW9136_ts_eint_work);
#else
		INIT_WORK(&AW9136_ts->pen_event_work, AW9136_ts_work);
	
		AW9136_ts->ts_workqueue = create_singlethread_workqueue(dev_name(&client->dev));
		if (!AW9136_ts->ts_workqueue) {
			err = -ESRCH;
			goto exit_create_singlethread;
		}
#endif

	input_dev = input_allocate_device();
	if (!input_dev) {
		err = -ENOMEM;
		dev_err(&client->dev, "failed to allocate input device\n");
		goto exit_input_dev_alloc_failed;
	}
	
	AW9136_ts->input_dev = input_dev;


	__set_bit(EV_KEY, input_dev->evbit);
	__set_bit(EV_SYN, input_dev->evbit);
	
	__set_bit(KEY_HOMEPAGE, input_dev->keybit);
	__set_bit(KEY_MENU, input_dev->keybit);
	__set_bit(KEY_BACK, input_dev->keybit);

	__set_bit(KEY_PREVIOUSSONG, input_dev->keybit);
	__set_bit(KEY_NEXTSONG, input_dev->keybit);

  /*F3 used as power key, others as sliding focus in camera*/
	__set_bit(KEY_F1, input_dev->keybit);
	__set_bit(KEY_F2, input_dev->keybit);
	__set_bit(KEY_F3, input_dev->keybit);
	__set_bit(KEY_F4, input_dev->keybit);
	__set_bit(KEY_F5, input_dev->keybit);
	__set_bit(KEY_F6, input_dev->keybit);
	__set_bit(KEY_F7, input_dev->keybit);
	__set_bit(KEY_F8, input_dev->keybit);
	__set_bit(KEY_F9, input_dev->keybit);
	__set_bit(KEY_F10, input_dev->keybit);
	__set_bit(KEY_F11, input_dev->keybit);
	__set_bit(KEY_F12, input_dev->keybit);
	input_dev->name		= AW9136_ts_NAME;		//dev_name(&client->dev)
	err = input_register_device(input_dev);
	if (err) {
		dev_err(&client->dev,
		"AW9136_ts_probe: failed to register input device: %s\n",
		dev_name(&client->dev));
		goto exit_input_register_device_failed;
	}

TS_DBG("==register_early_suspend =");
	AW9136_ts->early_suspend.level = EARLY_SUSPEND_LEVEL_DISABLE_FB + 1;
	AW9136_ts->early_suspend.suspend = AW9136_ts_suspend;
	AW9136_ts->early_suspend.resume	= AW9136_ts_resume;
	register_early_suspend(&AW9136_ts->early_suspend);
	

    msleep(50);

	AW9136_create_sysfs(client);
    
	WorkMode = 2;
	AW_NormalMode();

#ifdef AW_AUTO_CALI
	cali_flag = 1;
	cali_num = 0;
	cali_cnt = 0;
#endif
	reg_value1 = I2C_read_reg(0x01);
 	printk("AW9136 GCR = 0x%4x\n", reg_value1);

#ifdef AW9136_EINT_SUPPORT
   AW9136_ts_setup_eint();
#else
    AW9136_ts->touch_timer.function = AW9136_tpd_polling;
	AW9136_ts->touch_timer.data = 0;
	init_timer(&AW9136_ts->touch_timer);
	AW9136_ts->touch_timer.expires = jiffies + HZ*5;
	add_timer(&AW9136_ts->touch_timer);	
#endif

	printk("==probe over =\n");
    return 0;

exit_input_register_device_failed:
	input_free_device(input_dev);
exit_input_dev_alloc_failed:
	//free_irq(client->irq, AW9136_ts);
#ifdef AW9136_EINT_SUPPORT
	cancel_work_sync(&AW9136_ts->eint_work);
#else
	cancel_work_sync(&AW9136_ts->pen_event_work);
	destroy_workqueue(AW9136_ts->ts_workqueue);
#endif
exit_create_singlethread:
	TS_DBG("==singlethread error =\n");
	i2c_set_clientdata(client, NULL);
	kfree(AW9136_ts);
exit_alloc_data_failed:
exit_check_functionality_failed:
	//sprd_free_gpio_irq(AW9136_ts_setup.irq);
	return err;
}
/***********************************************************************************************
Name	:	 

Input	:	
                     

Output	:	

function	:	

***********************************************************************************************/
static int  AW9136_ts_remove(struct i2c_client *client)
{

	struct AW9136_ts_data *AW9136_ts = i2c_get_clientdata(client);

	TS_DBG("==AW9136_ts_remove=\n");
	
	unregister_early_suspend(&AW9136_ts->early_suspend);
	input_unregister_device(AW9136_ts->input_dev);

	//cancel_work_sync(&AW9136_ts->pen_event_work);
	//destroy_workqueue(AW9136_ts->ts_workqueue);
	
	kfree(AW9136_ts);
	
	i2c_set_clientdata(client, NULL);
	return 0;
}

static const struct i2c_device_id AW9136_ts_id[] = {
	{ AW9136_ts_NAME, 0 },{ }
};


MODULE_DEVICE_TABLE(i2c, AW9136_ts_id);

static struct i2c_board_info __initdata aw9136_i2c_led[] = {
	{
		I2C_BOARD_INFO(AW9136_ts_NAME, AW9136_ts_I2C_ADDR)	/* 0x2c */
	}
};

static struct i2c_driver AW9136_ts_driver = {
	.probe		= AW9136_ts_probe,
	.remove		= AW9136_ts_remove,
	.id_table	= AW9136_ts_id,
	//.suspend    = AW9136_ts_suspend,
	//.resume     = AW9136_ts_resume,
	.driver	= {
		.name	= AW9136_ts_NAME,
		.owner	= THIS_MODULE,
	},
};


//////////////////////////////////////////////////////
//
// for adb shell and APK debug
//
//////////////////////////////////////////////////////
static ssize_t AW9136_show_debug(struct device* cd,struct device_attribute *attr, char* buf);
static ssize_t AW9136_store_debug(struct device* cd, struct device_attribute *attr,const char* buf, size_t len);
static ssize_t AW9136_get_reg(struct device* cd,struct device_attribute *attr, char* buf);
static ssize_t AW9136_write_reg(struct device* cd, struct device_attribute *attr,const char* buf, size_t len);
static ssize_t AW9136_get_adbBase(struct device* cd,struct device_attribute *attr, char* buf);
static ssize_t AW9136_get_rawdata(struct device* cd,struct device_attribute *attr, char* buf);
static ssize_t AW9136_get_delta(struct device* cd,struct device_attribute *attr, char* buf);
static ssize_t AW_get_ramled(struct device* cd,struct device_attribute *attr, char* buf);
static ssize_t AW_set_ramled(struct device* cd,struct device_attribute *attr, char* buf);
static ssize_t AW9136_get_irqstate(struct device* cd,struct device_attribute *attr, char* buf);
	

static DEVICE_ATTR(debug, 0664, AW9136_show_debug, AW9136_store_debug);
static DEVICE_ATTR(getreg,  S_IWUGO | S_IRUGO, AW9136_get_reg,    AW9136_write_reg);
static DEVICE_ATTR(adbbase,  S_IWUGO | S_IRUGO, AW9136_get_adbBase,    NULL);
static DEVICE_ATTR(rawdata,  S_IWUGO | S_IRUGO, AW9136_get_rawdata,    NULL);
static DEVICE_ATTR(delta,  S_IWUGO | S_IRUGO, AW9136_get_delta,    NULL);
static DEVICE_ATTR(ramled,  S_IWUGO | S_IRUGO, AW_get_ramled,    AW_set_ramled);
static DEVICE_ATTR(getstate,  S_IWUGO | S_IRUGO, AW9136_get_irqstate,    NULL);



int AW_nvram_read(char *filename, char *buf, ssize_t len, int offset)
{	
    struct file *fd;
    //ssize_t ret;
    int retLen = -1;
    
    mm_segment_t old_fs = get_fs();
    set_fs(KERNEL_DS);
    
    fd = filp_open(filename, O_RDONLY, 0);
    
    if(IS_ERR(fd)) {
        printk("[AW9136][nvram_read] : failed to open!!\n");
        return -1;
    }
    do{
        if ((fd->f_op == NULL) || (fd->f_op->read == NULL))
    		{
            printk("[AW9136][nvram_read] : file can not be read!!\n");
            break;
    		} 
    		
        if (fd->f_pos != offset) {
            if (fd->f_op->llseek) {
        		    if(fd->f_op->llseek(fd, offset, 0) != offset) {
						printk("[AW9136][nvram_read] : failed to seek!!\n");
					    break;
        		    }
        	  } else {
        		    fd->f_pos = offset;
        	  }
        }    		
        
    		retLen = fd->f_op->read(fd,
    									  buf,
    									  len,
    									  &fd->f_pos);			
    		
    }while(false);
    
    filp_close(fd, NULL);
    
    set_fs(old_fs);
    
    return retLen;
}

int AW_nvram_write(char *filename, char *buf, ssize_t len, int offset)
{	
    struct file *fd;
    //ssize_t ret;
    int retLen = -1;
        
    mm_segment_t old_fs = get_fs();
    set_fs(KERNEL_DS);
    
    fd = filp_open(filename, O_WRONLY|O_CREAT, 0666);
    
    if(IS_ERR(fd)) {
        printk("[AW9136][nvram_write] : failed to open!!\n");
        return -1;
    }
    do{
        if ((fd->f_op == NULL) || (fd->f_op->write == NULL))
    		{
            printk("[AW9136][nvram_write] : file can not be write!!\n");
            break;
    		} /* End of if */
    		
        if (fd->f_pos != offset) {
            if (fd->f_op->llseek) {
        	    if(fd->f_op->llseek(fd, offset, 0) != offset) {
				    printk("[AW9136][nvram_write] : failed to seek!!\n");
                    break;
                }
            } else {
                fd->f_pos = offset;
            }
        }       		
        
        retLen = fd->f_op->write(fd,
                                 buf,
                                 len,
                                 &fd->f_pos);			
    		
    }while(false);
    
    filp_close(fd, NULL);
    
    set_fs(old_fs);
    
    return retLen;
}

//////////////////////////////////////////////////////////////////////
/* 	MIN(x1,x2,x3): min val in x1,x2,x3                              */
//////////////////////////////////////////////////////////////////////
unsigned int MIN(unsigned int x1, unsigned int x2, unsigned int x3)
{
	unsigned int min_val;
	
	if(x1>=x2) 
	{
		if(x2>=x3)
			min_val = x3;
		else
			min_val = x2;
	}
	else      
	{
		if(x1>=x3)
			min_val = x3;
		else
			min_val = x1;
	}

	return min_val;
}




static ssize_t AW9136_show_debug(struct device* cd,struct device_attribute *attr, char* buf)
{
	ssize_t ret = 0;
	
	sprintf(buf, "AW9136 Debug %d\n",debug_level);
	
	ret = strlen(buf) + 1;

	return ret;
}

static ssize_t AW9136_store_debug(struct device* cd, struct device_attribute *attr,
		       const char* buf, size_t len)
{
	unsigned long on_off = simple_strtoul(buf, NULL, 10);
	debug_level = on_off;

	TS_DBG("%s: debug_level=%d\n",__func__, debug_level);
	
	return len;
}



static ssize_t AW9136_get_reg(struct device* cd,struct device_attribute *attr, char* buf)
{
	unsigned int reg_val[1];
	ssize_t len = 0;
	u8 i;
	mt_eint_mask(AW9136_EINT_NUM);
	for(i=1;i<0x7F;i++)
	{
		reg_val[0] = I2C_read_reg(i);
		len += snprintf(buf+len, PAGE_SIZE-len, "reg%2X = 0x%4X, ", i,reg_val[0]);
	}
	mt_eint_unmask(AW9136_EINT_NUM);
	return len;

}

static ssize_t AW9136_write_reg(struct device* cd, struct device_attribute *attr,
		       const char* buf, size_t len)
{

	unsigned int databuf[2];
mt_eint_mask(AW9136_EINT_NUM);
	if(2 == sscanf(buf,"%x %x",&databuf[0], &databuf[1]))
	{
		I2C_write_reg((u8)databuf[0],databuf[1]);
	}
mt_eint_unmask(AW9136_EINT_NUM);
	return len;
}

static ssize_t AW9136_get_adbBase(struct device* cd,struct device_attribute *attr, char* buf)
{
	unsigned int dataS1,dataS2,dataS3,dataS4,dataS5,dataS6;
	ssize_t len = 0;
	mt_eint_mask(AW9136_EINT_NUM);
	len += snprintf(buf+len, PAGE_SIZE-len, "base: \n");
			I2C_write_reg(MCR,0x0003);
			
			dataS1=I2C_read_reg(0x36);
			dataS2=I2C_read_reg(0x37);
			dataS3=I2C_read_reg(0x38);
			dataS4=I2C_read_reg(0x39);
			dataS5=I2C_read_reg(0x3a);
			dataS6=I2C_read_reg(0x3b);
			len += snprintf(buf+len, PAGE_SIZE-len, "%d, ",dataS1);
			len += snprintf(buf+len, PAGE_SIZE-len, "%d, ",dataS2);
			len += snprintf(buf+len, PAGE_SIZE-len, "%d, ",dataS3);
			len += snprintf(buf+len, PAGE_SIZE-len, "%d, ",dataS4);
			len += snprintf(buf+len, PAGE_SIZE-len, "%d, ",dataS5);
			len += snprintf(buf+len, PAGE_SIZE-len, "%d, ",dataS6);

		len += snprintf(buf+len, PAGE_SIZE-len, "\n");
	mt_eint_unmask(AW9136_EINT_NUM);
	return len;
}

static ssize_t AW9136_get_rawdata(struct device* cd,struct device_attribute *attr, char* buf)
{
	unsigned int dataS1,dataS2,dataS3,dataS4,dataS5,dataS6;
	ssize_t len = 0;
	mt_eint_mask(AW9136_EINT_NUM);
	len += snprintf(buf+len, PAGE_SIZE-len, "base: \n");
			I2C_write_reg(MCR,0x0003);
			
			dataS1=I2C_read_reg(0x36);
			dataS2=I2C_read_reg(0x37);
			dataS3=I2C_read_reg(0x38);
			dataS4=I2C_read_reg(0x39);
			dataS5=I2C_read_reg(0x3a);
			dataS6=I2C_read_reg(0x3b);
			len += snprintf(buf+len, PAGE_SIZE-len, "%d, ",dataS1);
			len += snprintf(buf+len, PAGE_SIZE-len, "%d, ",dataS2);
			len += snprintf(buf+len, PAGE_SIZE-len, "%d, ",dataS3);
			len += snprintf(buf+len, PAGE_SIZE-len, "%d, ",dataS4);
			len += snprintf(buf+len, PAGE_SIZE-len, "%d, ",dataS5);
			len += snprintf(buf+len, PAGE_SIZE-len, "%d, ",dataS6);

		len += snprintf(buf+len, PAGE_SIZE-len, "\n");
	mt_eint_unmask(AW9136_EINT_NUM);
	return len;
}

static ssize_t AW9136_get_delta(struct device* cd,struct device_attribute *attr, char* buf)
{
	unsigned int deltaS1,deltaS2,deltaS3,deltaS4,deltaS5,deltaS6;
	ssize_t len = 0;
	mt_eint_mask(AW9136_EINT_NUM);
	len += snprintf(buf+len, PAGE_SIZE-len, "delta: \n");
			I2C_write_reg(MCR,0x0001);

			deltaS1=I2C_read_reg(0x36);if((deltaS1 & 0x8000) == 0x8000) { deltaS1 = 0; }
			deltaS2=I2C_read_reg(0x37);if((deltaS2 & 0x8000) == 0x8000) { deltaS2 = 0; }
			deltaS3=I2C_read_reg(0x38);if((deltaS3 & 0x8000) == 0x8000) { deltaS3 = 0; }
			deltaS4=I2C_read_reg(0x39);if((deltaS4 & 0x8000) == 0x8000) { deltaS4 = 0; }
			deltaS5=I2C_read_reg(0x3a);if((deltaS5 & 0x8000) == 0x8000) { deltaS5 = 0; }
			deltaS6=I2C_read_reg(0x3b);if((deltaS6 & 0x8000) == 0x8000) { deltaS6 = 0; }
			len += snprintf(buf+len, PAGE_SIZE-len, "%d, ",deltaS1);
			len += snprintf(buf+len, PAGE_SIZE-len, "%d, ",deltaS2);
			len += snprintf(buf+len, PAGE_SIZE-len, "%d, ",deltaS3);
			len += snprintf(buf+len, PAGE_SIZE-len, "%d, ",deltaS4);
			len += snprintf(buf+len, PAGE_SIZE-len, "%d, ",deltaS5);
			len += snprintf(buf+len, PAGE_SIZE-len, "%d, ",deltaS6);

		len += snprintf(buf+len, PAGE_SIZE-len, "\n");
	mt_eint_unmask(AW9136_EINT_NUM);
	return len;
}

static ssize_t AW9136_get_irqstate(struct device* cd,struct device_attribute *attr, char* buf)
{
	unsigned int keytouch,keyS1,keyS2,keyS3,keyS4,keyS5,keyS6;
	unsigned int gesture,slide1,slide2,slide3,slide4,doubleclick1,doubleclick2;
	ssize_t len = 0;
	mt_eint_mask(AW9136_EINT_NUM);
	len += snprintf(buf+len, PAGE_SIZE-len, "keytouch: \n");
			
			keytouch=I2C_read_reg(0x31);
			if((keytouch&0x1) == 0x1) keyS1=1;else keyS1=0;
			if((keytouch&0x2) == 0x2) keyS2=1;else keyS2=0;
			if((keytouch&0x4) == 0x4) keyS3=1;else keyS3=0;
			if((keytouch&0x8) == 0x8) keyS4=1;else keyS4=0;
			if((keytouch&0x10) == 0x10) keyS5=1;else keyS5=0;
			if((keytouch&0x20) == 0x20) keyS6=1;else keyS6=0;
			
			len += snprintf(buf+len, PAGE_SIZE-len, "%d, ",keyS1);
			len += snprintf(buf+len, PAGE_SIZE-len, "%d, ",keyS2);
			len += snprintf(buf+len, PAGE_SIZE-len, "%d, ",keyS3);
			len += snprintf(buf+len, PAGE_SIZE-len, "%d, ",keyS4);
			len += snprintf(buf+len, PAGE_SIZE-len, "%d, ",keyS5);
			len += snprintf(buf+len, PAGE_SIZE-len, "%d, ",keyS6);
			len += snprintf(buf+len, PAGE_SIZE-len, "\n");
			
	len += snprintf(buf+len, PAGE_SIZE-len, "gesture: \n");			
			gesture=I2C_read_reg(0x2e);
			if(gesture == 0x1) slide1=1;else slide1=0;
			if(gesture == 0x2) slide2=1;else slide2=0;
			if(gesture == 0x4) slide3=1;else slide3=0;
			if(gesture == 0x8) slide4=1;else slide4=0;
			if(gesture == 0x10) doubleclick1=1;else doubleclick1=0;
			if(gesture == 0x200) doubleclick2=1;else doubleclick2=0;

			len += snprintf(buf+len, PAGE_SIZE-len, "%d, ",slide1);
			len += snprintf(buf+len, PAGE_SIZE-len, "%d, ",slide2);
			len += snprintf(buf+len, PAGE_SIZE-len, "%d, ",slide3);
			len += snprintf(buf+len, PAGE_SIZE-len, "%d, ",slide4);
			len += snprintf(buf+len, PAGE_SIZE-len, "%d, ",doubleclick1);
			len += snprintf(buf+len, PAGE_SIZE-len, "%d, ",doubleclick2);

		len += snprintf(buf+len, PAGE_SIZE-len, "\n");
	mt_eint_unmask(AW9136_EINT_NUM);
	return len;
}






char DataToInt(char s_char)
{
	if((s_char>47)&&(s_char<58))   
		{   
			s_char-=48;   
		}   
		else   
			{
				if((s_char>64)&&(s_char<71))   
					{   
						s_char-=55;   
					}   
					else   
						{
							if((s_char>96)&&(s_char<103))   
							{   
								s_char-=87;   
							}   
						}
			}
		return s_char;
}

char AW9136_Get_UCF(void)
{
	int ret;
	int i=0;
	int j=0;
	static unsigned int buf[255];
	static unsigned char buff[1500];
	ret = AW_nvram_read(AW9136_UCF_FILENAME,&buff[0],1500,0);
	if(ret == -1)
	{
		printk("AW9136 NO UCF FILE,use default UCF\n");
		return 0;
	}
	else
	{		

			buff[i]=DataToInt(buff[i]);
			if(buff[i+1]==13&&buff[i+2]==10)//1bit
				{
					buf[j]=buff[i];
					i++;
					j++;
				}
			else if(buff[i+2]==13&&buff[i+3]==10)//2bit
				{
								
					buff[i+1]=DataToInt(buff[i+1]);
					buf[j]=10*buff[i]+buff[i+1];
					i=i+2;
					j++;
				}
			else if(buff[i+3]==13&&buff[i+4]==10)//3bit
				{
					buff[i+1]=DataToInt(buff[i+1]);
					buff[i+2]=DataToInt(buff[i+2]);
					buf[j]=10*10*buff[i]+10*buff[i+1]+buff[i+2];
					i=i+3;
					j++;
				}
			else if(buff[i+4]==13&&buff[i+5]==10)//4bit
				{
					buff[i+1]=DataToInt(buff[i+1]);
					buff[i+2]=DataToInt(buff[i+2]);
					buff[i+3]=DataToInt(buff[i+3]);
					buf[j]=10*10*10*buff[i]+10*10*buff[i+1]+10*buff[i+2]+buff[i+3];
					i=i+4;
					j++;

					}
			  while(i<ret)
				  	{
				  		if(buff[i]==32||buff[i]==10||buff[i]==13||buff[i]==44)
							{
								i++;
								continue;
				  			}
						else
							{

								buff[i+2]=DataToInt(buff[i+2]);
							    
								if(buff[i+3]==44)//1bit
									{
										buff[i+2]=DataToInt(buff[i+2]);
										buf[j]=buff[i+2];
										i=i+3;
										j++;
									}
								else
									{
										if(buff[i+4]==44)//2bit
											{									
												buff[i+3]=DataToInt(buff[i+3]);
												buff[i+2]=DataToInt(buff[i+2]);
												buf[j]=16*buff[i+2]+buff[i+3];
												i=i+4;
												j++;
											}
										else
											{
												if(buff[i+5]==44)//3bit
													{
														buff[i+4]=DataToInt(buff[i+4]);
														buff[i+3]=DataToInt(buff[i+3]);
														buff[i+2]=DataToInt(buff[i+2]);
														buf[j]=16*16*buff[i+2]+16*buff[i+3]+buff[i+4];
														i=i+5;
														j++;
													}
												else
													{
														if(buff[i+6]==44)//4bit
															{
																buff[i+5]=DataToInt(buff[i+5]);
																buff[i+4]=DataToInt(buff[i+4]);
																buff[i+3]=DataToInt(buff[i+3]);
																buff[i+2]=DataToInt(buff[i+2]);
																buf[j]=16*16*16*buff[i+2]+16*16*buff[i+3]+16*buff[i+4]+buff[i+5];
																i=i+6;
																j++;
															}
													}
														
											}

									}
							}
			  }
	
				
		arraylen=buf[0];
		for(i=0;i<arraylen;i++)
		{
			romcode[i]=buf[i+1];
		}
	
		
	}
	return 1;
}

static ssize_t AW_get_ramled(struct device* cd,struct device_attribute *attr, char* buf)
{
	unsigned char i,j;
	ssize_t len = 0;
	int ret;
		len += snprintf(buf+len, PAGE_SIZE-len, "romcode: \n");
		
		for(j=0;j<arraylen;j++)
		{
			len += snprintf(buf+len, PAGE_SIZE-len, "%x, ",romcode[j]);
		}
		len += snprintf(buf+len, PAGE_SIZE-len, "\n");
			return len;
}

static ssize_t AW_set_ramled(struct device* cd,struct device_attribute *attr, char* buf)
{
	unsigned char i,j;
	ssize_t len = 0;
	int ret;
	unsigned long on_off = simple_strtoul(buf, NULL, 10);
	
	if(on_off ==1)
	{
		    mt_eint_mask(AW9136_EINT_NUM);
			ret =AW9136_Get_UCF();
			
			if(ret == 1)
			{
				I2C_write_reg(GCR,0x0003);    // LED enable and touch scan enable
				  ///////////////////////////////////////
			        // LED RAM program
		
				I2C_write_reg(INTVEC,0x5);	
				I2C_write_reg(TIER,0x1c);  
				I2C_write_reg(PMR,0x0000);
				I2C_write_reg(RMR,0x0000);
				I2C_write_reg(WADDR,0x0000);
		
				for(i=0;i<arraylen;i++)
				{
					I2C_write_reg(WDATA,romcode[i]);
				}
		
				I2C_write_reg(SADDR,0x0000);
				I2C_write_reg(PMR,0x0001);
				I2C_write_reg(RMR,0x0002);
		

			}
			else
			{
				len += snprintf(buf+len, PAGE_SIZE-len, "AW9136 get ramled ucf fail!!! \n");
			}
		mt_eint_unmask(AW9136_EINT_NUM);
	}
		return len;
}

static int AW9136_create_sysfs(struct i2c_client *client)
{
	int err;
	struct device *dev = &(client->dev);

	TS_DBG("%s", __func__);
	
	err = device_create_file(dev, &dev_attr_debug);
	if(err < 0)
		{
			printk("[AW9136] failed to create file\n");
		}
	err = device_create_file(dev, &dev_attr_getreg);
	printk("err = %d\n",err);
	err = device_create_file(dev, &dev_attr_adbbase);
		printk("err = %d\n",err);
	err = device_create_file(dev, &dev_attr_rawdata);
		printk("err = %d\n",err);
	err = device_create_file(dev, &dev_attr_delta);
		printk("err = %d\n",err);
	err = device_create_file(dev, &dev_attr_ramled);
		printk("err = %d\n",err);
	err = device_create_file(dev, &dev_attr_getstate);
		printk("err = %d\n",err);
	return err;
}
/////////////////////////////////////**************************************************************************//////////////////////////////////////////



static const struct i2c_device_id aw9136_ts_ex_id[] = {{AW9136_ts_NAME,0},{}};
static struct i2c_board_info __initdata aw9136_ts_ex_i2c_tp={ I2C_BOARD_INFO(AW9136_ts_NAME, AW9136_ts_I2C_ADDR)};
static int aw9136_ts_ex_probe(struct platform_device *pdev);
static int aw9136_ts_ex_remove(struct platform_device *pdev);
static void aw9136_ts_ex_shutdown(struct platform_device *dev);

struct platform_device aw9136_ts_ex_device = 
{
		.name	 = AW9136_ts_NAME,
};

static struct platform_driver aw9136_ts_ex_driver =
{
		.driver	 = 
		{
			.name	 = AW9136_ts_NAME,
		},
		.probe 	 = aw9136_ts_ex_probe,
		.remove	 = aw9136_ts_ex_remove,
		.shutdown = aw9136_ts_ex_shutdown
};

static struct i2c_driver aw9136_ts_ex_i2c_driver = 
{  
		.probe 	 	= AW9136_ts_probe,
		.remove	 	= AW9136_ts_remove,
		.suspend	= NULL,
		.resume	 	= NULL,
		.id_table	= aw9136_ts_ex_id,
		.driver 	= 
		{
			.name			 = AW9136_ts_NAME,
		},
};
static int aw9136_ts_ex_probe(struct platform_device *pdev) 
{
		printk("----------------aw9136_ts_ex_probe--------------\n");
		if(i2c_add_driver(&aw9136_ts_ex_i2c_driver))
		{
			printk("----------------aw9136_ts_ex_probe---error-----------\n");
			return -1;
		}
		printk("----------------aw9136_ts_ex_probe---ok-----------\n");
		return 0;
}

static void aw9136_ts_ex_shutdown(struct platform_device *dev)
{
	return ;
}

static int aw9136_ts_ex_remove(struct platform_device *pdev)
{
		i2c_del_driver(&aw9136_ts_ex_i2c_driver);
		return 0;
}

/***********************************************************************************************
Name	:	 

Input	:	
                     

Output	:	

function	:	

***********************************************************************************************/
static int __init AW9136_ts_init(void)
{
	int ret;
	printk("==AW9136_ts_init==\n");
#if 0
    i2c_register_board_info(AW9136_ts_I2C_BUS, aw9136_i2c_led, 1);

    msleep(50);

	ret = i2c_add_driver(&AW9136_ts_driver);
	printk("ret = %d\n",ret);
  if(ret < 0)
  	{
  		printk("[AW9136] i2c_add_driver failed\n");
  	}
  #else
  	printk("----------------AW9136_ts_init--------------\n");
  	i2c_register_board_info(1, &aw9136_ts_ex_i2c_tp, 1); 
	
		ret = platform_device_register(&aw9136_ts_ex_device);
		printk("----------------AW9136_ts_init--------retval=%d------\n",ret);
		if(ret < 0)
		{
			return -2;
		}
		ret = platform_driver_register(&aw9136_ts_ex_driver);
		if(ret< 0)
		{
			 printk("----------------AW9136_ts_init--------platform_driver_register error----ret=%d--\n",ret);
			 return -1;
		}

			printk("-----------------AW9136_ts_init-end--------------\n");
  #endif	
	return ret;
}

/***********************************************************************************************
Name	:	 

Input	:	
                     

Output	:	

function	:	

***********************************************************************************************/
static void __exit AW9136_ts_exit(void)
{
	TS_DBG("==AW9136_ts_exit==\n");
	i2c_del_driver(&AW9136_ts_driver);
}




module_init(AW9136_ts_init);
module_exit(AW9136_ts_exit);

MODULE_AUTHOR("<lijunjiea@AWINIC.com>");
MODULE_DESCRIPTION("AWINIC AW9136 Touch driver");
MODULE_LICENSE("GPL");
