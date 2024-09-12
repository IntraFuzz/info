//#include "sgx_tcrypto.h"
#include <time.h>
#include "stdlib.h"
#include <string.h>
#include <cstdint>
#include <unistd.h>
#include "getopt.h"
#include "stdio.h"
#include "Benchmark/cpuidh.h" //benchmark
#include "sgx_tcrypto.h"
#include "evp.h"
#include "sgx_memset_s.h"

#define  SGX_AESGCM_MAC_SIZE  16
#define  SGX_AESGCM_KEY_SIZE  16
#define  SGX_AESGCM_IV_SIZE   12
#define MAX_FILE_NAME_SIZE  512
#define EINVAL 22
#define EOVERFLOW 75

#define TMP_FILE_PREFIX "tempfile_"
//void exiter() __attribute__((destructor));

static void * (* const volatile __memset_vp)(void *, int, size_t)
    = (memset);

static uint8_t key[16] = { 0x0, 0x1, 0x2, 0x3, 0x4, 0x5, 0x6, 0x7, 0x8, 0x9, 0xa, 0xb, 0xc, 0xd, 0xe, 0xf };

int memset_s(void *s, size_t smax, int c, size_t n)
{
    int err = 0;

    if (s == NULL) {
        err = EINVAL;
        goto out;
    }

    if (n > smax) {
        err = EOVERFLOW;
        n = smax;
    }

    /* Calling through a volatile pointer should never be optimised away. */
    (*__memset_vp)(s, c, n);

    out:
    if (err == 0)
        return 0;
    else {
        printf("menset_s error!");
        /* XXX call runtime-constraint handler */
        return err;
    }
}

void printDebug(const char *buf)

{
    printf("ENCLAVE: %s\n", buf);
}

void printAppUsage()
{ 
    printf("Usage: sgxCryptoFile [OPTIONS] [FILE]\n\n");
    printf("Options:\n");
    printf(" -d\tdecryption mode enabled\n");
    printf(" -e\tencryption mode enabled\n");
    printf(" -i\tinput file\n");
    printf(" -o\toutput file\n");
    printf("Example (Encryption): sgxCryptoFile -e -i [INPUT_FILE] -o [OUTPUT_FILE]\n");
    printf("Example (Decryption): sgxCryptoFile -d -i [INPUT_FILE] -o [OUTPUT_FILE]\n\n");
}

static int aes_gcm_encrypt_internal(const uint8_t *p_key, uint32_t key_len, const uint8_t *p_src, uint32_t src_len,
                                             uint8_t *p_dst, const uint8_t *p_iv, uint32_t iv_len, const uint8_t *p_aad, uint32_t aad_len, uint8_t *p_out_mac)
{
    int len = 0;
    EVP_CIPHER_CTX *pState = NULL;
    const EVP_CIPHER *chpher = (key_len == SGX_AESGCM_KEY_SIZE ? EVP_aes_128_gcm() : EVP_aes_256_gcm());
    do
    {
        // Create and init ctx
        //
        if (!(pState = EVP_CIPHER_CTX_new()))
        {
            printf("error!\n");
            break;
        }

        // Initialise encrypt, key and IV
        //
        if (1 != EVP_EncryptInit_ex(pState, chpher, NULL, (unsigned char *)p_key, p_iv))
        {
            break;
        }

        // Provide AAD data if exist
        //
        if (NULL != p_aad)
        {
            if (1 != EVP_EncryptUpdate(pState, NULL, &len, p_aad, aad_len))
            {
                break;
            }
        }
        if (src_len > 0)
        {
            // Provide the message to be encrypted, and obtain the encrypted output.
            //
            if (1 != EVP_EncryptUpdate(pState, p_dst, &len, p_src, src_len))
            {
                break;
            }
        }
        // Finalise the encryption
        //
        if (1 != EVP_EncryptFinal_ex(pState, p_dst + len, &len))
        {
            break;
        }

        // Get tag
        //
        if (1 != EVP_CIPHER_CTX_ctrl(pState, EVP_CTRL_GCM_GET_TAG, SGX_AESGCM_MAC_SIZE, p_out_mac))
        {
            break;
        }
        
    } while (0);

    // Clean up and return
    //
    if (pState)
    {
        EVP_CIPHER_CTX_free(pState);
    }
    return 0;
}

static int aes_gcm_decrypt_internal(const uint8_t *p_key, uint32_t key_len, const uint8_t *p_src, uint32_t src_len,
                                             uint8_t *p_dst, const uint8_t *p_iv, uint32_t iv_len, const uint8_t *p_aad, uint32_t aad_len, const uint8_t *p_in_mac)
{
    uint8_t l_tag[SGX_AESGCM_MAC_SIZE];

    int len = 0;
    EVP_CIPHER_CTX *pState = NULL;
    const EVP_CIPHER *chpher = (key_len == SGX_AESGCM_KEY_SIZE ? EVP_aes_128_gcm() : EVP_aes_256_gcm());
    // Autenthication Tag returned by Decrypt to be compared with Tag created during seal
    //
    memset_s(&l_tag, SGX_AESGCM_MAC_SIZE, 0, SGX_AESGCM_MAC_SIZE);
    memcpy(l_tag, p_in_mac, SGX_AESGCM_MAC_SIZE);

    do
    {
        // Create and initialise the context
        //
        if (!(pState = EVP_CIPHER_CTX_new()))
        {
            printf("error!\n");
            break;
        }

        // Initialise decrypt, key and IV
        //
        if (!EVP_DecryptInit_ex(pState, chpher, NULL, (unsigned char *)p_key, p_iv))
        {
            break;
        }

        // Provide AAD data if exist
        //
        if (NULL != p_aad)
        {
            if (!EVP_DecryptUpdate(pState, NULL, &len, p_aad, aad_len))
            {
                break;
            }
        }

        // Decrypt message, obtain the plaintext output
        //
        if (!EVP_DecryptUpdate(pState, p_dst, &len, p_src, src_len))
        {
            break;
        }

        // Update expected tag value
        //
        if (!EVP_CIPHER_CTX_ctrl(pState, EVP_CTRL_GCM_SET_TAG, SGX_AESGCM_MAC_SIZE, l_tag))
        {
            break;
        }

        // Finalise the decryption. A positive return value indicates success,
        // anything else is a failure - the plaintext is not trustworthy.
        //
        if (EVP_DecryptFinal_ex(pState, p_dst + len, &len) <= 0)
        {
            printf("error!\n");
            break;
        }

    } while (0);

    // Clean up and return
    //
    if (pState != NULL)
    {
        EVP_CIPHER_CTX_free(pState);
    }
    memset_s(&l_tag, SGX_AESGCM_MAC_SIZE, 0, SGX_AESGCM_MAC_SIZE);
    return 0;
}

int sgx_rijndael128GCM_encrypt(const uint8_t *p_key, const uint8_t *p_src, uint32_t src_len,
                                        uint8_t *p_dst, const uint8_t *p_iv, uint32_t iv_len, const uint8_t *p_aad, uint32_t aad_len,
                                        uint8_t *p_out_mac)
{
    return aes_gcm_encrypt_internal((const uint8_t *)p_key, sizeof(uint8_t), p_src, src_len, p_dst, p_iv, iv_len, p_aad, aad_len, p_out_mac);
}

int sgx_rijndael128GCM_decrypt(const uint8_t *p_key, const uint8_t *p_src,
                                        uint32_t src_len, uint8_t *p_dst, const uint8_t *p_iv, uint32_t iv_len,
                                        const uint8_t *p_aad, uint32_t aad_len, const uint8_t *p_in_mac)
{
    return aes_gcm_decrypt_internal((const uint8_t *)p_key, sizeof(uint8_t), p_src, src_len, p_dst, p_iv, iv_len, p_aad, aad_len, p_in_mac);
}

void sgxDecryptFile(unsigned char *encMessageIn, size_t len, unsigned char *decMessageOut, size_t lenOut)
{
    uint8_t *encMessage = (uint8_t *)encMessageIn;

    // 打印传入参数的信息
    //printf("encMessageIn: %p, len: %zu, decMessageOut: %p, lenOut: %zu\n", encMessageIn, len, decMessageOut, lenOut);

    // 检查传入参数的有效性
    if (encMessageIn == NULL || decMessageOut == NULL) {
        fprintf(stderr, "Error: encMessageIn or decMessageOut is NULL.\n");
        return;
    }

    if (lenOut <= 0) {
        fprintf(stderr, "Error: lenOut is invalid (%zu).\n", lenOut);
        return;
    }

    uint8_t p_dst[lenOut];

    sgx_rijndael128GCM_decrypt(
        &key[0],
        encMessage + SGX_AESGCM_MAC_SIZE + SGX_AESGCM_IV_SIZE,
        lenOut,
        p_dst,
        encMessage + SGX_AESGCM_MAC_SIZE, SGX_AESGCM_IV_SIZE,
        NULL, 0,
        (uint8_t *)encMessage);

    //printf("prepar to do memcpy\n");

    // 调用 memcpy 前打印调试信息
    //printf("Before memcpy: decMessageOut: %p, p_dst: %p, lenOut: %zu\n", decMessageOut, p_dst, lenOut);

    memcpy(decMessageOut, p_dst, lenOut);

    // 打印 memcpy 后的调试信息
    //printf("memcpy completed successfully\n");
}

void sgxEncryptFile(unsigned char *decMessageIn, size_t len, unsigned char *encMessageOut, size_t lenOut)
{
	uint8_t *origMessage = (uint8_t *) decMessageIn;
	uint8_t p_dst[lenOut];
    //sgx_status_t ret;
    // printDebug("INIT ENCLAVE ENCRYPTION...");
	// Generate the IV (nonce)
	sgx_rijndael128GCM_encrypt(
		&key[0],
		origMessage, len, 
		p_dst + SGX_AESGCM_MAC_SIZE + SGX_AESGCM_IV_SIZE,
		p_dst + SGX_AESGCM_MAC_SIZE, SGX_AESGCM_IV_SIZE,
		NULL, 0,
		(uint8_t *) (p_dst));	

        memcpy(encMessageOut,p_dst,lenOut);
}


int encryptFile(const char* input, const char* output)
{
    FILE *ifp = NULL;
    FILE *ofp = NULL;

    if((ifp = fopen(input, "rb")) == NULL)
    {
        printf("[APP ENCRYPT] Input File %s not found!\n",input);
        return -1;
    }

    if((ofp = fopen(output, "wb")) == NULL)
    {
        printf("[APP ENCRYPT] Error while creating output File %s\n",output);
        fclose(ifp);
        return -1;
    }

    fseek(ifp, 0, SEEK_END);
    long fsize = ftell(ifp);
    fseek(ifp, 0, SEEK_SET);  //same as rewind(f);
    unsigned char *message = (unsigned char*)malloc(fsize + 1);
    size_t bytes_read = fread(message, fsize, 1, ifp);

    // Display the read content
    printf("[APP ENCRYPT] Read content from input file (%s):\n", input);

    size_t encMessageLen = (SGX_AESGCM_MAC_SIZE + SGX_AESGCM_IV_SIZE + fsize); 
    unsigned char *encMessage = (unsigned char *) malloc((encMessageLen+1)*sizeof(unsigned char));

    start_time();

    sgxEncryptFile(message, fsize, encMessage, encMessageLen);

    end_time();

    encMessage[encMessageLen] = '\0';

    fwrite(encMessage, encMessageLen, 1, ofp);

    free(encMessage);
    free(message);

    fclose(ifp);
    fclose(ofp);

    printf("[APP ENCRYPT] Encryption file (%s) created!\n",output);
    printf("[APP ENCRYPT] Final encryption time: %6.6f seconds.\n", secs);
    return 0;
}

int decryptFile(const char* input, const char* output)
{
    FILE *ifp = NULL;
    FILE *ofp = NULL;

    FILE *file = fopen(input, "r");
    if (file == NULL) {
        perror("打开文件时出错");
        return 88;
    }

    char buf[1024];
    size_t bytes_read = fread(buf, 1, sizeof(buf) - 1, file);  // 留出一个字节空间给null终止符
    if (bytes_read < 0) {
        perror("读取文件时出错");
        fclose(file);
        return 99;
    }

    buf[bytes_read] = '\0';  // 添加null终止符
    //printf("从 %s 读取的内容: %s\n", input, buf);

    if (fseek(file, 0, SEEK_SET) != 0) {
        perror("fseek(0, SEEK_SET) 出错");
        fclose(file);
        return 66;
    }
    
    if((ifp = fopen(input, "rb")) == NULL)
    {
        printf("[APP DECRYPT] Input File %s not found!\n",input);
        return -1;
    }

    if((ofp = fopen(output, "wb")) == NULL)
    {
        printf("[APP DECRYPT] Error while creating output File %s\n",output);
        fclose(ifp);
        return -1;
    }

    fseek(ifp, 0, SEEK_END);
    long fsize = ftell(ifp);
    fseek(ifp, 0, SEEK_SET);  //same as rewind(f);
    unsigned char *message = (unsigned char*)malloc(fsize + 1);
    size_t bytes_read1 = fread(message, 1, fsize, ifp);

    // Display the read content
    printf("[APP DECRYPT] Read content from input file (%s):\n", input);
    /*for (long i = 0; i < fsize; i++) {
        printf("%c", message[i]);
    }
    printf("\n");*/

    size_t decMessageLen = fsize - (SGX_AESGCM_MAC_SIZE + SGX_AESGCM_IV_SIZE);
    unsigned char *decMessage = (unsigned char *) malloc((decMessageLen+1)*sizeof(unsigned char));

    start_time();

    sgxDecryptFile(message, fsize, decMessage, decMessageLen);

    end_time();

    decMessage[decMessageLen] = '\0';

    fwrite(decMessage, decMessageLen, 1, ofp);

    free(decMessage);
    free(message);

    fclose(ifp);
    fclose(ofp);

    printf("[APP DECRYPT] Decryption file (%s) created!\n",output);
    printf("[APP DECRYPT] Final decryption time: %6.6f seconds.\n", secs);
    return 0;
}


// Function to save content to a temporary file named with PID
char *saveContentToTempFile(const char *content) {
    char tempFileName[MAX_FILE_NAME_SIZE];
    pid_t pid = getpid();
    sprintf(tempFileName, "%s%d", TMP_FILE_PREFIX, pid);

    FILE *fp = fopen(tempFileName, "w");
    if (fp == NULL) {
        perror("Failed to create temporary file");
        return NULL;
    }

    size_t len = strlen(content);
    if (fwrite(content, 1, len, fp) != len) {
        perror("Error writing content to temporary file");
        fclose(fp);
        return NULL;
    }

    fclose(fp);
    return strdup(tempFileName);  // Return the name of the created temporary file
}

// Function to delete a file
int deleteFile(const char *filename) {
    if (remove(filename) != 0) {
        perror("Error deleting file");
        return -1;
    }
    return 0;
}

/*int main(int argc, char *argv[])
{
    int option = 0;
    int mode = 0;
    char inFileName[MAX_FILE_NAME_SIZE];
    char outFileName[MAX_FILE_NAME_SIZE];
    char *tempFileName = NULL;
    
    // Specifying the expected options 
    while ((option = getopt(argc, argv,"edi:o:")) != -1) {
        switch (option) {
            case 'e' : 
                mode = 1; //Encryption enabled 
                break;
            case 'd' : 
                mode = 2; //Decryption enabled 
                break;
            case 'i' : 
                if(optarg == NULL) {
                    exit(EXIT_FAILURE);
                }
                strncpy(inFileName, optarg, MAX_FILE_NAME_SIZE); 
                break;
            case 'o' : 
                if(optarg == NULL){
                    exit(EXIT_FAILURE);
                }
                strncpy(outFileName, optarg, MAX_FILE_NAME_SIZE); 
                break;
            default:
                printAppUsage(); 
                exit(EXIT_FAILURE);
        }
    }
    
    //Check if it is invalid mode
    if (mode == 0)
    {
        printAppUsage();
        exit(EXIT_FAILURE);
    }

    char newFileName[MAX_FILE_NAME_SIZE]="after";
    
    tempFileName = saveContentToTempFile(inFileName);
    if (tempFileName == NULL) {
        fprintf(stderr, "Failed to save content to temporary file\n");
        exit(EXIT_FAILURE);
    }

    if(mode == 1) {
        //Encrypt a file
        //printf("Encrypt a file\n");
        encryptFile(tempFileName, outFileName);
        decryptFile(outFileName, newFileName);
    }
    else {
    //Decrypt a file
        printf("Decrypt a file\n");
        int res = decryptFile(tempFileName, outFileName);
        printf("res: %d in decrypt\n",res);
        encryptFile(outFileName, newFileName);
    }
    deleteFile(tempFileName);
    return 0;
}*/
int main(int argc, char *argv[])
{
    int option = 0;
    int mode = 0;
    char inFileName[MAX_FILE_NAME_SIZE];
    char outFileName[MAX_FILE_NAME_SIZE];
    char tempFileName[L_tmpnam];  // 临时文件名
    tmpnam(tempFileName);  // 生成一个临时文件名

    // Specifying the expected options 
    while ((option = getopt(argc, argv,"edi:o:")) != -1) {
        switch (option) {
            case 'e' : 
                mode = 1; //Encryption enabled 
                break;
            case 'd' : 
                mode = 2; //Decryption enabled 
                break;
            case 'i' : 
                if(optarg == NULL) {
                    exit(EXIT_FAILURE);
                }
                strncpy(inFileName, optarg, MAX_FILE_NAME_SIZE); 
                break;
            case 'o' : 
                if(optarg == NULL){
                    exit(EXIT_FAILURE);
                }
                strncpy(outFileName, optarg, MAX_FILE_NAME_SIZE); 
                break;
            default:
                printAppUsage(); 
                exit(EXIT_FAILURE);
        }
    }
    
    //Check if it is invalid mode
    if (mode == 0)
    {
        printAppUsage();
        exit(EXIT_FAILURE);
    }

    // 将 inFileName 的内容写入临时文件 tempFileName
    FILE *inputFile = fopen(inFileName, "rb");
    if (!inputFile) {
        fprintf(stderr, "Failed to open input file: %s\n", inFileName);
        exit(EXIT_FAILURE);
    }

    FILE *tempFile = fopen(tempFileName, "wb");
    if (!tempFile) {
        fprintf(stderr, "Failed to create temporary file: %s\n", tempFileName);
        fclose(inputFile);
        exit(EXIT_FAILURE);
    }

    // 将输入文件的内容拷贝到临时文件
    char buffer[1024];
    size_t bytes;
    while ((bytes = fread(buffer, 1, sizeof(buffer), inputFile)) > 0) {
        fwrite(buffer, 1, bytes, tempFile);
    }

    fclose(inputFile);
    fclose(tempFile);

    char newFileName[MAX_FILE_NAME_SIZE] = "after";
    
    // 调用 encryptFile 和 decryptFile，保持原有逻辑
    if (mode == 1) {
        encryptFile(tempFileName, outFileName);  // 加密临时文件并输出到 outFileName
        decryptFile(outFileName, newFileName);  // 解密 outFileName 并输出到 newFileName
    } else {
        printf("Decrypt a file\n");
        int res = decryptFile(tempFileName, outFileName);  // 解密临时文件并输出到 outFileName
        printf("res: %d in decrypt\n", res);
        encryptFile(outFileName, newFileName);  // 再次加密 outFileName 并输出到 newFileName
    }

    // 删除临时文件
    if (remove(tempFileName) != 0) {
        perror("Failed to delete temporary file");
    }

    return 0;
}
