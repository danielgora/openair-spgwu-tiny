/*
* Copyright (c) 2017 Sprint
*
* Licensed under the Apache License, Version 2.0 (the "License");
* you may not use this file except in compliance with the License.
* You may obtain a copy of the License at
*
*    http://www.apache.org/licenses/LICENSE-2.0
*
* Unless required by applicable law or agreed to in writing, software
* distributed under the License is distributed on an "AS IS" BASIS,
* WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
* See the License for the specific language governing permissions and
* limitations under the License.
*/

#include "async_shell_cmd.hpp"
#include "common_defs.h"
#include "itti.hpp"
#include "logger.hpp"
#include "options.hpp"
#include "pid_file.hpp"
#include "pgw_app.hpp"
#include "pgw_config.hpp"
#include "sgwc_app.hpp"
#include "sgwc_config.hpp"

#include <boost/asio.hpp>
#include <iostream>
#include <thread>
#include <signal.h>
#include <stdint.h>
#include <unistd.h> // get_pid(), pause()

using namespace gtpv2c;
using namespace pgwc;
using namespace sgwc;
using namespace util;
using namespace std;

itti_mw *itti_inst = nullptr;
async_shell_cmd *async_shell_cmd_inst = nullptr;
pgw_app *pgw_app_inst = nullptr;
sgwc_app *sgwc_app_inst = nullptr;
pgw_config pgw_cfg;
sgwc_config sgwc_cfg;
boost::asio::io_service io_service;

//------------------------------------------------------------------------------
void my_app_signal_handler(int s){
  std::cout << "Caught signal " << s << std::endl;
  Logger::system().startup( "exiting" );
  itti_inst->send_terminate_msg(TASK_SGWC_APP);
  itti_inst->wait_tasks_end();
  std::cout << "Freeing Allocated memory..." << std::endl;
  if (async_shell_cmd_inst) delete async_shell_cmd_inst; async_shell_cmd_inst = nullptr;
  std::cout << "Async Shell CMD memory done." << std::endl;
  if (itti_inst) delete itti_inst; itti_inst = nullptr;
  std::cout << "ITTI memory done." << std::endl;
  if (sgwc_app_inst) delete sgwc_app_inst; sgwc_app_inst = nullptr;
  std::cout << "SGW APP memory done." << std::endl;
  if (pgw_app_inst) delete pgw_app_inst; pgw_app_inst = nullptr;
  std::cout << "PGW APP memory done." << std::endl;
  std::cout << "Freeing Allocated memory done" << std::endl;
  exit(0);
}
//------------------------------------------------------------------------------
int main(int argc, char **argv)
{
  // Logger
  Logger::init( "spgwc" );

  // Command line options
  if ( !Options::parse( argc, argv ) )
  {
     std::cout << "Options::parse() failed" << std::endl;
     return 1;
  }
  Logger::sgwc_app().startup( "Options parsed" );

  struct sigaction sigIntHandler;
  sigIntHandler.sa_handler = my_app_signal_handler;
  sigemptyset(&sigIntHandler.sa_mask);
  sigIntHandler.sa_flags = 0;
  sigaction(SIGINT, &sigIntHandler, NULL);

  // Config
  sgwc_cfg.load(Options::getlibconfigConfig());
  sgwc_cfg.display();
  pgw_cfg.load(Options::getlibconfigConfig());
  pgw_cfg.display();

  // Inter task Interface
  itti_inst = new itti_mw();
  itti_inst->start(pgw_cfg.itti.itti_timer_sched_params);

  // system command
  async_shell_cmd_inst = new async_shell_cmd(sgwc_cfg.itti.async_cmd_sched_params);

  // PGW application layer
  pgw_app_inst = new pgw_app(Options::getlibconfigConfig());

  // PID file
  // Currently hard-coded value. TODO: add as config option.
   string pid_file_name = get_exe_absolute_path("/var/run", pgw_cfg.instance);
  if (! is_pid_file_lock_success(pid_file_name.c_str())) {
    Logger::pgwc_app().error( "Lock PID file %s failed\n", pid_file_name.c_str());
    exit (-EDEADLK);
  }

  // SGW application layer
  sgwc_app_inst = new sgwc_app(Options::getlibconfigConfig());


  FILE *fp = NULL;
  std::string filename = fmt::format("/tmp/spgwc_{}.status", getpid());
  fp = fopen(filename.c_str(), "w+");
  fprintf(fp, "STARTED\n");
  fflush(fp);
  fclose(fp);

  // once all udp servers initialized
  io_service.run();

  pause();
  return 0;
}