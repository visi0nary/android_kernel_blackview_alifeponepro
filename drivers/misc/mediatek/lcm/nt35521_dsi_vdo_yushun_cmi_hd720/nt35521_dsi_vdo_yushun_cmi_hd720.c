#ifndef BUILD_LK
#include <linux/string.h>
#include <mach/mt_gpio.h>
#else
#include <platform/mt_gpio.h>
#endif

#include "lcm_drv.h"
// ---------------------------------------------------------------------------
//  Local Constants
// ---------------------------------------------------------------------------
#include <cust_adc.h>
#define MIN_VOLTAGE (0)
#define MAX_VOLTAGE (200)
#define LCM_ID (0x8394)

#define FRAME_WIDTH  										(720)
#define FRAME_HEIGHT 										(1280)

#define REGFLAG_DELAY             							0xFE
#define REGFLAG_END_OF_TABLE      							0xFF   // END OF REGISTERS MARKER

// ---------------------------------------------------------------------------
//  Local Variables
// ---------------------------------------------------------------------------

static LCM_UTIL_FUNCS lcm_util = {0};

#define SET_RESET_PIN(v)    (lcm_util.set_reset_pin((v)))

#define UDELAY(n) (lcm_util.udelay(n))
#define MDELAY(n) (lcm_util.mdelay(n))


// ---------------------------------------------------------------------------
//  Local Functions
// ---------------------------------------------------------------------------
#define dsi_set_cmdq_V3(para_tbl,size,force_update)        	lcm_util.dsi_set_cmdq_V3(para_tbl,size,force_update)
#define dsi_set_cmdq_V2(cmd, count, ppara, force_update)	lcm_util.dsi_set_cmdq_V2(cmd, count, ppara, force_update)
#define dsi_set_cmdq(pdata, queue_size, force_update)		lcm_util.dsi_set_cmdq(pdata, queue_size, force_update)
#define wrtie_cmd(cmd)									lcm_util.dsi_write_cmd(cmd)
#define write_regs(addr, pdata, byte_nums)				lcm_util.dsi_write_regs(addr, pdata, byte_nums)
#define read_reg											lcm_util.dsi_read_reg()
#define read_reg_v2(cmd, buffer, buffer_size)			lcm_util.dsi_dcs_read_lcm_reg_v2(cmd, buffer, buffer_size)

extern int IMM_GetOneChannelValue(int dwChannel, int data[4], int* rawdata);
extern void dsi_enter_hs(bool enter);

static struct LCM_setting_table {
    unsigned cmd;
    unsigned char count;
    unsigned char para_list[64];
};
#ifndef BUILD_LK
extern atomic_t ESDCheck_byCPU;
#endif
static struct LCM_setting_table lcm_initialization_setting[] = {
	
	/*
	Note :

	Data ID will depends on the following rule.
	
		count of parameters > 1	=> Data ID = 0x39
		count of parameters = 1	=> Data ID = 0x15
		count of parameters = 0	=> Data ID = 0x05

	Structure Format :

	{DCS command, count of parameters, {parameter list}}
	{REGFLAG_DELAY, milliseconds of time, {}},

	...

	Setting ending by predefined flag
	
	{REGFLAG_END_OF_TABLE, 0x00, {}}
	*/

	{0xFF,4,{0xAA,0x55,0xA5,0x80}},
	
	{0x6F,2,{0x11,0x00}},
	{0xF7,2,{0x20,0x00}},
	{0x6F,1,{0x06}},
	{0xF7,1,{0xA0}},
	{0x6F,1,{0x19}},
	{0xF7,1,{0x12}},
	{0x6F,1,{0x10}},
	{0xF5,1,{0x70}},

	{0xF0,5,{0x55,0xAA,0x52,0x08,0x00}},
	{0xC8,1,{0x80}},
	{0xB1,2,{0x60,0x21}},
	{0xB6,1,{0x0F}},
	{0xBC,2,{0x00,0x00}},
	{0xBD,5,{0x02,0xB0,0x1E,0x1E,0x00}},

	{0xF0,5,{0x55,0xAA,0x52,0x08,0x01}},
	{0xBC,2,{0x78,0x00}},
	{0xBD,2,{0x78,0x00}},

	{0xCA,1,{0x01}},
	{0xC0,1,{0x0C}},
	
	{0xBE,1,{0x56}},//4E
	
	{0xB3,2,{0x38,0x38}},
	{0xB4,2,{0x11,0x11}},
	{0xB6,2,{0x05,0x05}},
	{0xB9,2,{0x35,0x35}},
	{0xBA,2,{0x14,0x14}},
	{0xC4,2,{0x11,0x11}},
	{0xCE,1,{0x66}},

	{0xF0,5,{0x55,0xAA,0x52,0x08,0x02}},
	
	{0xEE,1,{0x01}},	
	
	{0xB0,16,{0x00,0x00,0x00,0x11,0x00,0x32,0x00,0x52,0x00,0x68,0x00,0x8D,0x00,0xAB,0x00,0xD9}},	
	{0xB1,16,{0x00,0xFF,0x01,0x3B,0x01,0x69,0x01,0xB4,0x01,0xF1,0x01,0xF3,0x02,0x2B,0x02,0x6C}},	
	{0xB2,16,{0x02,0x90,0x02,0xC7,0x02,0xE9,0x03,0x1E,0x03,0x3C,0x03,0x69,0x03,0x7E,0x03,0x9E}},	
	{0xB3,4,{0x03,0xD9,0x03,0xFF}},


	{0xF0,5,{0x55,0xAA,0x52,0x08,0x06}},
	{0xB0,2,{0x29,0x2A}},
	{0xB1,2,{0x10,0x12}},
	{0xB2,2,{0x14,0x16}},
	{0xB3,2,{0x18,0x1A}},
	{0xB4,2,{0x02,0x04}},
	{0xB5,2,{0x34,0x34}},
	{0xB6,2,{0x34,0x2E}},
	{0xB7,2,{0x2E,0x2E}},
	{0xB8,2,{0x34,0x00}},
	{0xB9,2,{0x34,0x34}},
	{0xBA,2,{0x34,0x34}},
	{0xBB,2,{0x01,0x34}},
	{0xBC,2,{0x2E,0x2E}},
	{0xBD,2,{0x2E,0x34}},
	{0xBE,2,{0x34,0x34}},
	{0xBF,2,{0x05,0x03}},
	{0xC0,2,{0x1B,0x19}},
	{0xC1,2,{0x17,0x15}},
	{0xC2,2,{0x13,0x11}},
	{0xC3,2,{0x2A,0x29}},
	{0xE5,2,{0x2E,0x2E}},

{0xC4,2,{0x29,0x2A}},
{0xC5,2,{0x1B,0x19}},
{0xC6,2,{0x17,0x15}},
{0xC7,2,{0x13,0x11}},
{0xC8,2,{0x01,0x05}},
{0xC9,2,{0x34,0x34}},
{0xCA,2,{0x34,0x2E}},
{0xCB,2,{0x2E,0x2E}},
{0xCC,2,{0x34,0x03}},
{0xCD,2,{0x34,0x34}},
{0xCE,2,{0x34,0x34}},
{0xCF,2,{0x02,0x34}},
{0xD0,2,{0x2E,0x2E}},
{0xD1,2,{0x2E,0x34}},
{0xD2,2,{0x34,0x34}},
{0xD3,2,{0x04,0x00}},
{0xD4,2,{0x10,0x12}},
{0xD5,2,{0x14,0x16}},
{0xD6,2,{0x18,0x1A}},
{0xD7,2,{0x2A,0x29}},
{0xE6,2,{0x2E,0x2E}},
{0xD8,5,{0x00,0x00,0x00,0x54,0x00}},
{0xD9,5,{0x00,0x15,0x00,0x00,0x00}},
{0xE7,1,{0x00}},	

	{0xF0,5,{0x55,0xAA,0x52,0x08,0x03}},
	{0xB1,2,{0x00,0x00}},
	{0xB0,2,{0x00,0x00}},
	{0xB2,5,{0x05,0x00,0x00,0x00,0x00}},
	{0xB3,5,{0x05,0x00,0x00,0x00,0x00}},
	{0xB4,5,{0x05,0x00,0x00,0x00,0x00}},
	{0xB5,5,{0x05,0x00,0x17,0x00,0x00}},
	{0xB6,5,{0x12,0x00,0x19,0x00,0x00}},
	{0xB7,5,{0x12,0x00,0x19,0x00,0x00}},
	{0xB8,5,{0x12,0x00,0x19,0x00,0x00}},
	{0xB9,5,{0x12,0x00,0x19,0x00,0x00}},
	{0xBA,5,{0x57,0x00,0x00,0x00,0x00}},
	{0xBB,5,{0x57,0x00,0x00,0x00,0x00}},
	{0xBC,5,{0x75,0x00,0x1A,0x00,0x00}},
	{0xBD,5,{0x53,0x00,0x1A,0x00,0x00}},

	{0xC0,4,{0x00,0x34,0x00,0x00}},
	{0xC1,4,{0x00,0x34,0x00,0x00}},
	{0xC2,4,{0x00,0x34,0x00,0x00}},
	{0xC3,4,{0x00,0x34,0x00,0x00}},
	{0xC4,1,{0x20}},
	{0xC5,1,{0x00}},
	{0xC6,1,{0x00}},
	{0xC7,1,{0x00}},
	
	{0xF0,5,{0x55,0xAA,0x52,0x08,0x05}},
	{0xED,1,{0x30}},
	
	{0xB0,2,{0x17,0x06}},
	{0xB8,1,{0x08}},
	{0xBD,5,{0x03,0x07,0x00,0x03,0x00}},

	{0xB1,2,{0x17,0x06}},
	{0xB9,1,{0x00}},
	{0xB2,2,{0x00,0x00}},
	{0xBA,1,{0x00}},
	{0xB3,2,{0x17,0x06}},
	{0xBB,1,{0x0A}},
	{0xB4,2,{0x17,0x06}},
	{0xB5,2,{0x17,0x06}},
	{0xB6,2,{0x14,0x03}},
	{0xB7,2,{0x00,0x00}},
	{0xBC,1,{0x02}},
	
	{0xE5,1,{0x06}},
	{0xE6,1,{0x06}},
	{0xE7,1,{0x00}},
	{0xE8,1,{0x06}},
	{0xE9,1,{0x06}},
	{0xEA,1,{0x06}},
	{0xEB,1,{0x00}},
	{0xEC,1,{0x00}},

	{0xC0,1,{0x07}},
	{0xC1,1,{0x80}},
	{0xC2,1,{0xA4}},
	{0xC3,1,{0x05}},
	{0xC4,1,{0x00}},
	{0xC5,1,{0x02}},
	{0xC6,1,{0x22}},
	{0xC7,1,{0x03}},
	{0xC8,2,{0x05,0x30}},
	{0xC9,2,{0x01,0x31}},
	
	{0xCA,2,{0x03,0x21}},
	{0xCB,2,{0x01,0x20}},
	{0xD1,5,{0x00,0x05,0x09,0x07,0x10}},
	{0xD2,5,{0x10,0x05,0x0E,0x03,0x10}},
	{0xD3,5,{0x20,0x00,0x48,0x07,0x10}},
	{0xD4,5,{0x30,0x00,0x43,0x07,0x10}},
	{0xD0,1,{0x00}},
	{0xCC,3,{0x00,0x00,0x3E}},
	{0xCD,3,{0x00,0x00,0x3E}},
	{0xCE,3,{0x00,0x00,0x02}},	
	{0xCF,3,{0x00,0x00,0x02}}, 

	{0x6F,1,{0x11}},
    {0xF3,1,{0x01}},

	{0x11,1,{0x00}},
	{REGFLAG_DELAY, 120, {}},
	{0x29,1,{0x00}},
};


static struct LCM_setting_table lcm_set_window[] = {
    {0x2A, 4, {0x00, 0x00, (FRAME_WIDTH>>8), (FRAME_WIDTH&0xFF)}},
    {0x2B, 4, {0x00, 0x00, (FRAME_HEIGHT>>8), (FRAME_HEIGHT&0xFF)}},
    {REGFLAG_END_OF_TABLE, 0x00, {}}
};


static struct LCM_setting_table lcm_sleep_out_setting[] = {
    // Sleep Out
    {0x11, 0, {}},
    {REGFLAG_DELAY, 120, {}},

    // Display ON
    {0x29, 0, {}},
    {REGFLAG_END_OF_TABLE, 0x00, {}}
};


static struct LCM_setting_table lcm_deep_sleep_mode_in_setting[] = {
    // Display off sequence
    {0x28, 0, {}},
	{REGFLAG_DELAY, 35, {}},
    // Sleep Mode On
    {0x10, 0, {}},
    {REGFLAG_DELAY, 120, {}},
    {REGFLAG_END_OF_TABLE, 0x00, {}}
};


static struct LCM_setting_table lcm_backlight_level_setting[] = {
    {0x51, 1, {0xFF}},
    {REGFLAG_END_OF_TABLE, 0x00, {}}
};


static void push_table(struct LCM_setting_table *table, unsigned int count, unsigned char force_update)
{
	unsigned int i;

    for(i = 0; i < count; i++)
	{
		unsigned cmd;
		cmd = table[i].cmd;
		
		switch (cmd)
		{
			case REGFLAG_DELAY :
				MDELAY(table[i].count);
				break;

				case REGFLAG_END_OF_TABLE :
				break;
				
			default:
				dsi_set_cmdq_V2(cmd, table[i].count, table[i].para_list, force_update);
		}
	}
}


// ---------------------------------------------------------------------------
//  LCM Driver Implementations
// ---------------------------------------------------------------------------

static void lcm_set_util_funcs(const LCM_UTIL_FUNCS *util)
{
    memcpy(&lcm_util, util, sizeof(LCM_UTIL_FUNCS));
}


static void lcm_get_params(LCM_PARAMS *params)
{
    memset(params, 0, sizeof(LCM_PARAMS));

    params->type   = LCM_TYPE_DSI;
    params->width  = FRAME_WIDTH;
    params->height = FRAME_HEIGHT;

    // enable tearing-free
    params->dbi.te_mode 				= LCM_DBI_TE_MODE_VSYNC_ONLY;
    params->dbi.te_edge_polarity		= LCM_POLARITY_RISING;
    params->dsi.mode   = SYNC_PULSE_VDO_MODE;

    // DSI
    /* Command mode setting */
    params->dsi.LANE_NUM				= LCM_FOUR_LANE;
    //The following defined the fomat for data coming from LCD engine.
    params->dsi.data_format.color_order = LCM_COLOR_ORDER_RGB;
    params->dsi.data_format.trans_seq   = LCM_DSI_TRANS_SEQ_MSB_FIRST;
    params->dsi.data_format.padding     = LCM_DSI_PADDING_ON_LSB;
    params->dsi.data_format.format      = LCM_DSI_FORMAT_RGB888;

    // Highly depends on LCD driver capability.
    // Not support in MT6573
    params->dsi.packet_size=256;
    params->dsi.PS=LCM_PACKED_PS_24BIT_RGB888;

    params->dsi.vertical_sync_active				= 8;
    params->dsi.vertical_backporch					= 14;
    params->dsi.vertical_frontporch					= 20;
    params->dsi.vertical_active_line				= FRAME_HEIGHT;

    params->dsi.horizontal_sync_active				= 12;
    params->dsi.horizontal_backporch				= 60;
    params->dsi.horizontal_frontporch				= 60;
    params->dsi.horizontal_active_pixel				= FRAME_WIDTH;

	params->dsi.PLL_CLOCK = 218; //this value must be in MTK suggested table

    params->dsi.noncont_clock=1;
    params->dsi.noncont_clock_period=2;

	params->dsi.esd_check_enable = 1;
	params->dsi.customization_esd_check_enable = 1;
	params->dsi.lcm_esd_check_table[0].cmd          = 0x0a;
	params->dsi.lcm_esd_check_table[0].count        = 1;
	params->dsi.lcm_esd_check_table[0].para_list[0] = 0x9c;

}


static void lcm_init(void)
{
	mt_set_gpio_out(GPIO_LCD_BIAS_ENP_PIN, GPIO_OUT_ONE);
    mt_set_gpio_out(GPIO_LCD_BIAS_ENN_PIN, GPIO_OUT_ONE);  
    SET_RESET_PIN(1);
    SET_RESET_PIN(0);
    MDELAY(10);
    SET_RESET_PIN(1);
    MDELAY(20);
//	dsi_set_cmdq_V3(lcm_initialization_setting,sizeof(lcm_initialization_setting)/sizeof(lcm_initialization_setting[0]),1);
    push_table(lcm_initialization_setting, sizeof(lcm_initialization_setting) / sizeof(struct LCM_setting_table), 1);
}


static void lcm_suspend(void)
{
    push_table(lcm_deep_sleep_mode_in_setting, sizeof(lcm_deep_sleep_mode_in_setting) / sizeof(struct LCM_setting_table), 1);
	mt_set_gpio_out(GPIO_LCD_BIAS_ENP_PIN, GPIO_OUT_ZERO);
    mt_set_gpio_out(GPIO_LCD_BIAS_ENN_PIN, GPIO_OUT_ZERO);  
}


static void lcm_resume(void)
{
    lcm_init();
}


static void lcm_update(unsigned int x, unsigned int y,
                       unsigned int width, unsigned int height)
{
	unsigned int x0 = x;
	unsigned int y0 = y;
	unsigned int x1 = x0 + width - 1;
	unsigned int y1 = y0 + height - 1;

	unsigned char x0_MSB = ((x0>>8)&0xFF);
	unsigned char x0_LSB = (x0&0xFF);
	unsigned char x1_MSB = ((x1>>8)&0xFF);
	unsigned char x1_LSB = (x1&0xFF);
	unsigned char y0_MSB = ((y0>>8)&0xFF);
	unsigned char y0_LSB = (y0&0xFF);
	unsigned char y1_MSB = ((y1>>8)&0xFF);
	unsigned char y1_LSB = (y1&0xFF);

	unsigned int data_array[16];

	data_array[0]= 0x00053902;
	data_array[1]= (x1_MSB<<24)|(x0_LSB<<16)|(x0_MSB<<8)|0x2a;
	data_array[2]= (x1_LSB);
	data_array[3]= 0x00053902;
	data_array[4]= (y1_MSB<<24)|(y0_LSB<<16)|(y0_MSB<<8)|0x2b;
	data_array[5]= (y1_LSB);
	data_array[6]= 0x002c3909;

	dsi_set_cmdq(&data_array, 7, 0);

}


static void lcm_setbacklight(unsigned int level)
{
	unsigned int default_level = 145;
	unsigned int mapped_level = 0;

	//for LGE backlight IC mapping table
	if(level > 255) 
			level = 255;
 
	if(level >0) 
			mapped_level = default_level+(level)*(255-default_level)/(255);
	else
			mapped_level=0;

	// Refresh value of backlight level.
	lcm_backlight_level_setting[0].para_list[0] = mapped_level;

	push_table(lcm_backlight_level_setting, sizeof(lcm_backlight_level_setting) / sizeof(struct LCM_setting_table), 1);
}


static void lcm_setpwm(unsigned int divider)
{
	// TBD
}


static unsigned int lcm_getpwm(unsigned int divider)
{
	// ref freq = 15MHz, B0h setting 0x80, so 80.6% * freq is pwm_clk;
	// pwm_clk / 255 / 2(lcm_setpwm() 6th params) = pwm_duration = 23706
	unsigned int pwm_clk = 23706 / (1<<divider);	
	return pwm_clk;
}

static unsigned int lcm_compare_id(void)
{
	int array[4];
	char buffer[5];
	int id=0;

	SET_RESET_PIN(1);
	MDELAY(20);
	SET_RESET_PIN(0);
	MDELAY(20);
	SET_RESET_PIN(1);
	MDELAY(150);

	array[0] = 0x00033700;// read id return two byte,version and id
	dsi_set_cmdq(array, 1, 1);
	read_reg_v2(0x04, buffer, 3);
	id = buffer[1]; //we only need ID

	
   #ifdef BUILD_LK
		printf("zbuffer %s \n", __func__);
		printf("%s id = 0x%08x \n", __func__, id);
	#else
		printk("zbuffer %s \n", __func__);
		printk("%s id = 0x%08x \n", __func__, id);
	
   #endif
	 
  	return (0x80 == id)?1:0;
}

static unsigned int rgk_lcm_compare_id(void)
{
    int data[4] = {0,0,0,0};
    int res = 0;
    int rawdata = 0;
    int lcm_vol = 0;

#ifdef AUXADC_LCM_VOLTAGE_CHANNEL
    res = IMM_GetOneChannelValue(AUXADC_LCM_VOLTAGE_CHANNEL,data,&rawdata);
    if(res < 0)
    { 
	#ifdef BUILD_LK
	printf("[adc_uboot]: get data error\n");
	#endif
	return 0;
		   
    }
#endif

    lcm_vol = data[0]*1000+data[1]*10;

#ifdef BUILD_LK
	printf("[adc_uboot]: lcm_vol= %d\n",lcm_vol);
#else
	printk("[adc_kernel]: lcm_vol= %d\n",lcm_vol);
#endif
    if (lcm_vol>=MIN_VOLTAGE &&lcm_vol <= MAX_VOLTAGE)
    {
		return 1;
    }

    return 0;

}

static unsigned int lcm_ata_check(unsigned char *buffer)
{
#ifndef BUILD_LK
	 int array[4];
	 char buf[5];
	 int id=0;
	
	 array[0] = 0x00033700;// read id return two byte,version and id
	 dsi_set_cmdq(array, 1, 1);
	 atomic_set(&ESDCheck_byCPU,1);
	 read_reg_v2(0x04, buf, 3);
	 atomic_set(&ESDCheck_byCPU,0);
	 id = buf[1]; //we only need ID
	  
	 return (0x80 == id)?1:0;
#else
	return 0;
#endif
}

LCM_DRIVER nt35521_dsi_vdo_yushun_cmi_hd720_lcm_drv = 
{
    .name			= "nt35521_dsi_vdo_yushun_cmi_hd720",
    .set_util_funcs = lcm_set_util_funcs,
    .get_params     = lcm_get_params,
    .init           = lcm_init,
    .suspend        = lcm_suspend,
    .resume         = lcm_resume,
	.compare_id		= rgk_lcm_compare_id,
	.ata_check		= lcm_ata_check,
};
