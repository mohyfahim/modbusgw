#include <csignal>
#include <errno.h>
#include <fstream>
#include <iostream>
#include <json/json.h>
#include <modbus/modbus.h>
#include <pthread.h>

#include <condition_variable>
#include <mutex>

// #include <uci.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <sys/un.h>
#include <unistd.h>

#include "type.h"

#define CONFIG_FILE_PATH "/etc/modbusgw/conf.json"
#define REGISTERS_FILE_PATH "/etc/modbusgw/registers.json"
#define MBGW_DB_ROOT "/var/modbusgw/"
#define MBGW_DB_DATA MBGW_DB_ROOT "data/"
#define MBGW_DB_REQUEST MBGW_DB_ROOT "request.txt"
#define SERVER_PATH "/var/run/mgd_unix_sock.server"

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
  if (request_to_read_flag) {
    request_to_read_flag = false;
    pthread_join(request_to_read, NULL);
    exit(-1);
  }
}
void *request_to_read_task(void *params) {
  std::fstream f;
  f.open(MBGW_DB_REQUEST, std::ios::out | std::ios::in | std::ios::trunc);
  if (!f.is_open()) {
    // TODO: log error
  }
  uint16_t buffer[32];
  while (request_to_read_flag) {
    std::cout << "request to read check" << std::endl;
    std::string line;
    while (std::getline(f, line)) {
      std::stringstream s(line);
      std::string pnu, wv;
      s >> pnu >> wv;

      lock.acquire();
      if (wv.length() > 0) {
        std::cout << "write command " << wv << std::endl;
        uint16_t value = stoi(wv);
        modbus_write_registers(mb, stoi(pnu) - 1, 1, &value);
      } else if (pnu.length() > 0) {
        std::cout << "read command " << std::endl;
        int rc = modbus_read_input_registers(mb, stoi(pnu) - 1, 1, buffer);
        if (rc == -1) {
          fprintf(stderr, "error %s\n", modbus_strerror(errno));
        }
        for (int i = 0; i < rc; i++) {
          printf("pnu[%s] reg[%d]=%d (0x%X)\n", pnu.c_str(), i, buffer[i],
                 buffer[i]);
        }
      } else {
        std::cout << "empty line?: " << line << std::endl;
      }
      lock.release();
    }

    sleep(2);
  }

  return NULL;
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

int main(int argc, char *argv[]) {

  if (argc != 3) {
    std::cout << "ERROR: usage " << argv[0] << " <PNU> <Value>" << std::endl;
    exit(-1);
  }

  int client_socket, rc;
  mbgw_sock_data_t data;
  struct sockaddr_un remote;
  char buf[256];
  memset(&remote, 0, sizeof(struct sockaddr_un));

  data.pnu = (unsigned int)atoi(argv[1]);
  data.value = (uint16_t)atoi(argv[2]);
  /****************************************/
  /* Create a UNIX domain datagram socket */
  /****************************************/
  client_socket = socket(AF_UNIX, SOCK_DGRAM, 0);
  if (client_socket == -1) {
    printf("SOCKET ERROR = %s\n", strerror(errno));
    exit(1);
  }

  remote.sun_family = AF_UNIX;
  strcpy(remote.sun_path, SERVER_PATH);

  /***************************************/
  /* Copy the data to be sent to the     */
  /* buffer and send it to the server.   */
  /***************************************/
  strcpy(buf, "hello");
  printf("Sending data...\n");
  rc = sendto(client_socket, (void *)&data, sizeof(mbgw_sock_data_t), 0,
              (struct sockaddr *)&remote, sizeof(remote));
  if (rc == -1) {
    printf("SENDTO ERROR = %s\n", strerror(errno));
    close(client_socket);
    exit(1);
  } else {
    printf("Data sent!\n");
  }

  // mbgw_serial_options_t serial_options;
  // Json::Value root;
  // int sleep_timer = 0;
  // struct uci_ptr ptr;
  // struct uci_context *uci_context = uci_alloc_context();
  // struct uci_package * pkg;
  // if (!uci_context) {
  //   std::cout << "cannot allocate uci context" << std::endl;
  //   exit(-1);
  // }

  // // if (uci_lookup_ptr(uci_context, &ptr, path, true) != UCI_OK ||
  // //     (ptr.o == NULL || ptr.o->v.string == NULL)) {
  // //   std::cout << "cannot read config from uci" << std::endl;
  // //   uci_free_context(uci_context);
  // //   exit(-1);
  // // }
  // signal(SIGSEGV, signal_handler);
  // signal(SIGTERM, signal_handler);
  // signal(SIGINT, signal_handler);

  // MBGW_LOAD_JSON_FROM_FILE(CONFIG_FILE_PATH, root);

  // Json::Value connection = root["connection"];
  // if (connection) {
  //   serial_options.baud = mbgw_get_int_or_default(connection, "baud", 19200);
  //   serial_options.data_bit =
  //       mbgw_get_int_or_default(connection, "data_bit", 8);
  //   serial_options.parity = mbgw_get_char_or_default(connection, "parity",
  //   'E'); serial_options.port =
  //       mbgw_get_string_or_default(connection, "port", "/dev/ttyUSB0");
  //   serial_options.stop_bit =
  //       mbgw_get_int_or_default(connection, "stop_bit", 1);
  // } else {
  //   std::cout << "error, connection info is mandatory";
  //   exit(-1);
  // }

  // sleep_timer = mbgw_get_int_or_default(root, "period", 5);

  // root.clear();

  // std::cout << serial_options.baud << ",";
  // std::cout << serial_options.data_bit << ",";
  // std::cout << serial_options.parity << ",";
  // std::cout << serial_options.port << ",";
  // std::cout << serial_options.stop_bit << std::endl;
  // std::cout << "read period: " << sleep_timer << std::endl;

  // MBGW_LOAD_JSON_FROM_FILE(REGISTERS_FILE_PATH, root);
  // Json::Value registers = root["data"];

  // uint16_t tab_reg[64];

  // mb = modbus_new_rtu(serial_options.port.c_str(), serial_options.baud,
  //                     serial_options.parity, serial_options.data_bit,
  //                     serial_options.stop_bit);
  // if (modbus_connect(mb) == -1) {
  //   fprintf(stderr, "Connection failed: %s\n", modbus_strerror(errno));
  //   modbus_free(mb);
  //   return -1;
  // }
  // modbus_set_slave(mb, 1);
  // modbus_rtu_set_serial_mode(mb, MODBUS_RTU_RS485);
  // pthread_create(&request_to_read, NULL, request_to_read_task, NULL);
  // lock.release();
  // while (true) {
  //   std::cout << "perodically read" << std::endl;
  //   for (int i = 0; i < registers.size(); i++) {
  //     Json::Value reg = registers[i];
  //     int pnu = reg["PNU"].asInt();
  //     lock.acquire();
  //     int rc = modbus_read_registers(mb, pnu - 1, 1, tab_reg);
  //     lock.release();
  //     if (rc == -1) {
  //       fprintf(stderr, "error %s\n", modbus_strerror(errno));
  //       return -1;
  //     }
  //     for (int i = 0; i < rc; i++) {
  //       printf("pnu[%d] reg[%d]=%d (0x%X)\n", pnu, i, tab_reg[i],
  //       tab_reg[i]);
  //     }
  //   }
  //   sleep(sleep_timer);
  // }

  // modbus_close(mb);
  // modbus_free(mb);
  rc = close(client_socket);
  return 0;
}