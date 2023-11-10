#include "loguru.hpp"
#include <condition_variable>
#include <csignal>
#include <cstring>
#include <errno.h>
#include <fstream>
#include <iostream>
#include <json/json.h>
#include <modbus/modbus.h>
#include <mutex>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <sys/un.h>
#include <uci.h>
#include <unistd.h>

#include "type.h"
int test_counter = 0;
#define CONFIG_FILE_PATH "/etc/modbusgw/conf.json"
#define REGISTERS_FILE_PATH "/etc/modbusgw/registers.json"
#define MBGW_DB_ROOT "/var/modbusgw/"
#define MBGW_DB_DATA MBGW_DB_ROOT "data/"
#define MBGW_DB_REQUEST MBGW_DB_ROOT "request.txt"
#define SOCK_PATH "/var/run/mgd_unix_sock.server"
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
modbus_t *mb;

pthread_t server_thread;
int server_sock = -1;

std::vector<std::string> split(const std::string &s, char delim) {
  std::vector<std::string> result;
  std::stringstream ss(s);
  std::string item;

  while (getline(ss, item, delim)) {
    result.push_back(item);
  }

  return result;
}

class Semaphore {
  std::mutex mutex_;
  std::condition_variable condition_;
  unsigned long count_ = 0; // Initialized as locked.

public:
  void release() {
    std::lock_guard<decltype(mutex_)> lock(mutex_);
    ++count_;
    condition_.notify_one();
  }

  void acquire() {
    std::unique_lock<decltype(mutex_)> lock(mutex_);
    while (!count_) // Handle spurious wake-ups.
      condition_.wait(lock);
    --count_;
  }

  bool try_acquire() {
    std::lock_guard<decltype(mutex_)> lock(mutex_);
    if (count_) {
      --count_;
      return true;
    }
    return false;
  }
};

Semaphore lock;
pthread_t request_to_read;
volatile bool request_to_read_flag = true;

std::string mbgw_get_string_or_default(Json::Value j, std::string key,
                                       std::string def) {
  std::string val = j[key].asString();
  if (val.length()) {
    return val;
  } else {
    return def;
  }
}

void signal_handler(int sig_code) {

  std::cout << "signal recevied " << sig_code << std::endl;
  if (sig_code == SIGTERM || sig_code == SIGINT) {
    lock.acquire(); // wait to finish the last job
  }
  if (server_sock != -1) {
    close(server_sock);
    pthread_cancel(server_thread);
  }
  if (mb != NULL) {
    modbus_close(mb);
    modbus_free(mb);
  }
  exit(-1);
}

int mbgw_get_int_or_default(Json::Value j, std::string key, int def) {
  Json::Int val = j[key].asInt();
  return val;
}

char mbgw_get_char_or_default(Json::Value j, std::string key, char def) {
  const char *val = j[key].asCString();
  if (val != NULL) {
    return val[0];
  } else {
    return def;
  }
}

typedef struct {
  int pnu;
  uint16_t value;
  bool error;
} mbgw_mb_data_t;

mbgw_mb_data_t extract_data_from_option(std::string line) {
  mbgw_mb_data_t output = {};
  std::vector<std::string> tokens = split(line, '_');
  std::cout << "tokens: " << tokens.size() << std::endl;
  if (tokens.size() == 3) {
    output.pnu = std::stoi(tokens[2]);
  } else {
    output.error = true;
  }
  return output;
}

int update_device_from_config_file() {
  std::cout << "start" << test_counter << std::endl;
  struct uci_context *ctx = uci_alloc_context();
  struct uci_package *pkg;

  if (!ctx) {
    fprintf(stderr, "Failed to initialize UCI context\n");
    return 1;
  }

  if (uci_load(ctx, "/etc/config/modbus", &pkg) != UCI_OK) {
    fprintf(stderr, "Failed to load config file\n");
    uci_free_context(ctx);
    return 1;
  }

  struct uci_element *e;
  struct uci_ptr ptr;
  uci_foreach_element(&pkg->sections, e) {
    struct uci_section *s = uci_to_section(e);
    if (s->e.name == NULL)
      break;
    printf("Section2: %s\n", s->e.name);
    printf("Section after\n");

    struct uci_element *opt_e;
    uci_foreach_element(&s->options, opt_e) {
      struct uci_option *o = uci_to_option(opt_e);
      if (o == NULL || o->e.name == NULL)
        break;
      printf("Option: %s=%s\n", o->e.name, o->v.string);
      mbgw_mb_data_t parcel = extract_data_from_option(o->e.name);
      parcel.value = (uint16_t)atoi(o->v.string);
      if (parcel.pnu == 4011) {
        continue; // skip read only parameters
      }
      int rc = modbus_write_register(mb, parcel.pnu - 1, parcel.value);
      if (rc == -1) {
        std::cerr << "error " << modbus_strerror(errno) << " to write "
                  << parcel.value << " in " << parcel.pnu << std::endl;
        return -1;
      }
    }
  }
  test_counter++;

  std::cout << "end" << test_counter << std::endl;
  uci_unload(ctx, pkg);
  uci_free_context(ctx);
  return 0;
}

int updating_config_file() {
  std::cout << "start" << test_counter << std::endl;
  struct uci_context *ctx = uci_alloc_context();
  struct uci_package *pkg;

  if (!ctx) {
    fprintf(stderr, "Failed to initialize UCI context\n");
    return 1;
  }

  if (uci_load(ctx, "/etc/config/modbus", &pkg) != UCI_OK) {
    fprintf(stderr, "Failed to load config file\n");
    uci_free_context(ctx);
    return 1;
  }

  struct uci_element *e;
  struct uci_ptr ptr;
  uci_foreach_element(&pkg->sections, e) {
    struct uci_section *s = uci_to_section(e);
    if (s->e.name == NULL)
      break;
    printf("Section2: %s\n", s->e.name);
    printf("Section after\n");

    struct uci_element *opt_e;
    uci_foreach_element(&s->options, opt_e) {
      struct uci_option *o = uci_to_option(opt_e);
      if (o == NULL || o->e.name == NULL)
        break;
      printf("Option: %s=%s\n", o->e.name, o->v.string);
      char option_string[128] = {};
      snprintf(option_string, sizeof option_string, "modbus.%s.%s", s->e.name,
               o->e.name);
      std::cout << "val:" << uci_validate_text(option_string) << std::endl;
      mbgw_mb_data_t parcel = extract_data_from_option(o->e.name);
      int rc = modbus_read_registers(mb, parcel.pnu - 1, 1, &(parcel.value));
      if (rc == -1) {
        std::cerr << "error " << modbus_strerror(errno) << std::endl;
        return -1;
      }
      uci_parse_ptr(ctx, &ptr, option_string);
      ptr.value = std::to_string(parcel.value).c_str();
      uci_set(ctx, &ptr);
    }
  }
  uci_commit(ctx, &pkg, true);
  test_counter++;

  std::cout << "end" << test_counter << std::endl;
  uci_unload(ctx, pkg);
  uci_free_context(ctx);
  return 0;
}

void *mbgw_sock_server_task(void *params) {
  struct sockaddr_un peer_sock;
  char buff[256] = {};
  int *server_sock = (int *)params;
  socklen_t len = sizeof(peer_sock);
  int bytes_rec = 0;

  while (request_to_read_flag) {
    memset(buff, 0, 256);
    bytes_rec = recvfrom(*server_sock, (void *)buff, sizeof(buff), 0,
                         (struct sockaddr *)&peer_sock, &len);
    if (bytes_rec == -1) {
      LOG_F(ERROR, "RECVFROM ERROR = %s", std::strerror(errno));
      close(*server_sock);
      exit(1);
    } else {
      LOG_F(INFO, "recv = %s", buff);
      if (!strcmp(buff, "hello")) {
        lock.acquire();
        update_device_from_config_file();
        lock.release();
      }
    }
  }

  close(*server_sock);

  return NULL;
}

int main(int argc, char *argv[]) {
  mbgw_serial_options_t serial_options;
  Json::Value root;
  int sleep_timer = 50;
  loguru::init(argc, argv);
  loguru::add_file("/var/log/modbus.log", loguru::Append,
                   loguru::Verbosity_MAX);
  LOG_F(INFO, "server is started");
  int len, rc;
  struct sockaddr_un server_sockaddr;
  memset(&server_sockaddr, 0, sizeof(struct sockaddr_un));

  server_sock = socket(AF_UNIX, SOCK_DGRAM, 0);
  if (server_sock == -1) {
    LOG_F(ERROR, "SOCKET ERROR: %s\r\n", std::strerror(errno));
    exit(1);
  }
  server_sockaddr.sun_family = AF_UNIX;
  strcpy(server_sockaddr.sun_path, SOCK_PATH);
  len = sizeof(server_sockaddr);
  unlink(SOCK_PATH);
  rc = bind(server_sock, (struct sockaddr *)&server_sockaddr, len);
  if (rc == -1) {
    LOG_F(ERROR, "BIND ERROR: %s\r\n", std::strerror(errno));
    close(server_sock);
    exit(1);
  }

  LOG_F(INFO, "socket is listening");

  pthread_create(&server_thread, NULL, mbgw_sock_server_task, &server_sock);

  signal(SIGSEGV, signal_handler);
  signal(SIGTERM, signal_handler);
  signal(SIGINT, signal_handler);

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
  } else {
    std::cout << "error, connection info is mandatory";
    exit(-1);
  }

  sleep_timer = mbgw_get_int_or_default(root, "period", 20);

  root.clear();

  std::cout << serial_options.baud << ",";
  std::cout << serial_options.data_bit << ",";
  std::cout << serial_options.parity << ",";
  std::cout << serial_options.port << ",";
  std::cout << serial_options.stop_bit << std::endl;
  std::cout << "read period: " << sleep_timer << std::endl;

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
  lock.release();

  while (true) {
    LOG_F(INFO, "update periodically");
    lock.acquire();
    updating_config_file();
    lock.release();
    sleep(sleep_timer);
  }

  modbus_close(mb);
  modbus_free(mb);
  pthread_join(server_thread, NULL);
  return 0;
}