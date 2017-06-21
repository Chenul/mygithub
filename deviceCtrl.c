/*
 * deviceCtrl.c
 *
 *  Created on: 2017Äê3ÔÂ17ÈÕ
 *      Author: fox hsu
 *      reference: https://lwn.net/Articles/247126/
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <getopt.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/time.h>
#include <sys/mman.h>
#include <sys/ioctl.h>
#include <asm/types.h>
#include <linux/videodev2.h>


#define DBG(x...) printf(x)
#define INFO(x...) printf(x)



void showControlTypes(enum v4l2_ctrl_type t)
{
	char *typeName;
	switch (t)
	{
	case V4L2_CTRL_TYPE_INTEGER:
		typeName = "V4L2_CTRL_TYPE_INTEGER";
		break;
	case V4L2_CTRL_TYPE_BOOLEAN:
		typeName = "V4L2_CTRL_TYPE_BOOLEAN";
		break;
	case V4L2_CTRL_TYPE_MENU:
		typeName = "V4L2_CTRL_TYPE_MENU";
		break;
	case V4L2_CTRL_TYPE_BUTTON:
		typeName = "V4L2_CTRL_TYPE_BUTTON";
		break;
	case V4L2_CTRL_TYPE_INTEGER64:
		typeName = "V4L2_CTRL_TYPE_INTEGER64";
		break;
	case V4L2_CTRL_TYPE_CTRL_CLASS:
		typeName = "V4L2_CTRL_TYPE_CTRL_CLASS";
		break;
	default:
		typeName = "V4L2_CTRL_TYPE UNKNOW";
	}



	INFO("control type: %s\n", typeName);
}


void queryDeviceControlCapability(int fd)
{
	int ret;
	int i;
	struct v4l2_queryctrl qc;
	struct v4l2_querymenu qm;


	INFO(">>>>VIDIOC_QUERYCTRL\n");
	for(i = V4L2_CID_BASE;i <= V4L2_CID_LASTP1;i++)
	{
		qc.id = i;
		ret = ioctl(fd, VIDIOC_QUERYCTRL, &qc);
		if(ret<0)
		{
			INFO("VIDIOC_QUERYCTRL ioctrl failed %d\n", i);
			continue;
		}

		INFO("idx: %d\n", qc.id);
		INFO("ctrl type: %d\n", qc.type);
		showControlTypes(qc.type);
		INFO("ctrl name: %s\n", qc.name);
		INFO("ctrl min: %d\n", qc.minimum);
		INFO("ctrl max: %d\n", qc.maximum);
		INFO("ctrl step: %d\n", qc.step);
		INFO("ctrl default:: %d\n", qc.default_value);
		INFO("============\n");

		if(qc.type == V4L2_CTRL_TYPE_MENU)
		{
			int m;
			qm.id = qc.id;
			for (m = qc.minimum; m <= qc.maximum; m++)
			{
				qm.index = m;
				ret = ioctl(fd, VIDIOC_QUERYMENU, &qm);
				if (ret < 0) {
					INFO("VIDIOC_QUERYMENU ioctrl failed index %d\n", m);
					continue;
				}
				INFO("     >>>> id: %d\n", qm.id);
				INFO("     >>>> index: %d\n", qm.index);
				INFO("     >>>> name: %s\n", qm.name);
				INFO("     >>>>>>>> \n");
			}
		}//end if menu type querying
	}//end for


	INFO("camera control query completed\n");
	INFO("============\n");


}
