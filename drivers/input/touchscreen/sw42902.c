// SPDX-License-Identifier: GPL-2.0-only
/* Minimal mainline input driver for the LG/Silicon Works SW42902. */

#include <linux/delay.h>
#include <linux/gpio/consumer.h>
#include <linux/i2c.h>
#include <linux/input.h>
#include <linux/input/mt.h>
#include <linux/interrupt.h>
#include <linux/module.h>
#include <linux/property.h>
#include <linux/unaligned.h>

#define SW42902_MAX_TOUCHES	10
#define SW42902_TC_CMD		0x0c00
#define SW42902_TC_IC_STATUS	0x0600
#define SW42902_SERIAL_SPI_EN	0x0fe4

struct sw42902_touch_data {
#if defined(__LITTLE_ENDIAN_BITFIELD)
	u32 track_id:5;
	u32 tool_type:3;
	u32 angle:8;
	u32 event:2;
	u32 x:14;
	u32 y:14;
	u32 pressure:8;
	u32 reserved1:10;
	u32 reserved2:4;
	u32 width_major:14;
	u32 width_minor:14;
#else
#error "SW42902 report layout is only defined for little-endian targets"
#endif
} __packed;

struct sw42902_pen_data {
	u8 data[24];
} __packed;

struct sw42902_report {
	u32 ic_status;
	u32 tc_status;
#if defined(__LITTLE_ENDIAN_BITFIELD)
	u32 wakeup_type:8;
	u32 touch_count:5;
	u32 button_count:3;
	u32 pen_count:1;
	u32 abnormal_status:7;
	u32 current_mode:3;
	u32 reserved:4;
	u32 palm:1;
#endif
	struct sw42902_pen_data pen;
	struct sw42902_touch_data touches[SW42902_MAX_TOUCHES];
} __packed;

struct sw42902 {
	struct i2c_client *client;
	struct input_dev *input;
	struct gpio_desc *reset_gpio;
	struct gpio_desc *vdd_gpio;
	struct gpio_desc *vcl_gpio;
	struct mutex lock;
};

static int sw42902_read(struct sw42902 *ts, u16 reg, void *data, size_t len)
{
	u8 addr[2] = {
		(len > 4 ? 0x20 : 0x00) | ((reg >> 8) & 0x0f),
		reg & 0xff,
	};
	struct i2c_msg addr_msg = {
		.addr = ts->client->addr,
		.flags = 0,
		.len = sizeof(addr),
		.buf = addr,
	};
	struct i2c_msg data_msg = {
		.addr = ts->client->addr,
		.flags = I2C_M_RD,
		.len = len,
		.buf = data,
	};
	int ret;

	/* The downstream QCT driver terminates the address phase with STOP. */
	ret = i2c_transfer(ts->client->adapter, &addr_msg, 1);
	if (ret != 1)
		return ret < 0 ? ret : -EIO;

	ret = i2c_transfer(ts->client->adapter, &data_msg, 1);
	return ret == 1 ? 0 : ret < 0 ? ret : -EIO;
}

static int sw42902_write_u32(struct sw42902 *ts, u16 reg, u32 value)
{
	u8 data[6] = {
		0x40 | ((reg >> 8) & 0x0f), reg & 0xff,
	};

	put_unaligned_le32(value, &data[2]);
	return i2c_master_send(ts->client, data, sizeof(data)) == sizeof(data) ? 0 : -EIO;
}

static void sw42902_report(struct sw42902 *ts, const struct sw42902_report *report)
{
	unsigned long active = 0;
	unsigned int count = min_t(unsigned int, report->touch_count,
				   SW42902_MAX_TOUCHES);
	unsigned int i;

	if (report->wakeup_type != 0)
		return;

	for (i = 0; i < count; i++) {
		const struct sw42902_touch_data *touch = &report->touches[i];
		unsigned int id = touch->track_id;
		bool down;

		if (id >= SW42902_MAX_TOUCHES)
			continue;

		down = touch->event == 1 || touch->event == 2;
		input_mt_slot(ts->input, id);
		input_mt_report_slot_state(ts->input, MT_TOOL_FINGER, down);
		if (!down)
			continue;

		active |= BIT(id);
		input_report_abs(ts->input, ABS_MT_POSITION_X, touch->x);
		input_report_abs(ts->input, ABS_MT_POSITION_Y, touch->y);
		input_report_abs(ts->input, ABS_MT_PRESSURE, touch->pressure);
		input_report_abs(ts->input, ABS_MT_TOUCH_MAJOR, touch->width_major);
		input_report_abs(ts->input, ABS_MT_TOUCH_MINOR, touch->width_minor);
		input_report_abs(ts->input, ABS_MT_ORIENTATION, (s8)touch->angle);
	}

	for (i = 0; i < SW42902_MAX_TOUCHES; i++) {
		if (active & BIT(i))
			continue;
		input_mt_slot(ts->input, i);
		input_mt_report_slot_state(ts->input, MT_TOOL_FINGER, false);
	}

	input_mt_sync_frame(ts->input);
	input_sync(ts->input);
}

static irqreturn_t sw42902_irq_thread(int irq, void *data)
{
	struct sw42902 *ts = data;
	struct sw42902_report report;
	int ret;

	guard(mutex)(&ts->lock);
	ret = sw42902_read(ts, SW42902_TC_IC_STATUS, &report, sizeof(report));
	if (ret) {
		dev_err_ratelimited(&ts->client->dev, "failed to read report: %d\n", ret);
		return IRQ_HANDLED;
	}

	sw42902_report(ts, &report);
	return IRQ_HANDLED;
}

static void sw42902_power_off(void *data)
{
	struct sw42902 *ts = data;

	gpiod_set_value_cansleep(ts->reset_gpio, 0);
	usleep_range(1000, 2000);
	gpiod_set_value_cansleep(ts->vdd_gpio, 0);
	usleep_range(1000, 2000);
	gpiod_set_value_cansleep(ts->vcl_gpio, 0);
}

static int sw42902_hw_init(struct sw42902 *ts)
{
	u32 status;
	int ret;

	gpiod_set_value_cansleep(ts->vcl_gpio, 1);
	usleep_range(1000, 2000);
	gpiod_set_value_cansleep(ts->vdd_gpio, 1);
	msleep(10);
	gpiod_set_value_cansleep(ts->reset_gpio, 1);
	msleep(90);

	ret = sw42902_read(ts, SW42902_TC_IC_STATUS, &status, sizeof(status));
	if (ret)
		return dev_err_probe(&ts->client->dev, ret, "controller did not respond\n");

	ret = sw42902_write_u32(ts, SW42902_SERIAL_SPI_EN, 0);
	if (ret)
		return ret;
	ret = sw42902_write_u32(ts, SW42902_TC_CMD, 1);
	if (ret)
		return ret;
	ret = sw42902_write_u32(ts, SW42902_TC_CMD + 1, 1);
	if (ret)
		return ret;

	/* U0: normal display-on scanning mode. */
	ret = sw42902_write_u32(ts, SW42902_TC_CMD + 3, 1);
	if (ret)
		return ret;

	dev_info(&ts->client->dev, "controller ready, status %#x\n", status);
	return 0;
}

static int sw42902_probe(struct i2c_client *client)
{
	struct device *dev = &client->dev;
	struct sw42902 *ts;
	int ret;

	ts = devm_kzalloc(dev, sizeof(*ts), GFP_KERNEL);
	if (!ts)
		return -ENOMEM;

	ts->client = client;
	mutex_init(&ts->lock);
	i2c_set_clientdata(client, ts);

	ts->reset_gpio = devm_gpiod_get(dev, "reset", GPIOD_OUT_LOW);
	if (IS_ERR(ts->reset_gpio))
		return dev_err_probe(dev, PTR_ERR(ts->reset_gpio), "failed to get reset GPIO\n");
	ts->vdd_gpio = devm_gpiod_get(dev, "vdd", GPIOD_OUT_LOW);
	if (IS_ERR(ts->vdd_gpio))
		return dev_err_probe(dev, PTR_ERR(ts->vdd_gpio), "failed to get VDD GPIO\n");
	ts->vcl_gpio = devm_gpiod_get(dev, "vcl", GPIOD_OUT_LOW);
	if (IS_ERR(ts->vcl_gpio))
		return dev_err_probe(dev, PTR_ERR(ts->vcl_gpio), "failed to get VCL GPIO\n");

	ret = devm_add_action_or_reset(dev, sw42902_power_off, ts);
	if (ret)
		return ret;

	ts->input = devm_input_allocate_device(dev);
	if (!ts->input)
		return -ENOMEM;

	ts->input->name = "LG SW42902 Touchscreen";
	ts->input->id.bustype = BUS_I2C;
	input_set_abs_params(ts->input, ABS_MT_POSITION_X, 0, 1079, 0, 0);
	input_set_abs_params(ts->input, ABS_MT_POSITION_Y, 0, 2459, 0, 0);
	input_set_abs_params(ts->input, ABS_MT_PRESSURE, 0, 255, 0, 0);
	input_set_abs_params(ts->input, ABS_MT_TOUCH_MAJOR, 0, 2459, 0, 0);
	input_set_abs_params(ts->input, ABS_MT_TOUCH_MINOR, 0, 2459, 0, 0);
	input_set_abs_params(ts->input, ABS_MT_ORIENTATION, -128, 127, 0, 0);

	ret = input_mt_init_slots(ts->input, SW42902_MAX_TOUCHES,
				  INPUT_MT_DIRECT | INPUT_MT_DROP_UNUSED);
	if (ret)
		return ret;

	ret = sw42902_hw_init(ts);
	if (ret)
		return ret;

	ret = devm_request_threaded_irq(dev, client->irq, NULL, sw42902_irq_thread,
					IRQF_ONESHOT | IRQF_TRIGGER_FALLING,
					dev_name(dev), ts);
	if (ret)
		return dev_err_probe(dev, ret, "failed to request IRQ\n");

	return input_register_device(ts->input);
}

static const struct of_device_id sw42902_of_match[] = {
	{ .compatible = "lge,sw42902" },
	{ }
};
MODULE_DEVICE_TABLE(of, sw42902_of_match);

static struct i2c_driver sw42902_driver = {
	.driver = {
		.name = "sw42902",
		.of_match_table = sw42902_of_match,
	},
	.probe = sw42902_probe,
};
module_i2c_driver(sw42902_driver);

MODULE_DESCRIPTION("LG SW42902 touchscreen driver");
MODULE_LICENSE("GPL");
