#pragma once

#include <stddef.h>
#include <stdint.h>
#include <hal/sha_types.h>

#ifdef __cplusplus
extern "C" {
#endif

int esp_sha(esp_sha_type type, const unsigned char *input, size_t length,
            unsigned char *output);

#ifdef __cplusplus
}
#endif
