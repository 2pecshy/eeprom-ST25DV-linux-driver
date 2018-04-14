/*
 * Copyright (C) 2018 Loïc Boban <loic.boban@gmail.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#include <linux/kernel.h>
#include <linux/slab.h>
#include <linux/module.h>
#include <linux/device.h>
#include <linux/capability.h>
#include <linux/jiffies.h>
#include <linux/delay.h>
#include <linux/i2c.h>
#include <linux/mutex.h>

#define DYN_REG_SIZE		0x8
#define SYS_MEM_SIZE		0x24
#define USER_MEM_SIZE		0x200

#define SYS_ADDR		0x57
#define USER_ADDR		0x53

#define DYN_REG_OFF		0x2000

#define MAX_TRY			10

#define USER_AREA 0
#define SYS_AREA 1
#define DYN_REG_AREA 2

/*the st25dv eeprom have two areas, the user area, and the system 
area to manage read/write protection for the NFC interface and I2C
interface. To drive the system area a dummy i2c_client is used*/

static struct mutex update_lock;//protect for concurrent updates
static struct i2c_client *client_sys_area; //i2c_client of the system area

/*one struct is used for each area*/
struct st25dv_data {
	u8 *data;//area data
	u16 type;
	struct st25dv_data *next;
};

/* Addresses to scan */
static const unsigned short normal_i2c[] = { 0x53, 0x57, I2C_CLIENT_END };

static ssize_t st25dv_read_area(struct file *filp, struct kobject *kobj,
			   struct bin_attribute *bin_attr,
			   char *buf, loff_t off, size_t count, int area)
{
	int r_size, nack;
	struct i2c_client *client = to_i2c_client(kobj_to_dev(kobj));
	struct st25dv_data *data = i2c_get_clientdata(client);
	u16 cur_off;

	while(data->type != area)
		data = data->next;

	mutex_lock(&update_lock);
	for(cur_off = off; cur_off-off < count; cur_off++){
		nack = 0;
retry_:
		if(nack > MAX_TRY){
			mutex_unlock(&update_lock);
			return r_size;
		}
		r_size = i2c_smbus_write_byte_data(client, (cur_off >> 8) & 0x0ff, cur_off & 0x0ff);
		if(r_size < 0){
			nack++;
			udelay(150);
			goto retry_;
		}
		r_size = i2c_smbus_read_byte(client);
		if(r_size < 0){
			nack++;
			udelay(150);
			goto retry_;
		}
		data->data[cur_off] = r_size;
	}
	memcpy(buf, &data->data[off], count);
	mutex_unlock(&update_lock);
	printk(KERN_WARNING "st25dv: %d byte reads.\n",count);

	return count;
}

static ssize_t st25dv_write_area(struct file *filp, struct kobject *kobj,
			struct bin_attribute *bin_attr,
			char *buf, loff_t off, size_t count, int area)
{
	int r_size;
	struct i2c_client *client = to_i2c_client(kobj_to_dev(kobj));
	struct st25dv_data *data = i2c_get_clientdata(client);
	u16 cur_off, tmp;
	u8 nack;

	while(data->type != area)
		data = data->next;
	mutex_lock(&update_lock);
	memcpy(data->data + off, buf, count);
	for(cur_off = off; cur_off-off < count; cur_off++){
		nack = 0;
		tmp = data->data[cur_off];
		tmp = (tmp << 8) & 0xff00;
		tmp |= cur_off & 0x00ff;
retry_:
		if(nack > MAX_TRY){
			mutex_unlock(&update_lock);
			return r_size;
		}
		r_size = i2c_smbus_write_word_data(client, (cur_off >> 8) & 0x0ff, tmp );
		if(r_size < 0){
			nack++;
			mdelay(1);
			goto retry_;
		}

	}
	mutex_unlock(&update_lock);
	printk(KERN_WARNING "st25dv: %d byte writes.\n",count);

	return count;
}

static ssize_t st25dv_read_dyn_reg(struct file *filp, struct kobject *kobj,
			   struct bin_attribute *bin_attr,
			   char *buf, loff_t off, size_t count)
{
	return st25dv_read_area(filp, kobj, bin_attr, buf, off + DYN_REG_OFF,
							 count, DYN_REG_AREA);
}

static ssize_t st25dv_read_user_mem(struct file *filp, struct kobject *kobj,
			   struct bin_attribute *bin_attr,
			   char *buf, loff_t off, size_t count)
{
	return st25dv_read_area(filp, kobj, bin_attr, buf, off,
							 count, USER_AREA);
}

static ssize_t st25dv_read_sys_mem(struct file *filp, struct kobject *kobj,
			   struct bin_attribute *bin_attr,
			   char *buf, loff_t off, size_t count)
{
	return st25dv_read_area(filp, kobj, bin_attr, buf, off,
							 count, SYS_AREA);
}

static ssize_t st25dv_write_dyn_reg(struct file *filp, struct kobject *kobj,
			   struct bin_attribute *bin_attr,
			   char *buf, loff_t off, size_t count)
{
	return st25dv_write_area(filp, kobj, bin_attr, buf, off + DYN_REG_OFF,
							 count, DYN_REG_AREA);
}

static ssize_t st25dv_write_user_mem(struct file *filp, struct kobject *kobj,
			   struct bin_attribute *bin_attr,
			   char *buf, loff_t off, size_t count)
{
	return st25dv_write_area(filp, kobj, bin_attr, buf, off,
							 count, USER_AREA);
}

static ssize_t st25dv_write_sys_mem(struct file *filp, struct kobject *kobj,
			   struct bin_attribute *bin_attr,
			   char *buf, loff_t off, size_t count)
{
	return st25dv_write_area(filp, kobj, bin_attr, buf, off,
							 count, SYS_AREA);
}

static const struct bin_attribute st25dv_user_attr = {
	.attr = {
		.name = "st25dv_user",
		.mode = S_IRUGO|S_IWUGO,
	},
	.size = USER_MEM_SIZE,
	.read = st25dv_read_user_mem,
	.write = st25dv_write_user_mem,
};

static const struct bin_attribute st25dv_sys_attr = {
	.attr = {
		.name = "st25dv_sys",
		.mode = S_IRUGO|S_IWUSR,
	},
	.size = SYS_MEM_SIZE,
	.read = st25dv_read_sys_mem,
	.write = st25dv_write_sys_mem,
};

static const struct bin_attribute st25dv_dyn_reg_attr = {
        .attr = {
                .name = "st25dv_dyn_reg",
                .mode = S_IRUGO|S_IWUSR,
        },
        .size = DYN_REG_SIZE,
        .read = st25dv_read_dyn_reg,
        .write = st25dv_write_dyn_reg,
};

/* Return 0 if detection is successful, -ENODEV otherwise */
static int st25dv_detect(struct i2c_client *client, struct i2c_board_info *info)
{
	struct i2c_adapter *adapter = client->adapter;

	if (!(adapter->class & I2C_CLASS_SPD) && (client->addr != USER_ADDR)){
		printk(KERN_WARNING "not st25dv eeprom.\n");
		return -ENODEV;
	}
	if(!i2c_new_dummy(client->adapter, SYS_ADDR)){
		printk(KERN_WARNING "not st25dv eeprom.\n");
		return -ENODEV;
	}
	if (!i2c_check_functionality(adapter, I2C_FUNC_SMBUS_READ_WORD_DATA)
	 && !i2c_check_functionality(adapter, I2C_FUNC_SMBUS_READ_I2C_BLOCK)){
		printk(KERN_WARNING "st25dv eeprom detect but no functionality.\n");
		return -ENODEV;
	}
	printk(KERN_WARNING "st25dv eeprom detect.\n");
	strlcpy(info->type, "st25dv", I2C_NAME_SIZE);

	return 0;
}

static int st25dv_probe(struct i2c_client *client,
			const struct i2c_device_id *id)
{
	int status;
	struct st25dv_data *data;
	struct st25dv_data *sys_data;
	struct st25dv_data *dyn_reg_data;

	/*use a dummy client to get st25dv system configuration*/
	client_sys_area = i2c_new_dummy(client->adapter, SYS_ADDR);
	if(!client_sys_area){
		printk(KERN_WARNING "st25dv sys eeprom not detected.\n");
		return -ENODEV;
	}
	data = devm_kzalloc(&client->dev, sizeof(struct st25dv_data), GFP_KERNEL);
	if (!data)
		goto err_mem;
	data->data = kmalloc(sizeof(u8)*USER_MEM_SIZE, GFP_KERNEL);
	if (!data->data)
		goto err_mem4;
	sys_data = devm_kzalloc(&client_sys_area->dev, sizeof(struct st25dv_data), GFP_KERNEL);
	if (!sys_data)
		goto err_mem3;
	sys_data->data = kmalloc(sizeof(u8)*SYS_MEM_SIZE, GFP_KERNEL);
	if (!sys_data->data)
		goto err_mem2;
	dyn_reg_data = kmalloc(sizeof(struct st25dv_data), GFP_KERNEL);
	if(!dyn_reg_data)
		goto err_mem1;
	dyn_reg_data->data = kmalloc(sizeof(u8)*DYN_REG_SIZE, GFP_KERNEL);
	if (!dyn_reg_data->data)
		goto err_mem0;
	memset(data->data, 0xff, USER_MEM_SIZE);
	memset(sys_data->data, 0xff, SYS_MEM_SIZE);
	memset(dyn_reg_data->data, 0xff, DYN_REG_SIZE);
	data->next = dyn_reg_data;
	dyn_reg_data->next = sys_data;
	sys_data->next = data;
	data->type = USER_AREA;
	dyn_reg_data->type = DYN_REG_AREA;
	sys_data->type = SYS_AREA;
	i2c_set_clientdata(client, data);
	i2c_set_clientdata(client_sys_area, sys_data);
	mutex_init(&update_lock);

	/* create tree sysfs eeprom files to for user area,
	 system area and dynamic registers */
	status = sysfs_create_bin_file(&client->dev.kobj, &st25dv_user_attr);
	if(status < 0){
		printk(KERN_WARNING "fail to create bin file.\n");
		return status;
	}
	status = sysfs_create_bin_file(&client_sys_area->dev.kobj, &st25dv_sys_attr);
	if(status < 0){
		printk(KERN_WARNING "fail to create bin file.\n");
		sysfs_remove_bin_file(&client->dev.kobj, &st25dv_user_attr);
		return status;
	}
	status = sysfs_create_bin_file(&client->dev.kobj, &st25dv_dyn_reg_attr);
	if(status < 0){
		printk(KERN_WARNING "fail to create bin file.\n");
		sysfs_remove_bin_file(&client->dev.kobj, &st25dv_user_attr);
		sysfs_remove_bin_file(&client_sys_area->dev.kobj, &st25dv_sys_attr);
		return status;
	}
	printk(KERN_WARNING "st25dv eeprom create bin file.\n");
	return status;
err_mem0:
	kfree(dyn_reg_data);
err_mem1:
	kfree(sys_data->data);
err_mem2:
	devm_kfree(&client_sys_area->dev, sys_data);
err_mem3:
	kfree(data->data);
err_mem4:
	devm_kfree(&client->dev, data);
err_mem:
	printk(KERN_WARNING "not enougth memory.\n");
	return -ENOMEM;
}

static int st25dv_remove(struct i2c_client *client)
{
	struct st25dv_data *data = i2c_get_clientdata(client);
	struct st25dv_data *sys_data = i2c_get_clientdata(client_sys_area);

	kfree(sys_data->data);
	kfree(data->data);
	/*free dyn_reg_data*/
	kfree(data->next->data);
	kfree(data->next);
	/*remove sysfs files*/
	sysfs_remove_bin_file(&client->dev.kobj, &st25dv_user_attr);
	sysfs_remove_bin_file(&client->dev.kobj, &st25dv_dyn_reg_attr);
	sysfs_remove_bin_file(&client_sys_area->dev.kobj, &st25dv_sys_attr);

	return 0;
}

static const struct i2c_device_id st25dv_id[] = {
	{ "st25dv", 0 },
	{ }
};

static struct i2c_driver st25dv_driver = {
	.driver = {
		.name	= "st25dv",
	},
	.probe		= st25dv_probe,
	.remove		= st25dv_remove,
	.id_table	= st25dv_id,

	.class		= I2C_CLASS_DDC | I2C_CLASS_SPD,
	.detect		= st25dv_detect,
	.address_list	= normal_i2c,
};

static int __init st25dv_i2c_init_driver(void)
{
	printk(KERN_WARNING "init st25dv eeprom driver.\n");
	return i2c_add_driver(&st25dv_driver);
}

static void __exit st25dv_i2c_exit_driver(void)
{
	printk(KERN_WARNING "remove st25dv eeprom driver.\n");
	i2c_del_driver(&st25dv_driver);
}

module_init(st25dv_i2c_init_driver);
module_exit(st25dv_i2c_exit_driver);

MODULE_INFO(intree, "y");
MODULE_AUTHOR("Loïc Boban <loic.boban@gmail.com>");
MODULE_DESCRIPTION("nfc/i2c eeprom st25dv driver");
MODULE_LICENSE("GPL");
