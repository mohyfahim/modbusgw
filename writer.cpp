#include <cerrno>
#include <fstream>
#include <iostream>
#include <sstream>
#include <stdio.h>
#include <string.h>
#include <vector>
// #include <json/json.h>
#include <modbus/modbus.h>

std::vector<std::string> split(const std::string &s, char delim) {
  std::vector<std::string> result;
  std::stringstream ss(s);
  std::string item;

  while (getline(ss, item, delim)) {
    result.push_back(item);
  }

  return result;
}

int main(int argc, char *argv[]) {

  int len, rc;
  std::ifstream config_file;
  modbus_t *mb;
  uint16_t tab_reg[64];

  // if (argc < 3 || argc > 4) {
  //   std::cerr << "ERROR, usage : " << argv[0]
  //             << " <mode R/W> <pnu> <value to write> " << std::endl;
  //   exit(EXIT_FAILURE);
  // }

  config_file.open("/etc/config/modbus");
  if (!config_file.is_open()) {
    std::cout << "unable to open config file" << std::endl;
    exit(EXIT_FAILURE);
  }

  mb = modbus_new_rtu("/dev/ttyUSB0", 19200, 'E', 8, 1);
  if (modbus_connect(mb) == -1) {
    std::cerr << "Connection failed:" << modbus_strerror(errno) << std::endl;
    modbus_free(mb);
    config_file.close();
    exit(EXIT_FAILURE);
  }
  modbus_set_slave(mb, 1);
  modbus_rtu_set_serial_mode(mb, MODBUS_RTU_RS485);

  std::string line;
  while (getline(config_file, line)) {
    if (line.find("option") != std::string::npos) {
      std::vector<std::string> tokens = split(line, '_');
      if (tokens.size() == 3) {
        std::vector<std::string> pnus = split(tokens[2], ' ');
        int pnu = std::stoi(pnus[0]);
        uint16_t value =
            (uint16_t)std::stoi(pnus[1].substr(1, pnus[1].size() - 2));
        std::cout << pnu << ":" << value << std::endl;
        rc = modbus_write_register(mb, pnu - 1, value);
        if (rc == -1) {
          std::cerr << "error " << modbus_strerror(errno) << std::endl;
          config_file.close();
          modbus_close(mb);
          modbus_free(mb);
          return -1;
        }
      }
    }
  }

  // int pnu = atoi(argv[2]);

  // if (!strcmp(argv[1], "r") || !strcmp(argv[1], "R")) {
  //   rc = modbus_read_registers(mb, pnu - 1, 1, tab_reg);
  //   if (rc == -1) {
  //     std::cerr << "error " << modbus_strerror(errno) << std::endl;
  //     return -1;
  //   }
  //   for (int i = 0; i < rc; i++) {
  //     printf("pnu[%d] reg[%d]=%d (0x%X)\n", pnu, i, tab_reg[i], tab_reg[i]);
  //     std::cout << "pnu[" << pnu << "] reg[" << i << "]: " << tab_reg[i]
  //               << std::endl;
  //   }

  // } else if (!strcmp(argv[1], "w") || !strcmp(argv[1], "W")) {
  //   tab_reg[0] = (uint16_t)atoi(argv[3]);
  //   rc = modbus_write_registers(mb, pnu - 1, 1, tab_reg);
  //   if (rc == -1) {
  //     std::cerr << "error " << modbus_strerror(errno) << std::endl;
  //     return -1;
  //   }
  // } else {
  //   exit(EXIT_FAILURE);
  // }

  config_file.close();
  modbus_close(mb);
  modbus_free(mb);
  return EXIT_SUCCESS;
}