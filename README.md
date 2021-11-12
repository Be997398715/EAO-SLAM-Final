# EAO-SLAM-Improve-Final-Poject
# This is my final improve for EAO-SLAM, full project can be found at https://pan.baidu.com/s/1Zgat7FRjKEi7cbN3QDtqbA, password：z3ku .

# 1> Reference: 
                  https://github.com/yanmin-wu/EAO-SLAM 
                  https://github.com/Be997398715/EAO-SLAM-Improve 
              
# 2> Done:  
            1. Opencv4 Support 
            2. YOLO Model support for online 
            3. Fix some bugs 
            4. Add 2d-object-tracker for data association[for Mono mode] 
            5. Add RGB-D mode for pointcloud viewer and octomap 
            6. Other TUM Sequences support 
            7. Yolact segment for better object's pose initial(in fact it's not good because unstable segmentation) 
            8. Optimization for Point, Object and Camera learn from CubeSLAM 
            9. Add Yolact results for semantic pointcloud map 

# 3> To Do: 
            None
            
          
# 4> Results: 
            Only compare with data association here: 
            1. EAO results with track 
            ![EAO-Only](https://github.com/Be997398715/EAO-SLAM-Improve/blob/v1.0/EAO-SLAM-master-improve/figures/EAO-only.png) EAO-Only 
            ![EAO-With-Track](https://github.com/Be997398715/EAO-SLAM-Improve/blob/v1.0/EAO-SLAM-master-improve/figures/eao-with-track.png) EAO-With-Track 
            2. IOU results with track 
            ![Iou-With-Track](https://github.com/Be997398715/EAO-SLAM-Improve/blob/v1.0/EAO-SLAM-master-improve/figures/iou-with-track.png) Iou-With-Track  
            3. FULL results with optimization 
            ![Full-Only](https://github.com/Be997398715/EAO-SLAM-Improve/blob/v1.0/EAO-SLAM-master-improve/figures/full.png) Full-Only
            ![Full-With-Track](https://github.com/Be997398715/EAO-SLAM-Improve/blob/v1.0/EAO-SLAM-master-improve/figures/full-with-track.png) Full-With-Track 
            ![Full-with-Track-Optimization](https://github.com/Be997398715/EAO-SLAM-Final/blob/main/figures/full-with-optimize.jpg) Full-with-Track-Optimization 
            4. Segment Result
            ![Yolact-Segment](https://github.com/Be997398715/EAO-SLAM-Final/blob/main/figures/segmask.jpg) Yolact-Segment 
            


# 5> Usage: 
            1. for mono: build/mono_tum Full data/rgbd_dataset_freiburg3_long_office_household/ Vocabulary/ORBvoc.bin Examples/Monocular/TUM3.yaml online  
            2. for rgbd: build/rgbd_tum Vocabulary/ORBvoc.bin Examples/RGB-D/TUM3.yaml data/rgbd_dataset_freiburg3_long_office_household/ Examples/RGB-D/associations.txt Full online 
            *notes:you need download yolov3.weights and data and yolact_base_54_800000.onnx before running program.

          
          
