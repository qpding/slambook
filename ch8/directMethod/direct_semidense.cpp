#include <iostream>
#include <fstream>
#include <vector>
#include <chrono>

#include <opencv2/core/core.hpp>
#include <opencv2/highgui/highgui.hpp>
#include <opencv2/imgproc/imgproc.hpp>
#include <opencv2/features2d/features2d.hpp>

#include <g2o/core/base_unary_edge.h>
#include <g2o/types/sba/types_six_dof_expmap.h>
#include <g2o/core/block_solver.h>
#include <g2o/solvers/dense/linear_solver_dense.h>
#include <g2o/core/optimization_algorithm_levenberg.h>

using namespace std;
using namespace g2o;

#define DEBUG

class EdgeSE3ProjectDirect: public BaseUnaryEdge< 1, double, VertexSE3Expmap >
{
public:
    EIGEN_MAKE_ALIGNED_OPERATOR_NEW

    EdgeSE3ProjectDirect() {}

    EdgeSE3ProjectDirect( Eigen::Vector3d point, float fx, float fy, float cx, float cy, cv::Mat* image )
        : x_world_(point), fx_(fx), fy_(fy), cx_(cx), cy_(cy), image_(image) {}

    virtual void computeError()
    {
        const VertexSE3Expmap* v = static_cast<const VertexSE3Expmap*>( _vertices[0] );
        Eigen::Vector3d x_local = v->estimate().map( x_world_ );
        float x = x_local[0] * fx_ / x_local[2] + cx_;
        float y = x_local[1] * fy_ / x_local[2] + cy_;
        // Check if x, y is in the image
        if( (x-4) < 0 || (x+4) > image_->cols || (y-4) < 0 || (y+4) > image_->rows )
        {
            _error( 0, 0 ) = 0.0;
            this->setLevel(1);
        }
        else
        {
            _error( 0, 0 ) = getPixelValue( x, y ) - _measurement;
        }
    }

    virtual void linearizeOplus()
    {
        if(level() == 1)
        {
            _jacobianOplusXi = Eigen::Matrix<double, 1, 6>::Zero();
            return;
        }
        VertexSE3Expmap* vtx = static_cast<VertexSE3Expmap*>( _vertices[0] );
        Eigen::Vector3d xyz_trans = vtx->estimate().map( x_world_ );

        double x = xyz_trans[0];
        double y = xyz_trans[1];
        double invz = 1.0 / xyz_trans[2];
        double invz_2 = invz * invz;

        float u = x * fx_ * invz + cx_;
        float v = y * fy_ * invz + cy_;

        // Jacobian from se3 to u, v
        Eigen::Matrix<double, 2, 6> jacobian_uv_ksai;

        jacobian_uv_ksai( 0, 0 ) = -fx_ * x * y * invz_2;
        jacobian_uv_ksai( 0, 1 ) = fx_ + fx_ * x * x * invz_2;
        jacobian_uv_ksai( 0, 2 ) = -fx_ * y * invz;
        jacobian_uv_ksai( 0, 3 ) = fx_ * invz;
        jacobian_uv_ksai( 0, 4 ) = 0;
        jacobian_uv_ksai( 0, 5 ) = -fx_ * x * invz_2;

        jacobian_uv_ksai( 1, 0 ) = -fy_ - fy_ * y * y * invz_2;
        jacobian_uv_ksai( 1, 1 ) = fy_ * x * y * invz_2;
        jacobian_uv_ksai( 1, 2 ) = fy_ * x * invz;
        jacobian_uv_ksai( 1, 3 ) = 0;
        jacobian_uv_ksai( 1, 4 ) = fy_ * invz;
        jacobian_uv_ksai( 1, 5 ) = -fy_ * y * invz_2;

        Eigen::Matrix<double, 1, 2> jacobian_pixel_uv;

        jacobian_pixel_uv( 0, 0 ) = ( getPixelValue( u+1, v ) - getPixelValue( u-1, v ) ) / 2;
        jacobian_pixel_uv( 0, 1 ) = ( getPixelValue( u, v+1 ) - getPixelValue( u, v-1 ) ) / 2;

        _jacobianOplusXi = jacobian_pixel_uv * jacobian_uv_ksai;
    }

    // Dummy read and write functions
    virtual bool read ( std::istream& in ) {}
    virtual bool write ( std::ostream& out ) const {}

protected:
    // Get a gray scale value from reference image (bilinear interpolated)
    inline float getPixelValue( float x, float y )
    {
        uchar* data = &image_->data[ int(y) * image_->step + int(x) ];
        float xx = x - floor(x);
        float yy = y - floor(y);
        return float( (1-xx) * (1-yy) * data[0] +
                      xx * (1-yy) * data[1] +
                      (1-xx) * yy * data[image_->step] +
                      xx * yy * data[image_->step + 1] );
    }

public:
    Eigen::Vector3d x_world_;    // 3D point in world coordinate
    float fx_ = 0, fy_ = 0, cx_ = 0, cy_ = 0;   // Camera intrinsics
    cv::Mat* image_ = nullptr;  // reference image
};

struct Measurement
{
    Measurement( Eigen::Vector3d p, float g ): pos_world(p), grayscale(g) {}
    Eigen::Vector3d pos_world;
    float grayscale;
};

Eigen::Vector3d project2Dto3D( int x, int y, int d, float fx, float fy, float cx, float cy, float scale )
{
    float zz = float(d) / scale;
    float xx = zz * ( x - cx ) / fx;
    float yy = zz * ( y - cy ) / fy;
    return Eigen::Vector3d( xx, yy, zz );
}

Eigen::Vector2d project3Dto2D ( float x, float y, float z, float fx, float fy, float cx, float cy )
{
    float u = fx*x/z+cx;
    float v = fy*y/z+cy;
    return Eigen::Vector2d ( u,v );
}

bool poseEstimationDirect( const vector<Measurement>& measurements, cv::Mat* gray, Eigen::Matrix3f& K, Eigen::Isometry3d& Tcw )
{
    // Initialize g2o
    typedef g2o::BlockSolver<g2o::BlockSolverTraits<6, 1>> DirectBlock;
    DirectBlock::LinearSolverType* linearSolver = new g2o::LinearSolverDense<DirectBlock::PoseMatrixType>();
    DirectBlock* solver_ptr = new DirectBlock( linearSolver );
    g2o::OptimizationAlgorithmLevenberg* solver = new g2o::OptimizationAlgorithmLevenberg( solver_ptr );
    g2o::SparseOptimizer optimizer;
    optimizer.setAlgorithm( solver );
    optimizer.setVerbose( true );

    g2o::VertexSE3Expmap* pose = new g2o::VertexSE3Expmap();
    pose->setEstimate( g2o::SE3Quat( Tcw.rotation(), Tcw.translation() ) );
    pose->setId( 0 );
    optimizer.addVertex( pose );

    // Add edges
    int id = 1;
    for( Measurement m: measurements )
    {
        EdgeSE3ProjectDirect* edge = new EdgeSE3ProjectDirect( m.pos_world,
                                                               K(0, 0), K(1, 1), K(0, 2), K(1, 2),
                                                               gray );
        edge->setVertex( 0, pose );
        edge->setMeasurement( m.grayscale );
        edge->setInformation( Eigen::Matrix<double, 1, 1>::Identity() );
        edge->setId( id++ );
        optimizer.addEdge( edge );
    }

    // cout << "Edges in graph: " << optimizer.edges().size() << endl;
    optimizer.initializeOptimization();
    optimizer.optimize( 30 );
    Tcw = pose->estimate();
    
    return true;
}

int main(int argc, char const *argv[])
{
    if ( argc != 2 )
    {
        cout<<"usage: direct_sparse path_to_dataset"<<endl;
        return 1;
    }
    srand ( ( unsigned int ) time ( 0 ) );
    string path_to_dataset = argv[1];
    string associate_file = path_to_dataset + "/associate.txt";
    
    ifstream fin( associate_file );
    if ( !fin ) 
    {
        cerr << "Couldn't find associate.txt" << endl;
        return 1;
    }
    
    string rgb_file, depth_file, time_rgb, time_depth;
    cv::Mat color, depth, gray;
    cv::Mat prev_color;
    vector<Measurement> measurements;

    // Camera intrinsics
    float cx = 325.5;
    float cy = 253.5;
    float fx = 518.0;
    float fy = 519.0;
    float depth_scale = 1000.0;
    Eigen::Matrix3f K;
    K << fx, 0.0, cx, 0.0, fy, cy, 0.0, 0.0, 1.0;

    Eigen::Isometry3d Tcw = Eigen::Isometry3d::Identity();

    vector<cv::Point> eular_x;
    vector<cv::Point> eular_y;
    vector<cv::Point> eular_z;

    // Use the first frame as reference image, and implement direct method to the following images 
    for(int index = 0; index < 600; index++)
    {
        cout << "--> Loop " << index << endl;
        fin >> time_rgb >> rgb_file >> time_depth >> depth_file;
        color = cv::imread( path_to_dataset + "/" + rgb_file );
        depth = cv::imread( path_to_dataset + "/" + depth_file, cv::IMREAD_UNCHANGED );
        if( color.data == nullptr || depth.data == nullptr )
            continue;
        cv::cvtColor( color, gray, cv::COLOR_BGR2GRAY );
        if( index == 0 )
        {
            // Select pixels with high gradients instead of extracting FAST keypoints
            for( int x = 10; x < gray.cols-10; x++ )
            {
                for( int y = 10; y < gray.rows-10; y++ )
                {
                    Eigen::Vector2d delta( gray.ptr<uchar>(y)[x+1] - gray.ptr<uchar>(y)[x-1],
                                           gray.ptr<uchar>(y+1)[x] - gray.ptr<uchar>(y-1)[x] );
                    if( delta.norm() < 50 )
                        continue;
                    ushort d = depth.ptr<ushort>( cvRound( y ) )[ cvRound( x ) ];
                    if( d == 0 )
                        continue;
                    Eigen::Vector3d p3d = project2Dto3D( x, y, d, fx, fy, cx, cy, depth_scale );
                    float grayscale = float( gray.ptr<uchar>( cvRound( y ) )[ cvRound( x ) ] );
                    measurements.push_back( Measurement( p3d, grayscale ) );
                }
            }
            prev_color = color;
            continue;
        }

        // Use direct method to compute camera movement
#ifdef DEBUG
        chrono::steady_clock::time_point t1 = chrono::steady_clock::now();
#endif
        poseEstimationDirect( measurements, &gray, K, Tcw );
#ifdef DEBUG
        chrono::steady_clock::time_point t2 = chrono::steady_clock::now();
        chrono::duration<double> time_used = chrono::duration_cast<chrono::duration<double>>( t2 - t1 );
        cout << "Time consumption: " << time_used.count() * 1000 << "ms" << endl;
        cout << "Tcw = " << Tcw.matrix() << endl;
#endif

        // Plot feature points
        cv::Mat img_show( color.rows * 2, color.cols, CV_8UC3 );
        prev_color.copyTo ( img_show ( cv::Rect ( 0,0,color.cols, color.rows ) ) );
        color.copyTo ( img_show ( cv::Rect ( 0,color.rows,color.cols, color.rows ) ) );

        for( auto m: measurements )
        {
            if ( rand() > RAND_MAX/5 )
                continue;
            Eigen::Vector3d p = m.pos_world;
            Eigen::Vector2d pixel_prev = project3Dto2D ( p ( 0,0 ), p ( 1,0 ), p ( 2,0 ), fx, fy, cx, cy );
            Eigen::Vector3d p2 = Tcw*m.pos_world;
            Eigen::Vector2d pixel_now = project3Dto2D ( p2 ( 0,0 ), p2 ( 1,0 ), p2 ( 2,0 ), fx, fy, cx, cy );
            if ( pixel_now(0,0)<0 || pixel_now(0,0)>=color.cols || pixel_now(1,0)<0 || pixel_now(1,0)>=color.rows )
                continue;
            
            cv::circle ( img_show, cv::Point2d ( pixel_prev ( 0,0 ), pixel_prev ( 1,0 ) ), 4, cv::Scalar ( 0,0,255 ), 2 );
            cv::circle ( img_show, cv::Point2d ( pixel_now ( 0,0 ), pixel_now ( 1,0 ) +color.rows ), 4, cv::Scalar ( 0,0,255 ), 2 );
            // cv::line ( img_show, cv::Point2d ( pixel_prev ( 0,0 ), pixel_prev ( 1,0 ) ), cv::Point2d ( pixel_now ( 0,0 ), pixel_now ( 1,0 ) +color.rows ), cv::Scalar ( 0,255,0 ), 1 );
        }
        
        Eigen::Matrix4d T = Tcw.matrix();
        Eigen::Vector3d eular_angle = T.block<3, 3>(0, 0).eulerAngles(0, 1, 2);
        Eigen::Vector3d trasnlation = T.block<3, 1>(0, 3);
        // cout << "-->eular angle"  << endl << eular_angle << endl;
        // cout << "-->trasnlation"  << endl << trasnlation << endl;

        eular_x.push_back( cv::Point( 2*index, eular_angle(0, 0) * 120.0 ) );
        eular_y.push_back( cv::Point( 2*index, eular_angle(1, 0) * 120.0 ) );
        eular_z.push_back( cv::Point( 2*index, eular_angle(2, 0) * 120.0 ) );

        cv::imshow ( "result", img_show );
#ifndef DEBUG
        cv::waitKey ( 500 );
#else
        cv::waitKey ( 0 );
#endif
    }

    cout << "-->eular angle x"  << endl;
    cv::Mat eularAngleImage( color.rows, 610 * 2, CV_8UC3 );
    eularAngleImage.setTo(cv::Scalar(100, 0, 0));
    cv::polylines( eularAngleImage, eular_x, false, cv::Scalar(0, 255, 0), 1, 8, 0 );
    cv::polylines( eularAngleImage, eular_y, false, cv::Scalar(255, 0, 0), 1, 8, 0 );
    cv::polylines( eularAngleImage, eular_z, false, cv::Scalar(0, 0, 255), 1, 8, 0 );
    cv::imshow ( "x", eularAngleImage );
    cv::waitKey ( 0 );

    return 0;
}



