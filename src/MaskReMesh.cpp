// src/MaskReMesh.cpp
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

MaskReMesh::MaskReMesh() {}
MaskReMesh::~MaskReMesh() {}

void MaskReMesh::BuildFromMask(vtkImageData* maskImage)
{
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

    // 收集所有出现过的正整数 label（一个 label 视为一个掩码 indice）
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

    // 对每一个 label 做一次阈值分割 + MarchingCubes，生成独立的 PolyData
    for (std::set<int>::const_iterator it = labelSet.begin();
         it != labelSet.end(); ++it)
    {
        int label = *it;
        std::cout << "[MaskReMesh] Processing label = " << label << std::endl;

        vtkSmartPointer<vtkImageThreshold> threshold =
            vtkSmartPointer<vtkImageThreshold>::New();
        threshold->SetInputData(maskImage);
        threshold->ThresholdBetween(label, label); // 只保留当前 label
        threshold->SetInValue(1);
        threshold->SetOutValue(0);
        threshold->SetOutputScalarTypeToUnsignedChar();
        threshold->Update();

        vtkSmartPointer<vtkMarchingCubes> mc =
            vtkSmartPointer<vtkMarchingCubes>::New();
        mc->SetInputConnection(threshold->GetOutputPort());
        mc->SetValue(0, 0.5);   // 二值图像：0 / 1，在 0.5 处取等值面
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

        // 给每个 cell 写入一个整型标量 “Label”，方便后续按 label 上色
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
    }

    std::cout << "[MaskReMesh] Total meshes = " << Meshes.size() << std::endl;
}

bool MaskReMesh::ExportToStl(const std::string& filePath) const
{
    if (Meshes.empty()) {
        std::cerr << "[MaskReMesh] No meshes to export." << std::endl;
        return false;
    }

    // 将所有 label 的 PolyData 合并成一个 PolyData，输出一个 STL 文件
    vtkSmartPointer<vtkAppendPolyData> append =
        vtkSmartPointer<vtkAppendPolyData>::New();

    for (std::size_t i = 0; i < Meshes.size(); ++i) {
        if (!Meshes[i].polyData) {
            continue;
        }
        append->AddInputData(Meshes[i].polyData);
    }
    append->Update();

    vtkSmartPointer<vtkSTLWriter> writer =
        vtkSmartPointer<vtkSTLWriter>::New();
    writer->SetFileName(filePath.c_str());
    writer->SetInputConnection(append->GetOutputPort());
    writer->SetFileTypeToBinary();  // 二进制 STL，体积小一点
    writer->Write();

    // 说明：
    //   标准 STL 格式本身不保存颜色或标量信息，
    //   上面写进去的 cell 标量 "Label" 在导出 STL 时会丢失。
    //   “每个掩码网格上色”这一点通常需要在 VTK / 3D 程序中加载后，
    //   再根据 label 做 LUT 上色（例如使用附加的 .vtp 写入带标量的 PolyData）。
    return true;
}

bool MaskReMesh::ExportToVTP(const std::string& filePath) const
{
    if (Meshes.empty()) {
        std::cerr << "[MaskReMesh] No meshes to export." << std::endl;
        return false;
    }

    // 将所有 label 的 PolyData 合并成一个多块数据（每个 cell 有标量 "Label"）
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
    writer->SetDataModeToBinary();  // 压缩但保留标量数据
    writer->EncodeAppendedDataOff();
    writer->Write();

    std::cout << "[MaskReMesh] Exported VTP with "
              << Meshes.size() << " label meshes -> "
              << filePath << std::endl;

    return true;
}
