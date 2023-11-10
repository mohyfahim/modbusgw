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

  int client_socket, rc;
  struct sockaddr_un remote;
  char buf[256];
  memset(&remote, 0, sizeof(struct sockaddr_un));
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
  rc = sendto(client_socket, (void *)buf, sizeof(buf), 0,
              (struct sockaddr *)&remote, sizeof(remote));
  if (rc == -1) {
    printf("SENDTO ERROR = %s\n", strerror(errno));
    close(client_socket);
    exit(1);
  } else {
    printf("Data sent!\n");
  }

  close(client_socket);
  return 0;
}