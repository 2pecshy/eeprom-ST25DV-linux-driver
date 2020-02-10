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

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/slab.h>
#include <linux/device.h>
#include <linux/i2c.h>
#include <linux/uaccess.h>
#include <linux/delay.h>
#include <linux/mutex.h>

#define DYN_REG_SIZE		0x8
#define SYS_MEM_SIZE		0x24
#define USER_MEM_SIZE		0x200
#define MAILBOX_MEM_SIZE        0x100
#define SYS_ADDR		0x57
#define USER_ADDR		0x53
#define PWD_OFF			0x0900
#define DYN_REG_OFF		0x2000
#define MAILBOX_OFF		0x2008
#define MAX_TRY			10

#define PWD_REQ_SIZE		0x13
#define PWD_SIZE		0x08
#define CMD_PRESENT_PWD		0x09
#define CMD_WRITE_PWD		0x07
#define PWD_CMD_POS 		10
#define PWD1_POS 		2
#define PWD2_POS 		11

#define MEM_04K                 512
#define MEM_16K                 2000
#define MEM_64K                 8000

static const struct bin_attribute st25dv_p_pwd_attr;

enum area_type{
	USER_AREA = 0,
	SYS_AREA = 1,
	DYN_REG_AREA = 2,
	MAILBOX_AREA = 3,
};

/*the st25dv eeprom have two areas, the user area, and the system 
area to manage read/write protection for the NFC interface and I2C
interface. To drive the system area a dummy i2c_client is used*/
static int mem_config[4] = {MEM_04K, MEM_04K, MEM_16K, MEM_64K};
static int area_off[4] = {0, 0, DYN_REG_OFF, MAILBOX_OFF};

/*one struct is used for each area*/
struct st25dv_data {
	u8 *data;//area data
	enum area_type type;
	struct bin_attribute bin_attr;
	struct i2c_client *client;
        struct mutex *update_lock;//protect for concurrent updates
	struct st25dv_data *next;
};

/* Addresses to scan */
static const unsigned short normal_i2c[] = { USER_ADDR, SYS_ADDR, I2C_CLIENT_END };

static ssize_t st25dv_read(struct file *filp, struct kobject *kobj,
			   struct bin_attribute *bin_attr, char *buf, loff_t off, size_t count)
{
	int r_size, nack;
	struct i2c_client *client = to_i2c_client(kobj_to_dev(kobj));
	struct st25dv_data *data = i2c_get_clientdata(client);
	u16 cur_off;

	while(&data->bin_attr != bin_attr){
		data = data->next;
	}
	off += area_off[data->type];
	mutex_lock(data->update_lock);
	for(cur_off = off; cur_off-off < count; cur_off++){
		nack = 0;
retry_:
		if(nack > MAX_TRY){
			mutex_unlock(data->update_lock);
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
	mutex_unlock(data->update_lock);
	//printk(KERN_WARNING "st25dv: %d byte reads.\n",count);

	return count;
}

static ssize_t st25dv_write_area(struct file *filp, struct kobject *kobj,
			struct bin_attribute *bin_attr, char *buf, loff_t off, size_t count)
{
	int r_size;
	struct i2c_client *client = to_i2c_client(kobj_to_dev(kobj));
	struct st25dv_data *data = i2c_get_clientdata(client);
	u16 cur_off, tmp;
	u8 nack;

	while(&data->bin_attr != bin_attr){
		data = data->next;
	}
	off += area_off[data->type];
	mutex_lock(data->update_lock);
	memcpy(data->data + off, buf, count);
	for(cur_off = off; cur_off-off < count; cur_off++){
		nack = 0;
		tmp = data->data[cur_off];
		tmp = (tmp << 8) & 0xff00;
		tmp |= cur_off & 0x00ff;
retry_:
		if(nack > MAX_TRY){
			mutex_unlock(data->update_lock);
			return r_size;
		}
		r_size = i2c_smbus_write_word_data(client, (cur_off >> 8) & 0x0ff, tmp );
		if(r_size < 0){
			nack++;
			mdelay(1);
			goto retry_;
		}

	}
	mutex_unlock(data->update_lock);
	//printk(KERN_WARNING "st25dv: %d byte writes.\n",count);

	return count;
}

//I2C_SMBUS_BLOCK_MAX = 9 page writes
//MAX tw = 9 * 5ms
//write_block is faster than write single byte but not supported by some adapter 
static ssize_t st25dv_write_block(struct file *filp, struct kobject *kobj,
				  struct bin_attribute *bin_attr, char *buf, loff_t off, size_t count)
{
	int r_size, to_write, not_write;
	struct i2c_client *client = to_i2c_client(kobj_to_dev(kobj));
	struct st25dv_data *data = i2c_get_clientdata(client);
	u16 cur_off, cur_buf_off;
	u8 nack, tmp[I2C_SMBUS_BLOCK_MAX];

	while(&data->bin_attr != bin_attr){
		data = data->next;
	}
	off += area_off[data->type];
	mutex_lock(data->update_lock);
	memcpy(data->data + off, buf, count);
	not_write = count;
	cur_off = off;
	cur_buf_off = 0;
	while(not_write){
		nack = 0;
		to_write = not_write > I2C_SMBUS_BLOCK_MAX-2 ? I2C_SMBUS_BLOCK_MAX-2 : not_write;
		not_write -= to_write;
		tmp[1] = cur_off;
		tmp[0] = cur_off >> 8;
		memcpy(tmp + 2, buf + cur_buf_off, to_write);
retry_:
		if(nack > MAX_TRY){
			mutex_unlock(data->update_lock);
			return r_size;
		}
		r_size = i2c_master_send(client, tmp, to_write+2);
		if(r_size < 0){
			nack++;
			mdelay(5);
			goto retry_;
		}
		mdelay(20);
		cur_off += to_write;
		cur_buf_off += to_write;
	}

	mutex_unlock(data->update_lock);
	//printk(KERN_WARNING "st25dv: %d byte writes.\n",count);

	return count;
}

static ssize_t st25dv_send_pwd_req(struct file *filp, struct kobject *kobj,
			   struct bin_attribute *bin_attr,
				   char *buf, loff_t off, size_t count)
{
	u8 pwd_req[PWD_REQ_SIZE], cmd, *pwd_ptr1, *pwd_ptr2;
	int r_size, off_tmp, nack;
	struct i2c_client *client = to_i2c_client(kobj_to_dev(kobj));
	struct st25dv_data *data = i2c_get_clientdata(client);

	cmd = bin_attr == &st25dv_p_pwd_attr ? CMD_PRESENT_PWD : CMD_WRITE_PWD;
	if(count != PWD_SIZE){
		printk(KERN_WARNING "st25dv: send pwd cmd fail count=%d.\n", count);
		return count;
	}
	nack = 0;
	pwd_req[0] = 0x09;
	pwd_req[1] = 0x00;
	pwd_ptr1 = &pwd_req[PWD1_POS];
	pwd_ptr2 = &pwd_req[PWD2_POS];
	pwd_req[PWD_CMD_POS] = cmd;
	for(off_tmp = PWD_SIZE-1; off_tmp >= 0; off_tmp--){
		*pwd_ptr1 = buf[off_tmp];
		*pwd_ptr2 = buf[off_tmp];
		pwd_ptr1++;
		pwd_ptr2++;
	}
	mutex_lock(data->update_lock);
retry_:
	if(nack > MAX_TRY){
		mutex_unlock(data->update_lock);
		return r_size;
	}
	r_size = i2c_master_send(client, pwd_req, PWD_REQ_SIZE);
	if(r_size < 0){
		nack++;
		mdelay(5);
		goto retry_;
	}
	mutex_unlock(data->update_lock);
	//printk(KERN_WARNING "st25dv: send pwd cmd send.\n");

	return count;
}

static struct bin_attribute st25dv_user_attr = {
	.attr = {
		.name = "st25dv_user",
		.mode = S_IRUGO|S_IWUGO,
	},
	.size = USER_MEM_SIZE,
	.read = st25dv_read,
	.write = st25dv_write_block,
};

static const struct bin_attribute st25dv_sys_attr = {
	.attr = {
		.name = "st25dv_sys",
		.mode = S_IRUGO|S_IWUSR,
	},
	.size = SYS_MEM_SIZE,
	.read = st25dv_read,
        .write = st25dv_write_block,
};

static const struct bin_attribute st25dv_dyn_reg_attr = {
        .attr = {
                .name = "st25dv_dyn_reg",
                .mode = S_IRUGO|S_IWUSR,
  },
        .size = DYN_REG_SIZE,
        .read = st25dv_read,
        .write = st25dv_write_block,
};

static const struct bin_attribute st25dv_mailbox_attr = {
        .attr = {
                .name = "st25dv_mailbox",
                .mode = S_IRUGO|S_IWUGO,
  },
        .size = MAILBOX_MEM_SIZE,
        .read = st25dv_read,
        .write = st25dv_write_block,
};

static const struct bin_attribute st25dv_w_pwd_attr = {
	.attr = {
		.name = "st25dv_write_pwd",
		.mode = S_IWUSR,
	},
	.size = PWD_SIZE,
	.write = st25dv_send_pwd_req,
};

static const struct bin_attribute st25dv_p_pwd_attr = {
	.attr = {
		.name = "st25dv_present_pwd",
		.mode = S_IWUSR,
	},
	.size = PWD_SIZE,
	.write = st25dv_send_pwd_req,
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
	struct mutex *st25dv_lock;
	struct st25dv_data *data;
	struct st25dv_data *sys_data;
	struct st25dv_data *dyn_reg_data;
	struct st25dv_data *mailbox_data;
	struct i2c_client *client_sys_area;//i2c_client of the system area

	/*dummy client for the system area at addr 0x57*/
	client_sys_area = i2c_new_dummy(client->adapter, SYS_ADDR);
	if(!client_sys_area){
		printk(KERN_WARNING "st25dv sys eeprom not detected.\n");
		return -ENODEV;
	} 
	data = devm_kzalloc(&client->dev, sizeof(struct st25dv_data), GFP_KERNEL);
	if (!data)
		goto err_mem;
	data->data = kmalloc(sizeof(u8)*mem_config[id->driver_data], GFP_KERNEL);
	if (!data->data)
		goto err_mem7;
	sys_data = devm_kzalloc(&client_sys_area->dev, sizeof(struct st25dv_data)
				, GFP_KERNEL);
	if (!sys_data)
		goto err_mem6;
	sys_data->data = kmalloc(sizeof(u8)*SYS_MEM_SIZE, GFP_KERNEL);
	if (!sys_data->data)
		goto err_mem5;
	dyn_reg_data = kmalloc(sizeof(struct st25dv_data), GFP_KERNEL);
	if(!dyn_reg_data)
		goto err_mem4;
	dyn_reg_data->data = kmalloc(sizeof(u8)*DYN_REG_SIZE, GFP_KERNEL);
	if (!dyn_reg_data->data)
		goto err_mem3;
	mailbox_data = kmalloc(sizeof(struct st25dv_data), GFP_KERNEL);
	if(!mailbox_data)
		goto err_mem2;
        mailbox_data->data = kmalloc(sizeof(u8)*MAILBOX_MEM_SIZE, GFP_KERNEL);
	if (!mailbox_data->data)
		goto err_mem1;

	st25dv_lock = kmalloc(sizeof(struct mutex), GFP_KERNEL);
	if (!st25dv_lock)
	        goto err_mem0;    
	mutex_init(st25dv_lock);
	data->client =  client;
	sys_data->client = client_sys_area;
	mailbox_data->client = client;
	dyn_reg_data->client = client;
	memset(data->data, 0xff, mem_config[id->driver_data]);
	memset(sys_data->data, 0xff, SYS_MEM_SIZE);
	memset(dyn_reg_data->data, 0xff, DYN_REG_SIZE);
	memset(mailbox_data->data, 0xff, MAILBOX_MEM_SIZE);

	/*set circular linked list and shared mutex*/
	data->next = dyn_reg_data;
	data->update_lock = st25dv_lock;
	dyn_reg_data->next = mailbox_data;
	dyn_reg_data->update_lock = st25dv_lock;
	mailbox_data->next = sys_data;
        mailbox_data->update_lock = st25dv_lock;
	sys_data->next = data;
	sys_data->update_lock = st25dv_lock;

	memcpy(&mailbox_data->bin_attr, &st25dv_mailbox_attr, sizeof(struct bin_attribute));
	memcpy(&dyn_reg_data->bin_attr, &st25dv_dyn_reg_attr, sizeof(struct bin_attribute));
	memcpy(&sys_data->bin_attr, &st25dv_sys_attr, sizeof(struct bin_attribute));
	memcpy(&data->bin_attr, &st25dv_user_attr, sizeof(struct bin_attribute));
	data->bin_attr.size = mem_config[id->driver_data];
	data->type = USER_AREA;
	dyn_reg_data->type = DYN_REG_AREA;
	sys_data->type = SYS_AREA;
	mailbox_data->type = MAILBOX_AREA;
	i2c_set_clientdata(client, data);
	i2c_set_clientdata(client_sys_area, sys_data);
	
	/* create tree sysfs eeprom files to for pwd, user area,
	 system area and dynamic registers */
	status = sysfs_create_bin_file(&client->dev.kobj, &data->bin_attr);
	if(status < 0)
		goto err_sysfs;
	status = sysfs_create_bin_file(&client_sys_area->dev.kobj, &sys_data->bin_attr);
	if(status < 0)
		goto err_sysfs4;
	status = sysfs_create_bin_file(&client->dev.kobj, &dyn_reg_data->bin_attr);
	if(status < 0)
		goto err_sysfs3;
	status = sysfs_create_bin_file(&client_sys_area->dev.kobj, &st25dv_w_pwd_attr);
	if(status < 0)
		goto err_sysfs2;
	status = sysfs_create_bin_file(&client_sys_area->dev.kobj, &st25dv_p_pwd_attr);
	if(status < 0)
		goto err_sysfs1;
	status = sysfs_create_bin_file(&client->dev.kobj, &mailbox_data->bin_attr);
	if(status < 0)
		goto err_sysfs0;
	printk(KERN_WARNING "st25dv eeprom create bin file.\n");
	return status;
err_sysfs0:
	sysfs_remove_bin_file(&client_sys_area->dev.kobj, &st25dv_p_pwd_attr);
err_sysfs1:
	sysfs_remove_bin_file(&client_sys_area->dev.kobj, &st25dv_w_pwd_attr);
err_sysfs2:
	sysfs_remove_bin_file(&client->dev.kobj, &dyn_reg_data->bin_attr);
err_sysfs3:
	sysfs_remove_bin_file(&client_sys_area->dev.kobj, &sys_data->bin_attr);
err_sysfs4:
	sysfs_remove_bin_file(&client->dev.kobj, &data->bin_attr);
err_sysfs:
	printk(KERN_WARNING "fail to create bin file.\n");
err_mem0:
	kfree(mailbox_data->data);
err_mem1:
	kfree(mailbox_data);
err_mem2:
	kfree(dyn_reg_data->data);
err_mem3:
	kfree(dyn_reg_data);
err_mem4:
	kfree(sys_data->data);
err_mem5:
	devm_kfree(&client_sys_area->dev, sys_data);
	i2c_unregister_device(client_sys_area);
err_mem6:
	kfree(data->data);
err_mem7:
	devm_kfree(&client->dev, data);
err_mem:
	printk(KERN_WARNING "not enougth memory.\n");
	return -ENOMEM;
}

static int st25dv_remove(struct i2c_client *client)
{
	struct st25dv_data *data = i2c_get_clientdata(client);
	struct st25dv_data *tmp_data2, *tmp_data;
	struct i2c_client *client_sys_area;

	/*get system area client*/
	tmp_data = data;
	while(tmp_data->type != SYS_AREA)
		 tmp_data = tmp_data->next;	
	client_sys_area = tmp_data->client;
	/*remove pwd sysfs files*/
	sysfs_remove_bin_file(&client_sys_area->dev.kobj, &st25dv_p_pwd_attr);
	sysfs_remove_bin_file(&client_sys_area->dev.kobj, &st25dv_w_pwd_attr);
	/*free all st25dv data allocations*/
	tmp_data = data->next;
	while(tmp_data != data){
		/*remove areas sysfs files*/
		sysfs_remove_bin_file(&tmp_data->client->dev.kobj, &tmp_data->bin_attr);
		tmp_data2 = tmp_data;
		kfree(tmp_data->data);
		tmp_data = tmp_data->next;
		/*sys_area data will be free by i2c_unregister_device*/
		if(tmp_data2->type != SYS_AREA)
			kfree(tmp_data2);
	}
	/*free st25dv user mem data*/
	sysfs_remove_bin_file(&data->client->dev.kobj, &tmp_data->bin_attr);
	kfree(data->data);
	kfree(data->update_lock);
	/*unregister dummy device*/
	i2c_unregister_device(client_sys_area);

	return 0;
}

static const struct i2c_device_id st25dv_id[] = {
	{ "st25dv", 0 },
	{ "st25dv04k", 1 },
	{ "st25dv16k", 2 },
	{ "st25dv64k", 3 },
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
