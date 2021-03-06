#include <fstream>
#include <math.h>
#include <uWS/uWS.h>
#include <chrono>
#include <iostream>
#include <thread>
#include <vector>
#include "Eigen-3.3/Eigen/Core"
#include "Eigen-3.3/Eigen/QR"
#include "json.hpp"
#include "spline.h"

using namespace std; 
//this line is for git testing
// for convenience
using json = nlohmann::json;

// For converting back and forth between radians and degrees.
constexpr double pi() { return M_PI; }
double deg2rad(double x) { return x * pi() / 180; }
double rad2deg(double x) { return x * 180 / pi(); }

// Checks if the SocketIO event has JSON data.
// If there is data the JSON object in string format will be returned,
// else the empty string "" will be returned.
string hasData(string s) {
  auto found_null = s.find("null");
  auto b1 = s.find_first_of("[");
  auto b2 = s.find_first_of("}");
  if (found_null != string::npos) {
    return "";
  } else if (b1 != string::npos && b2 != string::npos) {
    return s.substr(b1, b2 - b1 + 2);
  }
  return "";
}

double distance(double x1, double y1, double x2, double y2)
{
	return sqrt((x2-x1)*(x2-x1)+(y2-y1)*(y2-y1));
}
int ClosestWaypoint(double x, double y, const vector<double> &maps_x, const vector<double> &maps_y)
{

	double closestLen = 100000; //large number
	int closestWaypoint = 0;

	for(int i = 0; i < maps_x.size(); i++)
	{
		double map_x = maps_x[i];
		double map_y = maps_y[i];
		double dist = distance(x,y,map_x,map_y);
		if(dist < closestLen)
		{
			closestLen = dist;
			closestWaypoint = i;
		}

	}

	return closestWaypoint;

}

int NextWaypoint(double x, double y, double theta, const vector<double> &maps_x, const vector<double> &maps_y)
{

	int closestWaypoint = ClosestWaypoint(x,y,maps_x,maps_y);

	double map_x = maps_x[closestWaypoint];
	double map_y = maps_y[closestWaypoint];

	double heading = atan2( (map_y-y),(map_x-x) );

	double angle = abs(theta-heading);

	if(angle > pi()/4)
	{
		closestWaypoint++;
	}

	return closestWaypoint;

}

// Transform from Cartesian x,y coordinates to Frenet s,d coordinates
vector<double> getFrenet(double x, double y, double theta, const vector<double> &maps_x, const vector<double> &maps_y)
{
	int next_wp = NextWaypoint(x,y, theta, maps_x,maps_y);

	int prev_wp;
	prev_wp = next_wp-1;
	if(next_wp == 0)
	{
		prev_wp  = maps_x.size()-1;
	}

	double n_x = maps_x[next_wp]-maps_x[prev_wp];
	double n_y = maps_y[next_wp]-maps_y[prev_wp];
	double x_x = x - maps_x[prev_wp];
	double x_y = y - maps_y[prev_wp];

	// find the projection of x onto n
	double proj_norm = (x_x*n_x+x_y*n_y)/(n_x*n_x+n_y*n_y);
	double proj_x = proj_norm*n_x;
	double proj_y = proj_norm*n_y;

	double frenet_d = distance(x_x,x_y,proj_x,proj_y);

	//see if d value is positive or negative by comparing it to a center point

	double center_x = 1000-maps_x[prev_wp];
	double center_y = 2000-maps_y[prev_wp];
	double centerToPos = distance(center_x,center_y,x_x,x_y);
	double centerToRef = distance(center_x,center_y,proj_x,proj_y);

	if(centerToPos <= centerToRef)
	{
		frenet_d *= -1;
	}

	// calculate s value
	double frenet_s = 0;
	for(int i = 0; i < prev_wp; i++)
	{
		frenet_s += distance(maps_x[i],maps_y[i],maps_x[i+1],maps_y[i+1]);
	}

	frenet_s += distance(0,0,proj_x,proj_y);

	return {frenet_s,frenet_d};

}

// Transform from Frenet s,d coordinates to Cartesian x,y
vector<double> getXY(double s, double d, const vector<double> &maps_s, const vector<double> &maps_x, const vector<double> &maps_y)
{
	int prev_wp = -1;

	while(s > maps_s[prev_wp+1] && (prev_wp < (int)(maps_s.size()-1) ))
	{
		prev_wp++;
	}

	int wp2 = (prev_wp+1)%maps_x.size();

	double heading = atan2((maps_y[wp2]-maps_y[prev_wp]),(maps_x[wp2]-maps_x[prev_wp]));
	// the x,y,s along the segment
	double seg_s = (s-maps_s[prev_wp]);

	double seg_x = maps_x[prev_wp]+seg_s*cos(heading);
	double seg_y = maps_y[prev_wp]+seg_s*sin(heading);

	double perp_heading = heading-pi()/2;

	double x = seg_x + d*cos(perp_heading);
	double y = seg_y + d*sin(perp_heading);

	return {x,y};

}

int main() {
  uWS::Hub h;

  // Load up map values for waypoint's x,y,s and d normalized normal vectors
  vector<double> map_waypoints_x;
  vector<double> map_waypoints_y;
  vector<double> map_waypoints_s;
  vector<double> map_waypoints_dx;
  vector<double> map_waypoints_dy;

  // Waypoint map to read from
  string map_file_ = "../data/highway_map.csv";
  // The max s value before wrapping around the track back to 0
  double max_s = 6945.554;
  int lane = 1;//start in lane 1. Lanes are 0 for far left, 1 for middle, 2 for right
  int plan_steps = 50;// number of steps to project into the future
  double safety_gap = 30;//too close
   //Have a reference velocity to target
  double ref_vel = 0.0;//mph

  ifstream in_map_(map_file_.c_str(), ifstream::in);

  string line;
  while (getline(in_map_, line)) {
  	istringstream iss(line);
  	double x;
  	double y;
  	float s;
  	float d_x;
  	float d_y;
  	iss >> x;
  	iss >> y;
  	iss >> s;
  	iss >> d_x;
  	iss >> d_y;
  	map_waypoints_x.push_back(x);
  	map_waypoints_y.push_back(y);
  	map_waypoints_s.push_back(s);
  	map_waypoints_dx.push_back(d_x);
  	map_waypoints_dy.push_back(d_y);
  }


  h.onMessage([&map_waypoints_x,&map_waypoints_y,&map_waypoints_s,&map_waypoints_dx,&map_waypoints_dy,&lane,&plan_steps,&ref_vel](uWS::WebSocket<uWS::SERVER> ws, char *data, size_t length,
                     uWS::OpCode opCode) {
    // "42" at the start of the message means there's a websocket message event.
    // The 4 signifies a websocket message
    // The 2 signifies a websocket event
    //auto sdata = string(data).substr(0, length);
    //cout << sdata << endl;
    if (length && length > 2 && data[0] == '4' && data[1] == '2') {

      auto s = hasData(data);

      if (s != "") {
        auto j = json::parse(s);
        
        string event = j[0].get<string>();
        
        if (event == "telemetry") {
          // j[1] is the data JSON object
          
        	// Main car's localization Data
          	double car_x = j[1]["x"];
          	double car_y = j[1]["y"];
          	double car_s = j[1]["s"];
          	double car_d = j[1]["d"];
          	double car_yaw = j[1]["yaw"];
          	double car_speed = j[1]["speed"];

          	// Previous path data given to the Planner
          	auto previous_path_x = j[1]["previous_path_x"];
          	auto previous_path_y = j[1]["previous_path_y"];
          	// Previous path's end s and d values 
          	double end_path_s = j[1]["end_path_s"];
          	double end_path_d = j[1]["end_path_d"];

          	// Sensor Fusion Data, a list of all other cars on the same side of the road.
          	auto sensor_fusion = j[1]["sensor_fusion"];

          	int prev_size = previous_path_x.size();

          	if(prev_size == 0)
          	{
          		end_path_s = car_s;
          	}
          	double safety_gap = (ref_vel / 2)+5;
          	bool left_open = true;
          	bool right_open = true;
          	double left_clear = 10000.0;
          	double right_clear = 10000.0;
          	double speed_limit = 49.5;//mph
          	if(lane == 0)
          	{
          		left_open = false;         
          	}
          	if(lane == 2)
          	{
          		right_open = false;
          	}
          	bool too_close = false; 
          	//find ref_vel to use
          	//std::cout<<"test 0.3"<<endl;
          	for(int i = 0; i < sensor_fusion.size(); i++)//where i is the ith car
          	{
          		//car is in my lane
          		float d = sensor_fusion[i][6];
          		//which lane is it in? It is the lane sensor fusion says it is in.
          		float d_lane = 1;
          		if(d < 4)
          		{
          			d_lane = 0;
          		}
          		if(d > 8)
          		{
          			d_lane = 2;
          		}
          		//std::cout<<"test 0.4"<<endl;
          		//std::cout<<"lane = "<<lane<<endl;
          		
          			
          		double vx = sensor_fusion[i][3];
          		double vy = sensor_fusion[i][4];
          		double check_speed = sqrt(vx*vx+vy*vy)*0.02;
          		double obs_s = sensor_fusion[i][5];
          		double proj_obs_s = obs_s + ((double)prev_size * check_speed);
          		double gap = proj_obs_s - end_path_s;
          		
          		//double check_car_s = sensor_fusion[i][5];
				//check_car_s += ((double)prev_size*.02*check_speed);//if using prior points can project s value out
          			
          		//std::cout<<"check_car_s = "<<check_car_s<<endl;
          		//std::cout<<"car_s = "<<car_s<<endl;
          		
          		
          		/**********************************************************8
          		 * car_s is ego vehicle 
          		 * obs_s is ith vehicle
          		 * lane is ego vehicle lane
          		 * d_lane is ith vehicle lane
          		 * 
          		 * This set of nested if statements determines if the ego 
          		 * vehicle is too close to the vehicle in front of it.
          		 * If so, it determines if either of the other lanes has a 
          		 * gap large enough to safely get into.
          		 * 
          		 */
          		std::cout<<"i = "<<i<<endl;
          		std::cout<<"car_s = "<<car_s<<endl;
          		std::cout<<"obs_s = "<<obs_s<<endl;
          		std::cout<<"end_path_s = "<<end_path_s<<endl;
          		if(obs_s > car_s)
          		{
          			if(gap < safety_gap)
          			{
          				if(d_lane == lane)
          				{
          					too_close = true;
          				}
          				if(d_lane == (lane-1))
          				{
          					left_open = false;
          					if(gap < left_clear)
          					{
          						left_clear = gap;
          					}
          				}
          				if(d_lane == (lane+1))
          				{
          					right_open = false;
          					if (gap < right_clear)
          					{
          						right_clear = gap;
          					}
          				}
          			}	
          		}
          		if((obs_s > car_s) && (obs_s < end_path_s))
				{
	  	  	  	  	 if(d_lane == (lane-1))
	  	  	  	  	 {
	  	  	  	  		 left_open = false;
	  	  	  	  	 }
	  	  	  	  	 if(d_lane == (lane+1))
	  	  	  	  	 {
	  	  	  	  		 right_open = false;
	  	  	  	  	 }
				}
  	  	  	  	if((proj_obs_s > car_s) && (proj_obs_s < end_path_s))
  	  	  	  	{
  	  	  	  		if(d_lane == (lane-1))
  	  	  	  		{
  	  	  	  			left_open = false;
  	  	  	  		}
  	  	  	  		if(d_lane == (lane+1))
  	  	  	  		{
  	  	  	  			right_open = false;
  	  	  	  		}
  	  	  	  	}
          	}
          	if(too_close)
          	{ 
         		if(left_open)
          		{
          			if(right_open && (right_clear > left_clear))
          			{
          				lane = lane + 1;
          			}
          			else
          			{
          				lane = lane - 1;
          			}
          		}
          		else if(right_open)
          		{
          			lane = lane + 1;
          		}
          				
          			
          	}
          		
          
          		
          		//check s values greater than mine and s gap
          		/*
          			if((check_car_s > car_s) && ((check_car_s-car_s) < 30))//if car is in front of us and closer than 30 meters
          			{
          				
          				//Do some logic here, lower reference velocity so we don't crash into the car in front of us
          				//could also flag to change lanes
          				//ref_vel = 29.5;//mph
          				too_close = true;
          				if(lane>0)
          				{	
          					lane = 0;
          				}
          			}*/
          
          	


          	//std::cout<<"Test 1"<<endl;
          	// Creates a list of widely spaced waypoints (x,y), evenly spaced at 30m
          	//later we will interpolate these waypoings with a spline and fill it in with more points
          	vector<double> ptsx;
          	vector<double> ptsy;

          	//reference x,y, yaw this is the starting point or the prior points
          	double ref_x = car_x;
          	double ref_y = car_y;
          	double ref_yaw = deg2rad(car_yaw);

          	//Get the tangent line below
          	//if previous size is almost empty, use the car as starting point
          	if(prev_size < 2)
          	{
          		//Use the points that make the path tangent to the car
          		double prev_car_x = car_x - cos(car_yaw);
          		double prev_car_y = car_y - sin(car_yaw);

          		ptsx.push_back(prev_car_x);
          		ptsx.push_back(ref_x);

          		ptsy.push_back(prev_car_y);
          		ptsy.push_back(ref_y);
          	}
          	else
          	{
          		//use the previous path's end point as starting reference
          		ref_x = previous_path_x[prev_size-1];
          		ref_y = previous_path_y[prev_size-1];

          		double ref_x_prev = previous_path_x[prev_size-2];
          		double ref_y_prev = previous_path_y[prev_size-2];
          		ref_yaw = atan2(ref_y - ref_y_prev,ref_x - ref_x_prev);

          		//use two points that make the path tangent to the prior path's end points
          		ptsx.push_back(ref_x_prev);
          		ptsx.push_back(ref_x);

          		ptsy.push_back(ref_y_prev);
          		ptsy.push_back(ref_y);

          	}
          	
          	//In Frenet add evenly 30m spaced points ahead of the starting reference
          	//30m, 60m, and 90m are the points spline uses to build the curve
          	vector<double> next_wp0 = getXY(end_path_s+30.0,2+(4*lane),map_waypoints_s,map_waypoints_x,map_waypoints_y);
          	vector<double> next_wp1 = getXY(end_path_s+60.0,2+(4*lane),map_waypoints_s,map_waypoints_x,map_waypoints_y);
          	vector<double> next_wp2 = getXY(end_path_s+90.0,2+(4*lane),map_waypoints_s,map_waypoints_x,map_waypoints_y);

          	ptsx.push_back(next_wp0[0]);
          	ptsx.push_back(next_wp1[0]);
          	ptsx.push_back(next_wp2[0]);

          	ptsy.push_back(next_wp0[1]);
          	ptsy.push_back(next_wp1[1]);
          	ptsy.push_back(next_wp2[1]);

          	//std::cout<<"ptsx.size() = "<<ptsx.size()<<endl;
          	for (int i = 0;i < ptsx.size();i++)
          	{
          		//shift car reference angle to 0 degrees
          		//shifting the the car's reference frame makes the math easier

          		double shift_x = ptsx[i]-ref_x;
          		double shift_y = ptsy[i]-ref_y;

          		ptsx[i] = (shift_x *cos(0-ref_yaw)-shift_y*sin(0-ref_yaw));
          		ptsy[i] = (shift_x *sin(0-ref_yaw)+shift_y*cos(0-ref_yaw));
          		
          	}
          	
          	
          	//create spline
          	tk::spline s;
          	//std::cout<<"Test 4"<<endl;
          	//set (x,y) points to the spline
          	
          	s.set_points(ptsx,ptsy);

          	//Define the actual (x,y) points we will use for the planner
          	vector<double> next_x_vals;
          	vector<double> next_y_vals;


          	//std::cout<<"Test 4.5"<<endl;
          	//Start with all of the previous path points from the last time
          	//std::cout<<"previous_path_x.size() = "<<previous_path_x.size()<<endl;
          	for(int i = 0; i < prev_size;i++)
          	{
          		next_x_vals.push_back(previous_path_x[i]);
          		next_y_vals.push_back(previous_path_y[i]);
          		//std::cout<<"Test 4.6"<<" "<<i<<" "<<endl;
          	}

          	//Calculate how to break up spline points so that we travel at our desired reference velocity
          	double target_x = safety_gap;
          	double target_y = s(target_x);//asks spline s what y is given x
          	//std::cout<<"target_y = "<<target_y<<endl;
          	double target_dist = sqrt(target_x*target_x+target_y*target_y);//Pythagorean
          
          	double x_add_on = 0;
          	
          	double target_speed = ref_vel;
          	double curr_speed = ref_vel;
          	double speed_adj = 0.224;
          	
          	//Fill up the rest of the path planner after filling it with prior points. We will always have 50 points
          	//std::cout<<"Test 5"<<endl;
          	double x_point = ptsx[1];
          	double y_point = ptsy[1];
          	for(int i = 1; i <= plan_steps-prev_size; i++)
          	{
          		curr_speed = ref_vel;
          		//below decreases or increases speed at about 5 meters per second squared
          		//std::cout<<"too close = "<<too_close<<endl;
          		if(too_close)
          		{
          			target_speed -= speed_adj; //This works out to about 5 meters per second squared.
          		    //std::cout<<"minus .224"<<endl;
          		}
          		else 
          		{
          		    target_speed += speed_adj;
          		    //std::cout<<"plus .224"<<endl;
          		}
          		if(target_speed > speed_limit)
          		{
          			target_speed = speed_limit;
          		}
          		if(target_speed < 0)
          		{
          			target_speed = 0.0;
          		}
          		if(ref_vel < speed_adj)
          		{
          			ref_vel = speed_adj;
          		}
          		else if((ref_vel + speed_adj) <= target_speed)
          		{
          			ref_vel += speed_adj;
          		}
          		else
          		{
          			ref_vel -=speed_adj;
          		}
          		
          		double N = (target_dist/(0.02*ref_vel/2.24));//2.24 converts mph to mps; N is number of hash marks
          	
          		double x_point = x_add_on+(target_x)/N;
          		double y_point = s(x_point);

          		x_add_on = x_point;

          		
          		//rotate back to normal coordinates after rotating it earlier
          		double un_x = (x_point*cos(ref_yaw) -y_point*sin(ref_yaw));
          		double un_y = (x_point*sin(ref_yaw) +y_point*cos(ref_yaw));

          		

          		next_x_vals.push_back(un_x + ref_x);
          		next_y_vals.push_back(un_y + ref_y);


          	}
          	
          	//std::cout<<"Test 6"<<endl;
          	json msgJson;

          	msgJson["next_x"] = next_x_vals;
          	msgJson["next_y"] = next_y_vals;

          	auto msg = "42[\"control\","+ msgJson.dump()+"]";

          	//this_thread::sleep_for(chrono::milliseconds(1000));
          	ws.send(msg.data(), msg.length(), uWS::OpCode::TEXT);
          
        }
      } else {
        // Manual driving
        std::string msg = "42[\"manual\",{}]";
        ws.send(msg.data(), msg.length(), uWS::OpCode::TEXT);
      }
    }
  });

  // We don't need this since we're not using HTTP but if it's removed the
  // program
  // doesn't compile :-(
  h.onHttpRequest([](uWS::HttpResponse *res, uWS::HttpRequest req, char *data,
                     size_t, size_t) {
    const std::string s = "<h1>Hello world!</h1>";
    if (req.getUrl().valueLength == 1) {
      res->end(s.data(), s.length());
    } else {
      // i guess this should be done more gracefully?
      res->end(nullptr, 0);
    }
  });

  h.onConnection([&h](uWS::WebSocket<uWS::SERVER> ws, uWS::HttpRequest req) {
    std::cout << "Connected!!!" << std::endl;
  });

  h.onDisconnection([&h](uWS::WebSocket<uWS::SERVER> ws, int code,
                         char *message, size_t length) {
    ws.close();
    std::cout << "Disconnected" << std::endl;
  });

  int port = 4567;
  if (h.listen(port)) {
    std::cout << "Listening to port " << port << std::endl;
  } else {
    std::cerr << "Failed to listen to port" << std::endl;
    return -1;
  }
  h.run();
}
