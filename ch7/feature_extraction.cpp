#include <iostream>
#include <opencv2/core/core.hpp>
#include <opencv2/highgui/highgui.hpp>
#include <opencv2/features2d/features2d.hpp>

using namespace std;
using namespace cv;

int main(int argc, char const *argv[])
{
    // Print usage information if wrong usage occured.
    if( argc != 3 )
    {
        cout << "application usage: feature_extraction img1 img2" << endl;
        return 1;
    }

    // Import images
    Mat imag1 = imread( argv[1], CV_LOAD_IMAGE_COLOR );
    Mat imag2 = imread( argv[2], CV_LOAD_IMAGE_COLOR );

    // Initialization
    vector<KeyPoint> keypoints1, keypoints2;
    Mat descriptor1, descriptor2;
    Ptr<FeatureDetector> detector = ORB::create();
    Ptr<DescriptorExtractor> descriptor = ORB::create();
    Ptr<DescriptorMatcher> matcher = DescriptorMatcher::create("BruteForce-Hamming");

    // 1. Detect Oriented FAST corner points
    detector->detect( imag1, keypoints1 );
    detector->detect( imag2, keypoints2 );

    // 2. Compute BRIEF descriptor
    descriptor->compute( imag1, keypoints1, descriptor1 );
    descriptor->compute( imag2, keypoints2, descriptor2 );

    Mat outimag;
    drawKeypoints( imag1, keypoints1, outimag, Scalar::all(-1), DrawMatchesFlags::DEFAULT );
    imshow( "ORB Feature points", outimag );
    // waitKey(0);

    // 3. Pair BRIEF descriptors in two images, using Hamming distance
    vector<DMatch> matches;
    matcher->match( descriptor1, descriptor2, matches );

    // 4. Select pairs
    double min_dist = 10000, max_dist = 0;
    // Find minimum and maximum distance of all pairs
    for(int i = 0; i < descriptor1.rows; i++)
    {
        double dist = matches[i].distance;
        if(dist < min_dist) min_dist = dist;
        if(dist > max_dist) max_dist = dist;
    }
    cout << "-- Max dist = " << max_dist << endl;
    cout << "-- Min dist = " << min_dist << endl;
    // Consider those pairs wrong, whose distance is greater than twice min_dist;
    // but setting a lower limmit, say 30, is necessary, because minimum distance may sometimes be very small
    vector<DMatch> good_matches;
    for(int i = 0; i < descriptor1.rows; i++)
    {
        if (matches[i].distance <= max(2*min_dist, 30.0))
        {
            good_matches.push_back(matches[i]);
        }
        
    }
    
    // 5. Draw pair result
    Mat img_match;
    Mat img_goodMatch;
    drawMatches(imag1, keypoints1, imag2, keypoints2, matches, img_match);
    drawMatches(imag1, keypoints1, imag2, keypoints2, good_matches, img_goodMatch);
    imshow( "All pairs", img_match );
    imshow( "Selected pairs", img_goodMatch );
    waitKey(0);

    return 0;
}
