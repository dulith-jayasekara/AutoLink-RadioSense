#include <hwcrypto/sha.h>
#include <mbedtls/sha1.h>

extern "C" int esp_sha(esp_sha_type type, const unsigned char *input,
                        size_t length, unsigned char *output)
{
    if (type != SHA1 || input == nullptr || output == nullptr)
    {
        return -1;
    }

    mbedtls_sha1(input, length, output);
    return 0;
}
