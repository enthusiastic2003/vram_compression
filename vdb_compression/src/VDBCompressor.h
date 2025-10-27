#ifndef VDBCOMPRESSOR_H
#define VDBCOMPRESSOR_H

#include <openvdb/openvdb.h>
#include <string>
#include <vector>

class VDBCompressor {
private:
    struct Brick {
        int x, y, z;
        float minVal, maxVal;
        double similarity;
        
        bool operator<(const Brick& other) const {
            return similarity < other.similarity;
        }
    };

public:
    VDBCompressor();
    openvdb::FloatGrid::Ptr compressVTKVolume(
        const std::string& vtkFilename, 
        float quality = 0.5f, 
        int brickSize = 32,
        int metricType = 3);

private:
    float computeBackgroundValue(const std::vector<float>& data);
    void applyCompressionAlgorithm(
        openvdb::FloatGrid::Ptr grid,
        const std::vector<float>& volumeData,
        int W, int H, int D,
        float background,
        float quality,
        int brickSize,
        int metricType);
    void decomposeIntoBricks(
        std::vector<Brick>& bricks,
        const std::vector<float>& volumeData,
        int W, int H, int D,
        int brickSize,
        float background,
        int metricType);
    void computeBrickRange(
        Brick& brick,
        const std::vector<float>& volumeData,
        int W, int H, int D,
        int brickSize);
    float computeSimilarity(float lo, float hi, float background, int metricType);
    void activateExtremeCorners(
        openvdb::FloatTree& tree,
        const std::vector<float>& volumeData,
        int W, int H, int D);
    void activateBrick(
        openvdb::FloatTree& tree,
        const Brick& brick,
        const std::vector<float>& volumeData,
        int W, int H, int D,
        int brickSize);
};

#endif
