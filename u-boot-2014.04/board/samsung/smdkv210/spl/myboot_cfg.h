#ifndef __MYBOOT_CFG_H
#define __MYBOOT_CFG_H


#define BL1_NF_ADDR			0
#define BL1_APP_SIZE	(16 << 10)

#define BL2_NF_ADDR			BL1_APP_SIZE
#define BL2_APP_MAX_SIZE	((256 << 10)-BL1_APP_SIZE) //256KB-16KB

#define KERNEL_NF_ADDR		0x260000

#define BL2_RAM_ADDR		0x20000000

typedef void (*BL2_APP)(void);


#endif //__MYBOOT_CFG_H


