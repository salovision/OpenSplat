#include "rasterize_gaussians.hpp"
#include "vendor/gsplat/bindings.h"
#include "vendor/gsplat/config.h"

std::tuple<torch::Tensor,
        torch::Tensor,
        torch::Tensor,
        torch::Tensor,
        torch::Tensor> binAndSortGaussians(int numPoints, int numIntersects,
                                            torch::Tensor xys,
                                            torch::Tensor depths,
                                            torch::Tensor radii,
                                            torch::Tensor cumTilesHit,
                                            TileBounds tileBounds){
    auto t = map_gaussian_to_intersects_tensor(numPoints, numIntersects, 
                                        xys, depths, radii, cumTilesHit, tileBounds);
    
    // unique IDs for each gaussian in the form (tile | depth id)
    torch::Tensor isectIds = std::get<0>(t);

    // Tensor that maps isect_ids back to cumHitTiles
    torch::Tensor gaussianIds = std::get<1>(t);
    
    auto sorted = torch::sort(isectIds);

    // sorted unique IDs for each gaussian in the form (tile | depth id)
    torch::Tensor isectIdsSorted = std::get<0>(sorted);
    torch::Tensor sortedIndices = std::get<1>(sorted);

    // sorted Tensor that maps isect_ids back to cumHitTiles
    torch::Tensor gaussianIdsSorted = torch::gather(gaussianIds, 0, sortedIndices);

    // range of gaussians hit per tile
    torch::Tensor tileBins = get_tile_bin_edges_tensor(numIntersects, isectIdsSorted);
    return std::make_tuple(isectIds, gaussianIds, isectIdsSorted, gaussianIdsSorted, tileBins);
}

torch::Tensor RasterizeGaussians::forward(AutogradContext *ctx, 
            torch::Tensor xys,
            torch::Tensor depths,
            torch::Tensor radii,
            torch::Tensor conics,
            torch::Tensor numTilesHit,
            torch::Tensor colors,
            torch::Tensor opacity,
            int imgHeight,
            int imgWidth,
            torch::Tensor background
        ){
    
    int numPoints = xys.size(0);

    TileBounds tileBounds = std::make_tuple(
        (imgWidth + BLOCK_X - 1) / BLOCK_X,
        (imgHeight + BLOCK_Y - 1) / BLOCK_Y,
        1
    );
    std::tuple<int, int, int> block = std::make_tuple(BLOCK_X, BLOCK_Y, 1);
    std::tuple<int, int, int> imgSize = std::make_tuple(imgWidth, imgHeight, 1);
    
    torch::Tensor cumTilesHit = torch::cumsum(numTilesHit, 0, torch::kInt32);
    int numIntersects = cumTilesHit[cumTilesHit.size(0) - 1].item<int>();

    auto b = binAndSortGaussians(numPoints, numIntersects, xys, depths, radii, cumTilesHit, tileBounds);
    torch::Tensor gaussianIdsSorted = std::get<3>(b);
    torch::Tensor tileBins = std::get<4>(b);

    auto t = rasterize_forward_tensor(tileBounds, block, imgSize, 
                            gaussianIdsSorted,
                            tileBins,
                            xys,
                            conics,
                            colors,
                            opacity,
                            background);
    // Final image
    torch::Tensor outImg = std::get<0>(t);

    // Map of alpha-inverse (1 - finalTs = alpha)
    torch::Tensor finalTs = std::get<1>(t);

    // Map of tile bin IDs
    torch::Tensor finalIdx = std::get<2>(t);

    ctx->saved_data["imgWidth"] = imgWidth;
    ctx->saved_data["imgHeight"] = imgHeight;
    
    ctx->save_for_backward({ gaussianIdsSorted, tileBins, xys, conics, colors, opacity, background, finalTs, finalIdx });
    
    return outImg;
}

tensor_list RasterizeGaussians::backward(AutogradContext *ctx, tensor_list grad_outputs) {
    torch::Tensor v_outImg = grad_outputs[0];
    int imgHeight = ctx->saved_data["imgHeight"].toInt();
    int imgWidth = ctx->saved_data["imgWidth"].toInt();

    variable_list saved = ctx->get_saved_variables();
    torch::Tensor gaussianIdsSorted = saved[0];
    torch::Tensor tileBins = saved[1];
    torch::Tensor xys = saved[2];
    torch::Tensor conics = saved[3];
    torch::Tensor colors = saved[4];
    torch::Tensor opacity = saved[5];
    torch::Tensor background = saved[6];
    torch::Tensor finalTs = saved[7];
    torch::Tensor finalIdx = saved[8];

    // torch::Tensor v_outAlpha = torch::zeros({imgHeight, imgWidth}, torch::TensorOptions().device(v_outImg.get_device());
    torch::Tensor v_outAlpha = torch::zeros_like(v_outImg.index({"...", 0}));
    
    auto t = rasterize_backward_tensor(imgHeight, imgWidth, 
                            gaussianIdsSorted,
                            tileBins,
                            xys,
                            conics,
                            colors,
                            opacity,
                            background,
                            finalTs,
                            finalIdx,
                            v_outImg,
                            v_outAlpha);

    torch::Tensor v_xy = std::get<0>(t);
    torch::Tensor v_conic = std::get<1>(t);
    torch::Tensor v_colors = std::get<2>(t);
    torch::Tensor v_opacity = std::get<3>(t);
    torch::Tensor none;

    return { v_xy,
            none, // depths
            none, // radii
            v_conic,
            none, // numTilesHit
            v_colors,
            v_opacity,
            none, // imgHeight
            none, // imgWidth
            none // background
    };
}