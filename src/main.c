#include <asf.h>

#include "keyboard.h"
#include "keyboard_i2c.h"

int main (void)
{
	system_init();
	
	delay_init();
	
	delay_ms(500);
	
	configure_pins();
	configure_adc();
	system_interrupt_enable_global();
	configure_usb_hid();

	configure_i2c();
	
	configure_tc();
	configure_dac();
	
	while (1)
	{
		// sleepmgr_enter_sleep();
	}
}
