/* linux/drivers/media/platform/exynos/jpeg4/jpeg_dev.c
 *
 * Copyright (c) 2013 Samsung Electronics Co., Ltd.
 * http://www.samsung.com/
 *
 * Core file for Samsung Jpeg v4.x Interface driver
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
*/
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/version.h>
#include <linux/errno.h>
#include <linux/fs.h>
#include <linux/irq.h>
#include <linux/interrupt.h>
#include <linux/io.h>
#include <linux/slab.h>
#include <linux/err.h>
#include <linux/miscdevice.h>
#include <linux/platform_device.h>
#include <linux/mm.h>
#include <linux/init.h>
#include <linux/poll.h>
#include <linux/signal.h>
#include <linux/ioport.h>
#include <linux/kmod.h>
#include <linux/vmalloc.h>
#include <linux/time.h>
#include <linux/clk.h>
#include <linux/semaphore.h>
#include <linux/vmalloc.h>
#include <linux/workqueue.h>

#include <linux/of.h>
#include <linux/exynos_iovmm.h>
#include <asm/page.h>

#include <mach/irqs.h>

#include <plat/cpu.h>

#ifdef CONFIG_PM_RUNTIME
#include <linux/pm_runtime.h>
#endif

#include <media/v4l2-ioctl.h>

#include "jpeg_core.h"
#include "jpeg_dev.h"

#include "jpeg_mem.h"
#include "jpeg_regs.h"
#include "regs_jpeg_v4_x.h"

static int jpeg_buf_prepare(struct vb2_buffer *vb)
{
	int i;
	int num_plane = 0;

	struct jpeg_ctx *ctx = vb2_get_drv_priv(vb->vb2_queue);

	if (vb->vb2_queue->type == V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE) {
		num_plane = ctx->param.in_plane;
		ctx->jpeg_dev->vb2->buf_prepare(vb);
	} else if (vb->vb2_queue->type == V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE) {
		num_plane = ctx->param.out_plane;
		ctx->jpeg_dev->vb2->buf_prepare(vb);
	}

	for (i = 0; i < num_plane; i++)
		vb2_set_plane_payload(vb, i, ctx->payload[i]);

	return 0;
}

static int jpeg_buf_finish(struct vb2_buffer *vb)
{
	struct jpeg_ctx *ctx = vb2_get_drv_priv(vb->vb2_queue);

	if (vb->vb2_queue->type == V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE) {
		ctx->jpeg_dev->vb2->buf_finish(vb);
	} else if (vb->vb2_queue->type == V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE) {
		ctx->jpeg_dev->vb2->buf_finish(vb);
	}

	return 0;
}

static void jpeg_buf_queue(struct vb2_buffer *vb)
{
	struct jpeg_ctx *ctx = vb2_get_drv_priv(vb->vb2_queue);

	if (ctx->m2m_ctx)
		v4l2_m2m_buf_queue(ctx->m2m_ctx, vb);
}

static int jpeg_queue_setup(struct vb2_queue *vq,
					const struct v4l2_format *fmt, unsigned int *num_buffers,
					unsigned int *num_planes, unsigned int sizes[],
					void *allocators[])
{
	struct jpeg_ctx *ctx = vb2_get_drv_priv(vq);
	struct jpeg_dev *dev = ctx->jpeg_dev;
	struct jpeg_frame *frame;
	int i;

	frame = ctx_get_frame(ctx, vq->type);
	if (IS_ERR(frame))
		return PTR_ERR(frame);

	/* Get number of planes from format_list in driver */
	*num_planes = frame->jpeg_fmt->memplanes;
	for (i = 0; i < frame->jpeg_fmt->memplanes; i++) {
		if (dev->mode == ENCODING &&  vq->type == V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE) {
			sizes[i] = (frame->width * frame->height *
					frame->jpeg_fmt->depth[i] * 2) >> 3;
		} else if (dev->mode == DECODING && vq->type == V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE) {
			sizes[i] = (frame->width * frame->height *
					frame->jpeg_fmt->depth[i] * 2) >> 3;
		} else {
			sizes[i] = (frame->width * frame->height *
					frame->jpeg_fmt->depth[i]) >> 3;
		}
		allocators[i] = ctx->jpeg_dev->alloc_ctx;
	}

	return 0;
}

static void jpeg_lock(struct vb2_queue *vq)
{
	struct jpeg_ctx *ctx = vb2_get_drv_priv(vq);
	mutex_lock(&ctx->jpeg_dev->lock);
}

static void jpeg_unlock(struct vb2_queue *vq)
{
	struct jpeg_ctx *ctx = vb2_get_drv_priv(vq);
	mutex_unlock(&ctx->jpeg_dev->lock);
}

static struct vb2_ops jpeg_vb2_qops = {
	.queue_setup		= jpeg_queue_setup,
	.buf_prepare		= jpeg_buf_prepare,
	.buf_finish		= jpeg_buf_finish,
	.buf_queue		= jpeg_buf_queue,
	.wait_prepare		= jpeg_unlock,
	.wait_finish		= jpeg_lock,
};

static int queue_init(void *priv, struct vb2_queue *src_vq,
		      struct vb2_queue *dst_vq)
{
	struct jpeg_ctx *ctx = priv;
	int ret;

	memset(src_vq, 0, sizeof(*src_vq));
	src_vq->type = V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE;
	src_vq->io_modes = VB2_MMAP | VB2_USERPTR | VB2_DMABUF;
	src_vq->drv_priv = ctx;
	src_vq->buf_struct_size = sizeof(struct v4l2_m2m_buffer);
	src_vq->ops = &jpeg_vb2_qops;
	src_vq->mem_ops = ctx->jpeg_dev->vb2->ops;
	src_vq->timestamp_type = V4L2_BUF_FLAG_TIMESTAMP_COPY;

	ret = vb2_queue_init(src_vq);
	if (ret)
		return ret;

	memset(dst_vq, 0, sizeof(*dst_vq));
	dst_vq->type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
	dst_vq->io_modes = VB2_MMAP | VB2_USERPTR | VB2_DMABUF;
	dst_vq->drv_priv = ctx;
	dst_vq->buf_struct_size = sizeof(struct v4l2_m2m_buffer);
	dst_vq->ops = &jpeg_vb2_qops;
	dst_vq->mem_ops = ctx->jpeg_dev->vb2->ops;
	dst_vq->timestamp_type = V4L2_BUF_FLAG_TIMESTAMP_COPY;

	return vb2_queue_init(dst_vq);
}

static int jpeg_m2m_open(struct file *file)
{
	struct jpeg_dev *jpeg = video_drvdata(file);
	struct jpeg_ctx *ctx = NULL;
	int ret = 0;

	ctx = kzalloc(sizeof *ctx, GFP_KERNEL);
	if (!ctx)
		return -ENOMEM;

	file->private_data = ctx;
	ctx->jpeg_dev = jpeg;

	spin_lock_init(&ctx->slock);

	ctx->m2m_ctx =
		v4l2_m2m_ctx_init(jpeg->m2m_dev, ctx,
			queue_init);

	if (IS_ERR(ctx->m2m_ctx)) {
		ret = PTR_ERR(ctx->m2m_ctx);
		kfree(ctx);
		return ret;
	}
#ifdef CONFIG_PM_RUNTIME
	pm_runtime_get_sync(&jpeg->plat_dev->dev);
#endif
	return 0;
}

static int jpeg_m2m_release(struct file *file)
{
	struct jpeg_ctx *ctx = file->private_data;

	v4l2_m2m_ctx_release(ctx->m2m_ctx);

#ifdef CONFIG_PM_RUNTIME
	pm_runtime_put_sync(&ctx->jpeg_dev->plat_dev->dev);
#endif
	kfree(ctx);

	return 0;
}

static unsigned int jpeg_m2m_poll(struct file *file,
				     struct poll_table_struct *wait)
{
	struct jpeg_ctx *ctx = file->private_data;

	return v4l2_m2m_poll(file, ctx->m2m_ctx, wait);
}


static int jpeg_m2m_mmap(struct file *file, struct vm_area_struct *vma)
{
	struct jpeg_ctx *ctx = file->private_data;

	return v4l2_m2m_mmap(file, ctx->m2m_ctx, vma);
}

static const struct v4l2_file_operations jpeg_fops = {
	.owner		= THIS_MODULE,
	.open		= jpeg_m2m_open,
	.release		= jpeg_m2m_release,
	.poll			= jpeg_m2m_poll,
	.unlocked_ioctl = video_ioctl2,
	.mmap		= jpeg_m2m_mmap,
};

static struct video_device jpeg_videodev = {
	.name = JPEG_NAME,
	.fops = &jpeg_fops,
	.minor = EXYNOS_VIDEONODE_JPEG,
	.release = video_device_release,
};

static void jpeg_device_run(void *priv)
{
	struct jpeg_ctx *ctx = priv;
	struct jpeg_dev *jpeg = ctx->jpeg_dev;
	struct exynos_platform_jpeg *pdata = jpeg->pdata;
	struct jpeg_param param;
	struct vb2_buffer *vb = NULL;
	unsigned long flags;
	unsigned int component;
	unsigned int ret = 0;

	spin_lock_irqsave(&ctx->slock, flags);

	param = ctx->param;
	jpeg_sw_reset(jpeg->reg_base);
	jpeg_set_interrupt(jpeg->reg_base);

	jpeg_set_huf_table_enable(jpeg->reg_base, true);
	jpeg_set_enc_tbl(jpeg->reg_base, param.quality);
	jpeg_set_encode_tbl_select(jpeg->reg_base, param.quality);
	jpeg_set_stream_size(jpeg->reg_base,
			param.in_width, param.in_height);

	if (jpeg->mode == ENCODING) {
		jpeg_set_enc_out_fmt(jpeg->reg_base, param.out_fmt);
		jpeg_set_enc_in_fmt(jpeg->reg_base, param.in_fmt);
		vb = v4l2_m2m_next_dst_buf(ctx->m2m_ctx);
		jpeg_set_stream_buf_address(jpeg->reg_base, jpeg->vb2->plane_addr(vb, 0));

		vb = v4l2_m2m_next_src_buf(ctx->m2m_ctx);
		jpeg_set_frame_buf_address(jpeg->reg_base,
				param.in_fmt, jpeg->vb2->plane_addr(vb, 0), param.in_width, param.in_height);
		jpeg_set_encode_hoff_cnt(jpeg->reg_base, param.out_fmt);
	} else if (jpeg->mode == DECODING) {
		jpeg_set_dec_in_fmt(jpeg->reg_base, param.in_fmt);
		jpeg_set_dec_out_fmt(jpeg->reg_base, param.out_fmt);
		jpeg_alpha_value_set(jpeg->reg_base, 0xff);

		vb = v4l2_m2m_next_src_buf(ctx->m2m_ctx);
		jpeg_set_stream_buf_address(jpeg->reg_base, jpeg->vb2->plane_addr(vb, 0));

		vb = v4l2_m2m_next_dst_buf(ctx->m2m_ctx);
		jpeg_set_frame_buf_address(jpeg->reg_base,
				param.out_fmt, jpeg->vb2->plane_addr(vb, 0), param.out_width, param.out_height);

		if (param.out_width > 0 && param.out_height > 0) {
			if ((param.out_width == param.in_width / 2) &&
				(param.out_height == param.in_height / 2))
				jpeg_set_dec_scaling(jpeg->reg_base, JPEG_SCALE_2);
			else if ((param.out_width == param.in_width / 4) &&
				(param.out_height == param.in_height / 4))
				jpeg_set_dec_scaling(jpeg->reg_base, JPEG_SCALE_4);
			else if (is_ver_5h && (param.out_width == param.in_width / 8) &&
				(param.out_height == param.in_height / 8))
				jpeg_set_dec_scaling(jpeg->reg_base, JPEG_SCALE_8);
			else
				jpeg_set_dec_scaling(jpeg->reg_base, JPEG_SCALE_NORMAL);
		}

		if (param.top_margin != 0 || param.left_margin != 0 ||
				param.bottom_margin != 0 || param.right_margin != 0) {
			jpeg_set_window_margin(jpeg->reg_base, param.top_margin, param.bottom_margin,
					param.left_margin, param.right_margin);
		}

		if (param.in_fmt == JPEG_GRAY)
			component = 1;
		else
			component = 3;
		if ((ret = jpeg_set_number_of_component(jpeg->reg_base, component)))
			printk(KERN_ERR "component is incorrect\n");

		if (((jpeg->vb2->plane_addr(vb, 0) + 0x23f) + param.size) % 16 == 0) {
			jpeg_set_dec_bitstream_size(jpeg->reg_base, param.size / 16);
		} else {
			jpeg_set_dec_bitstream_size(jpeg->reg_base, (param.size / 16) + 1);
		}
	}
	jpeg_set_timer_count(jpeg->reg_base, param.in_width * param.in_height * 8 + 0xff);
	jpeg_set_enc_dec_mode(jpeg->reg_base, jpeg->mode);

	spin_unlock_irqrestore(&ctx->slock, flags);
}

static void jpeg_job_abort(void *priv) { }

static struct v4l2_m2m_ops jpeg_m2m_ops = {
	.device_run	= jpeg_device_run,
	.job_abort	= jpeg_job_abort,
};

int jpeg_int_pending(struct jpeg_dev *ctrl)
{
	unsigned int	int_status;

	int_status = jpeg_get_int_status(ctrl->reg_base);
	jpeg_dbg("state(%d)\n", int_status);

	return int_status;
}

static irqreturn_t jpeg_irq(int irq, void *priv)
{
	unsigned int int_status;
	struct vb2_buffer *src_vb, *dst_vb;
	struct jpeg_dev *ctrl = priv;
	struct jpeg_ctx *ctx;
	unsigned long payload_size = 0;

	jpeg_clean_interrupt(ctrl->reg_base);

	ctx = v4l2_m2m_get_curr_priv(ctrl->m2m_dev);
	if (ctx == 0) {
		printk(KERN_ERR "ctx is null.\n");
		jpeg_sw_reset(ctrl->reg_base);
		goto ctx_err;
	}

	spin_lock(&ctx->slock);

	src_vb = v4l2_m2m_src_buf_remove(ctx->m2m_ctx);
	dst_vb = v4l2_m2m_dst_buf_remove(ctx->m2m_ctx);

	int_status = jpeg_int_pending(ctrl);

	if (int_status) {
		switch (int_status & 0x1ff) {
		case 0x1:
			ctrl->irq_ret = ERR_PROT;
			break;
		case 0x2:
			ctrl->irq_ret = OK_ENC_OR_DEC;
			break;
		case 0x4:
			ctrl->irq_ret = ERR_DEC_INVALID_FORMAT;
			break;
		case 0x8:
			ctrl->irq_ret = ERR_MULTI_SCAN;
			break;
		case 0x10:
			ctrl->irq_ret = ERR_FRAME;
			break;
		case 0x20:
			ctrl->irq_ret = ERR_TIME_OUT;
			break;
		default:
			ctrl->irq_ret = ERR_UNKNOWN;
			break;
		}
	} else {
		ctrl->irq_ret = ERR_UNKNOWN;
	}

	if (ctrl->irq_ret == OK_ENC_OR_DEC) {
		if (ctrl->mode == ENCODING) {
			payload_size = jpeg_get_stream_size(ctrl->reg_base);
			vb2_set_plane_payload(dst_vb, 0, payload_size);
		}
		v4l2_m2m_buf_done(src_vb, VB2_BUF_STATE_DONE);
		v4l2_m2m_buf_done(dst_vb, VB2_BUF_STATE_DONE);
	} else {
		v4l2_m2m_buf_done(src_vb, VB2_BUF_STATE_ERROR);
		v4l2_m2m_buf_done(dst_vb, VB2_BUF_STATE_ERROR);
	}

	v4l2_m2m_job_finish(ctrl->m2m_dev, ctx->m2m_ctx);

	spin_unlock(&ctx->slock);
ctx_err:
	return IRQ_HANDLED;
}
#ifdef CONFIG_OF
static void jpeg_parse_dt(struct device_node *np, struct jpeg_dev *jpeg)
{
	struct exynos_platform_jpeg *pdata = jpeg->pdata;

	if (!np)
		return;

	of_property_read_u32(np, "ip_ver", &pdata->ip_ver);
}
#else
static void jpeg_parse_dt(struct device_node *np, struct gsc_dev *gsc)
{
	return;
}
#endif

static const struct of_device_id exynos_jpeg_match[] = {
	{
		.compatible = "samsung,fimp_v4",
	},
	{},
};
MODULE_DEVICE_TABLE(of, exynos_jpeg_match);

static int jpeg_probe(struct platform_device *pdev)
{
	struct jpeg_dev *jpeg;
	struct video_device *vfd;
	struct device *dev = &pdev->dev;
	struct exynos_platform_jpeg *pdata = NULL;

	struct resource *res;
	int ret;

	jpeg = devm_kzalloc(&pdev->dev, sizeof(struct jpeg_dev), GFP_KERNEL);
	if (!jpeg) {
		dev_err(&pdev->dev, "%s: not enough memory\n", __func__);
		return -ENOMEM;
	}

	if (!dev->platform_data) {
		jpeg->id = of_alias_get_id(pdev->dev.of_node, "fimp_v4");
	} else {
		jpeg->id = pdev->id;
		pdata = dev->platform_data;
		if (!pdata) {
			dev_err(&pdev->dev, "no platform data\n");
			return -EINVAL;
		}
	}

	jpeg->plat_dev = pdev;
	jpeg->pdata = devm_kzalloc(&pdev->dev, sizeof(struct exynos_platform_jpeg), GFP_KERNEL);
	if (!jpeg->pdata) {
		dev_err(&pdev->dev, "no memory for state\n");
		return -ENOMEM;
	}

	if (pdata)
		memcpy(jpeg->pdata, pdata, sizeof(*pdata));
	else
		jpeg_parse_dt(dev->of_node, jpeg);

	/* Init lock and wait queue */
	mutex_init(&jpeg->lock);
	init_waitqueue_head(&jpeg->wq);

	/* Get memory resource and map SFR region. */
	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	jpeg->reg_base = devm_request_and_ioremap(&pdev->dev, res);
	if (jpeg->reg_base == NULL) {
		dev_err(&pdev->dev, "failed to claim register region\n");
		return -ENOENT;
	}

	/* Get IRQ resource and register IRQ handler. */
	jpeg->irq_no = platform_get_irq(pdev, 0);
	if (jpeg->irq_no < 0) {
		jpeg_err("failed to get jpeg memory region resource\n");
		return jpeg->irq_no;
	}

	/* Get memory resource and map SFR region. */
	ret = devm_request_irq(&pdev->dev, jpeg->irq_no, (void *)jpeg_irq, 0,
			pdev->name, jpeg);
	if (ret) {
		dev_err(&pdev->dev, "failed to install irq\n");
		return ret;
	}

	/* clock */
	jpeg->clk = devm_clk_get(&pdev->dev, "jpeg");
	if (IS_ERR(jpeg->clk)) {
		jpeg_err("failed to find jpeg clock source\n");
		return -ENOMEM;
	}

	ret = v4l2_device_register(&pdev->dev, &jpeg->v4l2_dev);
	if (ret) {
		v4l2_err(&jpeg->v4l2_dev, "Failed to register v4l2 device\n");
		goto err_v4l2;
	}

	/* encoder & decoder */
	vfd = video_device_alloc();
	if (!vfd) {
		v4l2_err(&jpeg->v4l2_dev, "Failed to allocate video device\n");
		ret = -ENOMEM;
		goto err_vd_alloc;
	}

	*vfd = jpeg_videodev;
	vfd->ioctl_ops = get_jpeg_v4l2_ioctl_ops();
	vfd->lock	= &jpeg->lock;
	vfd->vfl_dir    = VFL_DIR_M2M;
	ret = video_register_device(vfd, VFL_TYPE_GRABBER,
			EXYNOS_VIDEONODE_JPEG);

	if (ret) {
		v4l2_err(&jpeg->v4l2_dev,
			 "%s(): failed to register video device\n", __func__);
		video_device_release(vfd);
		goto err_vd_alloc;
	}
	v4l2_info(&jpeg->v4l2_dev,
		"JPEG driver is registered to /dev/video%d\n", vfd->num);

	jpeg->vfd = vfd;
	jpeg->m2m_dev = v4l2_m2m_init(&jpeg_m2m_ops);
	jpeg->m2m_ops = &jpeg_m2m_ops;
	if (IS_ERR(jpeg->m2m_dev)) {
		v4l2_err(&jpeg->v4l2_dev,
			"failed to initialize v4l2-m2m device\n");
		ret = PTR_ERR(jpeg->m2m_dev);
		goto err_m2m_init;
	}
	video_set_drvdata(vfd, jpeg);

	platform_set_drvdata(pdev, jpeg);

#if defined(CONFIG_VIDEOBUF2_ION)
	jpeg->vb2 = &jpeg_vb2_ion;
#endif
	jpeg->alloc_ctx = jpeg->vb2->init(jpeg);

	if (IS_ERR(jpeg->alloc_ctx)) {
		ret = PTR_ERR(jpeg->alloc_ctx);
		goto err_video_reg;
	}

	exynos_create_iovmm(&pdev->dev, 2, 2);
	jpeg->vb2->resume(jpeg->alloc_ctx);
#ifdef CONFIG_BUSFREQ_OPP
	/* To lock bus frequency in OPP mode */
	jpeg->bus_dev = dev_get("exynos-busfreq");
#endif

#ifdef CONFIG_PM_RUNTIME
	pm_runtime_enable(&pdev->dev);
#endif
	return 0;

err_video_reg:
	v4l2_m2m_release(jpeg->m2m_dev);
err_m2m_init:
	video_unregister_device(jpeg->vfd);
	video_device_release(jpeg->vfd);
err_vd_alloc:
	v4l2_m2m_release(jpeg->m2m_dev);
	v4l2_device_unregister(&jpeg->v4l2_dev);
err_v4l2:
	clk_put(jpeg->clk);
	return ret;

}

static int jpeg_remove(struct platform_device *pdev)
{
	struct jpeg_dev *jpeg = platform_get_drvdata(pdev);

	v4l2_m2m_release(jpeg->m2m_dev);
	video_unregister_device(jpeg->vfd);

	v4l2_device_unregister(&jpeg->v4l2_dev);

	jpeg->vb2->cleanup(jpeg->alloc_ctx);

	free_irq(jpeg->irq_no, pdev);
	mutex_destroy(&jpeg->lock);
	iounmap(jpeg->reg_base);

	clk_put(jpeg->clk);
#ifdef CONFIG_PM_RUNTIME
	pm_runtime_disable(&pdev->dev);
#endif
	jpeg->vb2->suspend(jpeg->alloc_ctx);

	kfree(jpeg);
	return 0;
}

int jpeg_suspend(struct device *dev)
{
	struct platform_device *pdev = to_platform_device(dev);
	struct jpeg_dev *jpeg = platform_get_drvdata(pdev);

	if (!IS_ERR(jpeg->clk)) {
		clk_disable(jpeg->clk);
		clk_unprepare(jpeg->clk);
	}

	return 0;
}

int jpeg_resume(struct device *dev)
{
	struct platform_device *pdev = to_platform_device(dev);
	struct jpeg_dev *jpeg = platform_get_drvdata(pdev);

	if (!IS_ERR(jpeg->clk)) {
		clk_prepare(jpeg->clk);
		clk_enable(jpeg->clk);
	}

	return 0;
}

static const struct dev_pm_ops jpeg_pm_ops = {
	.suspend	= jpeg_suspend,
	.resume		= jpeg_resume,
	.runtime_suspend = jpeg_suspend,
	.runtime_resume = jpeg_resume,
};

static struct platform_driver jpeg_driver = {
	.probe		= jpeg_probe,
	.remove		= jpeg_remove,
	.driver		= {
		.owner	= THIS_MODULE,
		.name	= JPEG_NAME,
		.pm = &jpeg_pm_ops,
		.of_match_table = exynos_jpeg_match,
	},
};

static int __init jpeg_init(void)
{
	int ret;

	printk(KERN_CRIT "Initialize JPEG driver\n");

	ret = platform_driver_register(&jpeg_driver);

	if (ret)
		jpeg_err("failed to register jpeg driver\n");

	return ret;
}

static void __exit jpeg_exit(void)
{
	platform_driver_unregister(&jpeg_driver);
}

module_init(jpeg_init);
module_exit(jpeg_exit);

MODULE_AUTHOR("taeho07.lee@samsung.com>");
MODULE_DESCRIPTION("JPEG v4.x H/W Device Driver");
MODULE_LICENSE("GPL");
