#ifndef FRAME_H
#define FRAME_H

#include "myslam/common_include.h"
#include "myslam/camera.h"

namespace myslam
{

class Frame
{
public:
    Frame();

    Frame( long id, double time_stamp = 0, 
           SE3 T_c_w = SE3(), Camera::Ptr camera = nullptr,
           Mat color = Mat(), Mat depth = Mat() );
    
    // Factory function
    static Frame::Ptr createFrame();

    double findDepth( const cv::KeyPoint kp );

    Vector3d getCamCenter() const;

    bool isInFrame( const Vector3d& pt_world );

public:
    typedef std::shared_ptr<Frame> Ptr;
    unsigned long                  id_;
    double                         time_stamp_;
    SE3                            T_c_w_;
    Camera::Ptr                    camera_;
    Mat                            color_, depth_;
};

}

#endif // FRAME_H