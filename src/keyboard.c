#include "keyboard.h"

#include <string.h>

#define MAX_KEYPRESSES 6

static struct
{
	uint32_t row_mask_porta;
	uint32_t row_mask_portb;
	uint32_t col_mask_porta;
	uint32_t col_mask_portb;

	struct port_config row_read_config;
	struct port_config row_disable_config;
} g_keyPinConsts;

struct kbd_keypress_info
{
	uint8_t modifier_code;
	uint8_t multimedia_code;
	uint8_t keypress_array[MAX_KEYPRESSES];
	uint8_t num_keypresses;
};

static volatile bool g_enableKeyboard = false;
static volatile bool g_enableMultimedia = false;

static struct tc_module g_tcInstance;

static struct dac_module g_dacInstance;

static uint8_t g_I2CData[KBD_I2C_DATA_LEN];
static bool g_I2CHasData = false;

void configure_pins(void)
{
	// Calculate the row mask (for configuring multiple rows at a time)
	g_keyPinConsts.row_mask_porta = 0;
	g_keyPinConsts.row_mask_portb = 0;
	for (unsigned r = 0; r < NUM_ROWS; r++)
	{
		if ((ROWMAP[r] & 0x20) == 0)
		{
			g_keyPinConsts.row_mask_porta |= (1 << ROWMAP[r]);
		}
		else
		{
			g_keyPinConsts.row_mask_portb |= (1 << (ROWMAP[r] & 0x1f));
		}
	}

	// Fill config data for rows that are disabled (not currently being read)
	// These rows are set as tri-stated
	port_get_config_defaults(&g_keyPinConsts.row_disable_config);
	g_keyPinConsts.row_disable_config.powersave = true;
	port_group_set_config(&PORTA, g_keyPinConsts.row_mask_porta, &g_keyPinConsts.row_disable_config);
	port_group_set_config(&PORTB, g_keyPinConsts.row_mask_portb, &g_keyPinConsts.row_disable_config);

	// Fill config data for the actively-read row
	// This row is set as an output
	port_get_config_defaults(&g_keyPinConsts.row_read_config);
	g_keyPinConsts.row_read_config.direction = PORT_PIN_DIR_OUTPUT;

	// Calculate the column mask (for configuring/reading multiple columns at a time)
	g_keyPinConsts.col_mask_porta = 0;
	g_keyPinConsts.col_mask_portb = 0;
	for (unsigned c = 0; c < NUM_COLS; c++)
	{
		if ((COLMAP[c] & 0x20) == 0)
		{
			g_keyPinConsts.col_mask_porta |= (1 << COLMAP[c]);
		}
		else
		{
			g_keyPinConsts.col_mask_portb |= (1 << (COLMAP[c] & 0x1f));
		}
	}
	
	// Configure columns. Columns are set as inputs with pull-ups enabled
	struct port_config colconfig;
	port_get_config_defaults(&colconfig);
	colconfig.direction = PORT_PIN_DIR_INPUT;
	colconfig.input_pull = PORT_PIN_PULL_UP;
	port_group_set_config(&PORTA, g_keyPinConsts.col_mask_porta, &colconfig);
	port_group_set_config(&PORTB, g_keyPinConsts.col_mask_portb, &colconfig);
}

void configure_tc(void)
{
	struct tc_config timerconfig;
	tc_get_config_defaults(&timerconfig);
		
	timerconfig.counter_size = TC_COUNTER_SIZE_8BIT;
	timerconfig.clock_source = GCLK_GENERATOR_3;
	timerconfig.clock_prescaler = TC_CLOCK_PRESCALER_DIV256;
	timerconfig.counter_8_bit.period = 125;
	timerconfig.counter_8_bit.compare_capture_channel[0] = 100;
		
	tc_init(&g_tcInstance, TC3, &timerconfig);
	tc_enable(&g_tcInstance);
		
	tc_register_callback(&g_tcInstance, keyboard_scan_tc_callback, TC_CALLBACK_CC_CHANNEL0);
	tc_enable_callback(&g_tcInstance, TC_CALLBACK_CC_CHANNEL0);
}

void configure_i2c(void)
{
	configure_i2c_controller();

	i2c_kbd_data_register_callback(i2c_data_callback);
	i2c_kbd_data_enable_callback();
}

void configure_usb_hid(void)
{
	udc_start();
}

void configure_dac(void)
{
	struct dac_config dacconfig;
	dac_get_config_defaults(&dacconfig);
	
	dacconfig.reference = DAC_REFERENCE_AVCC;
	dacconfig.left_adjust = false;
	dacconfig.clock_source = GCLK_GENERATOR_3;
	
	dac_init(&g_dacInstance, DAC, &dacconfig);
	dac_enable(&g_dacInstance);
	
	
	struct dac_chan_config chanconfig;
	dac_chan_get_config_defaults(&chanconfig);
	
	dac_chan_set_config(&g_dacInstance, PIN_KBD_LED_CHAN, &chanconfig);
	dac_chan_enable(&g_dacInstance, PIN_KBD_LED_CHAN);
}

static inline void handle_keypress(uint16_t key_id, struct kbd_keypress_info* keyinfo)
{
	switch (key_id & 0xf000)
	{
		case KEY_SET_META:
			// TODO: Not implemented
			break;
		case KEY_SET_MULTIMEDIA:
			keyinfo->multimedia_code |= key_id & 0xFF;
			break;
		default:
		{
			uint8_t modcode = (key_id >> 8) & 0xF;
			if (modcode > 0)
			{
				keyinfo->modifier_code |= (1 << (modcode - 1));
			}

			uint8_t keycode = key_id & 0xFF;
			if (keycode > 0)
			{
				if (keyinfo->num_keypresses < 6)
				{
					keyinfo->keypress_array[keyinfo->num_keypresses] = keycode;
				}
				keyinfo->num_keypresses++;
			}

			break;
		}
	}
}

void keyboard_scan_tc_callback(struct tc_module *const module)
{
	if (!g_enableKeyboard && !g_enableMultimedia)
	{
		return;
	}
	
	struct kbd_keypress_info keyinfo;
	memset(&keyinfo, 0, sizeof(struct kbd_keypress_info));

	system_interrupt_enter_critical_section();
	if (g_I2CHasData)
	{
		keyinfo.modifier_code |= g_I2CData[0];
		keyinfo.multimedia_code |= g_I2CData[1];

		for (unsigned i = 2; i < KBD_I2C_DATA_LEN; i++)
		{
			if (g_I2CData[i] != 0)
			{
				if (keyinfo.num_keypresses < 6)
				{
					keyinfo.keypress_array[keyinfo.num_keypresses] = g_I2CData[i];
				}
				keyinfo.num_keypresses++;
			}
		}
	}
	system_interrupt_leave_critical_section();

	for (unsigned r = 0; r < NUM_ROWS; r++)
	{
		const uint32_t row_port_bitmask = 1 << (ROWMAP[r] & 0x1f);
		if ((ROWMAP[r] & 0x20) == 0)
		{
			port_group_set_config(&PORTA, g_keyPinConsts.row_mask_porta & (~row_port_bitmask), &g_keyPinConsts.row_disable_config);
			port_group_set_config(&PORTA, row_port_bitmask, &g_keyPinConsts.row_read_config);
			port_group_set_output_level(&PORTA, row_port_bitmask, 0);
			
			port_group_set_config(&PORTB, g_keyPinConsts.row_mask_portb, &g_keyPinConsts.row_disable_config);
		}
		else
		{
			port_group_set_config(&PORTB, g_keyPinConsts.row_mask_portb & (~row_port_bitmask), &g_keyPinConsts.row_disable_config);
			port_group_set_config(&PORTB, row_port_bitmask, &g_keyPinConsts.row_read_config);
			port_group_set_output_level(&PORTB, row_port_bitmask, 0);
			
			port_group_set_config(&PORTA, g_keyPinConsts.row_mask_porta, &g_keyPinConsts.row_disable_config);
		}

		const uint32_t col_porta = port_group_get_input_level(&PORTA, g_keyPinConsts.col_mask_porta);
		const uint32_t col_portb = port_group_get_input_level(&PORTB, g_keyPinConsts.col_mask_portb);
		for (unsigned c = 0; c < NUM_COLS; c++)
		{
			bool is_pressed;
			if ((COLMAP[c] & 0x20) == 0)
			{
				is_pressed = (col_porta & (1 << COLMAP[c])) == 0;
			}
			else
			{
				is_pressed = (col_portb & (1 << (COLMAP[c] & 0x1f))) == 0;
			}
			
			if (is_pressed)
			{
				handle_keypress(KEYMAP[r][c], &keyinfo);
			}
		}
	}
	port_group_set_config(&PORTA, g_keyPinConsts.row_mask_porta, &g_keyPinConsts.row_disable_config);
	port_group_set_config(&PORTB, g_keyPinConsts.row_mask_portb, &g_keyPinConsts.row_disable_config);

	if (g_enableKeyboard)
	{
		if (keyinfo.num_keypresses > 6)
		{
			memset(keyinfo.keypress_array, 0x01, 6);
		}
		udi_hid_kbd_send_event(keyinfo.modifier_code, keyinfo.keypress_array);
	}

	if (g_enableMultimedia)
	{
		udi_hid_multimedia_send_event(keyinfo.multimedia_code);
	}

	read_id_adc();

	UNUSED(module);
}


bool hid_keyboard_enable_callback(void)
{
	g_enableKeyboard = true;
	return true;
}

void hid_keyboard_disable_callback(void)
{
	g_enableKeyboard = false;
}


bool hid_multimedia_enable_callback(void)
{
	g_enableMultimedia = true;
	return true;
}

void hid_multimedia_disable_callback(void)
{
	g_enableMultimedia = false;
}


void i2c_data_callback(uint8_t address, uint8_t* value)
{
	switch (address)
	{
		case KBD_I2C_REG_KEY_DATA:
			memcpy(g_I2CData, value, KBD_I2C_DATA_LEN);
			g_I2CHasData = true;
			break;
	}
}
