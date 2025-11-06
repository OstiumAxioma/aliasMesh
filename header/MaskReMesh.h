// header/MaskReMesh.h
#pragma once

#include <vector>
#include <string>

#include <vtkSmartPointer.h>

class vtkImageData;
class vtkPolyData;

class MaskReMesh
{
public:
    struct MeshObject
    {
        int label;                                  // 掩码 indice / label
        vtkSmartPointer<vtkPolyData> polyData;      // 当前标签的网格
        double origin[3];                           // 从 NIfTI 中读取的 Origin
    };

    MaskReMesh();
    ~MaskReMesh();

    // 类函数1：传入 NIfTI 掩码图像指针，根据掩码生成每个 label 对应的 PolyData
    // 内部使用多线程，并打印：
    //  - 每个 label 的耗时
    //  - 全部 labels 的总耗时
    void BuildFromMask(vtkImageData* maskImage);

    // 类函数2：导出为 STL（所有掩码合并为一个 STL，不带颜色）
    bool ExportToStl(const std::string& filePath) const;

    // 导出为 VTP（PolyData XML，保留 Label 标量用于上色）
    bool ExportToVTP(const std::string& filePath) const;

    const std::vector<MeshObject>& GetMeshes() const { return Meshes; }

private:
    std::vector<MeshObject> Meshes;
};