// src/main.cpp
#include <vtkSmartPointer.h>
#include <vtkNIFTIImageReader.h>
#include <vtkImageData.h>
#include <iostream>
#include <string>
#include "MaskReMesh.h"

namespace {
    const std::string kInputNiftiPath = "D:/Project/TMS_Tag/aliasMesh/mask/BN_Atlas_246_1mm.nii.gz";
    const std::string kOutputVtpPath  = "D:/Project/TMS_Tag/aliasMesh/result/BN_Atlas_246_1mm.vtp";
}

int main(int argc, char* argv[]) {
    std::string inputPath  = kInputNiftiPath;
    std::string outputPath = kOutputVtpPath;
    unsigned int numThreads = 0; // 0 = 自动

    if (argc > 1) inputPath = argv[1];
    if (argc > 2) outputPath = argv[2];
    if (argc > 3) numThreads = std::stoi(argv[3]); // 第三个参数控制线程数

    std::cout << "[Main] Input:  " << inputPath << std::endl;
    std::cout << "[Main] Output: " << outputPath << std::endl;
    std::cout << "[Main] Threads: "
              << (numThreads == 0 ? std::string("auto") : std::to_string(numThreads)) << std::endl;

    auto reader = vtkSmartPointer<vtkNIFTIImageReader>::New();
    reader->SetFileName(inputPath.c_str());
    reader->Update();

    vtkImageData* image = reader->GetOutput();
    if (!image) {
        std::cerr << "Failed to read NIfTI image." << std::endl;
        return EXIT_FAILURE;
    }

    MaskReMesh remesher;
    remesher.BuildFromMask(image, numThreads);

    if (!remesher.ExportToVTP(outputPath)) {
        std::cerr << "Failed to export VTP." << std::endl;
        return EXIT_FAILURE;
    }

    std::cout << "[Main] Done. Total meshes: "
              << remesher.GetMeshes().size() << std::endl;
    return EXIT_SUCCESS;
}