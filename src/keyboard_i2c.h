#ifndef KEYBOARD_I2C_H_
#define KEYBOARD_I2C_H_

#include <asf.h>

#define PIN_KBD_ID      3
#define PIN_KBD_ID_CHAN ADC_POSITIVE_INPUT_PIN1

#define KBD_I2C_SERCOM_IFACE    SERCOM3
#define KBD_I2C_PERIPHERAL_ADDR 0x74

#define KBD_I2C_DATA_LEN 8

#define KBD_I2C_REG_KEY_DATA   0x00
#define KBD_I2C_REG_IND_LED    0x80
#define KBD_I2C_REG_BACKLIGHT  0x90


typedef void (*i2c_kbd_data_callback_t)(uint8_t, uint8_t*);


void configure_adc(void);
void configure_i2c_controller(void);

void read_id_adc(void);

void i2c_write_complete_callback(struct i2c_master_module *const module);
void i2c_read_complete_callback(struct i2c_master_module *const module);
void i2c_error_callback(struct i2c_master_module *const module);
void adc_complete_callback(struct adc_module *const module);

void i2c_kbd_data_register_callback(i2c_kbd_data_callback_t callback);
void i2c_kbd_data_enable_callback(void);
void i2c_kbd_data_disable_callback(void);


#endif /* KEYBOARD_I2C_H_ */