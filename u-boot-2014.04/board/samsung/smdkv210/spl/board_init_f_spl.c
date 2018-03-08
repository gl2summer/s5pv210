#include <stdbool.h>
#include "uart.h"
#include "clock_c.h"
#include "led.h"
#include "tim4.h"
#include "nand.h"
#include "xmodem.h"
#include "myboot_cfg.h"

unsigned char nand_buff[PAGE_SIZE];
unsigned int buff_offset;
unsigned int nand_address;

unsigned char uart_buf[128];
unsigned char buf_len;




void program_tonand(void)
{
	if(!(nand_address % BLOCK_SIZE))
	{
		nand_erase(nand_address);
	}
	nand_write_page_8bit(nand_buff, nand_address);
	nand_address += PAGE_SIZE;
}

void try_program(unsigned char *buffer, unsigned int buffer_len, bool bfinish)
{
	if(bfinish)
	{
		program_tonand();
		return ;
	}
	
	int i;
	for(i=0; i<buffer_len; i++)
	{
		nand_buff[buff_offset] = buffer[i];
		buff_offset++;
		if(buff_offset >= PAGE_SIZE)
		{
			program_tonand();
			buff_offset = 0;
		}
	}
}


void uart_buff(char c)
{
	if(buf_len < sizeof(uart_buf))
	{
		uart_buf[buf_len] = c;
		buf_len++;
	}
}

void xmCallback(XMODEM_EVT evt, unsigned int data, unsigned char *buffer, unsigned int buffer_len)
{
	if(evt==XMEVT_ENTRY)
	{
		buff_offset = 0;
	}
	else if(evt==XMEVT_SOH)
	{
		try_program(buffer, buffer_len, false);
	}
	else if(evt==XMEVT_EOT)
	{
		try_program(buffer, buffer_len, true);
	}
}

bool xmPutData(unsigned char* buffer, unsigned int buffer_len)
{
	put_chars((char*)buffer, buffer_len);
	return true;
}

bool xmGetData(unsigned char *buffer, unsigned int *buffer_len)
{
	unsigned char i;
	for(i=0; i<buf_len; i++)
	{
		if(buffer)
			buffer[i] = uart_buf[i];
	}
	if(buffer_len)
		*buffer_len = buf_len;
	buf_len = 0;
	
	return true;
}


//base_addr: flash存储的起始地址
void doXmodemDownloadInit(u32 base_addr)
{
	xmInit(xmPutData, xmGetData, xmCallback);
	
	uart_printf("please send your file: \n");
	
	xmStart();
	nand_address = base_addr;
}

/*return true when downloading*/
bool doXmodemDownloading(unsigned long timestamp)
{
	return (xModemFSM(timestamp)!=XMSTA_STOP);
}

void copybl2ToRamAndRun(void)
{
	int size_copy, addr_copy, addr_ram;
	
	size_copy = (BL2_APP_MAX_SIZE/PAGE_SIZE + ((BL2_APP_MAX_SIZE%PAGE_SIZE)>0?1:0))*PAGE_SIZE;
	addr_copy = BL2_NF_ADDR;
	addr_ram  = BL2_RAM_ADDR;
	
	uart_printf("size_copy=%d,BL2_nf_addr=0x%X,BL2_ram_addr=0x%X\r\n", size_copy, addr_copy, addr_ram);
	while(size_copy > 0)
	{
		NF8_ReadPage_8ECC(addr_copy, (unsigned char *)addr_ram);
		
		size_copy -= PAGE_SIZE;
		addr_copy += PAGE_SIZE;
		addr_ram  += PAGE_SIZE;
	}
	((BL2_APP)((u32 *)BL2_RAM_ADDR))();
}


#define TEST_BLOCK_ADDR (1*BLOCK_SIZE)
void bzero(u8 *s, int size)
{
	int i = 0;
	for (; i < size; i++)
		s[i] = 0;
}
/* 测试：使用Ecc校验写入一页数据，然后将某个位反转后不使用Ecc再此写入，然后使用Ecc校验读出数据 */
void testNandEcc(void)
{
	u32 i,ret;
	u8 buf[PAGE_SIZE];
	u8 oob[OOB_SIZE];


	/* 1.擦除第0块 */
	nand_erase(TEST_BLOCK_ADDR);
	
	for (i = 0; i < PAGE_SIZE; i++)
	{
		buf[i] = i&0xff;
	}
	
	uart_printf("begin write data..\r\n");
	/* 
	** 2.写入1页数据到0地址(正确数据，全部为0xAA)
	** 同时将计算出的ECC校验码保存到oob中
	*/
	nand_write_page_8bit(buf, TEST_BLOCK_ADDR);
	nand_read_oob(oob, TEST_BLOCK_ADDR, OOB_SIZE);
	
	/* 3.擦除第0块 */
	nand_erase(TEST_BLOCK_ADDR);
	
	/*
	** 4.将前8个数据的第0位取反(0xAB)，不使用应硬件ECC，将1页数据写入第0页
	** 同时将上面记录下的ECC写入spare区
	*/
	for (i = 0; i < 8; i++)
		buf[i] ^= 0x01;

	nand_write_page(buf, TEST_BLOCK_ADDR, oob);
	
	bzero(buf, PAGE_SIZE);

	/* 5.使用自定义函数或三星提供的8位硬件ECC校验拷贝函数读取一页数据 */
	//ret = NF8_ReadPage_8ECC(TEST_BLOCK_ADDR, buf);
	ret = nand_read_page_8bit(buf, TEST_BLOCK_ADDR);
	
	uart_printf("ret = %d, addr=0x%X\r\n", ret, TEST_BLOCK_ADDR);
	/* 打印第一个数据 */
	for (i = 0; i < 10; i++)
		uart_printf("%X ", buf[i]);
	uart_printf("\r\n");
}
void checkAppInNand(void)
{
	u32 i,ret;
	u8 buf[PAGE_SIZE];
	u32 addr;
	
	uart_printf("SIZE = %d\r\n", BL1_APP_SIZE+BL2_APP_MAX_SIZE);
	for(addr=BL1_NF_ADDR; addr<BL1_NF_ADDR+BL1_APP_SIZE+BL2_APP_MAX_SIZE; addr+=PAGE_SIZE)
	{
		ret = NF8_ReadPage_8ECC(addr, buf);
		//ret = nand_read_page_8bit(buf, addr);
		
		uart_printf("ret = %d, addr = 0x%X\r\n", ret, addr);
		for (i = 0; i < PAGE_SIZE; i++)
		{
			if(!(i%16))uart_printf("\r\n");
			uart_printf("%X ", buf[i]);
		}
		uart_printf("\r\n");
	}
}



typedef enum{MENU_ENTRY=0, MENU_AUTO_BOOT, MENU_CMD, MENU_DOWNLOAD, MENU_BOOT}MENU_STATE;
#define AUTOBOOT_TIMEOUTS 5

void print_menu(void)
{
	uart_printf("\r\n\r\n---------------- boot menu, please input your command: ----------------\r\n");
	uart_printf(">> D: download myboot(SPL+uboot) into nandflash(uart2, xModem, 0)\r\n");
	uart_printf(">> K: download kernel into nandflash(uart2, xModem, 0x260000)\r\n");
	uart_printf(">> B: start uboot\r\n");
	uart_printf(">> N: test nand with ecc\r\n");
	uart_printf(">> C: readback nand(BL1+BL2)\r\n");
	uart_printf("\r\n");
}

void menu(unsigned long timestamp, bool recved, char c)
{
	static MENU_STATE menu_state = MENU_ENTRY;
	static unsigned long timestamp_auto_boot = 0;
	static int auto_boot_timeout = AUTOBOOT_TIMEOUTS;
	
	switch(menu_state)
	{
		case MENU_AUTO_BOOT:
			if(recved)
			{
				print_menu();
				menu_state = MENU_CMD;
				break;
			}
		
			if(auto_boot_timeout <= 0)
			{
				menu_state = MENU_BOOT;
				break;
			}
			
			if(timestamp_auto_boot==0)
			{
				timestamp_auto_boot = timestamp;
				uart_printf("\rAuto boot in %d second...", auto_boot_timeout);
			}
			if(timestamp - timestamp_auto_boot >= 1000)
			{
				auto_boot_timeout--;
				timestamp_auto_boot = timestamp;
				uart_printf("\rAuto boot in %d second...", auto_boot_timeout);
			}
			break;
			
		case MENU_CMD:
			if(!recved)
				break;
				
			switch(c)
			{
				case 'd':
				case 'D':
					doXmodemDownloadInit(BL1_NF_ADDR);
					menu_state = MENU_DOWNLOAD;
				break;
				
				case 'K':
				case 'k':
					doXmodemDownloadInit(KERNEL_NF_ADDR);
					menu_state = MENU_DOWNLOAD;
				break;
				
				case 'b':
				case 'B':
					menu_state = MENU_BOOT;
				break;
				
				case 'n':
				case 'N':
					testNandEcc();
					break;
				case 'c':
				case 'C':
					checkAppInNand();
					break;
				default:
				break;
			}
			break;
		
		case MENU_DOWNLOAD:
			if(recved)
			{
				uart_buff(c);
			}
			if(!doXmodemDownloading(timestamp))
			{
				uart_printf("program finished!\n");
				print_menu();
				menu_state = MENU_CMD;
			}
			break;
		
		case MENU_BOOT:
			led_off();
			uart_printf("\r\nBooting...\r\n");
			copybl2ToRamAndRun();
			while(1);
			break;
		
		case MENU_ENTRY:
		default:
			uart_printf("Going to auto boot, press any KEY to cancel!\r\n");
			menu_state = MENU_AUTO_BOOT;
			timestamp_auto_boot = 0;
			auto_boot_timeout = AUTOBOOT_TIMEOUTS;
			break;
	}
}


#define MEM_TEST_START 0x20000000

void board_init_f_spl(void)
{
	uart_init();
	nand_init();
	tim4_init();

	print_clock();
	led_on();

	u8 id[5];
	nand_read_id(id);
	uart_printf("nand id: %X%X%X%X%X\r\n", id[0],id[1],id[2],id[3],id[4]);
	
#if 0
	int i;
	volatile unsigned int *p = (volatile unsigned int *)(MEM_TEST_START);
	for(i=0; i<1024; i++)
	{
		*p = i;
		uart_printf("(%d), %X = %d\r\n", i, (unsigned int)p, *p);
		p++;
	}
#endif
	//delay(100);

	while(1)
	{
		char c;
		bool recved = try_get_char(&c);
		unsigned long timestamp = tim4_poll();
		menu(timestamp, recved, c);
	}
}
