// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * DRM driver for Good Display GDEY037T03 e-ink panels
 */

#include <linux/delay.h>
#include <linux/gpio/consumer.h>
#include <linux/module.h>
#include <linux/property.h>
#include <linux/spi/spi.h>

#include <drm/drm_mipi_dbi.h>
#include <drm/drm_atomic_helper.h>
#include <drm/drm_drv.h>
#include <drm/drm_fb_helper.h>
#include <drm/drm_fb_dma_helper.h>
#include <drm/drm_gem.h>
#include <drm/drm_gem_atomic_helper.h>
#include <drm/drm_gem_dma_helper.h>
#include <drm/drm_gem_framebuffer_helper.h>
#include <drm/drm_fbdev_dma.h>
#include <drm/drm_managed.h>
#include <drm/drm_modeset_helper.h>
#include <video/mipi_display.h>
#include <drm/drm_framebuffer.h>

// Constants
#define GDEY037T03_WIDTH 240
#define GDEY037T03_HEIGHT 416
#define GDEY037T03_BUSY_TIMEOUT 18000
#define FULL_REFRESH_COUNT 30

// Control signals
struct gpio_desc *dc_gpio, *busy_gpio, *reset_gpio;

// Display buffers
u8 out_buffer[(GDEY037T03_WIDTH * GDEY037T03_HEIGHT) / 8];

// Global full-refresh counter
static int refresh_counter = 0;

/** -------------
 * Display functions
 * --------------
 */

/*
 * Down-samples the 8bpp framebuffer to 1bpp for the display.
 */
static void gdey037t03_framebuffer_to_buffer(u8 *buf, u32 width, u32 height,
					     u8 bpp)
{
	const int threshold = 128; // Simple threshold for monochrome
	int bytes_per_pixel = bpp / 8;

	memset(out_buffer, 0xFF, sizeof(out_buffer)); // Initialize to white

	for (int y = 0; y < height; y++) {
		for (int x = 0; x < width; x++) {
			int i = (y * width + x) * bytes_per_pixel;
			int r = buf[i];
			int g = buf[i + 1];
			int b = buf[i + 2];

			// Convert to grayscale
			int gray = (r * 212 + g * 715 + b * 72) / 1000;

			int j = (y * width + x) / 8;
			int bit = 7 - (x % 8);

			if (gray < threshold)
				out_buffer[j] &= ~(1 << bit); // Set to black
		}
	}
}

/*
 * Put the display to sleep
 */
static void gdey037t03_sleep(struct mipi_dbi *dbi)
{
	mipi_dbi_command(dbi, 0x07, 0xA5); // Deep sleep
}

/*
 * Power off the display
 */
static void gdey037t03_power_off(struct mipi_dbi *dbi)
{
	mipi_dbi_command(dbi, 0x02); // Power off
}

/*
 * Wait for the busy signal to clear
 */
static int gdey037t03_busy_wait(unsigned long timeout_ms)
{
	int i = (int)timeout_ms;
	int busy_val;
	pr_info("%s: Waiting for BUSY pin to go LOW", __func__);

	while (i--) {
		busy_val = gpiod_get_value_cansleep(busy_gpio);
		if (!busy_val) {
			pr_info("BUSY pin went LOW after %d iterations",
				 (int)timeout_ms - i);
			return 0;
		}

		if (i % 1000 == 0)
			pr_info("BUSY pin still HIGH after %d iterations",
				 (int)timeout_ms - i);

		usleep_range(1000, 10000);
	}

	pr_err("%s: busy_wait Timeout occurred\n", __func__);
	return -EBUSY;
}

/*
 * Perform a full update
 */
static void gdey037t03_full_update(struct mipi_dbi *dbi, bool fast)
{
	gdey037t03_busy_wait(GDEY037T03_BUSY_TIMEOUT);

	if (fast) {
		mipi_dbi_command(dbi, 0xE0, 0x02);
		mipi_dbi_command(dbi, 0xE5, 0x5F);
	} else {
		mipi_dbi_command(dbi, 0x50, 0x97);
	}

	mipi_dbi_command(dbi, 0x12); // Display update
}

/*
 * Perform a partial update
 */
static void gdey037t03_partial_update(struct mipi_dbi *dbi, u16 x_start,
				      u16 y_start, u16 width, u16 height)
{
	gdey037t03_busy_wait(GDEY037T03_BUSY_TIMEOUT);

	// Enter partial mode
	mipi_dbi_command(dbi, 0x91);

	// Set partial window
	mipi_dbi_command(dbi, 0x90, x_start, x_start + width - 1, y_start >> 8,
			 y_start & 0xFF, (y_start + height - 1) >> 8,
			 (y_start + height - 1) & 0xFF, 0x01);

	mipi_dbi_command(dbi, 0x12); // Display update
}

/*
 * Set the RAM area for data writes
 */
static void gdey037t03_set_ram_area(struct mipi_dbi *dbi, u16 x, u16 y,
				    u16 width, u16 height)
{
	gdey037t03_busy_wait(GDEY037T03_BUSY_TIMEOUT);

	mipi_dbi_command(dbi, 0x11, 0x03); // Data entry mode
	mipi_dbi_command(dbi, 0x44, x & 0xFF, ((x + width - 1) & 0xFF));
	mipi_dbi_command(dbi, 0x45, y & 0xFF, (y >> 8) & 0x01,
			 (y + height - 1) & 0xFF,
			 ((y + height - 1) >> 8) & 0x01);
	mipi_dbi_command(dbi, 0x4E, x & 0xFF);
	mipi_dbi_command(dbi, 0x4F, y & 0xFF, (y >> 8) & 0x01);
}

/*
 * Clear the display to white
 */
static void gdey037t03_clear(struct mipi_dbi *dbi)
{
	memset(out_buffer, 0xFF, sizeof(out_buffer)); // White

	gdey037t03_set_ram_area(dbi, 0, 0, GDEY037T03_WIDTH, GDEY037T03_HEIGHT);

	mipi_dbi_command_buf(dbi, 0x10, out_buffer,
			     sizeof(out_buffer)); // Old data
	mipi_dbi_command_buf(dbi, 0x13, out_buffer,
			     sizeof(out_buffer)); // New data

	gdey037t03_full_update(dbi, false);
}

/** ---------------
 * DRM Functions
 * -----------------
 */
static void gdey037t03_pipe_update(struct drm_simple_display_pipe *pipe,
				   struct drm_plane_state *old_state)
{
	struct drm_plane_state *state = pipe->plane.state;
	struct drm_framebuffer *fb = state->fb;
	struct mipi_dbi_dev *dbidev = drm_to_mipi_dbi_dev(pipe->crtc.dev);
	struct mipi_dbi *dbi = &dbidev->dbi;
	struct drm_gem_dma_object *gem_obj;
	u32 width, height;
	u8 bpp;
	int idx;

	if (!fb)
		return;

	if (!drm_dev_enter(pipe->crtc.dev, &idx))
		return;

	width = fb->width;
	height = fb->height;
	bpp = fb->format->cpp[0] * 8;

	gem_obj = drm_fb_dma_get_gem_obj(fb, 0);
	void *src = gem_obj->vaddr;

	gdey037t03_framebuffer_to_buffer(src, width, height, bpp);

	gdey037t03_set_ram_area(dbi, 0, 0, GDEY037T03_WIDTH, GDEY037T03_HEIGHT);

	mipi_dbi_command_buf(dbi, 0x10, out_buffer,
			     sizeof(out_buffer)); // Old data
	mipi_dbi_command_buf(dbi, 0x13, out_buffer,
			     sizeof(out_buffer)); // New data

	if (refresh_counter % FULL_REFRESH_COUNT == 0) {
		gdey037t03_full_update(dbi, false);
	} else {
		gdey037t03_partial_update(dbi, 0, 0, GDEY037T03_WIDTH,
					  GDEY037T03_HEIGHT);
	}
	refresh_counter++;

	drm_dev_exit(idx);
}

static void gdey037t03_enable(struct drm_simple_display_pipe *pipe,
			      struct drm_crtc_state *crtc_state,
			      struct drm_plane_state *plane_state)
{
	struct mipi_dbi_dev *dbidev = drm_to_mipi_dbi_dev(pipe->crtc.dev);
	struct mipi_dbi *dbi = &dbidev->dbi;
	int idx;

	if (!drm_dev_enter(pipe->crtc.dev, &idx))
		return;

	pr_info("gdey037t03: Initializing display\n");

	mipi_dbi_hw_reset(dbi);
	msleep(20);
	mipi_dbi_command(dbi, 0x04); // Power on
	gdey037t03_busy_wait(GDEY037T03_BUSY_TIMEOUT);

	mipi_dbi_command(dbi, 0x50, 0x97); // VCOM and data interval
	mipi_dbi_command(dbi, 0x00, 0x13); // Panel setting for GUI mode
	mipi_dbi_command(dbi, 0x61, 0xF0, 0x01, 0xA0); // Resolution (240x416)

	gdey037t03_clear(dbi);

	mipi_dbi_enable_flush(dbidev, crtc_state, plane_state);

	drm_dev_exit(idx);
}

static void gdey037t03_release(struct drm_device *drm)
{
	struct mipi_dbi_dev *dbidev = drm_to_mipi_dbi_dev(drm);
	struct mipi_dbi *dbi = &dbidev->dbi;

	gdey037t03_power_off(dbi);
	pr_info("gdey037t03: Powered off\n");
}

static const u32 gdey037t03_formats[] = {
	DRM_FORMAT_XRGB8888,
};

static const struct drm_simple_display_pipe_funcs gdey037t03_pipe_funcs = {
	.mode_valid = mipi_dbi_pipe_mode_valid,
	.enable = gdey037t03_enable,
	.disable = mipi_dbi_pipe_disable,
	.update = gdey037t03_pipe_update,
};

static const struct drm_display_mode gdey037t03_mode = {
	DRM_SIMPLE_MODE(240, 416, 60, 80),
};

DEFINE_DRM_GEM_DMA_FOPS(gdey037t03_fops);

static const struct drm_driver gdey037t03_drm_driver = {
	.driver_features = DRIVER_GEM | DRIVER_MODESET | DRIVER_ATOMIC,
	.release = gdey037t03_release,
	.fops = &gdey037t03_fops,
	DRM_GEM_DMA_DRIVER_OPS_VMAP,
	.name = "gdey037t03",
	.desc = "GoodDisplay GDEY037T03",
	.date = "20250414",
	.major = 1,
	.minor = 0,
};

static int gdey037t03_probe(struct spi_device *spi)
{
	struct device *dev = &spi->dev;
	struct mipi_dbi_dev *dbidev;
	struct drm_device *drm;
	struct mipi_dbi *dbi;
	u32 rotation = 0;
	int ret;

	dbidev = devm_drm_dev_alloc(dev, &gdey037t03_drm_driver,
				    struct mipi_dbi_dev, drm);
	if (IS_ERR(dbidev))
		return PTR_ERR(dbidev);

	dbi = &dbidev->dbi;
	drm = &dbidev->drm;

	dbi->reset = devm_gpiod_get(dev, "reset", GPIOD_OUT_HIGH);
	if (IS_ERR(dbi->reset))
		return dev_err_probe(dev, PTR_ERR(dbi->reset),
				     "Failed to get GPIO 'reset'\n");

	dc_gpio = devm_gpiod_get(dev, "dc", GPIOD_OUT_HIGH);
	if (IS_ERR(dc_gpio))
		return dev_err_probe(dev, PTR_ERR(dc_gpio),
				     "Failed to get GPIO 'dc'\n");

	busy_gpio = devm_gpiod_get(dev, "busy", GPIOD_IN);
	if (IS_ERR(busy_gpio))
		return dev_err_probe(dev, PTR_ERR(busy_gpio),
				     "Failed to get GPIO 'busy'\n");

	ret = mipi_dbi_spi_init(spi, dbi, dc_gpio);
	if (ret) {
		dev_err(dev, "Failed to initialize SPI: %d\n", ret);
		return ret;
	}

	reset_gpio = dbi->reset;
	dbi->read_commands = NULL;

	ret = mipi_dbi_dev_init_with_formats(
		dbidev, &gdey037t03_pipe_funcs, gdey037t03_formats,
		ARRAY_SIZE(gdey037t03_formats), &gdey037t03_mode, rotation,
		GDEY037T03_WIDTH * GDEY037T03_HEIGHT / 8);
	if (ret) {
		dev_err(dev, "Failed to initialize mipi_dbi_dev: %d\n", ret);
		return ret;
	}

	drm_mode_config_reset(drm);

	ret = drm_dev_register(drm, 0);
	if (ret) {
		dev_err(dev, "Failed to register DRM device: %d\n", ret);
		return ret;
	}

	spi_set_drvdata(spi, drm);

	drm_fbdev_dma_setup(drm, 0);

	pr_info("gdey037t03: Registered DRM device\n");

	return 0;
}

static void gdey037t03_remove(struct spi_device *spi)
{
	struct drm_device *drm = spi_get_drvdata(spi);

	drm_dev_unplug(drm);
	drm_atomic_helper_shutdown(drm);
}

static const struct of_device_id gdey037t03_dt_ids[] = {
	{ .compatible = "pamir,gdey037t03" },
	{},
};
MODULE_DEVICE_TABLE(of, gdey037t03_dt_ids);

static struct spi_driver gdey037t03_spi_driver = {
    .driver = {
        .name = "gdey037t03",
        .of_match_table = gdey037t03_dt_ids,
    },
    .probe = gdey037t03_probe,
    .remove = gdey037t03_remove,
};

module_spi_driver(gdey037t03_spi_driver);

MODULE_AUTHOR("Grok, adapted from BasicCode");
MODULE_DESCRIPTION("Good Display GDEY037T03 DRM driver");
MODULE_LICENSE("GPL v2");
