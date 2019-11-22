/****************************************************************************
 *
 *   Copyright (c) 2018 PX4 Development Team. All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in
 *    the documentation and/or other materials provided with the
 *    distribution.
 * 3. Neither the name PX4 nor the names of its contributors may be
 *    used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS
 * FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE
 * COPYRIGHT OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING,
 * BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS
 * OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED
 * AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN
 * ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 *
 ****************************************************************************/

#include "module.h"
#include "hg_temp.h"

#include <px4_platform_common/getopt.h>
#include <px4_platform_common/log.h>
#include <px4_platform_common/posix.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <uORB/uORB.h>
#include <uORB/topics/parameter_update.h>
// #include <uORB/topics/sensor_combined.h>
// #include <uORB/topics/vehicle_global_position.h>
#include <uORB/topics/vehicle_local_position.h>


int Module::print_status()
{
	PX4_INFO("Project_Brett is running");
	// TODO: print additional runtime information about the state of the module

	return 0;
}

int Module::custom_command(int argc, char *argv[])
{
	// if (!is_running()) {
	// 	print_usage("not running");
	// 	return 1;
	// }

	// // additional custom commands can be handled like this:
	// if (!strcmp(argv[0], "close-file")) {
	// 	get_instance()->close_file();
	// 	return 0;
	// }

	return print_usage("unknown command");
}


int Module::task_spawn(int argc, char *argv[])
{
	_task_id = px4_task_spawn_cmd("module",
				      SCHED_DEFAULT,
				      SCHED_PRIORITY_DEFAULT,
				      1024,
				      (px4_main_t)&run_trampoline,
				      (char *const *)argv);

	if (_task_id < 0) {
		_task_id = -1;
		return -errno;
	}

	return 0;
}

Module *Module::instantiate(int argc, char *argv[])
{
	char *file_name = NULL;
	bool log_flag = false;
	bool dummy_flag = false;
	bool error_flag = false;
	int frequency_param = 2;

	int myoptind = 1;
	int ch;
	const char *myoptarg = nullptr;

	// parse CLI arguments
	while ((ch = px4_getopt(argc, argv, "n:f:l:d", &myoptind, &myoptarg)) != EOF) {
		switch (ch) {
			case 'n':
			{
				size_t len = strlen(myoptarg);
				size_t prepend = strlen("/fs/microsd/data/");
				file_name = (char *)malloc(sizeof(char) * (prepend + len + 1));
				strcpy(file_name, "/fs/microsd/data/");
				strcat(file_name, myoptarg);
				break;
			}

			case 'f':
				frequency_param = (int)strtol(myoptarg, nullptr, 10);
				break;

			case 'l':
				log_flag = true;
				break;

			case 'd':
				dummy_flag = true;
				break;

			case '?':
				error_flag = true;
				break;

			default:
				PX4_WARN("unrecognized flag");
				error_flag = true;
				break;
		}
	}

	if (error_flag) {
		return nullptr;
	}

	if (file_name == NULL) {
		size_t len = strlen("/fs/microsd/data/out.txt");
		file_name = (char*)malloc(sizeof(char) * (len + 1));
		strcpy(file_name, "/fs/microsd/data/out.txt");
	}

	Module *instance = new Module();

	if (instance == nullptr) {
		PX4_ERR("alloc failed");
	}

	instance->log_flag = log_flag;
	instance->dummy_flag = dummy_flag;
	FILE *output_file = fopen(file_name, "w");
	if (output_file == NULL) {
		PX4_ERR("File failed to open");
	}
	instance->output_file = output_file;
	instance->frequency = frequency_param;

	printf("%s\n", file_name);
	printf("Logging: %d, Frequency: %d\n", instance->log_flag, instance->frequency);

	fprintf(instance->output_file, "time,x,y,ambient_temp,obj_temp\n");

	free(file_name);

	return instance;
}

Module::Module()
	: ModuleParams(nullptr)
{
}

void Module::run()
{
	// Run the loop synchronized to the gps topic publication
	int position_sub = orb_subscribe(ORB_ID(vehicle_local_position));

	/* limit the update rate to 5 Hz */
	orb_set_interval(position_sub, (int)(1000/get_instance()->frequency));

	px4_pollfd_struct_t fds[1];
	fds[0].fd = position_sub;
	fds[0].events = POLLIN;

	struct vehicle_local_position_s raw;

	// unsigned long long initial_time = 0;
	// bool time_set = false;

	// double dummy_x = 0;
	// double dummy_y = 0;
	// double dummy_max_x = 400;
	// double dummy_max_y = 400;
	// double dummy_row_height = 20;

	// initialize parameters
	parameters_update(true);

	while (!should_exit()) {

		// wait for up to 1000ms for data
		int pret = px4_poll(fds, (sizeof(fds) / sizeof(fds[0])), 1000);

		if (pret == 0) {
			// Timeout: let the loop run anyway, don't do `continue` here

		} else if (pret < 0) {
			// this is undesirable but not much we can do
			PX4_ERR("poll error %d, %d", pret, errno);
			px4_usleep(50000);
			continue;

		} else if (fds[0].revents & POLLIN) {
			orb_copy(ORB_ID(vehicle_local_position), position_sub, &raw);

			unsigned long long raw_timestamp = raw.timestamp;
			// if (!time_set) {
			// 	initial_time = raw_timestamp;
			// 	time_set = true;
			// }
			// unsigned long long elapsed_time = raw_timestamp - initial_time;

			double raw_x = (double)raw.x;
			double raw_y = (double)raw.y;

			if (get_instance()->dummy_flag) {

			}

			double ambient_temp = 0.0;
			double obj_temp = 0.0;
			ambient_temp = get_instance()->temp_sensor.readAmbientTempC();
			obj_temp = get_instance()->temp_sensor.readObjectTempC();

			FILE* file = get_instance()->output_file;
			if (file) {
				fprintf(file, "%llu,%.8f,%.8f,%.8f,%.8f\n", raw_timestamp, raw_x, raw_y, ambient_temp, obj_temp);
				if (get_instance()->log_flag) {
					printf("Time: %llu, X: %.8f, Y: %.8f, Ambient Temp: %.4f, Obj Temp: %.4f\n", raw_timestamp, raw_x, raw_y, ambient_temp, obj_temp);
				}
			}
		}

		parameters_update();
	}

	printf("Run done\n");

	FILE* file = get_instance()->output_file;
	if (file) {
		fclose(file);
	}

	orb_unsubscribe(position_sub);
}

void Module::parameters_update(bool force)
{
	// check for parameter updates
	if (_parameter_update_sub.updated() || force) {
		// clear update
		parameter_update_s update;
		_parameter_update_sub.copy(&update);

		// update parameters from storage
		updateParams();
	}
}

int Module::print_usage(const char *reason)
{
	if (reason) {
		PX4_WARN("%s\n", reason);
	}

	PRINT_MODULE_DESCRIPTION(
		R"DESCR_STR(
### Description
This module collects IR data and outputs it to a file

### Examples
CLI usage example:
$ project_brett start -l -f 20 -n "test.txt"

)DESCR_STR");

	PRINT_MODULE_USAGE_NAME("Project Brett (IR Data Collection)", "template");
	PRINT_MODULE_USAGE_COMMAND("start");
	PRINT_MODULE_USAGE_PARAM_FLAG('l', "Log/print values", true);
	PRINT_MODULE_USAGE_PARAM_FLAG('d', "Use dummy values", true);
	PRINT_MODULE_USAGE_PARAM_INT('f', 2, 1, 30, "Recording frequency", true);
	PRINT_MODULE_USAGE_PARAM_STRING('n', "out.txt", NULL, "Output file name", true);
	PRINT_MODULE_USAGE_DEFAULT_COMMANDS();

	return 0;
}

int project_brett_main(int argc, char *argv[])
{
	return Module::main(argc, argv);
}
