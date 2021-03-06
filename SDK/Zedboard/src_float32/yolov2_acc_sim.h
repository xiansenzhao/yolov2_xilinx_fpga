
#include "xyolo2_fpga_hw.h"

double what_time_is_it_now()
{
    struct timeval time;
    if (gettimeofday(&time,NULL)){
        return 0;
    }
    return (double)time.tv_sec + (double)time.tv_usec * .000001;
}

///////////////////////////////////////////////////////////////////////20181229 anti-reorg start => KxKxTmxTn
#define MAX(x,y) ((x)>(y)?(x):(y))
#define MIN(x,y) ((x)<(y)?(x):(y))
#define S 2
#define K 3

#define Tn 4
#define Tm 23
#define Tr 26
#define Tc 32
#define OnChipIB_Width  ((Tc-1)*S+K)
#define OnChipIB_Height ((Tr-1)*S+K)
#define MAX_BETA_LENGTH (1024)

#define WEIGHT_BASE (0x10000000) //203767168 = C253D80
#define BETA_BASE (0x1C25F000) //43044 = 0xA824
#define MEM_BASE (0x1C26A000) //416*416*32*4+208*208*32*4=173,056+43,264= 216,320*128 = 0x1A6_8000

int YOLO2_FPGA(unsigned int In_Address, unsigned int Out_Address, unsigned int Weight_offset, unsigned int Beta_offset,
							   int IFM_num, int OFM_num, int Ksize, int Kstride,
							   int Input_w, int Input_h, int Output_w, int Output_h, int Padding, bool IsNL, bool IsBN,
							   int TM, int TN, int TR, int TC, 
							   int OFM_num_bound, int mLoopsxTM, int mLoops_a1xTM, int LayerType)
{
	unsigned int ap_idle;
	unsigned int ap_done;

	unsigned long int PhysicalAddress = YOLO2_BASEADDR;
	int map_len = 0x100;
	int fd = open("/dev/mem", O_RDWR);

	unsigned char *xbase_address;
	xbase_address = (unsigned char *)mmap(NULL, map_len, PROT_READ | PROT_WRITE, MAP_SHARED, fd, (off_t)PhysicalAddress);
	if(xbase_address == MAP_FAILED)
	{
		perror("1:Init Mapping memory for absolute memory access failed.\n");
		return -1;
	}

	while(1)
	{
		ap_idle = ((ReadReg(xbase_address, XYOLO2_FPGA_CTRL_BUS_ADDR_AP_CTRL) >> 2) && 0x1);
		if(ap_idle)
			break;
	}

	WriteReg(xbase_address, XYOLO2_FPGA_CTRL_BUS_ADDR_INPUT_V_DATA,  In_Address);
	WriteReg(xbase_address, XYOLO2_FPGA_CTRL_BUS_ADDR_OUTPUT_V_DATA, Out_Address);
	WriteReg(xbase_address, XYOLO2_FPGA_CTRL_BUS_ADDR_WEIGHT_V_DATA,   WEIGHT_BASE + Weight_offset*4);
	WriteReg(xbase_address, XYOLO2_FPGA_CTRL_BUS_ADDR_BETA_V_DATA,     BETA_BASE + Beta_offset*4);

	WriteReg(xbase_address, XYOLO2_FPGA_CTRL_BUS_ADDR_IFM_NUM_DATA, IFM_num);
	WriteReg(xbase_address, XYOLO2_FPGA_CTRL_BUS_ADDR_OFM_NUM_DATA, OFM_num);
	WriteReg(xbase_address, XYOLO2_FPGA_CTRL_BUS_ADDR_KSIZE_DATA, Ksize);
	WriteReg(xbase_address, XYOLO2_FPGA_CTRL_BUS_ADDR_KSTRIDE_DATA, Kstride);
	WriteReg(xbase_address, XYOLO2_FPGA_CTRL_BUS_ADDR_INPUT_W_DATA, Input_w);
	WriteReg(xbase_address, XYOLO2_FPGA_CTRL_BUS_ADDR_INPUT_H_DATA, Input_h);
	WriteReg(xbase_address, XYOLO2_FPGA_CTRL_BUS_ADDR_OUTPUT_W_DATA, Output_w);
	WriteReg(xbase_address, XYOLO2_FPGA_CTRL_BUS_ADDR_OUTPUT_H_DATA, Output_h);
	WriteReg(xbase_address, XYOLO2_FPGA_CTRL_BUS_ADDR_PADDING_DATA, Padding);
	WriteReg(xbase_address, XYOLO2_FPGA_CTRL_BUS_ADDR_ISNL_DATA, IsNL);
	WriteReg(xbase_address, XYOLO2_FPGA_CTRL_BUS_ADDR_ISBN_DATA, IsBN);
	WriteReg(xbase_address, XYOLO2_FPGA_CTRL_BUS_ADDR_TM_DATA, TM);
	WriteReg(xbase_address, XYOLO2_FPGA_CTRL_BUS_ADDR_TN_DATA, TN);
	WriteReg(xbase_address, XYOLO2_FPGA_CTRL_BUS_ADDR_TR_DATA, TR);
	WriteReg(xbase_address, XYOLO2_FPGA_CTRL_BUS_ADDR_TC_DATA, TC);
	WriteReg(xbase_address, XYOLO2_FPGA_CTRL_BUS_ADDR_OFM_NUM_BOUND_DATA, OFM_num_bound);
	WriteReg(xbase_address, XYOLO2_FPGA_CTRL_BUS_ADDR_MLOOPSXTM_DATA, mLoopsxTM);
	WriteReg(xbase_address, XYOLO2_FPGA_CTRL_BUS_ADDR_MLOOPS_A1XTM_DATA, mLoops_a1xTM);
	WriteReg(xbase_address, XYOLO2_FPGA_CTRL_BUS_ADDR_LAYERTYPE_DATA, LayerType);

//	double time1,time2;
//	time1 = what_time_is_it_now();
	WriteReg(xbase_address, XYOLO2_FPGA_CTRL_BUS_ADDR_GIE, 0x0);
	WriteReg(xbase_address, XYOLO2_FPGA_CTRL_BUS_ADDR_AP_CTRL, 0x1);//Start
	while(1)
	{
		ap_done = ((ReadReg(xbase_address, XYOLO2_FPGA_CTRL_BUS_ADDR_AP_CTRL) >> 1) && 0x1);
		if(ap_done)
			break;
	}
//	time2 = what_time_is_it_now();
//	printf("START TO DONE in %f seconds.\n",time2 - time1);

	munmap((void *)xbase_address, map_len);
	close(fd);

	return 0;
}


#define MEM_LEN (416*416*32*4+208*208*32*4)
void generate_iofm_offset(unsigned int in_ptr[32], unsigned int out_ptr[32], unsigned int Memory_base, network *net)
{
#define ROUTE16_LEN (26*32*512*4)
#define CONV27_LEN (13*16*256*4)
#define CONV24_LEN (13*16*1024*4)

	unsigned int Memory_top = Memory_base;
	unsigned int Memory_bottom = Memory_top + MEM_LEN;
	int x;
	for(x=0;x<18;x++)
	{
		int out_w = net->layers[x].out_w;
		int out_w_align_256b = (out_w >> 3) << 3;
		if(out_w & 0x7)
			out_w_align_256b += 8;

		if(x%2==0)
		{
			in_ptr[x] = Memory_top;
			out_ptr[x] = Memory_bottom - net->layers[x].out_c *  net->layers[x].out_h * out_w_align_256b*4;
		}
		else
		{
			in_ptr[x] = out_ptr[x-1];
			out_ptr[x] = Memory_top;
		}
	}

	for(x=18;x<25;x++)
	{
		int out_w = net->layers[x].out_w;
		int out_w_align_256b = (out_w >> 3) << 3;
		if(out_w & 0x7)
			out_w_align_256b += 8;

		if(x%2==0)
		{
			in_ptr[x] = Memory_top;
			out_ptr[x] = Memory_bottom - ROUTE16_LEN - net->layers[x].out_c *  net->layers[x].out_h * out_w_align_256b*4;
		}else
		{
			in_ptr[x] = out_ptr[x-1];
			out_ptr[x] = Memory_top;
		}
	}

	in_ptr[26] = Memory_bottom - ROUTE16_LEN;
	out_ptr[26] = Memory_top;

	in_ptr[27] = Memory_top;
	out_ptr[27] = Memory_bottom - ROUTE16_LEN - CONV24_LEN - CONV27_LEN;

	in_ptr[29] = out_ptr[27];
	out_ptr[29] = Memory_top;

	in_ptr[30] = Memory_top;
	out_ptr[30] = Memory_bottom - (net->layers[30].outputs + 3*13*425)*4;

	if(out_ptr[30]%(4*1024)!=0)
	{
		out_ptr[30] = (out_ptr[30]/(4*1024)-1)*(4*1024);
	}

	in_ptr[31] = out_ptr[30];
}

void reorg_cpu(float *x, int w, int h, int c, int stride, float *out)
{
    int i,j,k;
    int out_c = c/(stride*stride);

	for(k = 0; k < c; ++k){
	    for(j = 0; j < h; ++j){
		for(i = 0; i < w; ++i){
		    int in_index  = i + w*(j + h*k);
		    int c2 = k % out_c;
		    int offset = k / out_c;
		    int w2 = i*stride + offset % stride;
		    int h2 = j*stride + offset / stride;
		    int out_index = w2 + w*stride*(h2 + h*stride*c2);
		    out[in_index] = x[out_index];
		}
	    }
	}
}

void yolov2_hls_ps(network *net, float *input)
{
	int weight_offset[32] = {864, 18432, 73728, 8192, 73728,
		294912, 32768, 294912, 1179648, 131072, 1179648, 131072,
		1179648, 4718592, 524288, 4718592, 524288, 4718592, 9437184,
		9437184, 32768, 11796480, 435200, 0, 0, 0, 0, 0, 0, 0, 0, 0};
	int beta_offset[32] = {32, 64, 128, 64, 128, 256, 128, 256, 512, 256, 512, 256, 512, 1024,
		512, 1024, 512, 1024, 1024, 1024, 64, 1024, 425, 0, 0, 0, 0, 0, 0, 0, 0, 0};

	double time1,time2;
	time1 = what_time_is_it_now();
	copy_file2mem("weights_reorg.bin", 203767168, WEIGHT_BASE);//->C253D80
	printf("yolov2_w copy ok\n");
	copy_file2mem("bias.bin", 43044, BETA_BASE);//->C268724 203812864 = C25F000
	printf("yolov2_b copy ok\n");
	time2 = what_time_is_it_now();
	printf("Predicted in %f seconds.\n",time2 - time1);

	float *region_buffer = (float *)calloc(13*16*425,sizeof(float));
	if(!region_buffer) file_error("region_buffer error \n");
	float *region_buffer2 = (float *)calloc(13*16*425,sizeof(float));
	if(!region_buffer2) file_error("region_buffer error \n");

//leave some memories for overflow, because the load_module will load extra pixels near boundary for padding
	unsigned int in_ptr[32];
	unsigned int out_ptr[32];
	generate_iofm_offset( in_ptr, out_ptr, MEM_BASE, net);

	copy_mem2dev((uint8_t *)input, 416*416*3*4, in_ptr[0]);
//	memcpy(in_ptr[0], input, 416*416*3*sizeof(float));//416x416x3 input_pic

    int i;
	int offset_index = 0;
	int woffset = 0;
	int boffset = 0;
	int TR,TC,TM,TN;
	int output_w,output_h;
	int mLoops;
	double sum_gop = 0.0;

	time1 = what_time_is_it_now();
    for(i = 0; i < net->n; ++i)
	{
        	layer l = net->layers[i];
		printf("Layer[%2d]: ",i);
		switch(l.type)
		{
			case CONVOLUTIONAL:
				printf("outputMemory:%8d;BN=%d;Activation=%d;conv  %5d %2d x%2d /%2d  %4d x%4d x%4d   ->  %4d x%4d x%4d  %5.3f BFLOPs\n",l.outputs,l.batch_normalize,l.activation, l.n, l.size, l.size, l.stride, l.w, l.h, l.c, l.out_w, l.out_h, l.out_c, (2.0 * l.n * l.size*l.size*l.c/l.groups * l.out_h*l.out_w)/1000000000.);
				sum_gop += (2.0 * l.n * l.size*l.size*l.c/l.groups * l.out_h*l.out_w)/1000000000.;
				output_w = (l.w - l.size + 2*l.pad)/l.stride + 1;
				output_h = (l.h - l.size + 2*l.pad)/l.stride + 1;

				TR = MIN(((OnChipIB_Height-l.size)/l.stride+1),Tr);//keep Kstride>=1
				TR = MIN(output_h,TR);
				TC = MIN(((OnChipIB_Width-l.size)/l.stride+1),Tc);
				TC = MIN(output_w,TC);
				TM = MIN(l.n,Tm);
				TN = MIN(l.c,Tn);
				mLoops = (int)ceil(((float)l.n)/TM);

				YOLO2_FPGA(in_ptr[i],out_ptr[i], woffset, boffset,
					l.c,l.n,l.size,
					l.stride,l.w,l.h,output_w, output_h, l.pad,l.activation==LEAKY?1:0,l.batch_normalize?1:0,
					TM,TN,TR,TC, (mLoops + 1)*TM, mLoops*TM, (mLoops + 1)*TM, 0);

				woffset += weight_offset[offset_index];
				boffset += beta_offset[offset_index];
				offset_index++;

				break;
			case MAXPOOL:
				printf("outputMemory:%8d;max          %d x %d / %d  %4d x%4d x%4d   ->  %4d x%4d x%4d\n",l.outputs, l.size, l.size, l.stride, l.w, l.h, l.c, l.out_w, l.out_h, l.out_c);
				//output_w = (l.w - l.size)/l.stride + 1 ;
				//output_h = (l.h - l.size)/l.stride + 1 ;
				output_w = l.out_h;
				output_h = l.out_w;

				TR = MIN(((OnChipIB_Height-l.size)/l.stride+1),Tr);//keep Kstride>=1
				TC = MIN(((OnChipIB_Width-l.size)/l.stride+1),Tc);
				TR = MIN(output_h,TR);
				TC = MIN(output_w,TC);
				TM = MIN(Tm,Tn);
				TM = MIN(l.c,TM);
				mLoops = (int)ceil(((float)l.c)/TM);

				YOLO2_FPGA(in_ptr[i],out_ptr[i],NULL,NULL,l.c,l.c,
					l.size,l.stride,l.w,l.h, output_w, output_h, l.pad,0,0,TM,0,TR,TC, (mLoops + 2)*TM, mLoops*TM, (mLoops + 1)*TM, 1);

				break;
			case REORG:
				printf("outputMemory:%8d;reorg              /%2d  %4d x%4d x%4d   ->  %4d x%4d x%4d\n",l.outputs,  l.stride, l.w, l.h, l.c, l.out_w, l.out_h, l.out_c);			
				output_w = 26;
				output_h = 32*13;

				copy_dev2mem((uint8_t *)region_buffer2, 26*32*64*4, in_ptr[i]);
				for(int k = 0; k<26*64; k++)
					memcpy((float *)(region_buffer + k*26), (float *)(region_buffer2 + k*32), 26*sizeof(float));
				reorg_cpu(region_buffer, output_w, output_h, 4, 2, region_buffer2);
				memset(region_buffer, 0,  13*16*256*sizeof(float));
				for(int k = 0; k<13*256; k++)
					memcpy((float *)(region_buffer + k*16), (float *)(region_buffer2 + k*13), 13*sizeof(float));
				copy_mem2dev((uint8_t *)region_buffer, 13*16*256*4, out_ptr[i]);
//				memcpy(out_ptr[i], tmp_ptr_f0, 13*16*256*sizeof(float));

				break;
			case ROUTE:
				printf("outputMemory:%8d;route ",l.outputs);
				int j;
				for(j = 0; j < l.n; ++j){
					printf(" %d", l.input_layers[j]);
				}
				printf("\n");
				break;
			case REGION:
				printf("outputMemory:%8d;Detection\n",l.outputs);
				copy_dev2mem((uint8_t *)region_buffer2, 13*16*425*4, in_ptr[i]);
				for(int k = 0; k<13*425; k++)
					for(int j = 0; j < 16; j++)
					{
						if(j < 13)
							region_buffer[k*13 + j] = region_buffer2[k*16 + j];
					}
//				copy_dev2mem((uint8_t *)region_buffer, 13*13*432*4, in_ptr[i]);
				forward_region_layer(l, region_buffer);
				break;
		}
    }
	printf("SUM_GOP=%g\n",sum_gop);
	time2 = what_time_is_it_now();
	printf("Inference in %f seconds.(+region)\n",time2 - time1);

	free(region_buffer);
	free(region_buffer2);

}
///////////////////////////////////////////////////////////////////////20181229 anti-reorg ok end n4m32
