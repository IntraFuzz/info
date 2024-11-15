#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>

// 定义 ms_encl_SetupCPUEmFloatArrays_t 结构
typedef struct {
    size_t ms_arraysize;
} ms_encl_SetupCPUEmFloatArrays_t;

// 模拟 SGX_CAST 宏
#define SGX_CAST(type, item) ((type)(item))

// 模拟 CHECK_REF_POINTER 函数（不严格检查指针有效性）
#define CHECK_REF_POINTER(ptr, size) \
    if (!(ptr)) { \
        printf("Invalid pointer detected.\n"); \
        return -1; \
    }

// 模拟 encl_SetupCPUEmFloatArrays 函数
void encl_SetupCPUEmFloatArrays(size_t arraysize) {
    printf("Array size is: %zu\n", arraysize);
}

// SGX 封装函数
int sgx_encl_SetupCPUEmFloatArrays(void* pms) {
    // 检查指针
    CHECK_REF_POINTER(pms, sizeof(ms_encl_SetupCPUEmFloatArrays_t));

    // 如果通过检查，继续执行
    ms_encl_SetupCPUEmFloatArrays_t* ms = SGX_CAST(ms_encl_SetupCPUEmFloatArrays_t*, pms);

    // 调用目标函数
    encl_SetupCPUEmFloatArrays(ms->ms_arraysize);
    return 0;
}

int main(int argc, char* argv[]) {
    if (argc != 2) {
        printf("Usage: %s <input>\n", argv[0]);
        return -1;
    }

    // 从命令行输入获取整数，并将其转换为指针
    uintptr_t input_value = strtoull(argv[1], NULL, 16); // 假设输入是 16 进制指针值
    void* test_pointer = (void*)input_value;

    // 调用 sgx_encl_SetupCPUEmFloatArrays 并传递测试指针
    int result = sgx_encl_SetupCPUEmFloatArrays(test_pointer);

    // 检查结果
    if (result != 0) {
        printf("Error: Null pointer dereference detected.\n");
    } else {
        printf("Function executed successfully.\n");
    }

    return 0;
}