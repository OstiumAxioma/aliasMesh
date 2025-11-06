// src/main.cpp
#include <vtkSmartPointer.h>
#include <vtkNIFTIImageReader.h>
#include <vtkImageData.h>

#include <iostream>
#include <string>

#include "MaskReMesh.h"

// 内部全局参数：默认的 NIfTI 掩码路径 & 导出 STL 路径
namespace
{
    // TODO: 改成你自己的掩码 nifti 路径
    const std::string kInputNiftiPath  = "D:/Project/TMS_Tag/aliasMesh/mask/BN_Atlas_246_1mm.nii.gz";
    // TODO: 改成你自己的导出 stl 路径
    const std::string kOutputStlPath   = "D:/Project/TMS_Tag/aliasMesh/result/116.vtp";
}

int main(int argc, char* argv[])
{
    std::string inputPath  = kInputNiftiPath;
    std::string outputPath = kOutputStlPath;

    // 允许用命令行参数覆盖全局路径：exe input.nii.gz output.stl
    if (argc > 1) {
        inputPath = argv[1];
    }
    if (argc > 2) {
        outputPath = argv[2];
    }

    std::cout << "Reading NIfTI mask: " << inputPath << std::endl;

    auto reader = vtkSmartPointer<vtkNIFTIImageReader>::New();
    reader->SetFileName(inputPath.c_str());
    reader->Update();

    vtkImageData* image = reader->GetOutput();
    if (!image) {
        std::cerr << "Failed to read NIfTI image." << std::endl;
        return EXIT_FAILURE;
    }

    int dims[3];
    double spacing[3];
    double origin[3];
    image->GetDimensions(dims);
    image->GetSpacing(spacing);
    image->GetOrigin(origin);

    std::cout << "Image dims: "
              << dims[0] << " x " << dims[1] << " x " << dims[2] << std::endl;
    std::cout << "Spacing   : "
              << spacing[0] << ", " << spacing[1] << ", " << spacing[2] << std::endl;
    std::cout << "Origin    : "
              << origin[0] << ", " << origin[1] << ", " << origin[2] << std::endl;

    MaskReMesh remesher;
    remesher.BuildFromMask(image);

    // if (!remesher.ExportToStl(outputPath)) {
    //     std::cerr << "Failed to export STL: " << outputPath << std::endl;
    //     return EXIT_FAILURE;
    // }

    if (!remesher.ExportToVTP(outputPath))
    {
        std::cerr << "Failed to export VTP: " << outputPath << std::endl;
        return EXIT_FAILURE;
    }

    std::cout << "Exported VTP: " << outputPath << std::endl;
    std::cout << "Total label meshes: " << remesher.GetMeshes().size() << std::endl;

    return EXIT_SUCCESS;
}