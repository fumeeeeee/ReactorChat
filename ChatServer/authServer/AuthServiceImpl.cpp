#include <openssl/evp.h>
#include <openssl/pem.h>
#include <openssl/err.h>
#include <vector>
#include <string>
#include <iostream>
#include "AuthServiceImpl.hpp"

// uint8_t 即 unsigned char
std::vector<uint8_t> AuthServiceImpl::rsaDecrypt(const std::string &encryptedData, const std::string &privateKeyPath)
{
    // 读取私钥文件到EVP_PKEY,利用私钥创建上下文ctx,利用ctx设置填充方式为OAEP,计算解密缓冲区大小,并执行解密,最后释放资源

    std::vector<uint8_t> decrypted;

    // 1. 读私钥文件
    FILE *fp = fopen(privateKeyPath.c_str(), "r");
    if (!fp)
    {
        std::cerr << "打开私钥文件失败\n";
        return decrypted;
    }

    EVP_PKEY *pkey = PEM_read_PrivateKey(fp, nullptr, nullptr, nullptr);
    fclose(fp);
    if (!pkey)
    {
        std::cerr << "加载私钥失败: " << ERR_error_string(ERR_get_error(), nullptr) << "\n";
        return decrypted;
    }

    // 2. 创建解密上下文
    EVP_PKEY_CTX *ctx = EVP_PKEY_CTX_new(pkey, nullptr);
    if (!ctx)
    {
        std::cerr << "创建 EVP_PKEY_CTX 失败\n";
        EVP_PKEY_free(pkey);
        return decrypted;
    }

    if (EVP_PKEY_decrypt_init(ctx) <= 0)
    {
        std::cerr << "EVP_PKEY_decrypt_init 失败\n";
        EVP_PKEY_free(pkey);
        EVP_PKEY_CTX_free(ctx);
        return decrypted;
    }

    // 3. 设置填充方式为 OAEP（与加密时保持一致）
    if (EVP_PKEY_CTX_set_rsa_padding(ctx, RSA_PKCS1_OAEP_PADDING) <= 0)
    {
        std::cerr << "设置 RSA OAEP 填充失败\n";
        EVP_PKEY_free(pkey);
        EVP_PKEY_CTX_free(ctx);
        return decrypted;
    }

    // 4. 计算解密缓冲区大小
    size_t outlen = 0;
    if (EVP_PKEY_decrypt(ctx, nullptr, &outlen,
                         reinterpret_cast<const unsigned char *>(encryptedData.data()),
                         encryptedData.size()) <= 0)
    {
        std::cerr << "计算解密长度失败\n";
        EVP_PKEY_free(pkey);
        EVP_PKEY_CTX_free(ctx);
        return decrypted;
    }

    decrypted.resize(outlen);

    // 5. 执行解密
    if (EVP_PKEY_decrypt(ctx, decrypted.data(), &outlen,
                         reinterpret_cast<const unsigned char *>(encryptedData.data()),
                         encryptedData.size()) <= 0)
    {
        std::cerr << "解密失败: " << ERR_error_string(ERR_get_error(), nullptr) << "\n";
        EVP_PKEY_free(pkey);
        EVP_PKEY_CTX_free(ctx);
        decrypted.clear();
        return decrypted;
    }

    decrypted.resize(outlen);

    // 6. 释放资源
    EVP_PKEY_free(pkey);
    EVP_PKEY_CTX_free(ctx);

    return decrypted;
}
