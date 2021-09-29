// SPDX-License-Identifier: GPL-2.0-only

/*
 * Copyright (C) 2021 Etienne Buira <etienne.buira@free.fr>
 *
 * Hardware monitoring on some Asus motherboards.
 *
 * Driver made using unreliable sources:
 * 	- reverse-engineering of WMI ASL
 * 	- documentation of another superio chip of the same family
 * 	  (Nuvoton does not release appropriate datasheet publicly)
 * Use at your own risks!
 */

#include <linux/hwmon.h>
#include <linux/hwmon-sysfs.h>
#include <linux/module.h>
#include <linux/dmi.h>
#include <linux/acpi.h>
#include <linux/limits.h>

static const struct dmi_system_id asus_accepted_dmis[] = {
	{
		.ident = "Prime B550-Plus",
		.matches = {
			DMI_EXACT_MATCH(DMI_BOARD_VENDOR, "ASUSTeK COMPUTER INC."),
			DMI_EXACT_MATCH(DMI_BOARD_NAME, "PRIME B550-PLUS"),
			DMI_EXACT_MATCH(DMI_BIOS_VERSION, "2006"),
			/* TODO: add check about the superio */
		},
	},
	{ .matches = { { .slot = DMI_NONE } } }
};

struct asus_primeb550plus_hwmon_data {
	struct acpi_device *acpi_dev;
	struct device *device;
	acpi_handle acpi_dev_handle;

	acpi_handle rhwm_method;
};

static const struct acpi_device_id asus_primeb550plus_hwmon_acpi_ids[] = {
	{"PNP0C14"},
	{""},
};
MODULE_DEVICE_TABLE(acpi, asus_primeb550plus_hwmon_acpi_ids);

static int asus_primeb550plus_hwmon_read_u8(struct asus_primeb550plus_hwmon_data *devdri_data, unsigned char bank, unsigned char index, unsigned char *dest)
{
	unsigned char a_buf[2] = { bank, index };
	union acpi_object buf_args = {
		.type = ACPI_TYPE_BUFFER,
		.buffer = {
			.type = ACPI_TYPE_BUFFER,
			.length = 2,
			.pointer = a_buf,
		},
	};
	struct acpi_object_list acpi_args = {
		.count = 1,
		.pointer = &buf_args,
	};
	unsigned long long val;
	acpi_status a_r;

	a_r = acpi_evaluate_integer(devdri_data->rhwm_method, NULL, &acpi_args, &val);
	if (!ACPI_SUCCESS(a_r))
		return -1;
	
	if (val > U8_MAX)
		return -1;

	*dest = val;
	return 0;
}

enum asus_primeb550plus_hwmon_data_type {
	ASUS_B550PLUS_HWMON_DATA_TYPE_UCHAR_MUL,
	ASUS_B550PLUS_HWMON_DATA_TYPE_TEMP_9BIT,
	ASUS_B550PLUS_HWMON_DATA_TYPE_TEMP_8BIT,
	ASUS_B550PLUS_HWMON_DATA_TYPE_TEMP_14BIT,
	ASUS_B550PLUS_HWMON_DATA_TYPE_PWM16,
};

struct asus_primeb550plus_hwmon_data_type_uchar_mul {
	unsigned char bank_no;
	unsigned char index_in_bank;
	int multiplier;
};

struct asus_primeb550plus_hwmon_data_type_temp_9bit {
	unsigned char int_bank_no;
	unsigned char int_index_in_bank;
	unsigned char frac_bank_no;
	unsigned char frac_index_in_bank;
};

struct asus_primeb550plus_hwmon_data_type_temp_8bit {
	unsigned char bank_no;
	unsigned char index_in_bank;
};

struct asus_primeb550plus_hwmon_data_type_temp_14bit {
	unsigned char int_bank_no;
	unsigned char int_idx;
	unsigned char frac_bank_no;
	unsigned char frac_idx;
};

struct asus_primeb550plus_hwmon_data_type_pwm16 {
	unsigned char high_bank_no;
	unsigned char high_idx;
	unsigned char low_bank_no;
	unsigned char low_idx;
};

struct asus_primeb550plus_hwmon_chip_field {
	enum asus_primeb550plus_hwmon_data_type data_type;
	union {
		struct asus_primeb550plus_hwmon_data_type_uchar_mul uchar_mul;
		struct asus_primeb550plus_hwmon_data_type_temp_9bit temp_9bit;
		struct asus_primeb550plus_hwmon_data_type_temp_8bit temp_8bit;
		struct asus_primeb550plus_hwmon_data_type_temp_14bit temp_14bit;
		struct asus_primeb550plus_hwmon_data_type_pwm16 pwm16;
	} data_address;
	char const * label;
};

enum asus_primeb550plus_hwmon_field_list {
	ASUS_B550PLUS_HWMON_FIELD_LIST_CPUVCORE,
	ASUS_B550PLUS_HWMON_FIELD_LIST_VIN1,
	ASUS_B550PLUS_HWMON_FIELD_LIST_AVSB,
	ASUS_B550PLUS_HWMON_FIELD_LIST_3VCC,
	ASUS_B550PLUS_HWMON_FIELD_LIST_VIN0,
	ASUS_B550PLUS_HWMON_FIELD_LIST_VIN8,
	ASUS_B550PLUS_HWMON_FIELD_LIST_VIN4,
	ASUS_B550PLUS_HWMON_FIELD_LIST_3VSB,
	ASUS_B550PLUS_HWMON_FIELD_LIST_VBAT,
	ASUS_B550PLUS_HWMON_FIELD_LIST_VTT,
	ASUS_B550PLUS_HWMON_FIELD_LIST_VIN5,
	ASUS_B550PLUS_HWMON_FIELD_LIST_VIN6,
	ASUS_B550PLUS_HWMON_FIELD_LIST_VIN2,
	ASUS_B550PLUS_HWMON_FIELD_LIST_VIN3,
	ASUS_B550PLUS_HWMON_FIELD_LIST_VIN7,
	ASUS_B550PLUS_HWMON_FIELD_LIST_VIN9,
	ASUS_B550PLUS_HWMON_FIELD_LIST_TEMP1,
	ASUS_B550PLUS_HWMON_FIELD_LIST_TEMP2,
	ASUS_B550PLUS_HWMON_FIELD_LIST_TEMP3,
	ASUS_B550PLUS_HWMON_FIELD_LIST_TEMP4,
	ASUS_B550PLUS_HWMON_FIELD_LIST_TEMP5,
	ASUS_B550PLUS_HWMON_FIELD_LIST_TEMP6,

	ASUS_B550PLUS_HWMON_FIELD_LIST_TEMPDIMM_0_0,
	ASUS_B550PLUS_HWMON_FIELD_LIST_TEMPDIMM_0_1,
	ASUS_B550PLUS_HWMON_FIELD_LIST_TEMPDIMM_1_0,
	ASUS_B550PLUS_HWMON_FIELD_LIST_TEMPDIMM_1_1,

	ASUS_B550PLUS_HWMON_FIELD_LIST_SMIOVT2,

	ASUS_B550PLUS_HWMON_FIELD_LIST_PCH_CHIP_TEMP,
	ASUS_B550PLUS_HWMON_FIELD_LIST_PCH_CPU_TEMP,

	ASUS_B550PLUS_HWMON_FIELD_LIST_CPU_TEMP,

	ASUS_B550PLUS_HWMON_FIELD_LIST_SYSTIN,
	ASUS_B550PLUS_HWMON_FIELD_LIST_CPUTIN,
	ASUS_B550PLUS_HWMON_FIELD_LIST_AUXTIN0,
	ASUS_B550PLUS_HWMON_FIELD_LIST_AUXTIN1,
	ASUS_B550PLUS_HWMON_FIELD_LIST_AUXTIN2,
	ASUS_B550PLUS_HWMON_FIELD_LIST_AUXTIN3,

	ASUS_B550PLUS_HWMON_FIELD_LIST_SYSFANIN,
	ASUS_B550PLUS_HWMON_FIELD_LIST_CPUFANIN,
	ASUS_B550PLUS_HWMON_FIELD_LIST_AUXFANIN0,
	ASUS_B550PLUS_HWMON_FIELD_LIST_AUXFANIN1,
	ASUS_B550PLUS_HWMON_FIELD_LIST_AUXFANIN2,
	ASUS_B550PLUS_HWMON_FIELD_LIST_AUXFANIN3,
	ASUS_B550PLUS_HWMON_FIELD_LIST_AUXFANIN4,

	ASUS_B550PLUS_HWMON_FIELD_LIST_MAX
};

#define ASUS_B550PLUS_HWMON_FIELD_UCHAR_MUL(_label, _bank_no, _idx_in_bank, _multiplier) \
	& (const struct asus_primeb550plus_hwmon_chip_field) { \
		.label = _label "\n", \
		.data_type = ASUS_B550PLUS_HWMON_DATA_TYPE_UCHAR_MUL, \
		.data_address.uchar_mul = { \
			.bank_no = (_bank_no), \
			.index_in_bank = (_idx_in_bank), \
			.multiplier = (_multiplier) \
		} \
	}

#define ASUS_B550PLUS_HWMON_FIELD_TEMP_9BIT(_label, _int_bank_no, _int_idx, _frac_bank_no, _frac_idx) \
	& (const struct asus_primeb550plus_hwmon_chip_field) { \
		.label = _label "\n", \
		.data_type = ASUS_B550PLUS_HWMON_DATA_TYPE_TEMP_9BIT, \
		.data_address.temp_9bit = { \
			.int_bank_no = (_int_bank_no), \
			.int_index_in_bank = (_int_idx), \
			.frac_bank_no = (_frac_bank_no), \
			.frac_index_in_bank = (_frac_idx), \
		} \
	}

#define ASUS_B550PLUS_HWMON_FIELD_TEMP_8BIT(_label, _bank_no, _idx) \
	& (const struct asus_primeb550plus_hwmon_chip_field) { \
		.label = _label "\n", \
		.data_type = ASUS_B550PLUS_HWMON_DATA_TYPE_TEMP_8BIT, \
		.data_address.temp_8bit = { \
			.bank_no = (_bank_no), \
			.index_in_bank = (_idx), \
		} \
	}

#define ASUS_B550PLUS_HWMON_FIELD_TEMP_14BIT(_label, _int_bank_no, _int_idx, _frac_bank_no, _frac_idx) \
	& (const struct asus_primeb550plus_hwmon_chip_field) { \
		.label = _label "\n", \
		.data_type = ASUS_B550PLUS_HWMON_DATA_TYPE_TEMP_14BIT, \
		.data_address.temp_14bit = { \
			.int_bank_no = (_int_bank_no), \
			.int_idx = (_int_idx), \
			.frac_bank_no = (_frac_bank_no), \
			.frac_idx = (_frac_idx), \
		} \
	}

#define ASUS_B550PLUS_HWMON_FIELD_PWM16(_label, _high_bank_no, _high_idx, _low_bank_no, _low_idx) \
	& (const struct asus_primeb550plus_hwmon_chip_field) { \
		.label = _label "\n", \
		.data_type = ASUS_B550PLUS_HWMON_DATA_TYPE_PWM16, \
		.data_address.pwm16 = { \
			.high_bank_no = (_high_bank_no), \
			.high_idx = (_high_idx), \
			.low_bank_no = (_low_bank_no), \
			.low_idx = (_low_idx),\
		} \
	}

static struct asus_primeb550plus_hwmon_chip_field const * const asus_primeb550plus_hwmon_chip_fields[ASUS_B550PLUS_HWMON_FIELD_LIST_MAX] = {
	[ ASUS_B550PLUS_HWMON_FIELD_LIST_CPUVCORE ] = ASUS_B550PLUS_HWMON_FIELD_UCHAR_MUL("cpuvcore", 4, 0x80, 8),
	[ ASUS_B550PLUS_HWMON_FIELD_LIST_VIN1 ] = ASUS_B550PLUS_HWMON_FIELD_UCHAR_MUL("vin1", 4, 0x81, 8),
	[ ASUS_B550PLUS_HWMON_FIELD_LIST_AVSB ] = ASUS_B550PLUS_HWMON_FIELD_UCHAR_MUL("avsb", 4, 0x82, 2*8),
	[ ASUS_B550PLUS_HWMON_FIELD_LIST_3VCC ] = ASUS_B550PLUS_HWMON_FIELD_UCHAR_MUL("3vcc", 4, 0x83, 2*8),
	[ ASUS_B550PLUS_HWMON_FIELD_LIST_VIN0 ] = ASUS_B550PLUS_HWMON_FIELD_UCHAR_MUL("vin0", 4, 0x84, 8),
	[ ASUS_B550PLUS_HWMON_FIELD_LIST_VIN8 ] = ASUS_B550PLUS_HWMON_FIELD_UCHAR_MUL("vin8", 4, 0x85, 8),
	[ ASUS_B550PLUS_HWMON_FIELD_LIST_VIN4 ] = ASUS_B550PLUS_HWMON_FIELD_UCHAR_MUL("vin4", 4, 0x86, 8),
	[ ASUS_B550PLUS_HWMON_FIELD_LIST_3VSB ] = ASUS_B550PLUS_HWMON_FIELD_UCHAR_MUL("3vsb", 4, 0x87, 2*8),
	[ ASUS_B550PLUS_HWMON_FIELD_LIST_VBAT ] = ASUS_B550PLUS_HWMON_FIELD_UCHAR_MUL("vbat", 4, 0x88, 2*8),
	[ ASUS_B550PLUS_HWMON_FIELD_LIST_VTT ] = ASUS_B550PLUS_HWMON_FIELD_UCHAR_MUL("vtt", 4, 0x89, 8),
	[ ASUS_B550PLUS_HWMON_FIELD_LIST_VIN5 ] = ASUS_B550PLUS_HWMON_FIELD_UCHAR_MUL("vin5", 4, 0x8a, 8),
	[ ASUS_B550PLUS_HWMON_FIELD_LIST_VIN6 ] = ASUS_B550PLUS_HWMON_FIELD_UCHAR_MUL("vin6", 4, 0x8b, 8),
	[ ASUS_B550PLUS_HWMON_FIELD_LIST_VIN2 ] = ASUS_B550PLUS_HWMON_FIELD_UCHAR_MUL("vin2", 4, 0x8c, 8),
	[ ASUS_B550PLUS_HWMON_FIELD_LIST_VIN3 ] = ASUS_B550PLUS_HWMON_FIELD_UCHAR_MUL("vin3", 4, 0x8d, 8),
	[ ASUS_B550PLUS_HWMON_FIELD_LIST_VIN7 ] = ASUS_B550PLUS_HWMON_FIELD_UCHAR_MUL("vin7", 4, 0x8e, 8),
	[ ASUS_B550PLUS_HWMON_FIELD_LIST_VIN9 ] = ASUS_B550PLUS_HWMON_FIELD_UCHAR_MUL("vin9", 4, 0x8f, 8),

	[ ASUS_B550PLUS_HWMON_FIELD_LIST_TEMP1 ] = ASUS_B550PLUS_HWMON_FIELD_TEMP_9BIT("temp1", 0, 0x73, 0, 0x74),
	[ ASUS_B550PLUS_HWMON_FIELD_LIST_TEMP2 ] = ASUS_B550PLUS_HWMON_FIELD_TEMP_9BIT("temp2", 0, 0x75, 0, 0x76),
	[ ASUS_B550PLUS_HWMON_FIELD_LIST_TEMP3 ] = ASUS_B550PLUS_HWMON_FIELD_TEMP_9BIT("temp3", 0, 0x77, 0, 0x78),
	[ ASUS_B550PLUS_HWMON_FIELD_LIST_TEMP4 ] = ASUS_B550PLUS_HWMON_FIELD_TEMP_9BIT("temp4", 0, 0x79, 0, 0x7a),
	[ ASUS_B550PLUS_HWMON_FIELD_LIST_TEMP5 ] = ASUS_B550PLUS_HWMON_FIELD_TEMP_9BIT("temp5", 0, 0x7b, 0, 0x7c),
	[ ASUS_B550PLUS_HWMON_FIELD_LIST_TEMP6 ] = ASUS_B550PLUS_HWMON_FIELD_TEMP_9BIT("temp6", 0, 0x7d, 0, 0x7e),

	[ ASUS_B550PLUS_HWMON_FIELD_LIST_TEMPDIMM_0_0 ] = ASUS_B550PLUS_HWMON_FIELD_TEMP_8BIT("agent0, dimm0", 4, 0x05),
	[ ASUS_B550PLUS_HWMON_FIELD_LIST_TEMPDIMM_0_1 ] = ASUS_B550PLUS_HWMON_FIELD_TEMP_8BIT("agent0, dimm1", 4, 0x06),
	[ ASUS_B550PLUS_HWMON_FIELD_LIST_TEMPDIMM_1_0 ] = ASUS_B550PLUS_HWMON_FIELD_TEMP_8BIT("agent1, dimm0", 4, 0x07),
	[ ASUS_B550PLUS_HWMON_FIELD_LIST_TEMPDIMM_1_1 ] = ASUS_B550PLUS_HWMON_FIELD_TEMP_8BIT("agent1, dimm1", 4, 0x08),

	[ ASUS_B550PLUS_HWMON_FIELD_LIST_SMIOVT2 ] = ASUS_B550PLUS_HWMON_FIELD_TEMP_9BIT("smiovt2 (default cputin)", 1, 0x50, 1, 0x51),

	[ ASUS_B550PLUS_HWMON_FIELD_LIST_PCH_CHIP_TEMP ] = ASUS_B550PLUS_HWMON_FIELD_TEMP_8BIT("pch chip", 4, 0x01),
	[ ASUS_B550PLUS_HWMON_FIELD_LIST_PCH_CPU_TEMP ] = ASUS_B550PLUS_HWMON_FIELD_TEMP_14BIT("cpu", 4, 0x02, 4, 0x03),

	[ ASUS_B550PLUS_HWMON_FIELD_LIST_SYSTIN ] = ASUS_B550PLUS_HWMON_FIELD_TEMP_8BIT("systin", 4, 0x90),
	[ ASUS_B550PLUS_HWMON_FIELD_LIST_CPUTIN ] = ASUS_B550PLUS_HWMON_FIELD_TEMP_8BIT("cputin", 4, 0x91),
	[ ASUS_B550PLUS_HWMON_FIELD_LIST_AUXTIN0 ] = ASUS_B550PLUS_HWMON_FIELD_TEMP_8BIT("auxtin0", 4, 0x92),
	[ ASUS_B550PLUS_HWMON_FIELD_LIST_AUXTIN1 ] = ASUS_B550PLUS_HWMON_FIELD_TEMP_8BIT("auxtin1", 4, 0x93),
	[ ASUS_B550PLUS_HWMON_FIELD_LIST_AUXTIN2 ] = ASUS_B550PLUS_HWMON_FIELD_TEMP_8BIT("auxtin2", 4, 0x94),
	[ ASUS_B550PLUS_HWMON_FIELD_LIST_AUXTIN3 ] = ASUS_B550PLUS_HWMON_FIELD_TEMP_8BIT("auxtin3", 4, 0x95),

	[ ASUS_B550PLUS_HWMON_FIELD_LIST_SYSFANIN ] = ASUS_B550PLUS_HWMON_FIELD_PWM16("sysfan", 4, 0xc0, 4, 0xc1),
	[ ASUS_B550PLUS_HWMON_FIELD_LIST_CPUFANIN ] = ASUS_B550PLUS_HWMON_FIELD_PWM16("cpufan", 4, 0xc2, 4, 0xc3),
	[ ASUS_B550PLUS_HWMON_FIELD_LIST_AUXFANIN0 ] = ASUS_B550PLUS_HWMON_FIELD_PWM16("auxfan0", 4, 0xc4, 4, 0xc5),
	[ ASUS_B550PLUS_HWMON_FIELD_LIST_AUXFANIN1 ] = ASUS_B550PLUS_HWMON_FIELD_PWM16("auxfan1", 4, 0xc6, 4, 0xc7),
	[ ASUS_B550PLUS_HWMON_FIELD_LIST_AUXFANIN2 ] = ASUS_B550PLUS_HWMON_FIELD_PWM16("auxfan2", 4, 0xc8, 4, 0xc9),
	[ ASUS_B550PLUS_HWMON_FIELD_LIST_AUXFANIN3 ] = ASUS_B550PLUS_HWMON_FIELD_PWM16("auxfan3", 4, 0xca, 4, 0xcb),
	[ ASUS_B550PLUS_HWMON_FIELD_LIST_AUXFANIN4 ] = ASUS_B550PLUS_HWMON_FIELD_PWM16("auxfan4", 4, 0xce, 4, 0xcf),
};

#undef ASUS_B550PLUS_HWMON_FIELD_UCHAR_MUL
#undef ASUS_B550PLUS_HWMON_FIELD_TEMP9BIT
#undef ASUS_B550PLUS_HWMON_FIELD_TEMP8BIT
#undef ASUS_B550PLUS_HWMON_FIELD_TEMP9BIT
#undef ASUS_B550PLUS_HWMON_FIELD_TEMP14BIT
#undef ASUS_B550PLUS_HWMON_FIELD_PWM16

static ssize_t asus_primeb550plus_hwmon_sysfs_val_uchar_mul_show(struct asus_primeb550plus_hwmon_data *devdri_data, struct asus_primeb550plus_hwmon_chip_field const * const chip_field, char *buf)
{
	unsigned char raw_in;
	struct asus_primeb550plus_hwmon_data_type_uchar_mul const * const field = &chip_field->data_address.uchar_mul;

	if (asus_primeb550plus_hwmon_read_u8(devdri_data, field->bank_no, field->index_in_bank, &raw_in))
		return -1;

	return scnprintf(buf, PAGE_SIZE, "%u\n", raw_in * field->multiplier);
}

static ssize_t asus_primeb550plus_hwmon_sysfs_val_temp_9bit_show(struct asus_primeb550plus_hwmon_data *devdri_data, struct asus_primeb550plus_hwmon_chip_field const * const chip_field, char *buf)
{
	unsigned char raw_in_int, raw_in_frac;
	signed char corrected_in_int;
	int fractional_value;
	struct asus_primeb550plus_hwmon_data_type_temp_9bit const * const field = &chip_field->data_address.temp_9bit;

	if (asus_primeb550plus_hwmon_read_u8(devdri_data, field->int_bank_no, field->int_index_in_bank, &raw_in_int))
		return -1;
	if (asus_primeb550plus_hwmon_read_u8(devdri_data, field->frac_bank_no, field->frac_index_in_bank, &raw_in_frac))
		return -1;

	corrected_in_int = raw_in_int;
	fractional_value = corrected_in_int >= 0 ? 500 : -500;

	return scnprintf(buf, PAGE_SIZE, "%i\n", corrected_in_int * 1000 + !!(raw_in_frac & 0x80) * fractional_value);
}

static ssize_t asus_primeb550plus_hwmon_sysfs_val_temp_8bit_show(struct asus_primeb550plus_hwmon_data *devdri_data, struct asus_primeb550plus_hwmon_chip_field const * const chip_field, char *buf)
{
	unsigned char raw_in;
	signed char corrected_in;
	struct asus_primeb550plus_hwmon_data_type_temp_8bit const * const field = &chip_field->data_address.temp_8bit;

	if (asus_primeb550plus_hwmon_read_u8(devdri_data, field->bank_no, field->index_in_bank, &raw_in))
		return -1;

	corrected_in = raw_in;

	return scnprintf(buf, PAGE_SIZE, "%i\n", corrected_in * 1000);
}

static ssize_t asus_primeb550plus_hwmon_sysfs_val_temp_14bit_show(struct asus_primeb550plus_hwmon_data *devdri_data, struct asus_primeb550plus_hwmon_chip_field const * const chip_field, char *buf)
{
	unsigned char raw_int_in, raw_frac_in;
	struct asus_primeb550plus_hwmon_data_type_temp_14bit const * const field = &chip_field->data_address.temp_14bit;

	if (asus_primeb550plus_hwmon_read_u8(devdri_data, field->int_bank_no, field->int_idx, &raw_int_in))
		return -1;
	if (asus_primeb550plus_hwmon_read_u8(devdri_data, field->frac_bank_no, field->frac_idx, &raw_frac_in))
		return -1;

	return scnprintf(buf, PAGE_SIZE, "%i\n", raw_int_in * 1000 + (raw_frac_in >> 2));
}

static ssize_t asus_primeb550plus_hwmon_sysfs_val_pwm16_show(struct asus_primeb550plus_hwmon_data *devdri_data, struct asus_primeb550plus_hwmon_chip_field const * const chip_field, char *buf)
{
	unsigned char raw_high, raw_low;
	struct asus_primeb550plus_hwmon_data_type_pwm16 const * const field = &chip_field->data_address.pwm16;

	if (asus_primeb550plus_hwmon_read_u8(devdri_data, field->high_bank_no, field->high_idx, &raw_high))
		return -1;
	if (asus_primeb550plus_hwmon_read_u8(devdri_data, field->low_bank_no, field->low_idx, &raw_low))
		return -1;

	return scnprintf(buf, PAGE_SIZE, "%u\n", (raw_high << 8) + raw_low);
}

static const struct asus_primeb550plus_hwmon_chip_field * const asus_primeb550plus_hwmon_chip_field_from_dev_attr(struct device_attribute const * const attr)
{
	struct sensor_device_attribute const * const s_dev_attr = to_sensor_dev_attr(attr);

	if (!s_dev_attr)
		return NULL;

	return asus_primeb550plus_hwmon_chip_fields[s_dev_attr->index];
}

static ssize_t asus_primeb550plus_hwmon_sysfs_val_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct asus_primeb550plus_hwmon_chip_field const * const chip_field = asus_primeb550plus_hwmon_chip_field_from_dev_attr(attr);
	struct asus_primeb550plus_hwmon_data *devdri_data = dev_get_drvdata(dev);

	if (!chip_field)
		return -1;

	switch(chip_field->data_type) {
		case ASUS_B550PLUS_HWMON_DATA_TYPE_UCHAR_MUL:
			return asus_primeb550plus_hwmon_sysfs_val_uchar_mul_show(devdri_data, chip_field, buf);
		case ASUS_B550PLUS_HWMON_DATA_TYPE_TEMP_9BIT:
			return asus_primeb550plus_hwmon_sysfs_val_temp_9bit_show(devdri_data, chip_field, buf);
		case ASUS_B550PLUS_HWMON_DATA_TYPE_TEMP_8BIT:
			return asus_primeb550plus_hwmon_sysfs_val_temp_8bit_show(devdri_data, chip_field, buf);
		case ASUS_B550PLUS_HWMON_DATA_TYPE_TEMP_14BIT:
			return asus_primeb550plus_hwmon_sysfs_val_temp_14bit_show(devdri_data, chip_field, buf);
		case ASUS_B550PLUS_HWMON_DATA_TYPE_PWM16:
			return asus_primeb550plus_hwmon_sysfs_val_pwm16_show(devdri_data, chip_field, buf);
		default:
			return -1;
	}
}

static ssize_t asus_primeb550plus_hwmon_sysfs_label_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct asus_primeb550plus_hwmon_chip_field const * const chip_field = asus_primeb550plus_hwmon_chip_field_from_dev_attr(attr);

	if (!chip_field)
		return -1;

	return strscpy(buf, chip_field->label, PAGE_SIZE);
}

#define ASUS_B550PLUS_HWMON_DEVICE_ATTR_RO(_name, _field_index) \
	static SENSOR_DEVICE_ATTR_RO(_name##_input, asus_primeb550plus_hwmon_sysfs_val, _field_index); \
	static SENSOR_DEVICE_ATTR_RO(_name##_label, asus_primeb550plus_hwmon_sysfs_label, _field_index) ;

ASUS_B550PLUS_HWMON_DEVICE_ATTR_RO(in0, ASUS_B550PLUS_HWMON_FIELD_LIST_CPUVCORE)
ASUS_B550PLUS_HWMON_DEVICE_ATTR_RO(in1, ASUS_B550PLUS_HWMON_FIELD_LIST_VIN1)
ASUS_B550PLUS_HWMON_DEVICE_ATTR_RO(in2, ASUS_B550PLUS_HWMON_FIELD_LIST_AVSB)
ASUS_B550PLUS_HWMON_DEVICE_ATTR_RO(in3, ASUS_B550PLUS_HWMON_FIELD_LIST_3VCC)
ASUS_B550PLUS_HWMON_DEVICE_ATTR_RO(in4, ASUS_B550PLUS_HWMON_FIELD_LIST_VIN0)
ASUS_B550PLUS_HWMON_DEVICE_ATTR_RO(in5, ASUS_B550PLUS_HWMON_FIELD_LIST_VIN8)
ASUS_B550PLUS_HWMON_DEVICE_ATTR_RO(in6, ASUS_B550PLUS_HWMON_FIELD_LIST_VIN4)
ASUS_B550PLUS_HWMON_DEVICE_ATTR_RO(in7, ASUS_B550PLUS_HWMON_FIELD_LIST_3VSB)
ASUS_B550PLUS_HWMON_DEVICE_ATTR_RO(in8, ASUS_B550PLUS_HWMON_FIELD_LIST_VBAT)
ASUS_B550PLUS_HWMON_DEVICE_ATTR_RO(in9, ASUS_B550PLUS_HWMON_FIELD_LIST_VTT)
ASUS_B550PLUS_HWMON_DEVICE_ATTR_RO(in10, ASUS_B550PLUS_HWMON_FIELD_LIST_VIN5)
ASUS_B550PLUS_HWMON_DEVICE_ATTR_RO(in11, ASUS_B550PLUS_HWMON_FIELD_LIST_VIN6)
ASUS_B550PLUS_HWMON_DEVICE_ATTR_RO(in12, ASUS_B550PLUS_HWMON_FIELD_LIST_VIN2)
ASUS_B550PLUS_HWMON_DEVICE_ATTR_RO(in13, ASUS_B550PLUS_HWMON_FIELD_LIST_VIN3)
ASUS_B550PLUS_HWMON_DEVICE_ATTR_RO(in14, ASUS_B550PLUS_HWMON_FIELD_LIST_VIN7)
ASUS_B550PLUS_HWMON_DEVICE_ATTR_RO(in15, ASUS_B550PLUS_HWMON_FIELD_LIST_VIN9)

ASUS_B550PLUS_HWMON_DEVICE_ATTR_RO(temp1, ASUS_B550PLUS_HWMON_FIELD_LIST_TEMP1)
ASUS_B550PLUS_HWMON_DEVICE_ATTR_RO(temp2, ASUS_B550PLUS_HWMON_FIELD_LIST_TEMP2)
ASUS_B550PLUS_HWMON_DEVICE_ATTR_RO(temp3, ASUS_B550PLUS_HWMON_FIELD_LIST_TEMP3)
ASUS_B550PLUS_HWMON_DEVICE_ATTR_RO(temp4, ASUS_B550PLUS_HWMON_FIELD_LIST_TEMP4)
ASUS_B550PLUS_HWMON_DEVICE_ATTR_RO(temp5, ASUS_B550PLUS_HWMON_FIELD_LIST_TEMP5)
ASUS_B550PLUS_HWMON_DEVICE_ATTR_RO(temp6, ASUS_B550PLUS_HWMON_FIELD_LIST_TEMP6)

ASUS_B550PLUS_HWMON_DEVICE_ATTR_RO(temp7, ASUS_B550PLUS_HWMON_FIELD_LIST_TEMPDIMM_0_0)
ASUS_B550PLUS_HWMON_DEVICE_ATTR_RO(temp8, ASUS_B550PLUS_HWMON_FIELD_LIST_TEMPDIMM_0_1)
ASUS_B550PLUS_HWMON_DEVICE_ATTR_RO(temp9, ASUS_B550PLUS_HWMON_FIELD_LIST_TEMPDIMM_1_0)
ASUS_B550PLUS_HWMON_DEVICE_ATTR_RO(temp10, ASUS_B550PLUS_HWMON_FIELD_LIST_TEMPDIMM_1_1)

ASUS_B550PLUS_HWMON_DEVICE_ATTR_RO(temp11, ASUS_B550PLUS_HWMON_FIELD_LIST_SMIOVT2)

ASUS_B550PLUS_HWMON_DEVICE_ATTR_RO(temp12, ASUS_B550PLUS_HWMON_FIELD_LIST_PCH_CHIP_TEMP)
ASUS_B550PLUS_HWMON_DEVICE_ATTR_RO(temp13, ASUS_B550PLUS_HWMON_FIELD_LIST_PCH_CPU_TEMP)

ASUS_B550PLUS_HWMON_DEVICE_ATTR_RO(temp14, ASUS_B550PLUS_HWMON_FIELD_LIST_SYSTIN)
ASUS_B550PLUS_HWMON_DEVICE_ATTR_RO(temp15, ASUS_B550PLUS_HWMON_FIELD_LIST_CPUTIN)
ASUS_B550PLUS_HWMON_DEVICE_ATTR_RO(temp16, ASUS_B550PLUS_HWMON_FIELD_LIST_AUXTIN0)
ASUS_B550PLUS_HWMON_DEVICE_ATTR_RO(temp17, ASUS_B550PLUS_HWMON_FIELD_LIST_AUXTIN1)
ASUS_B550PLUS_HWMON_DEVICE_ATTR_RO(temp18, ASUS_B550PLUS_HWMON_FIELD_LIST_AUXTIN2)
ASUS_B550PLUS_HWMON_DEVICE_ATTR_RO(temp19, ASUS_B550PLUS_HWMON_FIELD_LIST_AUXTIN3)

ASUS_B550PLUS_HWMON_DEVICE_ATTR_RO(fan1, ASUS_B550PLUS_HWMON_FIELD_LIST_SYSFANIN)
ASUS_B550PLUS_HWMON_DEVICE_ATTR_RO(fan2, ASUS_B550PLUS_HWMON_FIELD_LIST_CPUFANIN)
ASUS_B550PLUS_HWMON_DEVICE_ATTR_RO(fan3, ASUS_B550PLUS_HWMON_FIELD_LIST_AUXFANIN0)
ASUS_B550PLUS_HWMON_DEVICE_ATTR_RO(fan4, ASUS_B550PLUS_HWMON_FIELD_LIST_AUXFANIN1)
ASUS_B550PLUS_HWMON_DEVICE_ATTR_RO(fan5, ASUS_B550PLUS_HWMON_FIELD_LIST_AUXFANIN2)
ASUS_B550PLUS_HWMON_DEVICE_ATTR_RO(fan6, ASUS_B550PLUS_HWMON_FIELD_LIST_AUXFANIN3)
ASUS_B550PLUS_HWMON_DEVICE_ATTR_RO(fan7, ASUS_B550PLUS_HWMON_FIELD_LIST_AUXFANIN4)


#define ATTR_REF(_attr_name) &sensor_dev_attr_##_attr_name##_input.dev_attr.attr, &sensor_dev_attr_##_attr_name##_label.dev_attr.attr
static struct attribute *asus_primeb550plus_hwmon_attrs[] = {
	ATTR_REF(in0),
	ATTR_REF(in1),
	ATTR_REF(in2),
	ATTR_REF(in3),
	ATTR_REF(in4),
	ATTR_REF(in5),
	ATTR_REF(in6),
	ATTR_REF(in7),
	ATTR_REF(in8),
	ATTR_REF(in9),
	ATTR_REF(in10),
	ATTR_REF(in11),
	ATTR_REF(in12),
	ATTR_REF(in13),
	ATTR_REF(in14),
	ATTR_REF(in15),

	ATTR_REF(temp1),
	ATTR_REF(temp2),
	ATTR_REF(temp3),
	ATTR_REF(temp4),
	ATTR_REF(temp5),
	ATTR_REF(temp6),

	ATTR_REF(temp7),
	ATTR_REF(temp8),
	ATTR_REF(temp9),
	ATTR_REF(temp10),

	ATTR_REF(temp11),

	ATTR_REF(temp12),
	ATTR_REF(temp13),

	ATTR_REF(temp14),
	ATTR_REF(temp15),
	ATTR_REF(temp16),
	ATTR_REF(temp17),
	ATTR_REF(temp18),
	ATTR_REF(temp19),

	ATTR_REF(fan1),
	ATTR_REF(fan2),
	ATTR_REF(fan3),
	ATTR_REF(fan4),
	ATTR_REF(fan5),
	ATTR_REF(fan6),
	ATTR_REF(fan7),

	NULL
};
#undef ATTR_REF
ATTRIBUTE_GROUPS(asus_primeb550plus_hwmon);


static int asus_primeb550plus_hwmon_check_dmi(void)
{
	if (dmi_check_system(asus_accepted_dmis))
		return 0;
	
	return -ENODEV;
}

static int asus_primeb550plus_hwmon_get_method_handles(struct asus_primeb550plus_hwmon_data *devdri_data)
{
	struct device *dev = &devdri_data->acpi_dev->dev;
	acpi_handle a_hndl;
	acpi_status a_status;
	acpi_object_type a_type;

	a_status = acpi_get_handle(devdri_data->acpi_dev_handle, "RHWM", &a_hndl);
	if (ACPI_SUCCESS(a_status)) {
		devdri_data->rhwm_method = a_hndl;
	} else {
		dev_err(dev, "method RHWM not found: %s\n", acpi_format_exception(a_status));
		return -ENODEV;
	}

	a_status = acpi_get_type(devdri_data->rhwm_method, &a_type);
	if (!ACPI_SUCCESS(a_status)) {
		dev_err(dev, "Could not figure out acpi object type: %s\n", acpi_format_exception(a_status));
		return -EIO;
	}
	if (a_type != ACPI_TYPE_METHOD) {
		dev_err(dev, "RHWM found, but is not a method\n");
		return -ENODEV;
	}

	return 0;
}

struct asus_primeb550plus_hwmon_supported_superio {
	unsigned char vendor_id_high;
	unsigned char chip_id;
};

static const struct asus_primeb550plus_hwmon_supported_superio asus_primeb550plus_hwmon_supported_superios[] = {
	{ .vendor_id_high = 0x5c, .chip_id = 0xc1 },
};

static int asus_primeb550plus_hwmon_check_chip(struct device *dev, struct asus_primeb550plus_hwmon_data *devdri_data)
{
	size_t i;
	unsigned char vendor_id_high, chip_id;

	if (asus_primeb550plus_hwmon_read_u8(devdri_data, 0, 0x4f, &vendor_id_high))
		goto err_read;
	if (asus_primeb550plus_hwmon_read_u8(devdri_data, 0, 0x58, &chip_id))
		goto err_read;

	for (i=0 ; i<sizeof(asus_primeb550plus_hwmon_supported_superios)/sizeof(asus_primeb550plus_hwmon_supported_superios[0]) ; i++) {
		struct asus_primeb550plus_hwmon_supported_superio const * const chip = &asus_primeb550plus_hwmon_supported_superios[i];

		if (vendor_id_high == chip->vendor_id_high && chip_id == chip->chip_id) {
			dev_info(dev, "Found chip vendor_id=0x%02x, chip_id=0x%02x\n", vendor_id_high , chip_id);
			return 0;
		}
	}

	dev_info(dev, "Unknown chip vendor_id=0x%02x, chip_id=0x%02x\n", vendor_id_high, chip_id);
	return 1;

err_read:
	dev_info(dev, "Error while reading vendor/chip id\n");
	return 1;
}

static int asus_primeb550plus_hwmon_add(struct acpi_device *device)
{
	int err;
	struct asus_primeb550plus_hwmon_data *devdri_data;

	if ((err = asus_primeb550plus_hwmon_check_dmi())) {
		dev_info(&device->dev, "Unsupported system DMI\n");
		goto out;
	}

	if (strcmp("ASUSWMI", acpi_device_uid(device))) {
		err = -ENODEV;
		dev_info(&device->dev, "Unsupported device uid\n");
		goto out;
	}

	devdri_data = devm_kzalloc(&device->dev, sizeof(*devdri_data), GFP_KERNEL);
	if (!devdri_data) {
		err =  -ENOMEM;
		goto out;
	}

	devdri_data->acpi_dev = device;
	devdri_data->acpi_dev_handle = device->handle;

	if ((err = asus_primeb550plus_hwmon_get_method_handles(devdri_data)))
		goto out;

	if ((err = asus_primeb550plus_hwmon_check_chip(&device->dev, devdri_data)))
		goto out;

	dev_set_drvdata(&device->dev, devdri_data);

	devdri_data->device = devm_hwmon_device_register_with_groups(&device->dev, "asus_primeb550plus_hwmon", devdri_data, asus_primeb550plus_hwmon_groups);
	err = PTR_ERR_OR_ZERO(devdri_data->device);

out:
	return err;
}

static struct acpi_driver asus_primeb550plus_hwmon_driver = {
	.name = "asus-primeb550plus-hwmon",
	.class = "hwmon",
	.ids = asus_primeb550plus_hwmon_acpi_ids,
	.ops = {
		.add = asus_primeb550plus_hwmon_add,
	},
};

module_acpi_driver(asus_primeb550plus_hwmon_driver)
MODULE_LICENSE("GPL");


