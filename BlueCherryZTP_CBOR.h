#ifndef ZTPCBOR_H
#define ZTPCBOR_H

#include <stdint.h>
#include <stddef.h>

// CBOR context structure
typedef struct {
  uint8_t *buffer;  // Pointer to the output buffer
  size_t capacity;  // Maximum size of the buffer
  size_t position;  // Current write position in the buffer
} ZTP_CBOR;

// Initialize the CBOR context
int ztp_cbor_init(ZTP_CBOR *cbor, uint8_t *buffer, size_t capacity);

// Encode CBOR types
int ztp_cbor_encode_bytes(ZTP_CBOR *cbor, const uint8_t *data, size_t length);
int ztp_cbor_encode_string(ZTP_CBOR *cbor, const char *str);
int ztp_cbor_encode_uint64(ZTP_CBOR *cbor, uint64_t value);
int ztp_cbor_encode_int(ZTP_CBOR *cbor, int value);
int ztp_cbor_start_array(ZTP_CBOR *cbor, size_t size);
int ztp_cbor_start_map(ZTP_CBOR *cbor, size_t size);

// Decode CBOR
int ztp_cbor_decode_device_id(const uint8_t *cbor_data, size_t cbor_size, char *decoded_str, size_t decoded_size);
int ztp_cbor_decode_certificate(const uint8_t *cbor_data, size_t cbor_size, unsigned char *decoded_data, size_t *decoded_len);

// Utility functions
size_t ztp_cbor_size(const ZTP_CBOR *cbor); // Returns the size of encoded data

#endif // ZTPCBOR_H
