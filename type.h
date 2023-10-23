#pragma once

#include <stdint.h>

typedef struct {
  unsigned int pnu;
  uint16_t value;
} mbgw_sock_data_t __attribute__((packed));
