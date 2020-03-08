#include <ros/ros.h>

#include <sensor_msgs/Image.h>
#include <vision_msgs/Classification2D.h>
#include <vision_msgs/VisionInfo.h>

#include <jetson-inference/imageNet.h>
#include <jetson-utils/cudaMappedMemory.h>

#include "image_converter.h"

#include <unordered_map>

#include "iostream"
#include "stdio.h"
#include "stdlib.h"
#include "vector"
#include "string"
#include "sqlite3.h"

// globals
imageNet* 	 net = NULL;
imageConverter* cvt = NULL;

ros::Publisher* classify_pub = NULL;

vision_msgs::VisionInfo info_msg;


// callback triggered when a new subscriber connected to vision_info topic
void info_connect( const ros::SingleSubscriberPublisher& pub )
{
	ROS_INFO("new subscriber '%s' connected to vision_info topic '%s', sending VisionInfo msg", pub.getSubscriberName().c_str(), pub.getTopic().c_str());
	pub.publish(info_msg);
}

//callback triggered when wanting to update sql database
static int update_sql_callback(void *data, int argc, char **argv, char **azColName){
   int i;
   fprintf(stderr, "%s: ", (const char*)data);
   
   for(i = 0; i<argc; i++) {
      printf("%s = %s\n", azColName[i], argv[i] ? argv[i] : "NULL");
   }
   printf("\n");
   return 0;
}

// callback triggered when recieved a new image on input topic
void img_callback( const sensor_msgs::ImageConstPtr& input )
{
	// convert the image to reside on GPU
	if( !cvt || !cvt->Convert(input) )
	{
		ROS_INFO("failed to convert %ux%u %s image", input->width, input->height, input->encoding.c_str());
		return;	
	}

	// classify the image
	float confidence = 0.0f;
	const int img_class = net->Classify(cvt->ImageGPU(), cvt->GetWidth(), cvt->GetHeight(), &confidence);
	

	// verify the output	
	if( img_class >= 0 )
	{
		ROS_INFO("classified image, %f %s (class=%i)", confidence, net->GetClassDesc(img_class), img_class);
		
        /* Create SQL statement */
        if(confidence >= 50){
            //sqlite processing
            sqlite3 *db;
            char *zErrMsg = 0;
            int rc;
            std::string sql;
            const char* data = "Callback function called";
            
            
            sql = "UPDATE recognized_objects SET Confidence = " +std::to_string(confidence) + " WHERE ID= " std::to_string(img_class);

            rc = sqlite3_open("oro_vision_db.db", &db);

            if( rc ) {
                fprintf(stderr, "Can't open database: %s\n", sqlite3_errmsg(db));
                return(0);
            } else {
                fprintf(stderr, "Opened database successfully\n");
            }
            rc = sqlite3_exec(db, sql.c_str(), callback, (void*)data, &zErrMsg);
            if( rc != SQLITE_OK ) {
                fprintf(stderr, "SQL error: %s\n", zErrMsg);
                sqlite3_free(zErrMsg);
            } else {
                fprintf(stdout, "Operation done successfully\n");
            }
            sqlite3_close(db);
        }


		// create the classification message
		vision_msgs::Classification2D msg;
		vision_msgs::ObjectHypothesis obj;

		obj.id    = img_class;
		obj.score = confidence;

		msg.results.push_back(obj);	// TODO optionally add source image to msg
	
		// publish the classification message
		classify_pub->publish(msg);
	}
	else
	{
		// an error occurred if the output class is < 0
		ROS_ERROR("failed to classify %ux%u image", input->width, input->height);
	}
}


// node main loop
int main(int argc, char **argv)
{
	ros::init(argc, argv, "imagenet");
 
	ros::NodeHandle nh;
	ros::NodeHandle private_nh("~");

	/*
	 * retrieve parameters
	 */
	std::string class_labels_path;
	std::string prototxt_path;
	std::string model_path;
	std::string model_name;

	bool use_model_name = false;

	// determine if custom model paths were specified
	if( !private_nh.getParam("prototxt_path", prototxt_path) ||
	    !private_nh.getParam("model_path", model_path) ||
	    !private_nh.getParam("class_labels_path", class_labels_path) )
	{
		// without custom model, use one of the built-in pretrained models
		private_nh.param<std::string>("model_name", model_name, "googlenet");
		use_model_name = true;
	}

	
	/*
	 * load image recognition network
	 */
	if( use_model_name )
	{
		// determine which built-in model was requested
		imageNet::NetworkType model = imageNet::NetworkTypeFromStr(model_name.c_str());

		if( model == imageNet::CUSTOM )
		{
			ROS_ERROR("invalid built-in pretrained model name '%s', defaulting to googlenet", model_name.c_str());
			model = imageNet::GOOGLENET;
		}

		// create network using the built-in model
		net = imageNet::Create(model);
	}
	else
	{
		// create network using custom model paths
		net = imageNet::Create(prototxt_path.c_str(), model_path.c_str(), NULL, class_labels_path.c_str());
	}

	if( !net )
	{
		ROS_ERROR("failed to load imageNet model");
		return 0;
	}


	/*
	 * create the class labels parameter vector
	 */
	std::hash<std::string> model_hasher;  // hash the model path to avoid collisions on the param server
	std::string model_hash_str = std::string(net->GetModelPath()) + std::string(net->GetClassPath());
	const size_t model_hash = model_hasher(model_hash_str);
	
	ROS_INFO("model hash => %zu", model_hash);
	ROS_INFO("hash string => %s", model_hash_str.c_str());

	// obtain the list of class descriptions
	std::vector<std::string> class_descriptions;
	const uint32_t num_classes = net->GetNumClasses();

	for( uint32_t n=0; n < num_classes; n++ )
		class_descriptions.push_back(net->GetClassDesc(n));

	// create the key on the param server
	std::string class_key = std::string("class_labels_") + std::to_string(model_hash);
	private_nh.setParam(class_key, class_descriptions);
		
	// populate the vision info msg
	std::string node_namespace = private_nh.getNamespace();
	ROS_INFO("node namespace => %s", node_namespace.c_str());

	info_msg.database_location = node_namespace + std::string("/") + class_key;
	info_msg.database_version  = 0;
	info_msg.method 		  = net->GetModelPath();
	
	ROS_INFO("class labels => %s", info_msg.database_location.c_str());


	/*
	 * create an image converter object
	 */
	cvt = new imageConverter();
	
	if( !cvt )
	{
		ROS_ERROR("failed to create imageConverter object");
		return 0;
	}


	/*
	 * advertise publisher topics
	 */
	ros::Publisher pub = private_nh.advertise<vision_msgs::Classification2D>("classification", 5);
	classify_pub = &pub; // we need to publish from the subscriber callback

	// the vision info topic only publishes upon a new connection
	ros::Publisher info_pub = private_nh.advertise<vision_msgs::VisionInfo>("vision_info", 1, (ros::SubscriberStatusCallback)info_connect);


	/*
	 * subscribe to image topic
	 */
	//image_transport::ImageTransport it(nh);	// BUG - stack smashing on TX2?
	//image_transport::Subscriber img_sub = it.subscribe("image", 1, img_callback);
	ros::Subscriber img_sub = private_nh.subscribe("image_in", 5, img_callback);
	

	/*
	 * wait for messages
	 */
	ROS_INFO("imagenet node initialized, waiting for messages");

	ros::spin();

	return 0;
}
