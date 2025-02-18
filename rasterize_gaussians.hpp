#ifndef RASTERIZE_GAUSSIANS_H
#define RASTERIZE_GAUSSIANS_H

#include <torch/torch.h>
#include "tile_bounds.hpp"

using namespace torch::autograd;

std::tuple<torch::Tensor,
        torch::Tensor,
        torch::Tensor,
        torch::Tensor,
        torch::Tensor> binAndSortGaussians(int numPoints, int numIntersects,
                                            torch::Tensor xys,
                                            torch::Tensor depths,
                                            torch::Tensor radii,
                                            torch::Tensor cumTilesHit,
                                            TileBounds tileBounds);

class RasterizeGaussians : public Function<RasterizeGaussians>{
public:
    static torch::Tensor forward(AutogradContext *ctx, 
            torch::Tensor xys,
            torch::Tensor depths,
            torch::Tensor radii,
            torch::Tensor conics,
            torch::Tensor numTilesHit,
            torch::Tensor colors,
            torch::Tensor opacity,
            int imgHeight,
            int imgWidth,
            torch::Tensor background);
    static tensor_list backward(AutogradContext *ctx, tensor_list grad_outputs);
};

#endif