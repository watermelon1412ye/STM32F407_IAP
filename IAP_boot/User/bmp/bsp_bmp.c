/**
  ******************************************************************************
  * @file    bsp_key.c
  * @author  fire
  * @version V1.0
  * @date    2013-xx-xx
  * @brief   bmp????????????bmp???????????
  ******************************************************************************
  * @attention
  *
  * ?????:??? F103-????? STM32 ?????? 
  * ???    :http://www.firebbs.cn
  * ???    :https://fire-stm32.taobao.com
  *
  ******************************************************************************
  */ 
  
#include "FATFS/ff.h"
#include <stdio.h>
#include "./lcd/bsp_nt35510_lcd.h"
#include "./bmp/bsp_bmp.h"

#define RGB24TORGB16(R,G,B) ((unsigned short int)((((R)>>3)<<11) | (((G)>>2)<<5)	| ((B)>>3)))

/* 24 位 BMP 每行缓冲：横屏 800 宽需 800*3=2400 字节 */
#define BMP_ROW_MAX_BYTES  2400
BYTE pColorData[BMP_ROW_MAX_BYTES];
#define ROW16_MAX_WIDTH  800
BYTE pColorData16[ROW16_MAX_WIDTH * 2];
FIL bmpfsrc, bmpfdst; 
FRESULT bmpres;

/* close BMP debug print to avoid serial garbled (encoding) */
#define BMP_DEBUG_PRINTF(FORMAT,...)  ((void)0)	 

/* ???BMP?????????????????? */
static void showBmpHead(BITMAPFILEHEADER* pBmpHead)
{
    BMP_DEBUG_PRINTF("???????:\r\n");
    BMP_DEBUG_PRINTF("???????:%ld\r\n",(*pBmpHead).bfSize);
    BMP_DEBUG_PRINTF("??????:%d\r\n",(*pBmpHead).bfReserved1);
    BMP_DEBUG_PRINTF("??????:%d\r\n",(*pBmpHead).bfReserved2);
    BMP_DEBUG_PRINTF("???????????????????:%ld\r\n",(*pBmpHead).bfOffBits);
		BMP_DEBUG_PRINTF("\r\n");	
}

/* ???BMP?????????????????? */
static void showBmpInforHead(tagBITMAPINFOHEADER* pBmpInforHead)
{
    BMP_DEBUG_PRINTF("???????:\r\n");
    BMP_DEBUG_PRINTF("????????:%ld\r\n",(*pBmpInforHead).biSize);
    BMP_DEBUG_PRINTF("?????:%ld\r\n",(*pBmpInforHead).biWidth);
    BMP_DEBUG_PRINTF("?????:%ld\r\n",(*pBmpInforHead).biHeight);
    BMP_DEBUG_PRINTF("biPlanes?????:%d\r\n",(*pBmpInforHead).biPlanes);
    BMP_DEBUG_PRINTF("biBitCount???????????:%d\r\n",(*pBmpInforHead).biBitCount);
    BMP_DEBUG_PRINTF("??????:%ld\r\n",(*pBmpInforHead).biCompression);
    BMP_DEBUG_PRINTF("biSizeImage???????????????????:%ld\r\n",(*pBmpInforHead).biSizeImage);
    BMP_DEBUG_PRINTF("X????????:%ld\r\n",(*pBmpInforHead).biXPelsPerMeter);
    BMP_DEBUG_PRINTF("Y????????:%ld\r\n",(*pBmpInforHead).biYPelsPerMeter);
    BMP_DEBUG_PRINTF("?????????:%ld\r\n",(*pBmpInforHead).biClrUsed);
    BMP_DEBUG_PRINTF("????????:%ld\r\n",(*pBmpInforHead).biClrImportant);
		BMP_DEBUG_PRINTF("\r\n");
}





/**
 * @brief  ????ILI9341????BMP??
 * @param  x ?????????1????????????X???? 
 * @param  y ?????????1????????????Y???? 
 * @param  pic_name ??BMP?????????
 * @retval ??
 */
void LCD_Show_BMP ( uint16_t x, uint16_t y, char * pic_name )
{
	int i, j, k;
	int width, height, l_width;
	int top_down;

	BYTE red,green,blue;
	BITMAPFILEHEADER bitHead;
	BITMAPINFOHEADER bitInfoHead;
	WORD fileType;

	unsigned int read_num;

	printf("LCD_Show_BMP: %s\r\n", pic_name);

	/* 打开 BMP 文件 */
	bmpres = f_open( &bmpfsrc , (char *)pic_name, FA_OPEN_EXISTING | FA_READ);	
	if(bmpres == FR_OK)
	{
		/* 读取文件头信息  两个字节*/         
		f_read(&bmpfsrc,&fileType,sizeof(WORD),&read_num);     

		/* 判断是不是bmp文件 "BM"*/
		if(fileType != 0x4d42)
		{
			BMP_DEBUG_PRINTF("这不是一个 .bmp 文件!\r\n");
			f_close(&bmpfsrc);
			return;
		}       

		/* 读取BMP文件头信息*/
		f_read(&bmpfsrc,&bitHead,sizeof(tagBITMAPFILEHEADER),&read_num);        
		showBmpHead(&bitHead);

		/* 读取位图信息头信息 */
		f_read(&bmpfsrc,&bitInfoHead,sizeof(BITMAPINFOHEADER),&read_num);        
		showBmpInforHead(&bitInfoHead);
	}    
	else
	{
		printf("BMP open fail code=%d (4=no path 5=no file)\r\n", (int)bmpres);
		return;
	}    

	width = bitInfoHead.biWidth;
	height = bitInfoHead.biHeight;
	/* 记录是否为自上而下（biHeight<0），再取绝对值 */
	top_down = ( height < 0 );
	if ( height < 0 )
		height = -height;

	printf("BMP info: width=%ld, height=%ld (abs=%d), bitCount=%d, top_down=%d\r\n",
	       (long)bitInfoHead.biWidth,
	       (long)bitInfoHead.biHeight,
	       height,
	       bitInfoHead.biBitCount,
	       top_down);

	/* 计算位图的实际宽度并确保它为32的倍数	*/
	l_width = WIDTHBYTES(width * bitInfoHead.biBitCount);	

	/* 一行最多支持 800 像素（横屏 800*3=2400 字节）*/
	if ( l_width > BMP_ROW_MAX_BYTES )
	{
		printf("BMP too wide (max %d px), skip\r\n", BMP_ROW_MAX_BYTES / 3);
		f_close(&bmpfsrc);
		return;
	}
	
	/* 开一个图片大小的窗口（左上角 x,y 开始，宽 width，高 height）*/
	NT35510_OpenWindow(x, y, (uint16_t)width, (uint16_t)height);
	NT35510_StartPixelWrite();

	/* 仅支持 24bit 真彩 BMP；biHeight 正=自下而上，负=自上而下（已取绝对值） */
	if( bitInfoHead.biBitCount >= 24 )
	{
		for ( i = 0; i < height; i ++ )
		{
			/* 自下而上：文件里最后一行是屏幕第一行；自上而下：文件第一行即屏幕第一行 */
			long line_offset = top_down
				? ( (long)i * l_width )                    /* 从文件头顺序读 */
				: ( (long)( height - 1 - i ) * l_width );  /* 从文件末尾往前读 */
			f_lseek ( &bmpfsrc, bitHead.bfOffBits + line_offset );	
			
			f_read ( &bmpfsrc, pColorData, l_width, & read_num );				

			for(j=0; j<width; j++)
			{
				k = j*3;
				red   = pColorData[k+2];
				green = pColorData[k+1];
				blue  = pColorData[k];
				NT35510_WritePixelData ( RGB24TORGB16 ( red, green, blue ) );
			}            			
		}        		
	}    	
	else
	{
		printf("BMP not 24bit (biBitCount=%ld), skip\r\n", (long)bitInfoHead.biBitCount);
		f_close(&bmpfsrc);
		return;
	}
	
	f_close(&bmpfsrc);  
}




/**
 * @brief  ????ILI9341????BMP??
 * @param  x ?????????????X???? 
 * @param  y ?????????????Y???? 
 * @param  Width ?????????
 * @param  Height ???????? 
 * @retval ??
  *   ????????????????
  *     @arg 0 :??????
  *     @arg -1 :??????
 */
int Screen_Shot( uint16_t x, uint16_t y, uint16_t Width, uint16_t Height, char * filename)
{
	/* bmp  ???? 54????? */
	unsigned char header[54] =
	{
		0x42, 0x4d, 0, 0, 0, 0, 
		0, 0, 0, 0, 54, 0, 
		0, 0, 40,0, 0, 0, 
		0, 0, 0, 0, 0, 0, 
		0, 0, 1, 0, 24, 0, 
		0, 0, 0, 0, 0, 0, 
		0, 0, 0, 0, 0, 
		0, 0, 0, 0, 0,
		0, 0, 0, 0, 0, 
		0, 0, 0
	};
	
	int i;
	int j;
	long file_size;     
	long width;
	long height;
	unsigned char r,g,b;	
	unsigned int mybw;
	unsigned int read_data;
	char kk[4]={0,0,0,0};
	
	uint8_t ucAlign;//
	
	
	/* ??*?? +???????? + ?????? */
	file_size = (long)Width * (long)Height * 3 + Height*(Width%4) + 54;		

	/* ??????? 4????? */
	header[2] = (unsigned char)(file_size &0x000000ff);
	header[3] = (file_size >> 8) & 0x000000ff;
	header[4] = (file_size >> 16) & 0x000000ff;
	header[5] = (file_size >> 24) & 0x000000ff;
	
	/* ????? 4????? */
	width=Width;	
	header[18] = width & 0x000000ff;
	header[19] = (width >> 8) &0x000000ff;
	header[20] = (width >> 16) &0x000000ff;
	header[21] = (width >> 24) &0x000000ff;
	
	/* ????? 4????? */
	height = Height;
	header[22] = height &0x000000ff;
	header[23] = (height >> 8) &0x000000ff;
	header[24] = (height >> 16) &0x000000ff;
	header[25] = (height >> 24) &0x000000ff;
		
	/* ????????? */
	bmpres = f_open( &bmpfsrc , (char*)filename, FA_CREATE_ALWAYS | FA_WRITE );
	
	/* ???????????????????????? */
	f_close(&bmpfsrc);
		
	bmpres = f_open( &bmpfsrc , (char*)filename,  FA_OPEN_EXISTING | FA_WRITE);

	if ( bmpres == FR_OK )
	{    
		/* ??????????bmp????????????????? */
		bmpres = f_write(&bmpfsrc, header,sizeof(unsigned char)*54, &mybw);		
			
		ucAlign = Width % 4;
		
		for(i=0; i<Height; i++)					
		{
			for(j=0; j<Width; j++)  
			{					
				read_data = NT35510_GetPointPixel ( x + j, y + Height - 1 - i );					
				
				r =  GETR_FROM_RGB16(read_data);
				g =  GETG_FROM_RGB16(read_data);
				b =  GETB_FROM_RGB16(read_data);

				bmpres = f_write(&bmpfsrc, &b,sizeof(unsigned char), &mybw);
				bmpres = f_write(&bmpfsrc, &g,sizeof(unsigned char), &mybw);
				bmpres = f_write(&bmpfsrc, &r,sizeof(unsigned char), &mybw);
			}
				
			if( ucAlign )				/* ???????4?????? */
				bmpres = f_write ( & bmpfsrc, kk, sizeof(unsigned char) * ( ucAlign ), & mybw );

		}/* ??????? */

		f_close(&bmpfsrc); 
		
		return 0;
		
	}	
	else/* ??????? */
		return -1;

}


/*
 * @brief  显示 RGB565 原始数据文件(.bin)
 * @param  x,y    显示起点坐标
 * @param  Width  图像宽度（像素）
 * @param  Height 图像高度（像素）
 * @param  filename SD 卡上的文件名，例如 "0:/xxx.bin"
 * @retval 0 成功，-1 失败
 */
int LCD_Show_RAW565( uint16_t x, uint16_t y, uint16_t Width, uint16_t Height, const char * filename )
{
	FIL f;
	UINT br;
	uint32_t row_bytes;
	uint16_t row, col;

	/* 边界和宽度检查：宽度不能超过行缓冲限制 */
	if ( Width == 0 || Height == 0 )
		return -1;
	if ( (uint32_t)Width > ROW16_MAX_WIDTH )
	{
		printf("RAW565 width too big (max %d)\r\n", ROW16_MAX_WIDTH);
		return -1;
	}
	if ( x + Width > LCD_X_LENGTH || y + Height > LCD_Y_LENGTH )
	{
		printf("RAW565 out of LCD range\r\n");
		return -1;
	}

	row_bytes = (uint32_t)Width * 2; /* RGB565 每像素 2 字节 */

	if ( f_open(&f, filename, FA_READ) != FR_OK )
	{
		printf("open RAW565 fail: %s\r\n", filename);
		return -1;
	}

	for ( row = 0; row < Height; row++ )
	{
		/* 读取一行像素到 pColorData16 缓冲区 */
		if ( f_read(&f, pColorData16, row_bytes, &br) != FR_OK || br != row_bytes )
		{
			printf("read RAW565 row %d fail\r\n", row);
			f_close(&f);
			return -1;
		}

		/* 设置当前行的窗口，并开始写像素 */
		NT35510_OpenWindow( x, y + row, Width, 1 );
		NT35510_StartPixelWrite();

		for ( col = 0; col < Width; col++ )
		{
			/* Python 写入时是小端序：低字节在前，高字节在后 */
			uint16_t lo = pColorData16[col * 2];
			uint16_t hi = pColorData16[col * 2 + 1];
			uint16_t px = (hi << 8) | lo;
			NT35510_WritePixelData( px );
		}
	}

	f_close(&f);
	return 0;
}

