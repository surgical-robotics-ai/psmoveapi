
 /**
 * PS Move API - An interface for the PS Move Motion Controller
 * Copyright (c) 2012 Thomas Perl <m@thp.io>
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 *    1. Redistributions of source code must retain the above copyright
 *       notice, this list of conditions and the following disclaimer.
 *
 *    2. Redistributions in binary form must reproduce the above copyright
 *       notice, this list of conditions and the following disclaimer in the
 *       documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE
 * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 **/


#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#include <math.h>
#include <string.h>

#include "psmove_tracker_opencv.h"
#include "psmove_fusion.h"

#include <ros/ros.h>
#include <geometry_msgs/PoseStamped.h>
#include <sensor_msgs/Joy.h>

#define MAX_CONTROLLERS 5

using namespace std;

extern "C" {
    void cvShowImage(const char *, void*);
};

inline float SIGN(float x) { 
	return (x >= 0.0f) ? +1.0f : -1.0f; 
}

inline float NORM(float a, float b, float c, float d) { 
	return sqrt(a * a + b * b + c * c + d * d); 
}

// Copied from https://gist.github.com/lb5160482/e812a671f778c0c7b14f80612c5389f7
// quaternion = [w, x, y, z]'
void transMat2Quat(float* mat, float* quat) {
	// float r11 = mat[0];
	// float r21 = mat[1];
	// float r31 = mat[2];
	// float r12 = mat[4];
	// float r22 = mat[5];
	// float r32 = mat[6];
	// float r13 = mat[8];
	// float r23 = mat[9];
	// float r33 = mat[10];

	float r11 = mat[0];
	float r21 = mat[4];
	float r31 = mat[8];
	float r12 = mat[1];
	float r22 = mat[5];
	float r32 = mat[9];
	float r13 = mat[2];
	float r23 = mat[6];
	float r33 = mat[10];

	float q0 = (r11 + r22 + r33 + 1.0f) / 4.0f;
	float q1 = (r11 - r22 - r33 + 1.0f) / 4.0f;
	float q2 = (-r11 + r22 - r33 + 1.0f) / 4.0f;
	float q3 = (-r11 - r22 + r33 + 1.0f) / 4.0f;
	if (q0 < 0.0f) {
		q0 = 0.0f;
	}
	if (q1 < 0.0f) {
		q1 = 0.0f;
	}
	if (q2 < 0.0f) {
		q2 = 0.0f;
	}
	if (q3 < 0.0f) {
		q3 = 0.0f;
	}
	q0 = sqrt(q0);
	q1 = sqrt(q1);
	q2 = sqrt(q2);
	q3 = sqrt(q3);
	if (q0 >= q1 && q0 >= q2 && q0 >= q3) {
		q0 *= +1.0f;
		q1 *= SIGN(r32 - r23);
		q2 *= SIGN(r13 - r31);
		q3 *= SIGN(r21 - r12);
	}
	else if (q1 >= q0 && q1 >= q2 && q1 >= q3) {
		q0 *= SIGN(r32 - r23);
		q1 *= +1.0f;
		q2 *= SIGN(r21 + r12);
		q3 *= SIGN(r13 + r31);
	}
	else if (q2 >= q0 && q2 >= q1 && q2 >= q3) {
		q0 *= SIGN(r13 - r31);
		q1 *= SIGN(r21 + r12);
		q2 *= +1.0f;
		q3 *= SIGN(r32 + r23);
	}
	else if (q3 >= q0 && q3 >= q1 && q3 >= q2) {
		q0 *= SIGN(r21 - r12);
		q1 *= SIGN(r31 + r13);
		q2 *= SIGN(r32 + r23);
		q3 *= +1.0f;
	}
	else {
		printf("coding error\n");
	}
	float r = NORM(q0, q1, q2, q3);
	q0 /= r;
	q1 /= r;
	q2 /= r;
	q3 /= r;

	quat[0] = q0;
	quat[1] = q1;
	quat[2] = q2;
	quat[3] = q3;
}

void transMat2Pos(float* mat, float* pos){
    pos[0] = mat[12];
    pos[1] = mat[13];
    pos[2] = mat[14];
}

class Tracker{
	public:
	Tracker(){

	}

	~Tracker(){
		if (m_tracker)
			psmove_tracker_free(m_tracker);
	}

	bool init(){
		if (! m_initialized){
	        m_tracker = psmove_tracker_new();
			if (m_tracker == NULL){
				printf("ERROR! NO COMPATIBLE TRACKER FOUND \n");
				return 0;
			}
			m_initialized = true;
		}
		return true;
	}

	void update(){
		psmove_tracker_update_image(m_tracker);
        psmove_tracker_update(m_tracker, NULL);
        cvShowImage("asdf", psmove_tracker_opencv_get_frame(m_tracker));
	}

	PSMoveTracker* getTracker(){
		return m_tracker;
	}

private: 
    PSMoveTracker *m_tracker;
	bool m_initialized;
};

class Controller
{
    public:
		Controller(int index): m_index(index){
			m_quaternion = new float[4];
            m_position = new float[3];
			m_initialized = false;
			m_move = nullptr;
		}

		~Controller(){
			if (m_move)
				psmove_disconnect(m_move);
		}

		bool init(Tracker* tracker){
			m_tracker = tracker->getTracker();
			bool valid = true;
			m_move = psmove_connect_by_id(m_index);
            int quit = 0;

            if (m_move == NULL) {
                fprintf(stderr, "INFO! Could not connect to controller.\n");
				valid = false;
				return valid;
            }

			char *serial = psmove_get_serial(m_move);
    		printf("INFO! CONNECTED MOVE\'S SERIAL NUMBER : %s\n", serial);
    		psmove_free_mem(serial);

			PSMove_Connection_Type ctype = psmove_connection_type(m_move);
    		switch (ctype) {
			case Conn_USB:
				printf("Connected via USB.\n");
				break;
			case Conn_Bluetooth:
				printf("Connected via Bluetooth.\n");
				break;
			case Conn_Unknown:
				printf("Unknown connection type.\n");
				break;
    		}

			PSMove_Battery_Level battery = psmove_get_battery(m_move);

			if (battery == Batt_CHARGING) {
                printf("INFO! Battery Charging\n");
            } else if (battery == Batt_CHARGING_DONE) {
                printf("INFO! Battery Fully Charged (On Charger)\n");
            } else if (battery >= Batt_MIN && battery <= Batt_MAX) {
                printf("INFO! Battery Level: %d / %d\n", battery, Batt_MAX);
            } else {
                printf("INFO! Battery Level: unknown (%x)\n", battery);
            }

            if (psmove_tracker_enable(m_tracker, m_move) != Tracker_CALIBRATED){
                printf("WARNING! COULD NOT CALIBRATE CONTROLLER %d, IGNORING! \n", m_index);
                return false;
            }

            psmove_enable_orientation(m_move, true);

            m_fusion = psmove_fusion_new(m_tracker, 0.1, 100);
			m_initialized = true;
			printf("********************\n");
			return valid;
		}

        void update()
        {	
			if (!m_initialized){
				printf("ERROR! CONTROLLER AT INDEX %d NOT INITIALIZED, IGNORING UPDATE\n", m_index);
				return;
			}
            while (psmove_poll(m_move)) {
				m_buttons = psmove_get_buttons(m_move);
				m_trigger = (int)psmove_get_trigger(m_move) / 255.0;
                if (m_buttons & Btn_PS) {
                    break;
                }
                if (m_buttons & Btn_MOVE) {
                    psmove_reset_orientation(m_move);
                }
                psmove_get_orientation(m_move, &m_quaternion[0], &m_quaternion[1], &m_quaternion[2], &m_quaternion[3]);
                // psmove_tracker_get_position(tracker, move, &x, &y, &z);

                m_modelViewMat = psmove_fusion_get_modelview_matrix(m_fusion, m_move);
                //                transMat2Quat(m_modelViewMat, m_quaternion);
                transMat2Pos(m_modelViewMat, m_position);
            }
        }

public:
    float* m_quaternion;
    float* m_position;
    float* m_modelViewMat;
	float m_trigger;
	int m_buttons;

private:
    PSMove *m_move;
	PSMoveTracker* m_tracker;
    PSMoveFusion* m_fusion;
	const int m_index;
	bool m_initialized;
};

class ControllerROSInterface{
	public:
		ControllerROSInterface(Controller* controller, ros::NodeHandle* node, string a_namespace, string a_name){
			if (!controller){
				return;
			}
			m_controller = controller;
			string prefix = a_namespace + a_name;
			m_posePub = node->advertise<geometry_msgs::PoseStamped>(prefix + "/pose", 1);
			m_joyPub = node->advertise<sensor_msgs::Joy>(prefix + "/joy", 1);
		}

		~ControllerROSInterface(){
			if (m_controller) delete m_controller;
		}

		void update(){
			m_controller->update();
		}

		void publish(){
			geometry_msgs::PoseStamped pose_msg;
			pose_msg.header.frame_id = "map";
        	pose_msg.header.stamp = ros::Time::now();
        	pose_msg.pose.position.x = m_controller->m_position[0];
        	pose_msg.pose.position.y = m_controller->m_position[1];
        	pose_msg.pose.position.z = m_controller->m_position[2];

        	pose_msg.pose.orientation.w = m_controller->m_quaternion[0];
        	pose_msg.pose.orientation.x = m_controller->m_quaternion[1];
        	pose_msg.pose.orientation.y = m_controller->m_quaternion[2];
        	pose_msg.pose.orientation.z = m_controller->m_quaternion[3];

        	m_posePub.publish(pose_msg);

			sensor_msgs::Joy joy_msg;
			joy_msg.header.frame_id = "map";
        	joy_msg.header.stamp = ros::Time::now();
			joy_msg.axes.push_back(m_controller->m_trigger);
			joy_msg.buttons.push_back(m_controller->m_buttons);

			m_joyPub.publish(joy_msg);

		}

	private:
		ros::Publisher m_posePub;
		ros::Publisher m_joyPub;
		Controller* m_controller;
};

class ROSMoveManager{
	public:
		ROSMoveManager(Tracker* tracker, int argc, char** argv, string node_name = "psmove_ros"){
			m_addedControllers = 0;
			m_tracker = tracker;

			ros::init(argc, argv, node_name);
			m_node = new ros::NodeHandle();
			m_loopRate = new ros::Rate(120);

			m_indexToString[0] = "zero";
			m_indexToString[1] = "one";
			m_indexToString[2] = "two";
			m_indexToString[3] = "three";
			m_indexToString[4] = "four";
			m_indexToString[5] = "five";
		}

		~ROSMoveManager(){
			if (m_tracker) delete m_tracker;
			if (m_loopRate) delete m_loopRate;
			for (auto it : m_controllerROSInterfaces){
				delete it;
			}
			if (m_node) delete m_node;
		}

		unsigned int getControllerCount(){
			return psmove_count_connected();
		}

        bool addControllerROSInterface(Controller* controller, string a_namespace, string a_name){

			bool success = false;

			if (controller->init(m_tracker)){
				ControllerROSInterface* contIfc = new ControllerROSInterface(controller, m_node, a_namespace, a_name);
				m_controllerROSInterfaces.push_back(contIfc);
				success = true;
			}
			return success;
		}

        bool addControllerROSInterface(unsigned int index, string a_namespace="/psmove", string a_name=""){

			if (! (index < MAX_CONTROLLERS)){
				printf("ERROR! ONLY CONTROLLER INDEXES UPTO %d ARE SUPPORTED \n", MAX_CONTROLLERS-1);
				return false;
			}

			bool success = false;

			if (getControllerCount() > 0){
				Controller* controller = new Controller(index);
				if (a_name.empty()) a_name = m_indexToString[index+1];
                success = addControllerROSInterface(controller, a_namespace, a_name);
			}
			return success;
		}

        bool addAllControllerROSInterfaces(){
			bool success = false;
			for (unsigned int i = 0 ; i < getControllerCount() ; i++){
                success |= addControllerROSInterface(i);
			}
			return success;
		}

		void update(){
			for (auto it : m_controllerROSInterfaces){
				it->update();
				it->publish();
			}
			m_tracker->update();
			m_loopRate->sleep();
		}

	private:
		Tracker* m_tracker;
		vector<ControllerROSInterface*> m_controllerROSInterfaces;
		unsigned int m_addedControllers;
		ros::NodeHandle* m_node;
		ros::Rate* m_loopRate;
		map<unsigned int, string> m_indexToString;
};

