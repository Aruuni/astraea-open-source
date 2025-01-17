#include <getopt.h>
#include <signal.h>
#include <stdio.h>

#include <atomic>
#include <chrono>
#include <fstream>
#include <iostream>
#include <mutex>
#include <string>
#include <thread>

#include "address.hh"
#include "common.hh"
#include "logging.hh"
#include "socket.hh"

#define BUFFER 1024

using namespace std;
using clock_type = std::chrono::high_resolution_clock;

std::chrono::_V2::system_clock::time_point ts_now = clock_type::now();
std::unique_ptr<std::ofstream> perf_log;
std::atomic<bool> recv_traffic(true);
std::atomic<size_t> recv_cnt = 0;
static size_t last_observed_recv_cnt = 0;
bool terminal_out = false;
void signal_handler(int sig) {
  if (sig == SIGINT or sig == SIGKILL or sig == SIGTERM) {
    // terminate pyhelper
    recv_traffic = false;
    // close iperf
    if (perf_log) {
      perf_log->close();
    }
    // IPC socket will be closed later
    std::this_thread::sleep_for(std::chrono::microseconds(100));
    exit(1);
  }
}

void perf_log_thread(const std::chrono::milliseconds interval) {
  // start regular congestion control pattern
  auto when_started = clock_type::now();
  auto target_time = when_started + interval;
  size_t tmp = 0;
  while (recv_traffic.load()) {
    // log the current throughput in Mbps
    tmp = recv_cnt;  // use this to avoid read/write competing
    double current_thr = (double)(tmp - last_observed_recv_cnt) * 8 / (double)interval.count() * 1000 / 1000000;
    last_observed_recv_cnt = tmp;
    if (perf_log) {
          // Convert the time_point to milliseconds since epoch
          auto millis_since_epoch = std::chrono::duration_cast<std::chrono::milliseconds>(
              std::chrono::system_clock::now().time_since_epoch()
          ).count();
          *perf_log << millis_since_epoch << "," << current_thr << "\n";
          
    }

    auto millis_since_epoch = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::system_clock::now().time_since_epoch()
        ).count();
    cout << millis_since_epoch << "," << current_thr << "\n";
    
    std::this_thread::sleep_until(target_time);
    target_time += interval;
  }
}

void usage_error(const string& program_name) {
  cerr << "Usage: " << program_name << " [OPTION]... [COMMAND]" << "\n";
  cerr << "\n";
  cerr << "Options = --ip=IP_ADDR --port=PORT --cong=ALGORITHM (default: "
          "CUBIC) --perf-log=PATH(default is None) --perf-interval=MS "
          "--one-off"
       << "\n"
       << "If perf_log is specified, the default log interval is 500ms" << "\n";
  cerr << "\n";

  throw runtime_error("invalid arguments");
}

int main(int argc, char** argv) {
  if (argc < 1) {
    usage_error(argv[0]);
  }

  // New boolean flag for one-off mode
  bool one_off = false;

  const option command_line_options[] = {
      {"port", required_argument, nullptr, 'p'},
      {"cong", optional_argument, nullptr, 'c'},
      {"perf-log", optional_argument, nullptr, 'l'},
      {"perf-interval", optional_argument, nullptr, 'i'},
      {"terminal-out", no_argument, nullptr, 't'},
      {"one-off", no_argument, nullptr, 'o'},
      {0, 0, nullptr, 0}};

  string service, cong_ctl, interval, perf_log_path;
  while (true) {
    const int opt = getopt_long(argc, argv, "", command_line_options, nullptr);
    if (opt == -1) { /* end of options */
      break;
    }
    switch (opt) {
    case 'c':
      cong_ctl = optarg;
      break;
    case 'i':
      interval = optarg;
      break;
    case 'l':
      perf_log_path = optarg;
      break;
    case 'p':
      service = optarg;
      break;
    case 't':
      terminal_out = true;
      break;
    case 'o':
      one_off = true;
      break;
    case '?':
      usage_error(argv[0]);
      break;
    default:
      throw runtime_error("getopt_long: unexpected return value " +
                          to_string(opt));
    }
  }

  if (optind > argc) {
    usage_error(argv[0]);
  }

  /* default CC is cubic */
  if (cong_ctl.empty()) {
    cong_ctl = "cubic";
  }

  // init perf log file
  std::chrono::milliseconds log_interval(500ms);
  if (!perf_log_path.empty()) {
    perf_log.reset(new std::ofstream(perf_log_path));
    if (!perf_log->good()) {
      throw runtime_error(perf_log_path + ": error opening for writing");
    }
  }

  if (!interval.empty()) {
    log_interval = std::chrono::milliseconds(stoi(interval));
  }
  int port = stoi(service);
  // init server addr
  Address address("0.0.0.0", port);
  TCPSocket server;
  /* set reuse_addr */
  server.set_reuseaddr();
  server.bind(address);
  server.listen();
  LOG(INFO) << "Server listen at " << port;

  // Accept exactly one client if --one-off is set, otherwise loop
  if (one_off) {
    LOG(INFO) << "One-off mode: will accept a single connection and then exit.";
  } else {
    LOG(INFO) << "Normal mode: will accept connections continuously.";
  }

  // Start logging thread (if needed)
  thread log_thread;
  if (perf_log || terminal_out) {
    cerr << "Server start with perf logger" << (one_off ? " one-off mode is enabled.\n" : "\n");
    log_thread = std::move(std::thread(perf_log_thread, log_interval));
    if (perf_log) {
      *perf_log << "time,goodput" << "\n";
    }
  }
  if (terminal_out){
    cout << "----START----" << "\n";
    //cout << "time,goodput" << "\n";
  }
  while (true) {
    // For multiple connections, we'd normally do this in a loop
    // but for simplicity, let's do it once if one_off is true
    TCPSocket client = server.accept();
    struct timeval timeout = {10, 0};  // 10-second timeout
    setsockopt(client.fd_num(), SOL_SOCKET, SO_RCVTIMEO, (char *)&timeout,
               sizeof(timeout));
    setsockopt(client.fd_num(), SOL_SOCKET, SO_SNDTIMEO, (char *)&timeout,
               sizeof(timeout));
    client.set_congestion_control(cong_ctl);
    LOG(DEBUG) << "Congestion control algorithm: " << cong_ctl;

    // Now read data until the connection is closed or an error
    while (true) {
      string data = client.read(BUFFER);
      if (data.empty()) {
        LOG(INFO) << "Connection closed by client";
        break;
      }
      recv_cnt += data.size();
    }

    if (one_off) {
      // Exit with 0 after the single connection is finished
      LOG(INFO) << "One-off connection ended, exiting with code 0";
      // Signal the logging thread to end
      recv_traffic = false;
      if (log_thread.joinable()) {
        log_thread.join();
      }
      cout << "----END----" << "\n";

      return 0;
    }

    // If not one-off, we go back and accept the next connection
  }

  // If we ever exit the loop in non--one-off mode, close logging and exit
  recv_traffic = false;
  if (log_thread.joinable()) {
    log_thread.join();
  }
  return 0;
}
