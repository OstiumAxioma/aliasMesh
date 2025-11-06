#include "MaskReMesh.h"

#include <vtkImageData.h>
#include <vtkPointData.h>
#include <vtkDataArray.h>
#include <vtkImageThreshold.h>
#include <vtkMarchingCubes.h>
#include <vtkPolyData.h>
#include <vtkAppendPolyData.h>
#include <vtkSTLWriter.h>
#include <vtkIntArray.h>
#include <vtkCellData.h>
#include <vtkXMLPolyDataWriter.h>

#include <set>
#include <iostream>
#include <chrono>   // ✅ 计时头文件

MaskReMesh::MaskReMesh() {}
MaskReMesh::~MaskReMesh() {}

void MaskReMesh::BuildFromMask(vtkImageData* maskImage)
{
    using namespace std::chrono;

    Meshes.clear();

    if (!maskImage) {
        std::cerr << "[MaskReMesh] maskImage is nullptr." << std::endl;
        return;
    }

    vtkPointData* pointData = maskImage->GetPointData();
    if (!pointData) {
        std::cerr << "[MaskReMesh] No point data in image." << std::endl;
        return;
    }

    vtkDataArray* scalars = pointData->GetScalars();
    if (!scalars) {
        std::cerr << "[MaskReMesh] No scalar array in image." << std::endl;
        return;
    }

    vtkIdType numTuples = scalars->GetNumberOfTuples();
    if (numTuples == 0) {
        std::cerr << "[MaskReMesh] Empty scalar array." << std::endl;
        return;
    }

    // 收集所有出现过的正整数 label
    std::set<int> labelSet;
    for (vtkIdType i = 0; i < numTuples; ++i) {
        double v = scalars->GetTuple1(i);
        int label = static_cast<int>(v);
        if (label > 0) {
            labelSet.insert(label);
        }
    }

    if (labelSet.empty()) {
        std::cerr << "[MaskReMesh] No positive labels found in mask." << std::endl;
        return;
    }

    double origin[3] = { 0.0, 0.0, 0.0 };
    maskImage->GetOrigin(origin);

    std::cout << "[MaskReMesh] Found " << labelSet.size() << " labels." << std::endl;

    // ✅ 开始总计时
    auto globalStart = high_resolution_clock::now();

    // 每个 label 的处理
    for (std::set<int>::const_iterator it = labelSet.begin();
         it != labelSet.end(); ++it)
    {
        int label = *it;
        std::cout << "[MaskReMesh] Processing label = " << label << std::endl;

        auto start = high_resolution_clock::now();

        vtkSmartPointer<vtkImageThreshold> threshold =
            vtkSmartPointer<vtkImageThreshold>::New();
        threshold->SetInputData(maskImage);
        threshold->ThresholdBetween(label, label);
        threshold->SetInValue(1);
        threshold->SetOutValue(0);
        threshold->SetOutputScalarTypeToUnsignedChar();
        threshold->Update();

        vtkSmartPointer<vtkMarchingCubes> mc =
            vtkSmartPointer<vtkMarchingCubes>::New();
        mc->SetInputConnection(threshold->GetOutputPort());
        mc->SetValue(0, 0.5);
        mc->ComputeNormalsOn();
        mc->Update();

        vtkSmartPointer<vtkPolyData> surface =
            vtkSmartPointer<vtkPolyData>::New();
        surface->ShallowCopy(mc->GetOutput());

        if (surface->GetNumberOfPoints() == 0) {
            std::cout << "[MaskReMesh] Label " << label
                      << " produced empty surface, skip." << std::endl;
            continue;
        }

        vtkIdType numCells = surface->GetNumberOfCells();
        vtkSmartPointer<vtkIntArray> labelArray =
            vtkSmartPointer<vtkIntArray>::New();
        labelArray->SetName("Label");
        labelArray->SetNumberOfComponents(1);
        labelArray->SetNumberOfTuples(numCells);
        for (vtkIdType c = 0; c < numCells; ++c) {
            labelArray->SetValue(c, label);
        }
        surface->GetCellData()->AddArray(labelArray);
        surface->GetCellData()->SetActiveScalars("Label");

        MeshObject obj;
        obj.label = label;
        obj.polyData = surface;
        obj.origin[0] = origin[0];
        obj.origin[1] = origin[1];
        obj.origin[2] = origin[2];

        Meshes.push_back(obj);

        // ✅ 单个 label 耗时
        auto end = high_resolution_clock::now();
        double seconds = duration_cast<duration<double>>(end - start).count();
        std::cout << "[MaskReMesh] Label " << label
                  << " finished in " << seconds << " s" << std::endl;
    }

    // ✅ 总耗时
    auto globalEnd = high_resolution_clock::now();
    double totalSeconds = duration_cast<duration<double>>(globalEnd - globalStart).count();

    std::cout << "[MaskReMesh] Total " << Meshes.size()
              << " labels processed in " << totalSeconds << " s" << std::endl;
}

bool MaskReMesh::ExportToStl(const std::string& filePath) const
{
    if (Meshes.empty()) {
        std::cerr << "[MaskReMesh] No meshes to export." << std::endl;
        return false;
    }

    vtkSmartPointer<vtkAppendPolyData> append =
        vtkSmartPointer<vtkAppendPolyData>::New();

    for (std::size_t i = 0; i < Meshes.size(); ++i) {
        if (!Meshes[i].polyData)
            continue;
        append->AddInputData(Meshes[i].polyData);
    }
    append->Update();

    vtkSmartPointer<vtkSTLWriter> writer =
        vtkSmartPointer<vtkSTLWriter>::New();
    writer->SetFileName(filePath.c_str());
    writer->SetInputConnection(append->GetOutputPort());
    writer->SetFileTypeToBinary();
    writer->Write();

    return true;
}

bool MaskReMesh::ExportToVTP(const std::string& filePath) const
{
    if (Meshes.empty()) {
        std::cerr << "[MaskReMesh] No meshes to export." << std::endl;
        return false;
    }

    vtkSmartPointer<vtkAppendPolyData> append =
        vtkSmartPointer<vtkAppendPolyData>::New();

    for (std::size_t i = 0; i < Meshes.size(); ++i)
    {
        if (!Meshes[i].polyData)
            continue;
        append->AddInputData(Meshes[i].polyData);
    }
    append->Update();

    vtkSmartPointer<vtkXMLPolyDataWriter> writer =
        vtkSmartPointer<vtkXMLPolyDataWriter>::New();
    writer->SetFileName(filePath.c_str());
    writer->SetInputConnection(append->GetOutputPort());
    writer->SetDataModeToBinary();
    writer->EncodeAppendedDataOff();
    writer->Write();

    std::cout << "[MaskReMesh] Exported VTP with "
              << Meshes.size() << " label meshes -> "
              << filePath << std::endl;

    return true;
}