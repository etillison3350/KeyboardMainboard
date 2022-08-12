#include "keyboard_i2c.h"

// Number of consecutive positive ADC high results before I2C data is sent.
// Since the ADC is read during the keyboard scan, this means that the
// peripheral device needs to be connected for at least 4 x 64 = 256ms before
// data is sent 
#define KBD_ADC_DELAY_CYCLES 64

#define KBD_I2C_MAX_WRITE 2

#define KBD_I2C_TX_BUFFER_SIZE 4

static bool write_i2c_data(uint8_t reg, uint8_t value);
static bool read_i2c_data(uint8_t reg);
static void start_next_transmission(struct i2c_master_module *const module);

struct i2c_transmission
{
	struct i2c_master_packet packet;
	bool is_read;
	bool started;
	struct
	{
		uint8_t reg;
		uint8_t values[KBD_I2C_MAX_WRITE - 1];
	} data;
};

static struct
{
	struct i2c_transmission data[KBD_I2C_TX_BUFFER_SIZE];
	volatile size_t head;
	volatile size_t tail;
	volatile size_t size;
} g_I2CTransmissionBuffer = { .head = 0, .tail = 0, .size = 0 };

static uint8_t g_I2CReceivedData[KBD_I2C_DATA_LEN];

static struct i2c_master_module g_I2CControllerInstance;

static struct adc_module g_adcInstance;
static uint16_t g_adcResult;

// Number of consecutive ADC HIGH results (see KBD_ADC_DELAY_CYCLES)
static volatile unsigned g_adcHighCycles = 0;

static i2c_kbd_data_callback_t g_I2CDataCallback;
static bool g_I2CDataCallbackEnable = false;


void configure_adc(void)
{
	struct adc_config adcconfig;
	adc_get_config_defaults(&adcconfig);
	
	adcconfig.gain_factor = ADC_GAIN_FACTOR_1X;
	adcconfig.clock_prescaler = ADC_CLOCK_PRESCALER_DIV8;
	adcconfig.reference = ADC_REFERENCE_INT1V;
	adcconfig.resolution = ADC_RESOLUTION_8BIT;
	adcconfig.positive_input = PIN_KBD_ID_CHAN;
	
	adcconfig.clock_source = GCLK_GENERATOR_3;
	
	adc_init(&g_adcInstance, ADC, &adcconfig);
	adc_enable(&g_adcInstance);
	
	adc_register_callback(&g_adcInstance, adc_complete_callback, ADC_CALLBACK_READ_BUFFER);
	adc_enable_callback(&g_adcInstance, ADC_CALLBACK_READ_BUFFER);
}

void configure_i2c_controller(void)
{
	struct i2c_master_config i2cconfig;
	i2c_master_get_config_defaults(&i2cconfig);
	
	i2cconfig.buffer_timeout = 65535;
	i2cconfig.generator_source = GCLK_GENERATOR_3;
	
	i2c_master_init(&g_I2CControllerInstance, KBD_I2C_SERCOM_IFACE, &i2cconfig);
	i2c_master_enable(&g_I2CControllerInstance);

	i2c_master_register_callback(&g_I2CControllerInstance, i2c_read_complete_callback, I2C_MASTER_CALLBACK_READ_COMPLETE);
	i2c_master_enable_callback(&g_I2CControllerInstance, I2C_MASTER_CALLBACK_READ_COMPLETE);
	i2c_master_register_callback(&g_I2CControllerInstance, i2c_write_complete_callback, I2C_MASTER_CALLBACK_WRITE_COMPLETE);
	i2c_master_enable_callback(&g_I2CControllerInstance, I2C_MASTER_CALLBACK_WRITE_COMPLETE);
	i2c_master_register_callback(&g_I2CControllerInstance, i2c_error_callback, I2C_MASTER_CALLBACK_ERROR);
	i2c_master_enable_callback(&g_I2CControllerInstance, I2C_MASTER_CALLBACK_ERROR);
}


inline void read_id_adc(void)
{
    i2c_master_cancel_job(&g_I2CControllerInstance);
	adc_read_buffer_job(&g_adcInstance, &g_adcResult, 1);
}


void adc_complete_callback(struct adc_module *const module)
{
	if (g_adcResult > 0xD2)
	{
		if (++g_adcHighCycles > KBD_ADC_DELAY_CYCLES)
		{
			read_i2c_data(KBD_I2C_REG_KEY_DATA);
		}
	}
	else
	{
		g_adcHighCycles = 0;
	}

	UNUSED(module);
}

bool write_i2c_data(uint8_t reg, uint8_t value)
{
	Assert(reg >= 0x80);

	system_interrupt_enter_critical_section();
	if (g_I2CTransmissionBuffer.size == KBD_I2C_TX_BUFFER_SIZE)
	{
		return false;
	}

	struct i2c_transmission *i2c_data = &g_I2CTransmissionBuffer.data[g_I2CTransmissionBuffer.tail];
	if (++g_I2CTransmissionBuffer.tail >= KBD_I2C_TX_BUFFER_SIZE)
	{
		g_I2CTransmissionBuffer.tail = 0;
	}

	i2c_data->is_read = false;
	i2c_data->started = false;

	i2c_data->packet.address = KBD_I2C_PERIPHERAL_ADDR;
	i2c_data->packet.ten_bit_address = false;
	i2c_data->packet.high_speed = false;

	i2c_data->data.reg = reg;
	i2c_data->data.values[0] = value;

	i2c_data->packet.data = (uint8_t *) &i2c_data->data;
	i2c_data->packet.data_length = 2;

	if (g_I2CTransmissionBuffer.size++ == 0)
	{
		start_next_transmission(&g_I2CControllerInstance);
	}
	system_interrupt_leave_critical_section();

	return true;
}

bool read_i2c_data(uint8_t reg)
{
	Assert(reg < 0x80);

	system_interrupt_enter_critical_section();
	if (g_I2CTransmissionBuffer.size == KBD_I2C_TX_BUFFER_SIZE)
	{
		return false;
	}

	struct i2c_transmission *i2c_data = &g_I2CTransmissionBuffer.data[g_I2CTransmissionBuffer.tail];
	if (++g_I2CTransmissionBuffer.tail >= KBD_I2C_TX_BUFFER_SIZE)
	{
		g_I2CTransmissionBuffer.tail = 0;
	}

	i2c_data->is_read = true;
	i2c_data->started = false;

	i2c_data->packet.address = KBD_I2C_PERIPHERAL_ADDR;
	i2c_data->packet.ten_bit_address = false;
	i2c_data->packet.high_speed = false;

	i2c_data->data.reg = reg;

	i2c_data->packet.data = (uint8_t *) &i2c_data->data;
	i2c_data->packet.data_length = 1;

	if (g_I2CTransmissionBuffer.size++ == 0)
	{
		start_next_transmission(&g_I2CControllerInstance);
	}
	system_interrupt_leave_critical_section();

	return true;
}

void i2c_write_complete_callback(struct i2c_master_module *const module)
{
	struct i2c_transmission *i2c_data = &g_I2CTransmissionBuffer.data[g_I2CTransmissionBuffer.head];
	if (i2c_data->is_read)
	{
		i2c_data->packet.data = (uint8_t *) g_I2CReceivedData;
		i2c_data->packet.data_length = KBD_I2C_DATA_LEN; // TODO: may vary based on register

		i2c_master_read_packet_job(&g_I2CControllerInstance, &i2c_data->packet);
	}
	else
	{
		i2c_master_send_stop(module);

		system_interrupt_enter_critical_section();
		if (++g_I2CTransmissionBuffer.head >= KBD_I2C_TX_BUFFER_SIZE)
		{
			g_I2CTransmissionBuffer.head = 0;
		}
		g_I2CTransmissionBuffer.size--;

		start_next_transmission(module);

		system_interrupt_leave_critical_section();
	}
}

void i2c_read_complete_callback(struct i2c_master_module *const module)
{
	const uint8_t reg = g_I2CTransmissionBuffer.data[g_I2CTransmissionBuffer.head].data.reg;
	g_I2CDataCallback(reg, g_I2CReceivedData);

	system_interrupt_enter_critical_section();
	if (++g_I2CTransmissionBuffer.head >= KBD_I2C_TX_BUFFER_SIZE)
	{
		g_I2CTransmissionBuffer.head = 0;
	}
	g_I2CTransmissionBuffer.size--;

	start_next_transmission(module);

	system_interrupt_leave_critical_section();
}

void i2c_error_callback(struct i2c_master_module *const module)
{
	system_interrupt_enter_critical_section();
	if (++g_I2CTransmissionBuffer.head >= KBD_I2C_TX_BUFFER_SIZE)
	{
		g_I2CTransmissionBuffer.head = 0;
	}
	g_I2CTransmissionBuffer.size--;

	start_next_transmission(module);

	system_interrupt_leave_critical_section();
}

void start_next_transmission(struct i2c_master_module *const module)
{
	if (g_I2CTransmissionBuffer.size == 0)
	{
		return;
	}

	struct i2c_transmission *const i2c_data = &g_I2CTransmissionBuffer.data[g_I2CTransmissionBuffer.head];
	if (i2c_data->started)
	{
		return;
	}
	i2c_data->started = true;

	i2c_master_write_packet_job_no_stop(module, &i2c_data->packet);
}


void i2c_kbd_data_register_callback(i2c_kbd_data_callback_t callback)
{
    g_I2CDataCallback = callback;
}

void i2c_kbd_data_enable_callback(void)
{
    g_I2CDataCallbackEnable = true;
}

void i2c_kbd_data_disable_callback(void)
{
    g_I2CDataCallbackEnable = false;
}

