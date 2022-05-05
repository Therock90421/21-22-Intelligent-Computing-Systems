#include "inference.h"
#include "cnrt.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "stdlib.h"
#include <sys/time.h>
#include <time.h>

namespace StyleTransfer{

typedef unsigned short half;


Inference :: Inference(std::string offline_model){
    offline_model_ = offline_model;
}

void Inference :: run(DataTransfer* DataT){
    cnrtInit(0);
    // load model
    cnrtModel_t model;
    cnrtLoadModel(&model, offline_model_.c_str());

    // set current device
    cnrtDev_t dev;
    cnrtGetDeviceHandle(&dev, 0);
    cnrtSetCurrentDevice(dev);
    
    float* input_data = reinterpret_cast<float*>(malloc(256*256*3*sizeof(float)));
    float* output_data = reinterpret_cast<float*>(malloc(256*256*3*sizeof(float)));
    int t = 256*256;
    for(int i=0;i<t;i++)
        for(int j=0;j<3;j++)
            input_data[i*3+j] = DataT->input_data[t*j+i];      

    int number = 0;
    cnrtGetFunctionNumber(model, &number);


    // load extract function
    cnrtFunction_t function;
    if (CNRT_RET_SUCCESS != cnrtCreateFunction(&function)) {
      printf("cnrtCreateFunction Failed!\n");
      exit(-1);
    }
    
    if (CNRT_RET_SUCCESS != cnrtExtractFunction(&function, model, "subnet0")) {
      printf("cnrtExtractFunction Failed!\n");
      exit(-1);
    }
    

    int inputNum, outputNum;
    int64_t *inputSizeS, *outputSizeS;
    cnrtGetInputDataSize(&inputSizeS, &inputNum, function);
    cnrtGetOutputDataSize(&outputSizeS, &outputNum, function);  // prepare data on cpu


    DataT->output_data = reinterpret_cast<float*>(malloc(256 * 256 * 3 * sizeof(float)));
    half* input_half = (half*)malloc(256 * 256 * 3 * sizeof(half));
    half* output_half = (half*)malloc(256 * 256 * 3 * sizeof(half));
    for (int i=0;i<256*256*3;i++)
        cnrtConvertFloatToHalf(input_half+i,input_data[i]);
    for (int i=0;i<256*256*3;i++)
        cnrtConvertFloatToHalf(output_half+i,DataT->output_data[i]);

  
  

    // allocate I/O data memory on MLU
    void *mlu_input, *mlu_output;

    // prepare input buffer
    if (CNRT_RET_SUCCESS != cnrtMalloc(&(mlu_input), inputSizeS[0])) {
      printf("cnrtMalloc Failed!\n");
      exit(-1);
    }
    if (CNRT_RET_SUCCESS != cnrtMalloc(&(mlu_output), outputSizeS[0])) {
      printf("cnrtMalloc output Failed!\n");
      exit(-1);
    }
    if (CNRT_RET_SUCCESS != cnrtMemcpy(mlu_input, input_half, 256 * 256 * 3 * sizeof(half), CNRT_MEM_TRANS_DIR_HOST2DEV)) {
      printf("cnrtMemcpy Failed!\n");
      exit(-1);
    }
    

    // setup runtime ctx
    cnrtRuntimeContext_t ctx;
    cnrtCreateRuntimeContext(&ctx, function, NULL);

    // bind device
    cnrtSetRuntimeContextDeviceId(ctx, 0);
    cnrtInitRuntimeContext(ctx, NULL);
    
    void *param[2];
    param[0] = mlu_input;
    param[1] = mlu_output;
    // compute offline
    cnrtQueue_t queue;
    cnrtRuntimeContextCreateQueue(ctx, &queue);
    cnrtInvokeRuntimeContext(ctx, (void**)param, queue, nullptr);
    cnrtSyncQueue(queue);

    
    if (CNRT_RET_SUCCESS != cnrtMemcpy(output_half, mlu_output, 256 * 256 * 3 * sizeof(half), CNRT_MEM_TRANS_DIR_DEV2HOST)) {
      printf("cnrtMemcpy output Failed!\n");
      exit(-1);
    }
    for (int i=0;i<256*256*3;i++)
        cnrtConvertHalfToFloat(output_data+i,output_half[i]);


    for(int i=0;i<t;i++)
        for(int j=0;j<3;j++)
            DataT->output_data[t*j+i] = output_data[i*3+j];

    // free memory spac
    if (CNRT_RET_SUCCESS != cnrtFree(mlu_input)) {
      printf("cnrtFree Failed!\n");
      exit(-1);
    }
    if (CNRT_RET_SUCCESS != cnrtFree(mlu_output)) {
      printf("cnrtFree output Failed!\n");
      exit(-1);
    }

    if (CNRT_RET_SUCCESS != cnrtDestroyQueue(queue)) {
      printf("cnrtDestroyQueue Failed!\n");
      exit(-1);
    }

    cnrtDestroy();
    //free(param);
    free(input_half);
    free(output_half);
}

} // namespace StyleTransfer