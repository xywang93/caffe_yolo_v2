#include <algorithm>
#include <fstream>  // NOLINT(readability/streams)
#include <map>
#include <string>
#include <utility>
#include <vector>
#include <utility>

#include "boost/filesystem.hpp"
#include "boost/foreach.hpp"

#include "caffe/layers/region_loss_layer.hpp"
#include "caffe/layers/detection_output_layer.hpp"
//#include "caffe/layers/detection_evaluate_layer.hpp"
#include "caffe/util/io.hpp"
#include "caffe/util/bbox_util.hpp"

namespace caffe {
template <typename Dtype>
void DetectionOutputLayer<Dtype>::LayerSetUp(const vector<Blob<Dtype>*>& bottom,
      const vector<Blob<Dtype>*>& top) {
  const DetectionOutputParameter& detection_output_param =
      this->layer_param_.detection_output_param();
  CHECK(detection_output_param.has_classes()) << "Must specify classes";
  side_ = detection_output_param.side();
  num_classes_ = detection_output_param.classes();
  num_box_ = detection_output_param.num_box();
  coords_ = detection_output_param.coords();
  confidence_threshold_ = detection_output_param.confidence_threshold();
  nms_threshold_ = detection_output_param.nms_threshold();

  for (int c = 0; c < detection_output_param.biases_size(); ++c) {
     biases_.push_back(detection_output_param.biases(c)); 
  } //0.73 0.87;2.42 2.65;4.30 7.04;10.24 4.59;12.68 11.87;

  if (detection_output_param.has_label_map_file())
  {
    string label_map_file = detection_output_param.label_map_file();
    if (label_map_file.empty()) 
    {
      // Ignore saving if there is no label_map_file provided.
      LOG(WARNING) << "Provide label_map_file if output results to files.";
    } 
    else 
    {
      LabelMap label_map;
      CHECK(ReadProtoFromTextFile(label_map_file, &label_map))
          << "Failed to read label map file: " << label_map_file;
      CHECK(MapLabelToName(label_map, true, &label_to_name_))
          << "Failed to convert label to name.";
    }
  }
}

template <typename Dtype>
void DetectionOutputLayer<Dtype>::Reshape(const vector<Blob<Dtype>*>& bottom,
      const vector<Blob<Dtype>*>& top) {

  CHECK_EQ(bottom[0]->num(), 1);
  // num() and channels() are 1.
  vector<int> top_shape(2, 1);
  // Since the number of bboxes to be kept is unknown before nms, we manually
  // set it to (fake) 1.
  top_shape.push_back(1);
  // Each row is a 7 dimension vector, which stores
  // [image_id, label, confidence, x, y, w, h]
  top_shape.push_back(7);
  top[0]->Reshape(top_shape);
}

template <typename Dtype>
void DetectionOutputLayer<Dtype>::Forward_cpu(
    const vector<Blob<Dtype>*>& bottom, const vector<Blob<Dtype>*>& top) {
  const int num = bottom[0]->num();

  Blob<Dtype> swap;
  swap.Reshape(bottom[0]->num(), bottom[0]->height()*bottom[0]->width(), num_box_, bottom[0]->channels() / num_box_);
  //std::cout<<"4"<<std::endl;  
  Dtype* swap_data = swap.mutable_cpu_data();
  int index = 0;
  for (int b = 0; b < bottom[0]->num(); ++b)
    for (int h = 0; h < bottom[0]->height(); ++h)
      for (int w = 0; w < bottom[0]->width(); ++w)
        for (int c = 0; c < bottom[0]->channels(); ++c)
        {
          swap_data[index++] = bottom[0]->data_at(b,c,h,w);	
        }
    
    //CHECK_EQ(bottom[0]->data_at(0,4,1,2),swap.data_at(0,15,0,4));
    //std::cout<<"5"<<std::endl;
    //*********************************************************Activation********************************************************//
    //disp(swap);
  vector< PredictionResult<Dtype> > results;
  
  for (int b = 0; b < swap.num(); ++b){
	vector< PredictionResult<Dtype> > predicts;
    PredictionResult<Dtype> predict;
	predicts.clear(); 
    for (int j = 0; j < side_; ++j)
      for (int i = 0; i < side_; ++i)
        for (int n = 0; n < num_box_; ++n){
          int index = b * swap.channels() * swap.height() * swap.width() + (j * side_ + i) * swap.height() * swap.width() + n * swap.width();
          CHECK_EQ(swap_data[index],swap.data_at(b, j * side_ + i, n, 0));          
          predict.objScore = sigmoid(swap_data[index+4]);
          class_index_and_score(swap_data+index+5, num_classes_, predict);
          predict.confidence = predict.objScore * predict.classScore;
          if (predict.confidence >= confidence_threshold_){
 			get_region_box(swap_data, predict, biases_, n, index, i, j, side_, side_);
			predict.batchID = b;
            predicts.push_back(predict);
          }
        }
    vector<int> idxes;
    int num_kept = 0;
    if(predicts.size() > 0){
      ApplyNms(predicts, idxes, nms_threshold_);
      num_kept = idxes.size();
	  for (int i = 0; i < num_kept; i++){
	  	results.push_back(predicts[idxes[i]]);
      }    
    }
/*
    for(int p = 0; p < predicts.size(); p++)
    {
    	LOG(INFO) << "predict " << p << ": classType=" <<predicts[p].classType <<" confidence="<< predicts[p].confidence;
    }
    for(int p = 0; p < idxes.size(); p++)
    {
    	LOG(INFO) << "kept :" << p;
    }
  */
  vector<int> top_shape(2, 1);
  top_shape.push_back(results.size());
  top_shape.push_back(7);

  Dtype* top_data;
  if (results.size() == 0) {
    LOG(INFO) << "Couldn't find any detections";
    top_shape[2] = swap.num();
    top[0]->Reshape(top_shape);
    top_data = top[0]->mutable_cpu_data();
    caffe_set<Dtype>(top[0]->count(), -1, top_data);
    // Generate fake results per image.
    for (int i = 0; i < num; ++i) {
      top_data[0] = i;
      top_data += 7;
    }
  } else {
    top[0]->Reshape(top_shape);
    top_data = top[0]->mutable_cpu_data();
    for (int i = 0; i < results.size(); i++){
	  PredictionResult<Dtype> & predict = results[i];
      top_data[i*7] = predict.batchID;      //Image_Id
      top_data[i*7+1] = predict.classType;  //label
      top_data[i*7+2] = predict.confidence; //confidence
      top_data[i*7+3] = predict.x;          
      top_data[i*7+4] = predict.y;
      top_data[i*7+5] = predict.w;
      top_data[i*7+6] = predict.h;
    }
  }
  }
}

#ifdef CPU_ONLY
//STUB_GPU_FORWARD(DetectionOutputLayer, Forward);
#endif

INSTANTIATE_CLASS(DetectionOutputLayer);
REGISTER_LAYER_CLASS(DetectionOutput);

}  // namespace caffe
