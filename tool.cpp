#include <cerrno>
#include <iostream>
#include <stdio.h>
#include <string.h>
// #include <json/json.h>
#include <modbus/modbus.h>

int main(int argc, char *argv[]) {

  int len, rc;

  modbus_t *mb;
  uint16_t tab_reg[64];

  if (argc < 3 || argc > 4) {
    std::cerr << "ERROR, usage : " << argv[0]
              << " <mode R/W> <pnu> <value to write> " << std::endl;
    exit(EXIT_FAILURE);
  }
  mb = modbus_new_rtu("/dev/ttyUSB0", 19200, 'E', 8, 1);
  if (modbus_connect(mb) == -1) {
    std::cerr << "Connection failed:" << modbus_strerror(errno) << std::endl;
    modbus_free(mb);
    exit(EXIT_FAILURE);
  }
  modbus_set_slave(mb, 1);
  modbus_rtu_set_serial_mode(mb, MODBUS_RTU_RS485);
  int pnu = atoi(argv[2]);
  if (!strcmp(argv[1], "r") || !strcmp(argv[1], "R")) {
    rc = modbus_read_registers(mb, pnu - 1, 1, tab_reg);
    if (rc == -1) {
      std::cerr << "error " << modbus_strerror(errno) << std::endl;
      return -1;
    }
    for (int i = 0; i < rc; i++) {
      printf("pnu[%d] reg[%d]=%d (0x%X)\n", pnu, i, tab_reg[i], tab_reg[i]);
      std::cout << "pnu[" << pnu << "] reg[" << i << "]: " << tab_reg[i]
                << std::endl;
    }

  } else if (!strcmp(argv[1], "w") || !strcmp(argv[1], "W")) {
    tab_reg[0] = (uint16_t)atoi(argv[3]);
    rc = modbus_write_registers(mb, pnu - 1, 1, tab_reg);
    if (rc == -1) {
      std::cerr << "error " << modbus_strerror(errno) << std::endl;
      return -1;
    }
  } else {
    exit(EXIT_FAILURE);
  }

  modbus_close(mb);
  modbus_free(mb);
  return 0;
}