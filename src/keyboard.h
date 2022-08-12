#ifndef KEYBOARD_H_
#define KEYBOARD_H_

#include <asf.h>

#include "keyboard_i2c.h"

#include "udi_hid_kbd.h"
#include "udi_hid_multimedia.h"

#define KEY_SET_DEFAULT    0x0000
#define KEY_SET_MULTIMEDIA 0xc000
#define KEY_SET_META       0xf000

#define NUM_ROWS 6
#define NUM_COLS 15

static const uint16_t KEYMAP[NUM_ROWS][NUM_COLS] = {
	{   0x29,   0x3a,   0x3b,   0x3c,   0x3d,   0x3e,   0x3f,   0x40,   0x41,   0x42,   0x43,   0x44,   0x45,   0x49,   0x4c },
	{   0x35,   0x1e,   0x1f,   0x20,   0x21,   0x22,   0x23,   0x24,   0x25,   0x26,   0x27,   0x2d,   0x2e,   0x2a,      0 },
	{   0x2b,   0x14,   0x1a,   0x08,   0x15,   0x17,   0x1c,   0x18,   0x0c,   0x12,   0x13,   0x2f,   0x30,      0,   0x31 },
	{   0x39,   0x04,   0x16,   0x07,   0x09,   0x0a,   0x0b,   0x0d,   0x0e,   0x0f,   0x33,   0x34,   0x28,      0,      0 },
	{ 0x0200,      0,   0x1d,   0x1b,   0x06,   0x19,   0x05,   0x11,   0x10,   0x36,   0x37,   0x38,   0x4a,   0x52,   0x4d },
	{ 0x0100, 0xf015, 0x0400, 0x0300,      0,      0,   0x2c,      0,      0, 0x0700, 0xf0a5, 0xf0b5,   0x50,   0x51,   0x4f }
};

#define PA(n) (n)
#define PB(n) (0x20 | (n))

static const uint8_t ROWMAP[NUM_ROWS] = { PB(8), PA(10), PA(11), PB(10), PB(11), PA(12) };
static const uint8_t COLMAP[NUM_COLS] = { PA(9), PA(8), PA(7), PA(6), PA(5), PA(4), PB(9), PA(22), PA(21), PA(20), PA(19), PA(18), PA(15), PA(14), PA(13) };

#define PIN_KBD_LED      2
#define PIN_KBD_LED_CHAN DAC_CHANNEL_0

void configure_pins(void);
void configure_tc(void);
void configure_i2c(void);
void configure_usb_hid(void);
void configure_dac(void);

void keyboard_scan_tc_callback(struct tc_module *const module);
// bool usb_keyboard_enable_callback(void);
// void usb_keyboard_disable_callback(void);
// void usb_keyboard_led_callback(uint8_t value);

void i2c_data_callback(uint8_t address, uint8_t* value);

#endif /* KEYBOARD_H_ */