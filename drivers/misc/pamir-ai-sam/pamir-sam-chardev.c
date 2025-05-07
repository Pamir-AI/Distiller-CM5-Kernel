// SPDX-License-Identifier: GPL-2.0-only
/*
 * Pamir AI Sensor and Actuator Module (SAM) Character Device
 *
 * Character device interface for userspace access.
 *
 * Copyright (C) 2025 Pamir AI Incorporated - http://www.pamir.ai/
 */
#include "pamir-sam.h"

/* Global device data pointer for char device operations */
static struct sam_protocol_data *g_protocol_data;

/**
 * sam_protocol_open() - Open character device
 * @inode: Inode pointer
 * @filp: File pointer
 *
 * Handle open() syscall on the character device.
 *
 * Return: 0 on success, negative error code on failure
 */
static int sam_protocol_open(struct inode *inode, struct file *filp)
{
	struct sam_protocol_data *priv = g_protocol_data;

	if (!priv)
		return -ENODEV;

	filp->private_data = priv;
	return 0;
}

/**
 * sam_protocol_write() - Write data to UART
 * @filp: File pointer
 * @buf: User buffer containing data to send
 * @count: Number of bytes to send
 * @ppos: File position pointer (unused)
 *
 * Handle write() syscall on the character device to send packets.
 *
 * Return: Number of bytes sent on success, negative error code on failure
 */
static ssize_t sam_protocol_write(struct file *filp, const char __user *buf,
				   size_t count, loff_t *ppos)
{
	struct sam_protocol_data *priv = filp->private_data;
	struct sam_protocol_packet packet;

	if (!priv || !priv->serdev)
		return -ENODEV;

	if (count < PACKET_SIZE)
		return -EINVAL;

	if (copy_from_user(&packet, buf, PACKET_SIZE))
		return -EFAULT;

	if (send_packet(priv, &packet) != 0)
		return -EIO;

	return PACKET_SIZE;
}

/**
 * sam_protocol_read() - Read debug data from driver
 * @filp: File pointer
 * @buf: User buffer to fill
 * @count: Number of bytes to read
 * @ppos: File position pointer
 *
 * Read debug information from the debug circular buffer.
 *
 * Return: Number of bytes read on success, negative error code on failure
 */
static ssize_t sam_protocol_read(struct file *filp, char __user *buf,
				  size_t count, loff_t *ppos)
{
	struct sam_protocol_data *priv = filp->private_data;
	struct debug_code_entry entry;
	char debug_info[64];
	int len;

	if (!priv)
		return -ENODEV;

	mutex_lock(&priv->debug_mutex);

	if (priv->debug_head == priv->debug_tail) {
		mutex_unlock(&priv->debug_mutex);
		return 0;  /* No data available */
	}

	/* Get oldest entry */
	entry = priv->debug_codes[priv->debug_tail];
	priv->debug_tail = (priv->debug_tail + 1) % DEBUG_QUEUE_SIZE;

	mutex_unlock(&priv->debug_mutex);

	/* Format debug info */
	len = snprintf(debug_info, sizeof(debug_info),
		    "CAT:%u CODE:%u PARAM:%u TIME:%lu\n",
		    entry.category, entry.code, entry.param,
		    entry.timestamp);

	if (len <= 0)
		return -EINVAL;

	if (count < len)
		return -EINVAL;

	if (copy_to_user(buf, debug_info, len))
		return -EFAULT;

	return len;
}

/**
 * sam_protocol_release() - Release character device
 * @inode: Inode pointer
 * @filp: File pointer
 *
 * Handle close() syscall on the character device.
 *
 * Return: Always returns 0
 */
static int sam_protocol_release(struct inode *inode, struct file *filp)
{
	return 0;
}

/* File operations for character device */
static const struct file_operations sam_protocol_fops = {
	.owner = THIS_MODULE,
	.open = sam_protocol_open,
	.read = sam_protocol_read,
	.write = sam_protocol_write,
	.release = sam_protocol_release,
};

/**
 * setup_char_device() - Set up character device
 * @priv: Driver's private data
 *
 * Create character device node for userspace communication.
 *
 * Return: 0 on success, negative error code on failure
 */
int setup_char_device(struct sam_protocol_data *priv)
{
	int ret;
	struct device *device;

	/* Initialize mutex */
	mutex_init(&priv->tx_mutex);
	mutex_init(&priv->debug_mutex);

	/* Allocate device number */
	ret = alloc_chrdev_region(&priv->dev_no, 0, 1, DEVICE_NAME);
	if (ret < 0) {
		dev_err(&priv->serdev->dev,
	  "Failed to allocate device number: %d\n", ret);
		return ret;
	}

	/* Initialize character device */
	cdev_init(&priv->cdev, &sam_protocol_fops);
	priv->cdev.owner = THIS_MODULE;

	/* Add character device to system */
	ret = cdev_add(&priv->cdev, priv->dev_no, 1);
	if (ret < 0) {
		dev_err(&priv->serdev->dev,
	  "Failed to add character device: %d\n", ret);
		unregister_chrdev_region(priv->dev_no, 1);
		return ret;
	}

	/* Create device class */
	priv->dev_class = class_create(DEVICE_NAME);
	if (IS_ERR(priv->dev_class)) {
		ret = PTR_ERR(priv->dev_class);
		dev_err(&priv->serdev->dev,
	  "Failed to create device class: %d\n", ret);
		cdev_del(&priv->cdev);
		unregister_chrdev_region(priv->dev_no, 1);
		return ret;
	}

	/* Create device node */
	device = device_create(priv->dev_class, NULL, priv->dev_no, NULL,
			    DEVICE_NAME);
	if (IS_ERR(device)) {
		ret = PTR_ERR(device);
		dev_err(&priv->serdev->dev, "Failed to create device: %d\n", ret);
		class_destroy(priv->dev_class);
		cdev_del(&priv->cdev);
		unregister_chrdev_region(priv->dev_no, 1);
		return ret;
	}

	/* Store global data pointer for file operations */
	g_protocol_data = priv;

	dev_dbg(&priv->serdev->dev, "SAM protocol device created: /dev/%s\n",
	     DEVICE_NAME);
	return 0;
}

/**
 * cleanup_char_device() - Clean up character device resources
 * @priv: Driver's private data
 *
 * Release all resources allocated for the character device.
 */
void cleanup_char_device(struct sam_protocol_data *priv)
{
	if (priv->dev_class) {
		device_destroy(priv->dev_class, priv->dev_no);
		class_destroy(priv->dev_class);
	}

	cdev_del(&priv->cdev);
	unregister_chrdev_region(priv->dev_no, 1);

	g_protocol_data = NULL;
}
