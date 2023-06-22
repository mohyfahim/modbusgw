#include <errno.h>
#include <fstream>
#include <iostream>
#include <json/json.h>
#include <modbus/modbus.h>

#define CONFIG_FILE_PATH "conf.json"
#define REGISTERS_FILE_PATH "registers.json"

#define MBGW_LOAD_JSON_FROM_FILE(file_path, root)                              \
  do {                                                                         \
    std::ifstream ifs;                                                         \
    Json::CharReaderBuilder builder;                                           \
    JSONCPP_STRING errs;                                                       \
    ifs.open(file_path);                                                       \
    if (!Json::parseFromStream(builder, ifs, &root, &errs)) {                  \
      std::cout << "errors: " << errs << std::endl;                            \
      return EXIT_FAILURE;                                                     \
    }                                                                          \
    ifs.close();                                                               \
  } while (0)

typedef struct {
  std::string port;
  char parity;
  int baud;
  int stop_bit;
  int data_bit;
} mbgw_serial_options_t;

std::string mbgw_get_string_or_default(Json::Value j, std::string key,
                                       std::string def) {
  std::string val = j[key].asString();
  if (val.length()) {
    return val;
  } else {
    return def;
  }
}

int mbgw_get_int_or_default(Json::Value j, std::string key, int def) {
  Json::Int val = j[key].asInt();
  if (val != NULL)
    return val;
  else
    return def;
}

char mbgw_get_char_or_default(Json::Value j, std::string key, char def) {
  const char *val = j[key].asCString();
  if (val != NULL) {
    return val[0];
  } else {
    return def;
  }
}

int main(void) {
  mbgw_serial_options_t serial_options;
  Json::Value root;

  MBGW_LOAD_JSON_FROM_FILE(CONFIG_FILE_PATH, root);

  Json::Value connection = root["connection"];
  if (connection) {
    serial_options.baud = mbgw_get_int_or_default(connection, "baud", 19200);
    serial_options.data_bit =
        mbgw_get_int_or_default(connection, "data_bit", 8);
    serial_options.parity = mbgw_get_char_or_default(connection, "parity", 'E');
    serial_options.port =
        mbgw_get_string_or_default(connection, "port", "/dev/ttyUSB0");
    serial_options.stop_bit =
        mbgw_get_int_or_default(connection, "stop_bit", 1);
  }
  root.clear();

  std::cout << serial_options.baud << ",";
  std::cout << serial_options.data_bit << ",";
  std::cout << serial_options.parity << ",";
  std::cout << serial_options.port << ",";
  std::cout << serial_options.stop_bit << std::endl;

  MBGW_LOAD_JSON_FROM_FILE(REGISTERS_FILE_PATH, root);
  Json::Value registers = root["data"];

  modbus_t *mb;
  uint16_t tab_reg[64];

  mb = modbus_new_rtu(serial_options.port.c_str(), serial_options.baud,
                      serial_options.parity, serial_options.data_bit,
                      serial_options.stop_bit);
  if (modbus_connect(mb) == -1) {
    fprintf(stderr, "Connection failed: %s\n", modbus_strerror(errno));
    modbus_free(mb);
    return -1;
  }
  modbus_set_slave(mb, 1);
  modbus_rtu_set_serial_mode(mb, MODBUS_RTU_RS485);

  for (int i = 0; i < registers.size(); i++) {
    Json::Value reg = registers[i];
    int pnu = reg["PNU"].asInt();
    int rc = modbus_read_registers(mb, pnu - 1, 1, tab_reg);
    if (rc == -1) {
      fprintf(stderr, "error %s\n", modbus_strerror(errno));
      return -1;
    }
    for (int i = 0; i < rc; i++) {
      printf("pnu[%d] reg[%d]=%d (0x%X)\n", pnu, i, tab_reg[i], tab_reg[i]);
    }
  }

  modbus_close(mb);
  modbus_free(mb);
  return 0;
}